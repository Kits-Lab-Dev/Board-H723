#include "ramdisk.h"
#include "sdram.h"
#include <fcntl.h>
#include <errno.h>
#include "FreeRTOS.h"
#include "semphr.h"
#include "stdio.h"
#include "string.h"
#include "diskio.h"

static bool isMounted = false;
FATFS ramfs;

static volatile DSTATUS Stat = STA_NOINIT;
static uint8_t *sdram_ptr = NULL;

DSTATUS ramDiskInitialize()
{
    if (sdram_ptr != NULL)
        return Stat;
    sdram_ptr = (uint8_t *)sdramMalloc(RAM_DISK_SIZE);
    if (sdram_ptr != NULL)
    {
        Stat = RES_OK;
        return RES_OK;
    }
    else
        return RES_NOTRDY;
}

DSTATUS ramDiskStatus()
{
    return Stat;
}

DSTATUS ramDiskRead(BYTE *buff, DWORD sector, UINT count)
{
    uint32_t *pSrcBuffer = (uint32_t *)buff;
    uint32_t BufferSize = (BLOCK_SIZE * count) / 4;
    uint32_t *pSdramAddress = (uint32_t *)(sdram_ptr + (sector * BLOCK_SIZE));

    for (; BufferSize != 0; BufferSize--)
    {
        *pSrcBuffer++ = *(__IO uint32_t *)pSdramAddress++;
    }
    return RES_OK;
}

DSTATUS ramDiskWrite(const BYTE *buff, DWORD sector, UINT count)
{
    uint32_t *pDstBuffer = (uint32_t *)buff;
    uint32_t BufferSize = (BLOCK_SIZE * count) / 4;
    uint32_t *pSramAddress = (uint32_t *)(sdram_ptr + (sector * BLOCK_SIZE));

    for (; BufferSize != 0; BufferSize--)
    {
        *(__IO uint32_t *)pSramAddress++ = *pDstBuffer++;
    }
    return RES_OK;
}

DSTATUS ramDiskIOctl(BYTE cmd, void *buff)
{
    int res = RES_ERROR;

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
        *(DWORD *)buff = RAM_DISK_SIZE / BLOCK_SIZE;
        res = RES_OK;
        break;

    /* Get R/W sector size (WORD) */
    case GET_SECTOR_SIZE:
        *(WORD *)buff = BLOCK_SIZE;
        res = RES_OK;
        break;

    /* Get erase block size in unit of sector (DWORD) */
    case GET_BLOCK_SIZE:
        *(DWORD *)buff = 1;
        res = RES_OK;
        break;

    default:
        res = RES_PARERR;
    }

    return res;
}

bool ramDiskMount()
{
    if (!isMounted)
    {
        MKFS_PARM param;
        param.fmt = FM_EXFAT;
        param.align = 0;
        param.n_fat = 0;
        param.n_root = 0;
        param.au_size = 0;
        isMounted = f_mount(&ramfs, "/RAM", 1) == FR_OK;
        if (!isMounted)
        {
            char *work = (char *)sdramMalloc(1024);
            f_mkfs("/RAM", &param, work, 1024);
            sdramFree(work);
            isMounted = f_mount(&ramfs, "/RAM", 1) == FR_OK;
        }
    }
    return isMounted;
}

bool ramDiskUnmount()
{
    if (isMounted)
        isMounted = f_mount(NULL, "", 1) != FR_OK;
    return isMounted;
}
