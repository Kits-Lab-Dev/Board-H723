#include <ff.h>
#include "diskio.h"
#include "ramdisk.h"
#include "sddisk.h"

typedef struct
{
    DSTATUS (*initialize) (void);
    DSTATUS (*status) (void);
    DRESULT (*read) (BYTE* buff, LBA_t sector, UINT count);
    DRESULT (*write) (const BYTE* buff, LBA_t sector, UINT count);
    DRESULT (*ioctl) (BYTE cmd, void* buff);   
} Dio;

static Dio dio[] = {
    {ramDiskInitialize, ramDiskStatus, ramDiskRead, ramDiskWrite, ramDiskIOctl},
    {sdDiskInitialize, sdDiskStatus, sdDiskRead, sdDiskWrite, sdDiskIOctl}
};

DSTATUS disk_status(BYTE pdrv)
{
    return dio[pdrv].status();
}

DSTATUS disk_initialize(BYTE pdrv)
{
    return dio[pdrv].initialize();
}

DRESULT disk_read(BYTE pdrv, BYTE *buff, LBA_t sector, UINT count)
{
   return dio[pdrv].read(buff, sector, count);
}

DRESULT disk_write(BYTE pdrv, const BYTE *buff, LBA_t sector, UINT count)
{
    return dio[pdrv].write(buff, sector, count);
}

DRESULT disk_ioctl(BYTE pdrv, BYTE cmd, void *buff)
{
    return dio[pdrv].ioctl(cmd, buff);
}

DWORD get_fattime(void)
{
    return 0;
}
