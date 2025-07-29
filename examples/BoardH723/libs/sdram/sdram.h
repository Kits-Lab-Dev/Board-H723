#ifndef SDRAM_H
#define SDRAM_H

#include "lwmem/lwmem.h"
#include "stdbool.h"
#include "main.h"

#define SDRAM_TIMEOUT ((uint32_t)0xFFFF)
#define SDRAM_MODEREG_BURST_LENGTH_1 ((uint16_t)0x0000)
#define SDRAM_MODEREG_BURST_LENGTH_2 ((uint16_t)0x0001)
#define SDRAM_MODEREG_BURST_LENGTH_4 ((uint16_t)0x0002)
#define SDRAM_MODEREG_BURST_LENGTH_8 ((uint16_t)0x0004)
#define SDRAM_MODEREG_BURST_TYPE_SEQUENTIAL ((uint16_t)0x0000)
#define SDRAM_MODEREG_BURST_TYPE_INTERLEAVED ((uint16_t)0x0008)
#define SDRAM_MODEREG_CAS_LATENCY_2 ((uint16_t)0x0020)
#define SDRAM_MODEREG_CAS_LATENCY_3 ((uint16_t)0x0030)
#define SDRAM_MODEREG_OPERATING_MODE_STANDARD ((uint16_t)0x0000)
#define SDRAM_MODEREG_WRITEBURST_MODE_PROGRAMMED ((uint16_t)0x0000)
#define SDRAM_MODEREG_WRITEBURST_MODE_SINGLE ((uint16_t)0x0200)

#define SDRAM_BANK1_ADDR ((uint32_t)0xC0000000)
#define SDRAM_BANK2_ADDR ((uint32_t)0xD0000000)
#define SDRAM_BANK1_SIZE ((uint32_t)0x2000000)

bool sdramInit();
void *sdramMalloc(size_t size);
void *sdramRealloc(void *p, size_t size);
void *sdramCalloc(size_t nmemb, size_t size);
void sdramFree(void *p);
uint32_t sdramGetUseMem();
uint32_t sdramGetFreeMem();

#ifdef __cplusplus
extern "C"
{
#endif

#ifdef __cplusplus
}
#endif

#endif