#include <driver.h>
#include "control.h"
#include "xNetworkInterface.h"
#include "app.h"
#include "bt.h"

#define ESP_UART 1

#if (ESP_UART)
#include "usart.h"
static uint8_t uartRxb;
static char uartRxMsg[1024];
static int uartRxIndex = 0;
static QueueHandle_t uartRxQueue;

static void UART_RX_Callback(UART_HandleTypeDef *)
{
    xQueueSendFromISR(uartRxQueue, &uartRxb, NULL);
    HAL_UART_RegisterCallback(&huart7, HAL_UART_RX_COMPLETE_CB_ID, UART_RX_Callback);
    HAL_UART_Receive_IT(&huart7, &uartRxb, 1);
}

static void UART_RX_Task(void *)
{
    uartRxQueue = xQueueCreate(32, sizeof(char));
    char b;
    while (1)
    {
        xQueueReceive(uartRxQueue, &b, portMAX_DELAY);
        if (b == '\r' || b == '\n')
        {
            uartRxMsg[uartRxIndex++] = '\n';
            uartRxMsg[uartRxIndex] = '\0';
            uartRxIndex = 0;
            DBG(uartRxMsg);
        }
        else
        {
            uartRxMsg[uartRxIndex++] = b;
        }
    }
}
#endif

#define TRANSACTION_TASK_STACK_SIZE (1024)
#define TRANSACTION_TASK_PRIORITY ipconfigIP_TASK_PRIORITY
static StackType_t transactionTaskStack[TRANSACTION_TASK_STACK_SIZE] = {0}; // Declare the stack for the Lua task
static StaticTask_t transactionTaskBuffer = {0};

typedef struct
{
    __IO uint32_t ISR; /*!< DMA interrupt status register */
    __IO uint32_t Reserved0;
    __IO uint32_t IFCR; /*!< DMA interrupt flag clear register */
} DMA_Base_Registers;

extern NetworkInterface_t staNetInterface;
extern NetworkInterface_t apNetInterface;

static SPI_HandleTypeDef *hspi = &hspi5;
static DMA_HandleTypeDef *hdmarx;
static DMA_HandleTypeDef *hdmatx;

uint8_t rxbuff[SPI_BUF_LEN] __attribute__((section(".spi_buf_section")));
uint8_t txbuff[SPI_BUF_LEN] __attribute__((section(".spi_buf_section")));

static SemaphoreHandle_t transactionSem;
static SemaphoreHandle_t transactionMutex;
static QueueHandle_t transactionQueue;
static SemaphoreHandle_t txCmpltSem;

static void transactionTask(void *pvParameters);

bool spiTransaction(uint8_t *tx);

static void spiDMAInit(void)
{
    hdmarx = hspi->hdmarx;
    hdmatx = hspi->hdmatx;

    hspi->RxISR = NULL;
    hspi->TxISR = NULL;

    SPI_2LINES(hspi);

    __HAL_DMA_DISABLE(hdmarx);
    __HAL_DMA_DISABLE(hdmatx);
    /* Reset the Tx/Rx DMA bits */
    CLEAR_BIT(hspi->Instance->CFG1, SPI_CFG1_TXDMAEN | SPI_CFG1_RXDMAEN);

    hdmarx->DMAmuxChannelStatus->CFR = hdmarx->DMAmuxChannelStatusMask;
    DMA_Base_Registers *regs_dmarx = (DMA_Base_Registers *)hdmarx->StreamBaseAddress;
    /* Clear all interrupt flags at correct offset within the register */
    regs_dmarx->IFCR = 0x3FUL << (hdmarx->StreamIndex & 0x1FU);
    /* Clear DBM bit */
    ((DMA_Stream_TypeDef *)hdmarx->Instance)->CR &= (uint32_t)(~DMA_SxCR_DBM);
    /* Configure DMA Stream data length */
    ((DMA_Stream_TypeDef *)hdmarx->Instance)->NDTR = (uint32_t)SPI_BUF_LEN;
    ((DMA_Stream_TypeDef *)hdmarx->Instance)->PAR = (uint32_t)&hspi->Instance->RXDR;
    ((DMA_Stream_TypeDef *)hdmarx->Instance)->M0AR = (uint32_t)rxbuff;

    /* Enable Common interrupts*/
    __HAL_DMA_DISABLE_IT(hdmarx, DMA_IT_TE | DMA_IT_HT | DMA_IT_DME);
    __HAL_DMA_DISABLE_IT(hdmarx, DMA_IT_FE);

    hdmatx->DMAmuxChannelStatus->CFR = hdmatx->DMAmuxChannelStatusMask;
    DMA_Base_Registers *regs_dmatx = (DMA_Base_Registers *)hdmatx->StreamBaseAddress;
    /* Clear all interrupt flags at correct offset within the register */
    regs_dmatx->IFCR = 0x3FUL << (hdmatx->StreamIndex & 0x1FU);
    /* Clear DBM bit */
    ((DMA_Stream_TypeDef *)hdmatx->Instance)->CR &= (uint32_t)(~DMA_SxCR_DBM);
    /* Configure DMA Stream data length */
    ((DMA_Stream_TypeDef *)hdmatx->Instance)->NDTR = (uint32_t)SPI_BUF_LEN;
    ((DMA_Stream_TypeDef *)hdmatx->Instance)->PAR = (uint32_t)&hspi->Instance->TXDR;
    ((DMA_Stream_TypeDef *)hdmatx->Instance)->M0AR = (uint32_t)txbuff;

    MODIFY_REG(hspi->Instance->CR2, SPI_CR2_TSIZE, (uint16_t)SPI_BUF_LEN);
    /* Enable Tx DMA Request */

    __HAL_DMA_DISABLE_IT(hdmatx, DMA_IT_TE | DMA_IT_HT | DMA_IT_DME);
    __HAL_DMA_DISABLE_IT(hdmatx, DMA_IT_FE);

    /* Enable the SPI Error Interrupt Bit */
    __HAL_SPI_DISABLE_IT(hspi, (SPI_IT_EOT | SPI_IT_OVR | SPI_IT_UDR | SPI_IT_FRE | SPI_IT_MODF));
}

bool spiDriverInit(void)
{
#if (ESP_UART)
    xTaskCreate(UART_RX_Task, "espuartrx", 1024, NULL, 5, NULL);
    HAL_UART_RegisterCallback(&huart7, HAL_UART_RX_COMPLETE_CB_ID, UART_RX_Callback);
    HAL_UART_Receive_IT(&huart7, &uartRxb, 1);
#endif

    spiDMAInit();

    transactionSem = xSemaphoreCreateBinary();
    txCmpltSem = xSemaphoreCreateBinary();
    transactionMutex = xSemaphoreCreateMutex();
    transactionQueue = xQueueCreate(20, sizeof(p_spi_buf));

    if (transactionSem == NULL || transactionMutex == NULL || transactionQueue == NULL)
    {
        return false;
    }
    if (xTaskCreateStatic(transactionTask, "transactionTask", TRANSACTION_TASK_STACK_SIZE, NULL, TRANSACTION_TASK_PRIORITY, transactionTaskStack, &transactionTaskBuffer) == NULL)
    {
        return false;
    }
    vTaskDelay(10);
    return true;
}

bool spiDriverWrite(p_spi_buf *buf)
{
    bool ret = xQueueSend(transactionQueue, buf, portMAX_DELAY) == pdTRUE;
    if (!ret)
        return false;
    xSemaphoreGive(transactionSem);
    return true;
}

static void transactionTask(void *pvParameters)
{
    UNUSED(pvParameters);

    p_spi_buf buf;

    for (;;)
    {
        // Уровни, а не только фронты: пока ESP готова (handshake) и есть что передать
        // или забрать (data ready), транзакции идут подряд без сна
        bool handshake = HAL_GPIO_ReadPin(ESP_HANDSHAKE_GPIO_Port, ESP_HANDSHAKE_Pin) == GPIO_PIN_SET;
        bool dataReady = HAL_GPIO_ReadPin(ESP_DATA_READY_GPIO_Port, ESP_DATA_READY_Pin) == GPIO_PIN_SET;
        if (handshake && (dataReady || uxQueueMessagesWaiting(transactionQueue) > 0))
        {
            if (xQueueReceive(transactionQueue, &buf, 0) == pdTRUE && buf.data && buf.len)
            {
                spi_buf *tx = (spi_buf *)txbuff;
                memcpy(tx->data, buf.data, buf.len);
                tx->len = buf.len;
                tx->type = buf.type;

                if (buf.free_data_fn && buf.is_released)
                {
                    buf.free_data_fn(buf.data);
                }

                spiTransaction(txbuff);
            }
            else
            {
                spiTransaction(NULL);
            }
            continue;
        }
        // Ждать фронта handshake/data ready или нового кадра на отправку
        xSemaphoreTake(transactionSem, 1);
    }
}

void ESP_EXTI_Callback()
{
    if (transactionSem != NULL)
    {
        // разбудить transactionTask сразу, а не на следующем тике планировщика
        BaseType_t woken = pdFALSE;
        xSemaphoreGiveFromISR(transactionSem, &woken);
        portYIELD_FROM_ISR(woken);
    }
}

bool spiTransaction(uint8_t *tx)
{
    if (xSemaphoreTake(transactionMutex, portMAX_DELAY) != pdTRUE)
    {
        return false;
    }

    bool ret = false;
    if (!tx)
    {
        memset(txbuff, 0, 3); // стираем только type и len
        tx = txbuff;
    }
    uint32_t alignedAddr;
    alignedAddr = (uint32_t)tx & ~0x1F;
    SCB_CleanDCache_by_Addr((uint32_t *)alignedAddr, SPI_BUF_LEN + ((uint32_t)tx - alignedAddr));

    ((DMA_Base_Registers *)hdmarx->StreamBaseAddress)->IFCR = 0x3FUL << (hdmarx->StreamIndex & 0x1FU);
    ((DMA_Base_Registers *)hdmatx->StreamBaseAddress)->IFCR = 0x3FUL << (hdmatx->StreamIndex & 0x1FU);

    __HAL_DMA_DISABLE(hdmarx);
    __HAL_DMA_DISABLE(hdmatx);

    ((DMA_Stream_TypeDef *)hdmarx->Instance)->NDTR = (uint32_t)SPI_BUF_LEN;

    ((DMA_Stream_TypeDef *)hdmatx->Instance)->NDTR = (uint32_t)SPI_BUF_LEN;

    SET_BIT(hspi->Instance->CFG1, SPI_CFG1_RXDMAEN | SPI_CFG1_TXDMAEN);
    __HAL_SPI_ENABLE_IT(hspi, SPI_IT_EOT);

    __HAL_DMA_ENABLE(hdmatx);
    __HAL_DMA_ENABLE(hdmarx);

    __HAL_SPI_ENABLE(hspi);
    /* Master transfer start */
    SET_BIT(hspi->Instance->CR1, SPI_CR1_CSTART);

    ret = xSemaphoreTake(txCmpltSem, 1000) == pdTRUE;

    if (ret)
    {
        uint8_t *rx = rxbuff;
        alignedAddr = (uint32_t)rx & ~0x1F;
        SCB_CleanDCache_by_Addr((uint32_t *)alignedAddr, SPI_BUF_LEN + ((uint32_t)rx - alignedAddr));
        spi_buf *buf = (spi_buf *)rx;
        if ((!buf->len) || (buf->len > SPI_BUF_DATA_LEN) || buf->type > ESP_CONTROL)
        {
            ret = false;
        }
        else
        {
            switch (buf->type)
            {
            case ESP_STA:
                xNetworkInterfaceInput(&staNetInterface, buf);
                break;
            case ESP_AP:
                xNetworkInterfaceInput(&apNetInterface, buf);
                break;
            case ESP_BT:
                bt_rx_process(buf->data, buf->len);
                break;
            case ESP_CONTROL:
                control_rx_process(buf->data, buf->len);
                break;
            default:
                break;
            }
        }
    }
    xSemaphoreGive(transactionMutex);
    return ret;
}

void DMA1_Stream0_IRQHandler(void)
{
    DMA_Base_Registers *regs_dma = (DMA_Base_Registers *)hdmarx->StreamBaseAddress;

    uint32_t tmpisr_dma = regs_dma->ISR;
    if ((tmpisr_dma & (DMA_FLAG_TCIF0_4 << (hdmarx->StreamIndex & 0x1FU))) != 0U)
    {
        if (__HAL_DMA_GET_IT_SOURCE(hdmarx, DMA_IT_TC) != 0U)
        {
            regs_dma->IFCR = DMA_FLAG_TCIF0_4 << (hdmarx->StreamIndex & 0x1FU);
            xSemaphoreGiveFromISR(txCmpltSem, NULL);
        }
    }
    /* Clear all interrupt flags at correct offset within the register */
    regs_dma->IFCR = 0x3FUL << (hdmarx->StreamIndex & 0x1FU);
}

/**
 * @brief This function handles DMA1 stream1 global interrupt.
 */
void DMA1_Stream1_IRQHandler(void)
{
    DMA_Base_Registers *regs_dma = (DMA_Base_Registers *)hdmatx->StreamBaseAddress;

    uint32_t tmpisr_dma = regs_dma->ISR;
    if ((tmpisr_dma & (DMA_FLAG_TCIF0_4 << (hdmatx->StreamIndex & 0x1FU))) != 0U)
    {
        if (__HAL_DMA_GET_IT_SOURCE(hdmatx, DMA_IT_TC) != 0U)
        {
            xSemaphoreGiveFromISR(txCmpltSem, NULL);
        }
    }
    /* Clear all interrupt flags at correct offset within the register */
    regs_dma->IFCR = 0x3FUL << (hdmatx->StreamIndex & 0x1FU);
}

void SPI5_IRQHandler(void)
{
    uint32_t itsource = hspi->Instance->IER;
    uint32_t itflag = hspi->Instance->SR;
    uint32_t trigger = itsource & itflag;

    if (HAL_IS_BIT_SET(trigger, SPI_FLAG_EOT))
    {
        /* Clear EOT/TXTF/SUSP flag */
        __HAL_SPI_CLEAR_EOTFLAG(hspi);
        __HAL_SPI_CLEAR_TXTFFLAG(hspi);
        __HAL_SPI_CLEAR_SUSPFLAG(hspi);

        /* Disable EOT interrupt */
        __HAL_SPI_DISABLE_IT(hspi, SPI_IT_EOT);

        /* Disable Tx DMA Request */
        CLEAR_BIT(hspi->Instance->CFG1, SPI_CFG1_TXDMAEN | SPI_CFG1_RXDMAEN);

        BaseType_t woken = pdFALSE;
        xSemaphoreGiveFromISR(txCmpltSem, &woken);
        portYIELD_FROM_ISR(woken);
    }
}