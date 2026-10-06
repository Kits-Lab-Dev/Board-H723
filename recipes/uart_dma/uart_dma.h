#ifndef UART_DMA_H
#define UART_DMA_H

#include <stddef.h>
#include <stdint.h>

// Приём USART1 (PB15, 115200) через DMA в кольцевой буфер, разбор строк-команд.
// Передача остаётся за логом (printf/INF — тоже USART1, DMA1 Stream2).
void uart_dma_start(void);

// Прочитать до len байт, принятых по UART; ждёт не дольше timeoutMs. Возвращает число байт.
size_t uartDmaRead(char *buf, size_t len, uint32_t timeoutMs);

#endif
