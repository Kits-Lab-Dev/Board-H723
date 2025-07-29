#ifndef EXEC_H
#define EXEC_H

#define __ITCMRAM __attribute__((section(".itcmram_bss")))
#define __RAM_D1 __attribute__((section(".RamD1")))
// #define DEBUG 1

#define ITCMRAM_ADDR 0x00000000 //   LENGTH = 64K
#define DTCMRAM_ADDR 0x20000000 //   LENGTH = 128K
#define FLASH_ADDR 0x08000000   //   LENGTH = 1024K
#define RAM_D1_ADDR 0x24000000  //  LENGTH = 320K
#define RAM_D2_ADDR 0x30000000  //  LENGTH = 32K
#define RAM_D3_ADDR 0x38000000  //  LENGTH = 16K
#define SDRAM_ADDR 0xC0000000   //  LENGTH = 32M

#define IS_ADDR(x) ((size_t)x & 0xFF000000)

#include "stdbool.h"
#include "main.h"
#include "log.h"
#include "FreeRTOS.h"

extern volatile float CPU_IDLE;

void app();
void bootloader();
UBaseType_t uxRand(void);

#ifdef WITH_LUA

#if (defined(LUA_OUT))
#define LUA_PRINT(s, l) luaPrint(s, l, L)
#else
#define LUA_PRINT(s, l) ((void)0)
#endif

#define LUA_GETC(f)		luaGChar(f)
#define LUA_LOCKFILE(f)		((void)0)
#define LUA_UNLOCKFILE(f)		((void)0)
#define LUA_USERSTATETHREAD(L,L1) luaUserStateThread(L, L1)
#define LUA_USERSTATEFREE(L,L1) luaUserStateFree(L, L1)



#endif

#endif