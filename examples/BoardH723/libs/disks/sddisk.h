#ifndef SDDISK_H
#define SDDISK_H

#include <ff.h>
#include "stdbool.h"
#include "stdint.h"
#include "diskio.h"

#ifdef __cplusplus
extern "C"
{
#endif

bool sdCardPresent(void);
void sdCardDetectLoop(void);

DSTATUS sdDiskInitialize();
DSTATUS sdDiskStatus();
DSTATUS sdDiskRead(BYTE *buff, DWORD sector, UINT count);
DSTATUS sdDiskWrite(const BYTE *buff, DWORD sector, UINT count);
DSTATUS sdDiskIOctl(BYTE cmd, void *buff);

bool sdDiskMount();
bool sdDiskIsMounted(void);
bool sdDiskUnmount();

#ifdef __cplusplus
}
#endif

#endif