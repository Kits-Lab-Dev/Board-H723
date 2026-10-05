#include "sdram.h"
#include <fcntl.h>
#include <errno.h>
#include "FreeRTOS.h"
#include "queue.h"
#include "semphr.h"
#include "stdio.h"
#include "string.h"
#include "ff.h"
#include "diskio.h"
#include "sdmmc.h"
#include "sddisk.h"
#include "app.h"

#define SD_TIMEOUT 30 * 1000
#define SD_DEFAULT_BLOCK_SIZE BLOCKSIZE

static volatile DSTATUS Stat = STA_NOINIT;
static bool sdCardInsert = false;
static SD_HandleTypeDef *hsd = &hsd2;
static xSemaphoreHandle rSem = NULL;
static xSemaphoreHandle wSem = NULL;
static xSemaphoreHandle mutex = NULL;

static bool isMounted = false;
static uint8_t wblock[SD_DEFAULT_BLOCK_SIZE] __attribute__((section(".spi_buf_section")));
static uint8_t rblock[SD_DEFAULT_BLOCK_SIZE] __attribute__((section(".spi_buf_section")));

// DMA работает только с rblock/wblock, поэтому FATFS может лежать в обычной памяти
FATFS sdfs;

#define ENABLE_SD_DMA_CACHE_MAINTENANCE 1

#if (ENABLE_SD_DMA_CACHE_MAINTENANCE == 1)
uint32_t alignedAddr;
#endif

static void txCmplt(SD_HandleTypeDef *)
{
    xSemaphoreGiveFromISR(wSem, NULL);
}

static void rxCmplt(SD_HandleTypeDef *)
{
    xSemaphoreGiveFromISR(rSem, NULL);
}

bool sdCardPresent(void)
{
    return sdCardInsert;
}

void sdCardDetectLoop(void)
{
    if (HAL_GPIO_ReadPin(SD_DET_GPIO_Port, SD_DET_Pin) == GPIO_PIN_RESET && !sdCardInsert)
    {
        hsd->Instance = SDMMC2;
        hsd->Init.ClockEdge = SDMMC_CLOCK_EDGE_RISING;
        hsd->Init.ClockPowerSave = SDMMC_CLOCK_POWER_SAVE_DISABLE;
        hsd->Init.BusWide = SDMMC_BUS_WIDE_4B;
        hsd->Init.HardwareFlowControl = SDMMC_HARDWARE_FLOW_CONTROL_DISABLE;
        hsd->Init.ClockDiv = 2;
        if (HAL_SD_Init(hsd) == HAL_OK)
        {
            if (HAL_SD_ConfigWideBusOperation(hsd, SDMMC_BUS_WIDE_4B) == HAL_OK)
            {
                sdCardInsert = true;
                sdDiskMount();
            }
            else
            {
                HAL_SD_DeInit(hsd);
            }
        }
    }
    else if (HAL_GPIO_ReadPin(SD_DET_GPIO_Port, SD_DET_Pin) == GPIO_PIN_SET && sdCardInsert)
    {
        if (HAL_SD_DeInit(hsd) == HAL_OK)
        {
            sdCardInsert = false;
            sdDiskUnmount();
        }
    }
}

bool sdDiskMount()
{
    if (!isMounted)
    {
        memset(&sdfs, 0, sizeof(FATFS));
        isMounted = f_mount(&sdfs, "/SD", 1) == FR_OK;
        // if (!isMounted)
        // {
        //     WRN("Formatting SD card...\r\n");
        //     char *work = (char *)pvPortMalloc(1024);
        //     FRESULT res = f_mkfs("/SD", &param, work, 1024);
        //     vPortFree(work);
        //     if (res != FR_OK)
        //         return false;
        //     isMounted = f_mount(&sdfs, "/SD", 1) == FR_OK;
        // }
    }
    return isMounted;
}

bool sdDiskUnmount()
{
    if (isMounted)
        isMounted = f_mount(NULL, "/SD", 0) != FR_OK;
    return isMounted;
}

DSTATUS sdDiskInitialize()
{
    Stat = STA_NOINIT;

    if (sdCardInsert)
    {
        Stat = sdDiskStatus();
        if (Stat != STA_NOINIT)
        {
            if (rSem == NULL)
            {
                rSem = xSemaphoreCreateBinary();
            }
            if (wSem == NULL)
            {
                wSem = xSemaphoreCreateBinary();
            }
            if (mutex == NULL)
            {
                mutex = xSemaphoreCreateMutex();
            }

            if (rSem == NULL || wSem == NULL || mutex == NULL)
            {
                Stat |= STA_NOINIT;
            }
        }
    }
    return Stat;
}

DSTATUS sdDiskStatus()
{
    Stat = STA_NOINIT;
    if (sdCardInsert)
    {
        if (HAL_SD_GetCardState(&hsd2) == HAL_SD_CARD_TRANSFER)
        {
            Stat &= ~STA_NOINIT;
        }
    }
    return Stat;
}

static int sdDiskStatusWithTimeout(uint32_t timeout)
{
    uint32_t timer;

    timer = xTaskGetTickCount();
    while (xTaskGetTickCount() - timer < timeout)
    {
        if (sdDiskStatus() != STA_NOINIT)
        {
            return 0;
        }
    }
    return -1;
}
bool readSD(const uint8_t *buff, uint32_t sector, uint32_t count)
{
    uint8_t ret;
    bool res = false;
    uint32_t timer;
    HAL_SD_RegisterCallback(hsd, HAL_SD_RX_CPLT_CB_ID, rxCmplt);
    ret = HAL_SD_ReadBlocks_DMA(hsd, (uint8_t *)buff, (uint32_t)(sector), count);
    if (ret == HAL_OK)
    {
        if (xSemaphoreTake(rSem, SD_TIMEOUT) == pdTRUE)
        {
            timer = xTaskGetTickCount();
            /* block until SDIO IP is ready or a timeout occur */
            while (xTaskGetTickCount() - timer < SD_TIMEOUT)
            {
                if (sdDiskStatus() != STA_NOINIT)
                {
                    res = true;
#if (ENABLE_SD_DMA_CACHE_MAINTENANCE == 1)

                    alignedAddr = (uint32_t)buff & ~0x1F;
                    SCB_InvalidateDCache_by_Addr((uint32_t *)alignedAddr, count * BLOCKSIZE + ((uint32_t)buff - alignedAddr));
#endif
                    break;
                }
            }
        }
    }
    return res;
}

DSTATUS sdDiskRead(BYTE *buff, DWORD sector, UINT count)
{
    xSemaphoreTake(mutex, portMAX_DELAY);
    DRESULT res = RES_ERROR;
    if (sdDiskStatusWithTimeout(SD_TIMEOUT) < 0)
    {
        xSemaphoreGive(mutex);
        return res;
    }

    // if (IS_ADDR(buff) != RAM_D1_ADDR && IS_ADDR(buff) != RAM_D2_ADDR && IS_ADDR(buff) != RAM_D3_ADDR)
    // {
        BYTE* b = buff;
        res = RES_OK;
        for (uint32_t i = 0; i < count; i++)
        {
            if (!readSD(rblock, sector++, 1))
            {
                res = RES_ERROR;
                break;
            }
            memcpy(b, rblock, BLOCKSIZE);
            b += BLOCKSIZE;
        }
    // }
    // else
    // {
    //     if (readSD(buff, sector, count))
    //         res = RES_OK;
    // }
    xSemaphoreGive(mutex);
    return res;
}

bool writeSD(const uint8_t *buff, uint32_t sector, uint32_t count)
{
    uint32_t timer;
    bool res = false;
    HAL_SD_RegisterCallback(hsd, HAL_SD_TX_CPLT_CB_ID, txCmplt);
    if (HAL_SD_WriteBlocks_DMA(hsd, buff, (uint32_t)(sector), count) == HAL_OK)
    {
        if (xSemaphoreTake(wSem, SD_TIMEOUT) == pdTRUE)
        {
            timer = xTaskGetTickCount();
            /* block until SDIO IP is ready or a timeout occur */
            while (xTaskGetTickCount() - timer < SD_TIMEOUT)
            {
                if (sdDiskStatus() != STA_NOINIT)
                {
                    res = true;
                    break;
                }
            }
        }
    }
    return res;
}

DSTATUS sdDiskWrite(const BYTE *buff, DWORD sector, UINT count)
{
    xSemaphoreTake(mutex, portMAX_DELAY);
    DRESULT res = RES_ERROR;
    if (sdDiskStatusWithTimeout(SD_TIMEOUT) < 0)
    {
        xSemaphoreGive(mutex);
        return res;
    }

    // if (IS_ADDR(buff) != RAM_D1_ADDR && IS_ADDR(buff) != RAM_D2_ADDR && IS_ADDR(buff) != RAM_D3_ADDR)
    // {
        BYTE* b = (BYTE*)buff;
        res = RES_OK;
        for (uint32_t i = 0; i < count; i++)
        {
            memcpy((void *)wblock, b, BLOCKSIZE);
#if (ENABLE_SD_DMA_CACHE_MAINTENANCE == 1)
            uint32_t alignedAddr;
            alignedAddr = (uint32_t)b & ~0x1F;
            SCB_CleanDCache_by_Addr((uint32_t *)alignedAddr, BLOCKSIZE + ((uint32_t)b - alignedAddr));
#endif
            b += BLOCKSIZE;
            if (!writeSD(wblock, sector++, 1))
            {
                res = RES_ERROR;
                break;
            }
        }
    // }
    // else
    // {
#if (ENABLE_SD_DMA_CACHE_MAINTENANCE == 1)
        // uint32_t alignedAddr;
        // alignedAddr = (uint32_t)buff & ~0x1F;
        // SCB_CleanDCache_by_Addr((uint32_t *)alignedAddr, count * BLOCKSIZE + ((uint32_t)buff - alignedAddr));
#endif
    //     if (writeSD(buff, sector, count))
    //         res = RES_OK;
    // }
    xSemaphoreGive(mutex);
    return res;
}

DSTATUS sdDiskIOctl(BYTE cmd, void *buff)
{
    DRESULT res = RES_ERROR;
    HAL_SD_CardInfoTypeDef CardInfo;

    if (Stat & STA_NOINIT)
        return RES_NOTRDY;

    switch (cmd)
    {
    /* Make sure that no pending write process */
    case CTRL_SYNC:
        res = RES_OK;
        break;

    /* Get number of sectors on the disk (DWORD) */
    case GET_SECTOR_COUNT:
        HAL_SD_GetCardInfo(hsd, &CardInfo);
        *(DWORD *)buff = CardInfo.LogBlockNbr;
        res = RES_OK;
        break;

    /* Get R/W sector size (WORD) */
    case GET_SECTOR_SIZE:
        HAL_SD_GetCardInfo(hsd, &CardInfo);
        *(WORD *)buff = CardInfo.LogBlockSize;
        res = RES_OK;
        break;

    /* Get erase block size in unit of sector (DWORD) */
    case GET_BLOCK_SIZE:
        HAL_SD_GetCardInfo(hsd, &CardInfo);
        *(DWORD *)buff = CardInfo.LogBlockSize / SD_DEFAULT_BLOCK_SIZE;
        res = RES_OK;
        break;

    default:
        res = RES_PARERR;
    }

    return res;
}