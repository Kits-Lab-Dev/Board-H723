#ifndef DISKS_H
#define DISKS_H

#include <ff.h>
#include "stdbool.h"
#include "stdint.h"
#include "diskio.h"

int fcopy(const char *src, const char *dest);

#endif // DISKS_H