#include "app.h"
#include "log.h"
#include "FreeRTOS.h"
#include "task.h"
#include "semphr.h"
#include "string.h"
#include <stdio.h>
#include <stdarg.h>
#include <unistd.h>
#include "sdram.h"

#define LOG_UART

static SemaphoreHandle_t printMutex;
static char *printBuf;

#if defined(LOG_UART)
#include "usart.h"
#define UART_DMA_BUFFER_SIZE 512
static uint8_t *uartBufDMA;
static UART_HandleTypeDef *huart = &huart1;
static SemaphoreHandle_t uartMutex = NULL;
static SemaphoreHandle_t uartTxSemaphore = NULL;

static void UART_TransmitCompleteCallback(UART_HandleTypeDef *huart)
{
    UNUSED(huart);

    if (uartTxSemaphore)
        xSemaphoreGiveFromISR(uartTxSemaphore, NULL);
}

#endif

static int lw(char *str, int len)
{
#if defined(LOG_UART)

    uint32_t l = len;
    char *p = str;
    while (l)
    {
        uint32_t lp = l > UART_DMA_BUFFER_SIZE ? UART_DMA_BUFFER_SIZE : l;
        memcpy(uartBufDMA, p, lp);
        l -= lp;
        p += lp;
        HAL_UART_RegisterCallback(huart, HAL_UART_TX_COMPLETE_CB_ID, UART_TransmitCompleteCallback);
        HAL_UART_Transmit_DMA(huart, uartBufDMA, lp);
        if (uartTxSemaphore)
        {
            if (xSemaphoreTake(uartTxSemaphore, 100) == pdTRUE)
            {
                continue;
            }
        }
        return -1;
    }

#endif
    return len;
}

void logInit(void)
{
#if defined(LOG_UART)
    uartMutex = xSemaphoreCreateMutex();
    uartTxSemaphore = xSemaphoreCreateBinary();
    uartBufDMA = (uint8_t *)pvPortMalloc(UART_DMA_BUFFER_SIZE);
#endif
    printMutex = xSemaphoreCreateMutex();
    printBuf = sdramMalloc(1024 * 256);
}

void logDeinit(void)
{
}

// ------------- переопределение stdio -------------
int _write(int file, char *ptr, int len)
{
    (void)file;
    lw(ptr, len);
    if (ptr[len - 1] == '\n')
        lw("\r", 1);
    return len;
}

extern bool isLuaIO;
extern QueueHandle_t telnetRxQueue;

int _read(int file, char *ptr, int len)
{
    return read(file, ptr, len);
}
// ------------- переопределение stdio -------------

int mcuPrint(const char *msg, ...)
{
    if (xMutexTake(printMutex, 200) == pdFALSE)
        return 0;

    va_list p_args;
    int r;
    int ret = -1;
    va_start(p_args, msg);
    r = vsprintf(printBuf, msg, p_args);
    va_end(p_args);

    if (r > 0)
    {
        ret = _write(0, printBuf, strlen(printBuf));
    }

    xMutexGive(printMutex);
    return ret;
}

void mcuDebug(DebugLevel level, char *msg, ...)
{
    if (xMutexTake(printMutex, 200) == pdFALSE)
        return;
    switch (level)
    {
    case DEBUG_LEVEL_NONE:
        xMutexGive(printMutex);
        return;
    case DEBUG_LEVEL_ERROR:
        _write(0, "\033[31m", 6);
        break;
    case DEBUG_LEVEL_WARNING:
        _write(0, "\033[33m", 6);
        break;
    case DEBUG_LEVEL_INFO:
        _write(0, "\033[32m", 6);
        break;
    case DEBUG_LEVEL_DEBUG:
        _write(0, "\033[35m", 6);
        break;
    default:
        xMutexGive(printMutex);
        return;
    }

    va_list p_args;
    int r;
    va_start(p_args, msg);
    r = vsprintf(printBuf, msg, p_args);
    va_end(p_args);
    /* printf(buf); */
    if (r > 0)
    {
        _write(0, printBuf, strlen(printBuf));
    }
    _write(0, "\033[0m", 5);
    // _write(0, "\r\n", 2);
    xMutexGive(printMutex);
}

void mcuDump(uint8_t *data, int len)
{
    if (xSemaphoreTake(printMutex, 200) == pdFALSE)
        return;

    for (int i = 0; i < len; i += 8)
    {
        int chunk = (len - i) > 8 ? 8 : (len - i);
        char hexStr[64] = {0};
        char asciiStr[9] = {0};

        // Формируем HEX-часть
        for (int j = 0; j < chunk; j++)
        {
            uint8_t byte = data[i + j];
            sprintf(hexStr + strlen(hexStr), "%02X ", byte);

            // Добавляем разделитель после 4 байт
            if (j == 3)
                strcat(hexStr, " ");

            // Формируем ASCII-часть
            asciiStr[j] = (byte >= 0x20 && byte <= 0x7E) ? byte : '.';
        }

        // Дополняем пробелами если неполная строка
        for (int j = chunk; j < 8; j++)
        {
            strcat(hexStr, "   ");
            if (j == 3)
                strcat(hexStr, " ");
            asciiStr[j] = ' ';
        }

        // Собираем полную строку
        sprintf(printBuf, "%.24s | %s\r\n", hexStr, asciiStr);
        _write(0, printBuf, strlen(printBuf));
    }

    xSemaphoreGive(printMutex);
}
