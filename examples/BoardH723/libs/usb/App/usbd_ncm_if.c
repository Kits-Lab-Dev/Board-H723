#include "usbd_ncm_if.h"
#include "xNetworkInterface.h"
#include "dhcpserver.h"

uint8_t UserRxBufferHS[APP_RX_DATA_SIZE];
uint8_t UserTxBufferHS[APP_TX_DATA_SIZE];
uint8_t ncmMACAddress[6] = {NCM_MAC_ADDR0, NCM_MAC_ADDR1, NCM_MAC_ADDR2, NCM_MAC_ADDR3, NCM_MAC_ADDR4, NCM_MAC_ADDR5};

extern USBD_HandleTypeDef hUsbDeviceHS;

QueueHandle_t rxQueue = NULL;
TaskHandle_t rxTaskHandle;

#define NUM_RX_BUFFERS 4                // Количество буферов в пуле
#define RX_BUFFER_SIZE APP_RX_DATA_SIZE // Максимальный размер пакета

typedef struct
{
    uint8_t buffer[NUM_RX_BUFFERS][RX_BUFFER_SIZE];
    uint32_t lengths[NUM_RX_BUFFERS];
    volatile uint8_t write_idx;
    uint8_t read_idx;
} RxBufferPool;

static RxBufferPool *rx_pool = NULL;

typedef struct
{
    uint8_t buf_idx; // Индекс буфера в пуле
} RxMsg;

static int8_t NCM_Init();
static int8_t NCM_DeInit();
static int8_t NCM_Receive(uint8_t *pbuf, uint32_t Len);

USBD_NCM_ItfTypeDef USBD_Interface_fops_HS =
    {
        NCM_Init,
        NCM_DeInit,
        NCM_Receive};

static bool isConnected = false;
static bool lastConnectedState = false;

static void rxTask(void *)
{
    rx_pool = pvPortMalloc(sizeof(RxBufferPool));
    memset(rx_pool, 0, sizeof(RxBufferPool));
    rxQueue = xQueueCreate(NUM_RX_BUFFERS, sizeof(RxMsg));
    RxMsg msg;
    for (;;)
    {
        if (xQueueReceive(rxQueue, &msg, 1000) == pdPASS)
        {
            // Обрабатываем данные из пула
            ncmNetworkInterfaceInput(rx_pool->buffer[msg.buf_idx], rx_pool->lengths[msg.buf_idx]);
            // Освобождаем буфер
            // __disable_irq();
            rx_pool->read_idx = (msg.buf_idx + 1) % NUM_RX_BUFFERS;
            // __enable_irq();
        }

        if (isConnected == true && lastConnectedState == false)
        {
            vNetworkNotifyIFUp(NCM_IF);
        }
        else if (isConnected == false && lastConnectedState == true)
        {
            vNetworkNotifyIFDown(NCM_IF);
        }
        lastConnectedState = isConnected;
    }
}

static int8_t NCM_Init()
{
    USBD_NCM_SetTxBuffer(&hUsbDeviceHS, UserTxBufferHS, 0);
    USBD_NCM_SetRxBuffer(&hUsbDeviceHS, UserRxBufferHS);
    USBD_NCM_Connect(&hUsbDeviceHS, 12000000);
    isConnected = true;
    return (USBD_OK);
}

static int8_t NCM_DeInit()
{
    isConnected = false;

    return (USBD_OK);
}

static int8_t NCM_Receive(uint8_t *Buf, uint32_t len)
{
    // __disable_irq();
    if (rx_pool && rxQueue && len <= RX_BUFFER_SIZE && isConnected)
    {
        uint8_t next_idx = (rx_pool->write_idx + 1) % NUM_RX_BUFFERS;
        
        // Проверяем что следующий буфер свободен
        if (next_idx != rx_pool->read_idx)
        {
            // Копируем данные в статический буфер
            memcpy(rx_pool->buffer[rx_pool->write_idx], Buf, len);
            rx_pool->lengths[rx_pool->write_idx] = len;

            RxMsg msg;
            msg.buf_idx = rx_pool->write_idx;

            // Обновляем индекс записи
            rx_pool->write_idx = next_idx;

            xQueueSendFromISR(rxQueue, &msg, pdFALSE);
        }
        
    }
    // __enable_irq();
    return (USBD_OK);
}

uint8_t NCM_Transmit(uint8_t *Buf, uint16_t len)
{
    if (isConnected != true || lastConnectedState != true)
        return USBD_BUSY;

    uint8_t result = USBD_OK;
    USBD_NCM_HandleTypeDef *hcdc = (USBD_NCM_HandleTypeDef *)hUsbDeviceHS.pClassData;

    if (hcdc == NULL || hcdc->TxState != 0 || hcdc->Notify.Connection.wValue == 0)
    {
        return USBD_BUSY;
    }

    uint16_t wlen = ((len + 3) & ~3U) + 4;

    if ((sizeof(NTH16_Header) + sizeof(NDP16_Header) + wlen) > USBD_NCM_MAX_IN_SIZE)
    {
        return USBD_FAIL;
    }

    uint16_t header_size = sizeof(NTH16_Header);
    uint16_t ndp_offset = header_size + wlen - 4;
    uint16_t data_offset = header_size;

    NTH16_Header *nth = (NTH16_Header *)hcdc->TxBuffer;
    nth->dwSignature = NCM_NTH16_SIGNATURE;
    nth->wHeaderLength = sizeof(NTH16_Header);
    nth->wSequence = hcdc->TxSequence++;
    nth->wBlockLength = header_size + sizeof(NDP16_Header) + wlen;
    nth->wNdpIndex = ndp_offset;

    NDP16_Header *ndp = (NDP16_Header *)((uint8_t *)nth + ndp_offset);
    ndp->dwSignature = NCM_NDP16_SIGNATURE;
    ndp->wLength = sizeof(NDP16_Header) + 4;
    ndp->wNextNdpIndex = 0;
    ndp->Datagram[0].Index = header_size;
    ndp->Datagram[0].Length = len;
    ndp->Datagram[1].Index = 0;
    ndp->Datagram[1].Length = 0;

    hcdc->TxLength = nth->wBlockLength;
    hcdc->NthPtr = (uint8_t *)nth;

    uint8_t *data_ptr = hcdc->TxBuffer + data_offset;
    memcpy(data_ptr, Buf, len);
    USBD_NCM_SetTxBuffer(&hUsbDeviceHS, hcdc->TxBuffer, hcdc->TxLength);
    result = USBD_NCM_TransmitPacket(&hUsbDeviceHS);
    return result;
}

extern NetworkInterface_t ncmNetInterface;

#define NCM_TASK_STACK_SIZE (1024 * 2)
#define NCM_TASK_PRIORITY (ipconfigIP_TASK_PRIORITY - 1)
static StackType_t taskStack[NCM_TASK_STACK_SIZE] __ITCMRAM; 
static StaticTask_t taskBuffer __ITCMRAM;

void ncmInit()
{
    static DHCPServer dhcpServer;
    dhcpServer.stopRequest = false;
    dhcpServer.ipAddr.ulIP_IPv4 = FreeRTOS_inet_addr("192.168.4.1");
    dhcpServer.subnetMask.ulIP_IPv4 = FreeRTOS_inet_addr("255.255.255.0");
    dhcpServer.gateway.ulIP_IPv4 = FreeRTOS_inet_addr("0.0.0.0");
    dhcpServer.netInterface = &ncmNetInterface;
    dhcpAddServer(&dhcpServer);

    xTaskCreateStatic(rxTask, "ncmRxTask", 2048, NULL, NCM_TASK_PRIORITY, taskStack, &taskBuffer);
}