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

// Буфер DMA: до SD_DMA_SECTORS секторов за одну команду (CMD18/CMD25), а не по
// одному сектору — запись так быстрее в разы. Лежит в AXI SRAM (куча FreeRTOS), не в
// SDRAM: шину SDRAM делят LTDC, DMA2D и процессор, и SDMMC на 50 МГц не успевал
// получить данные (TX underrun, 0x10) или отдать их (RX overrun, 0x20).
// Память кэшируется (write-back), поэтому перед записью кэш сбрасывается, при чтении —
// инвалидируется. Буфер выровнен по строке кэша (32 байта), чтобы не задеть соседние данные.
#define SD_DMA_SECTORS 64
#define SD_DMA_BUF_SIZE (SD_DMA_SECTORS * SD_DEFAULT_BLOCK_SIZE)
// Сбой обмена (помеха, перегрузка шины) — повторить блок
#define SD_RETRIES 3
static uint8_t *dmabuf = NULL;

// DMA работает только с dmabuf, поэтому FATFS может лежать в обычной памяти
FATFS sdfs;

static void txCmplt(SD_HandleTypeDef *)
{
    xSemaphoreGiveFromISR(wSem, NULL);
}

static void rxCmplt(SD_HandleTypeDef *)
{
    xSemaphoreGiveFromISR(rSem, NULL);
}

// Ошибка передачи: будим ожидающего сразу, а не через SD_TIMEOUT
static void errCmplt(SD_HandleTypeDef *)
{
    xSemaphoreGiveFromISR(rSem, NULL);
    xSemaphoreGiveFromISR(wSem, NULL);
}

bool sdDiskIsMounted(void)
{
    return isMounted;
}

bool sdCardPresent(void)
{
    return sdCardInsert;
}

// Ядро SDMMC тактируется от PLL2R = 100 МГц, SDMMC_CK = 100 / (2 * CLKDIV)
#define SD_CLKDIV_DEFAULT 2 // 25 МГц — предел Default Speed
#define SD_CLKDIV_HIGH 1    // 50 МГц — High Speed, только после CMD6

static void sdSetClockDiv(uint32_t div)
{
    MODIFY_REG(hsd->Instance->CLKCR, SDMMC_CLKCR_CLKDIV, div);
    hsd->Init.ClockDiv = div;
}

// Переключает карту в High Speed (CMD6) и шину на 50 МГц. Проверка — чтение
// сектора 0; если не прошло, остаёмся на 25 МГц (в High Speed карта работает
// и на меньшей частоте). Возвращает частоту шины в МГц.
static uint32_t sdTryHighSpeed(void)
{
    static uint8_t probe[BLOCKSIZE] __attribute__((aligned(32)));
    if (HAL_SD_ConfigSpeedBusOperation(hsd, SDMMC_SPEED_MODE_HIGH) != HAL_OK)
    {
        hsd->ErrorCode = HAL_SD_ERROR_NONE;
        return 25;
    }
    sdSetClockDiv(SD_CLKDIV_HIGH);
    bool ok = HAL_SD_ReadBlocks(hsd, probe, 0, 1, 1000) == HAL_OK;
    uint32_t t = HAL_GetTick();
    while (ok && HAL_SD_GetCardState(hsd) != HAL_SD_CARD_TRANSFER)
    {
        if (HAL_GetTick() - t > 1000)
            ok = false;
    }
    if (!ok)
    {
        WRN("SD: 50 MHz read failed, error 0x%08lx, back to 25 MHz\n", hsd->ErrorCode);
        hsd->ErrorCode = HAL_SD_ERROR_NONE;
        sdSetClockDiv(SD_CLKDIV_DEFAULT);
        return 25;
    }
    return 50;
}

void sdCardDetectLoop(void)
{
    if (HAL_GPIO_ReadPin(SD_DET_GPIO_Port, SD_DET_Pin) == GPIO_PIN_RESET && !sdCardInsert)
    {
        // Сначала 4 бита; если карта или пайка D1–D3 не тянут — 1 бит.
        // HAL_SD_Init сам переключает шину на Init.BusWide (повторный
        // HAL_SD_ConfigWideBusOperation читает SCR уже в 4-битном режиме и зависает)
        static const uint32_t widths[] = {SDMMC_BUS_WIDE_4B, SDMMC_BUS_WIDE_1B};
        for (size_t i = 0; i < sizeof(widths) / sizeof(widths[0]) && !sdCardInsert; i++)
        {
            hsd->Instance = SDMMC2;
            hsd->Init.ClockEdge = SDMMC_CLOCK_EDGE_RISING;
            hsd->Init.ClockPowerSave = SDMMC_CLOCK_POWER_SAVE_DISABLE;
            hsd->Init.BusWide = widths[i];
            hsd->Init.HardwareFlowControl = SDMMC_HARDWARE_FLOW_CONTROL_DISABLE;
            hsd->Init.ClockDiv = SD_CLKDIV_DEFAULT;
            if (HAL_SD_Init(hsd) == HAL_OK)
            {
                uint32_t mhz = widths[i] == SDMMC_BUS_WIDE_4B ? sdTryHighSpeed() : 25;
                INF("SD: %d bit bus, %lu MHz\n", widths[i] == SDMMC_BUS_WIDE_4B ? 4 : 1, mhz);
                sdCardInsert = true;
                sdDiskMount();
            }
            else
            {
                WRN("SD: %d bit init failed, error 0x%08lx\n",
                    widths[i] == SDMMC_BUS_WIDE_4B ? 4 : 1, hsd->ErrorCode);
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
            if (dmabuf == NULL)
            {
                uint8_t *p = pvPortMalloc(SD_DMA_BUF_SIZE + 32);
                if (p != NULL)
                {
                    dmabuf = (uint8_t *)(((uint32_t)p + 31) & ~31UL);
                }
            }

            if (rSem == NULL || wSem == NULL || mutex == NULL || dmabuf == NULL)
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

// Ждёт, пока карта вернётся в TRANSFER (после записи она занята программированием)
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
        vTaskDelay(1);
    }
    return -1;
}

static bool readSD(uint8_t *buff, uint32_t sector, uint32_t count)
{
    uint32_t size = count * BLOCKSIZE;
    xSemaphoreTake(rSem, 0);
    // грязные строки кэша могли бы вытесниться поверх данных DMA
    SCB_CleanInvalidateDCache_by_Addr((uint32_t *)buff, size);
    hsd->ErrorCode = HAL_SD_ERROR_NONE;
    if (HAL_SD_ReadBlocks_DMA(hsd, buff, sector, count) != HAL_OK)
    {
        WRN("SD: read start failed, sector %lu, error 0x%08lx\n", sector, hsd->ErrorCode);
        return false;
    }
    if (xSemaphoreTake(rSem, SD_TIMEOUT) != pdTRUE || hsd->ErrorCode != HAL_SD_ERROR_NONE)
    {
        WRN("SD: read failed, sector %lu x %lu, error 0x%08lx\n", sector, count, hsd->ErrorCode);
        return false;
    }
    SCB_InvalidateDCache_by_Addr((uint32_t *)buff, size);
    return sdDiskStatusWithTimeout(SD_TIMEOUT) == 0;
}

static bool writeSD(uint8_t *buff, uint32_t sector, uint32_t count)
{
    xSemaphoreTake(wSem, 0);
    SCB_CleanDCache_by_Addr((uint32_t *)buff, count * BLOCKSIZE);
    hsd->ErrorCode = HAL_SD_ERROR_NONE;
    if (HAL_SD_WriteBlocks_DMA(hsd, buff, sector, count) != HAL_OK)
    {
        WRN("SD: write start failed, sector %lu, error 0x%08lx\n", sector, hsd->ErrorCode);
        return false;
    }
    if (xSemaphoreTake(wSem, SD_TIMEOUT) != pdTRUE || hsd->ErrorCode != HAL_SD_ERROR_NONE)
    {
        WRN("SD: write failed, sector %lu x %lu, error 0x%08lx\n", sector, count, hsd->ErrorCode);
        return false;
    }
    return sdDiskStatusWithTimeout(SD_TIMEOUT) == 0;
}

static void sdRegisterCallbacks(void)
{
    HAL_SD_RegisterCallback(hsd, HAL_SD_RX_CPLT_CB_ID, rxCmplt);
    HAL_SD_RegisterCallback(hsd, HAL_SD_TX_CPLT_CB_ID, txCmplt);
    HAL_SD_RegisterCallback(hsd, HAL_SD_ERROR_CB_ID, errCmplt);
}

DSTATUS sdDiskRead(BYTE *buff, DWORD sector, UINT count)
{
    xSemaphoreTake(mutex, portMAX_DELAY);
    DRESULT res = RES_ERROR;
    if (sdDiskStatusWithTimeout(SD_TIMEOUT) == 0)
    {
        sdRegisterCallbacks();
        res = RES_OK;
        while (count > 0)
        {
            uint32_t n = count < SD_DMA_SECTORS ? count : SD_DMA_SECTORS;
            int tries = SD_RETRIES;
            while (!readSD(dmabuf, sector, n) && --tries > 0)
                sdDiskStatusWithTimeout(SD_TIMEOUT);
            if (tries == 0)
            {
                res = RES_ERROR;
                break;
            }
            memcpy(buff, dmabuf, n * BLOCKSIZE);
            buff += n * BLOCKSIZE;
            sector += n;
            count -= n;
        }
    }
    xSemaphoreGive(mutex);
    return res;
}

DSTATUS sdDiskWrite(const BYTE *buff, DWORD sector, UINT count)
{
    xSemaphoreTake(mutex, portMAX_DELAY);
    DRESULT res = RES_ERROR;
    if (sdDiskStatusWithTimeout(SD_TIMEOUT) == 0)
    {
        sdRegisterCallbacks();
        res = RES_OK;
        while (count > 0)
        {
            uint32_t n = count < SD_DMA_SECTORS ? count : SD_DMA_SECTORS;
            memcpy(dmabuf, buff, n * BLOCKSIZE);
            int tries = SD_RETRIES;
            while (!writeSD(dmabuf, sector, n) && --tries > 0)
                sdDiskStatusWithTimeout(SD_TIMEOUT);
            if (tries == 0)
            {
                res = RES_ERROR;
                break;
            }
            buff += n * BLOCKSIZE;
            sector += n;
            count -= n;
        }
    }
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