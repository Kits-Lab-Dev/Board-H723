#ifndef APP_LOG_H
#define APP_LOG_H

#include "main.h"

#if(APP_DEBUG_LEVEL > 3 && defined(APP_DEBUG))
#define DBG(...) mcuDebug(DEBUG_LEVEL_DEBUG, __VA_ARGS__)
#define DUMP(BUF, LEN, ...) mcuDebug(DEBUG_LEVEL_DEBUG, __VA_ARGS__); mcuDump(BUF, LEN)
#else
#define DUMP(BUF, LEN, ...) (void)0
#define DBG(...)  (void)0
#endif
#if(APP_DEBUG_LEVEL > 2 && defined(APP_DEBUG))
#define INF(...) mcuDebug(DEBUG_LEVEL_INFO, __VA_ARGS__)
#else
#define INF(...)  (void)0
#endif
#if(APP_DEBUG_LEVEL > 1 && defined(APP_DEBUG))
#define WRN(...) mcuDebug(DEBUG_LEVEL_WARNING, __VA_ARGS__)
#else
#define WRN(...)  (void)0
#endif
#if(APP_DEBUG_LEVEL > 0 && defined(APP_DEBUG))
#define ERR(...) mcuDebug(DEBUG_LEVEL_ERROR, __VA_ARGS__)
#else
#define ERR(...)  (void)0
#endif

typedef enum DebugLevel{
    DEBUG_LEVEL_NONE = 0,
    DEBUG_LEVEL_ERROR,
    DEBUG_LEVEL_WARNING,
    DEBUG_LEVEL_INFO,
    DEBUG_LEVEL_DEBUG
} DebugLevel;

void logInit(void);
void logDeinit(void);
int mcuPrint(const char *msg, ...);
void mcuDebug(DebugLevel level, char *msg, ...);
void mcuDump(uint8_t* data, int len);

#endif