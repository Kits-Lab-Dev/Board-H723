#ifndef RAMDISK_H
#define RAMDISK_H

#include <ff.h>
#include "stdbool.h"
#include "stdint.h"
#include "diskio.h"


#define RAM_DISK_SIZE (1024 * 1024 * 4)
#define BLOCK_SIZE FF_MAX_SS
#define CLASTER_SIZE BLOCK_SIZE

#ifdef __cplusplus
extern "C"
{
#endif

DSTATUS ramDiskInitialize();
DSTATUS ramDiskStatus();
DSTATUS ramDiskRead(BYTE *buff, DWORD sector, UINT count);
DSTATUS ramDiskWrite(const BYTE *buff, DWORD sector, UINT count);
DSTATUS ramDiskIOctl(BYTE cmd, void *buff);

bool ramDiskMount();
bool ramDiskUnmount();

#ifdef __cplusplus
}
#endif

#endif