#include <stdint.h>
#include <string.h>
#include "uart_dma.h"
#include "app.h"
#include "usart.h"
#include "FreeRTOS.h"
#include "task.h"
#include "stream_buffer.h"

#define RX_DMA_SIZE 256      // кольцевой буфер DMA, кратен строке кэша (32 байта)
#define RX_STREAM_SIZE 1024  // буфер между прерыванием и задачей
#define LINE_MAX 128
#define DMA_IRQ_PRIORITY 6   // не выше configLIBRARY_MAX_SYSCALL_INTERRUPT_PRIORITY (5)
#define TASK_STACK_SIZE 512

static UART_HandleTypeDef *huart = &huart1;
static DMA_HandleTypeDef hdmaRx;
static uint8_t *rxBuf;  // в RAM_D1: DMA1 не видит DTCM, где лежат обычные static-массивы
static size_t rxPos;    // до какого места буфер уже разобран

static StreamBufferHandle_t stream;
static StaticStreamBuffer_t streamTcb;
static uint8_t streamStorage[RX_STREAM_SIZE + 1];
static StackType_t taskStack[TASK_STACK_SIZE];
static StaticTask_t taskTcb;

void DMA1_Stream3_IRQHandler(void)
{
    HAL_DMA_IRQHandler(&hdmaRx);
}

// Вызывается HAL на половине буфера, в конце буфера и на паузе в линии (IDLE).
// pos — сколько байт буфера заполнено с начала; DMA идёт по кругу, поэтому
// новые данные — от rxPos до pos (или до конца буфера и с начала до pos).
static void rxEvent(UART_HandleTypeDef *h, uint16_t pos)
{
    (void)h;
    BaseType_t woken = pdFALSE;
    // RAM_D1 кэшируется: без инвалидации процессор прочтёт из кэша старые байты
    SCB_InvalidateDCache_by_Addr(rxBuf, RX_DMA_SIZE);
    if (pos != rxPos)
    {
        if (pos > rxPos)
        {
            xStreamBufferSendFromISR(stream, rxBuf + rxPos, pos - rxPos, &woken);
        }
        else
        {
            xStreamBufferSendFromISR(stream, rxBuf + rxPos, RX_DMA_SIZE - rxPos, &woken);
            xStreamBufferSendFromISR(stream, rxBuf, pos, &woken);
        }
        rxPos = pos == RX_DMA_SIZE ? 0 : pos;
    }
    portYIELD_FROM_ISR(woken);
}

static void startRx(void)
{
    rxPos = 0;
    HAL_UARTEx_ReceiveToIdle_DMA(huart, rxBuf, RX_DMA_SIZE);
}

// Шум на линии (frame/noise error) останавливает DMA — запускаем приём заново
static void rxError(UART_HandleTypeDef *h)
{
    (void)h;
    startRx();
}

size_t uartDmaRead(char *buf, size_t len, uint32_t timeoutMs)
{
    return xStreamBufferReceive(stream, buf, len, pdMS_TO_TICKS(timeoutMs));
}

static void handleLine(char *line)
{
    if (strcmp(line, "help") == 0)
    {
        INF("uart_dma: commands: help, led on, led off, uptime\n");
    }
    else if (strcmp(line, "led on") == 0 || strcmp(line, "led off") == 0)
    {
        appLedHeartbeat = false;
        HAL_GPIO_WritePin(LED_GPIO_Port, LED_Pin, line[5] == 'n' ? GPIO_PIN_SET : GPIO_PIN_RESET);
        INF("uart_dma: %s\n", line);
    }
    else if (strcmp(line, "uptime") == 0)
    {
        INF("uart_dma: uptime %lu ms\n", xTaskGetTickCount() * portTICK_PERIOD_MS);
    }
    else
    {
        INF("uart_dma: rx '%s' (%u bytes)\n", line, (unsigned)strlen(line));
    }
}

// Собирает строки до \r или \n и отдаёт их в handleLine
static void uartTask(void *arg)
{
    (void)arg;
    char line[LINE_MAX];
    size_t n = 0;
    char chunk[32];
    for (;;)
    {
        size_t got = uartDmaRead(chunk, sizeof(chunk), 1000);
        for (size_t i = 0; i < got; i++)
        {
            char c = chunk[i];
            if (c == '\r' || c == '\n')
            {
                if (n == 0)
                    continue;
                line[n] = '\0';
                handleLine(line);
                n = 0;
            }
            else if (n < LINE_MAX - 1)
            {
                line[n++] = c;
            }
        }
    }
}

void uart_dma_start(void)
{
    rxBuf = pvPortMalloc(RX_DMA_SIZE + 32);
    if (!rxBuf)
    {
        ERR("uart_dma: no memory\n");
        return;
    }
    // Буфер выровнен по строке кэша: инвалидация не заденет соседние данные
    rxBuf = (uint8_t *)(((uintptr_t)rxBuf + 31) & ~(uintptr_t)31);

    stream = xStreamBufferCreateStatic(RX_STREAM_SIZE, 1, streamStorage, &streamTcb);
    xTaskCreateStatic(uartTask, "uartRx", TASK_STACK_SIZE, NULL, tskIDLE_PRIORITY + 2, taskStack, &taskTcb);

    // RX без подтяжки «висит в воздухе», если к PB15 ничего не подключено, и ловит шум
    GPIO_InitTypeDef gpio = {
        .Pin = GPIO_PIN_15,
        .Mode = GPIO_MODE_AF_PP,
        .Pull = GPIO_PULLUP,
        .Speed = GPIO_SPEED_FREQ_LOW,
        .Alternate = GPIO_AF4_USART1,
    };
    HAL_GPIO_Init(GPIOB, &gpio);

    // DMA1 Stream3 свободен (Stream0/1 — SPI5 у ESP32-C2, Stream2 — передача лога)
    hdmaRx.Instance = DMA1_Stream3;
    hdmaRx.Init.Request = DMA_REQUEST_USART1_RX;
    hdmaRx.Init.Direction = DMA_PERIPH_TO_MEMORY;
    hdmaRx.Init.PeriphInc = DMA_PINC_DISABLE;
    hdmaRx.Init.MemInc = DMA_MINC_ENABLE;
    hdmaRx.Init.PeriphDataAlignment = DMA_PDATAALIGN_BYTE;
    hdmaRx.Init.MemDataAlignment = DMA_MDATAALIGN_BYTE;
    hdmaRx.Init.Mode = DMA_CIRCULAR;
    hdmaRx.Init.Priority = DMA_PRIORITY_LOW;
    hdmaRx.Init.FIFOMode = DMA_FIFOMODE_DISABLE;
    if (HAL_DMA_Init(&hdmaRx) != HAL_OK)
    {
        ERR("uart_dma: DMA init failed\n");
        return;
    }
    __HAL_LINKDMA(huart, hdmarx, hdmaRx);
    HAL_NVIC_SetPriority(DMA1_Stream3_IRQn, DMA_IRQ_PRIORITY, 0);
    HAL_NVIC_EnableIRQ(DMA1_Stream3_IRQn);

    HAL_UART_RegisterRxEventCallback(huart, rxEvent);
    HAL_UART_RegisterCallback(huart, HAL_UART_ERROR_CB_ID, rxError);
    startRx();
    INF("uart_dma: started, type 'help' in USART1 terminal\n");
}
