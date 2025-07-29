#ifndef __USB_NCM_H
#define __USB_NCM_H

#ifdef __cplusplus
extern "C"
{
#endif


#include "usbd_ioreq.h"

#ifndef NCM_IN_EP
#define NCM_IN_EP 0x81U /* EP1 for data IN */
#endif                  /* NCM_IN_EP */
#ifndef NCM_OUT_EP
#define NCM_OUT_EP 0x01U /* EP1 for data OUT */
#endif                   /* NCM_OUT_EP */
#ifndef NCM_CMD_EP
#define NCM_CMD_EP 0x82U /* EP2 for NCM commands */
#endif                   /* NCM_CMD_EP  */

#ifndef NCM_HS_BINTERVAL
#define NCM_HS_BINTERVAL 0x07U
#endif /* NCM_HS_BINTERVAL */

#ifndef NCM_FS_BINTERVAL
#define NCM_FS_BINTERVAL 0x0AU
#endif /* NCM_FS_BINTERVAL */

    /* NCM Endpoints parameters: you can fine tune these values depending on the needed baudrates and performance. */

#define NCM_CMD_PACKET_SIZE 16U /* Control Endpoint Packet size */

#define USB_NCM_CONFIG_DESC_SIZ 93U

#define NCM_MAX_SEGMENT_SIZE 1514

#define NCM_REQ_MAX_DATA_SIZE 0x7U

#ifndef USBD_NCM_MAX_OUT_SIZE
#define USBD_NCM_MAX_OUT_SIZE 2048
#endif
#ifndef USBD_NCM_MAX_IN_SIZE
#define USBD_NCM_MAX_IN_SIZE 2048
#endif

#define NCM_NTH16_SIGNATURE 0x484D434E // 'NCMH'
#define NCM_NDP16_SIGNATURE 0x304D434E // 'NCM0'
#define NCM_NTH32_SIGNATURE 0x686D636E // 'NCMH'
#define NCM_NDP32_SIGNATURE 0x306D636E // 'NCM0'

    /*---------------------------------------------------------------------*/
    /*  NCM definitions                                                    */
    /*---------------------------------------------------------------------*/
    typedef enum
    {
        CDC_SEND_ENCAPSULATED_COMMAND = 0x00, /*!< Host command in the supported control protocol format */
        CDC_GET_ENCAPSULATED_RESPONSE = 0x01, /*!< Device response in the supported control protocol format */
        CDC_SET_COMM_FEATURE = 0x02,          /*!< Change a feature (indexed by wIndex) */
        CDC_GET_COMM_FEATURE = 0x03,          /*!< Read a feature (indexed by wIndex) */
        CDC_CLEAR_COMM_FEATURE = 0x04,        /*!< Reset a feature (indexed by wIndex) to default state */
        CDC_SET_LINE_CODING = 0x20,           /*!< Set asynchronous line-character formatting properties */
        CDC_GET_LINE_CODING = 0x21,           /*!< Read the current line coding */
        CDC_SET_CONTROL_LINE_STATE = 0x22,    /*!< Generate RS-232 control signals
                                                       (wValue = 0b[RTS activation][DTR activation]) */
        CDC_SEND_BREAK = 0x23,                /*!< Generates an RS-232 style break
                                                       (wValue = break length in ms,
                                                       if = 0xFFFF it is continuous until
                                                       another request with 0 is received) */

        /* CDC class request for NCM */
        CDC_SET_ENET_MULTICAST_FILTERS = 0x40, /*!< Controls the receipt of Ethernet frames
                                                        that are received with �multicast� destination addresses */
        CDC_SET_ENET_PWR_MGMT_PFILTER = 0x41,  /*!<  */
        CDC_GET_ENET_PWR_MGMT_PFILTER = 0x42,  /*!< Retrieves the status of the above power management pattern filter setting */
        CDC_SET_ENET_PACKET_FILTER = 0x43,     /*!< Controls the types of Ethernet frames that are to be received via the function */
        CDC_GET_ENET_STATISTIC = 0x44,         /*!< Retrieves Ethernet statistics such as
                                                        frames transmitted, frames received, and bad frames received */
        CDC_GET_NTB_PARAMETERS = 0x80,         /*!< Requests the function to report parameters that characterize the NCB */
        CDC_GET_NET_ADDRESS = 0x81,            /*!< Requests the current EUI-48 network address */
        CDC_SET_NET_ADDRESS = 0x82,            /*!< Changes the current EUI-48 network address */
        CDC_GET_NTB_FORMAT = 0x83,             /*!< Get current NTB Format */
        CDC_SET_NTB_FORMAT = 0x84,             /*!< Select 16 or 32 bit Network Transfer Blocks */
        CDC_GET_NTB_INPUT_SIZE = 0x85,         /*!< Get the current value of maximum NTB input size */
        CDC_SET_NTB_INPUT_SIZE = 0x86,         /*!< Selects the maximum size of NTBs to be transmitted
                                                        by the function over the bulk IN pipe */
        CDC_GET_MAX_DATAGRAM_SIZE = 0x87,      /*!< Requests the current maximum datagram size */
        CDC_SET_MAX_DATAGRAM_SIZE = 0x88,      /*!< Sets the maximum datagram size to a value other than the default */
        CDC_GET_CRC_MODE = 0x89,               /*!< Requests the current CRC mode */
        CDC_SET_CRC_MODE = 0x90,               /*!< Sets the current CRC mode */
    } USBD_CDC_RequestType;

    typedef enum
    {
        USB_DIRECTION_OUT = 0, /*!< Host to device */
        USB_DIRECTION_IN = 1,  /*!< Device to host */
    } USB_DirectionType;

    typedef enum
    {
        CDC_NOT_NETWORK_CONNECTION = 0x00,      /*!< Notify the host about network connection status (Value = 1 / 0) */
        CDC_NOT_RESPONSE_AVAILABLE = 0x01,      /*!< Notify the host that an encapsulated response is available */
        CDC_NOT_SERIAL_STATE = 0x29,            /*!< Asynchronous notification of UART status */
        CDC_NOT_CONNECTION_SPEED_CHANGE = 0x2A, /*!< Change in the uplink / downlink bit rate of the connection */
    } USBD_CDC_NotificationType;

    typedef struct
    {
        uint32_t bitrate;
        uint8_t format;
        uint8_t paritytype;
        uint8_t datatype;
    } USBD_NCM_LineCodingTypeDef;

    typedef struct __packed
    {
        uint16_t Length;                /* Size in bytes of this NTBT structure */
        uint16_t NtbFormatsSupported;   /* 1 if only 16bit, 3 if 32bit is supported as well */
        uint32_t NtbInMaxSize;          /* IN NTB Maximum Size in bytes */
        uint16_t NdpInDivisor;          /* Divisor used for IN NTB Datagram payload alignment */
        uint16_t NdpInPayloadRemainder; /* Remainder used to align input datagram payload within the NTB */
        uint16_t NdpInAlignment;        /* Datagram alignment */
        uint16_t reserved;              /* Keep 0 */
        uint32_t NtbOutMaxSize;
        uint16_t NdpOutDivisor;
        uint16_t NdpOutPayloadRemainder;
        uint16_t NdpOutAlignment;
        uint16_t NtbOutMaxDatagrams; /* Maximum number of datagrams in a single OUT NTB */
    } USBD_NCM_ParametersType;

    /**
     * @brief  Структура уведомления CDC-NCM согласно спецификации
     */
    typedef struct __packed
    {
        uint8_t bmRequestType;     // 0xA1 (CLASS | INTERFACE | IN)
        uint8_t bNotificationType; // Тип уведомления (CDC_NOTIFY_NETWORK_CONNECTION и т.д.)
        uint16_t wValue;           // Зависит от типа уведомления
        uint16_t wIndex;           // Номер интерфейса
        uint16_t wLength;          // Длина данных
    } NCM_NotificationHeader;

    typedef struct __packed
    {
        uint32_t DLBitRate;         // скорость передачи данных приема (канал IN) */
        uint32_t ULBitRate;         // скорость передачи данных передачи (канал OUT) */
    } USBD_CDC_SpeedChangeType;

    typedef struct
    {
        uint8_t CmdOpCode;
        uint8_t CmdLength;
        uint8_t *RxBuffer;
        uint8_t *TxBuffer;
        uint32_t RxLength;
        uint32_t TxLength;

        __IO uint32_t TxState;
        __IO uint32_t RxState;
        uint8_t altSetting;

        USBD_NCM_ParametersType NCMParam;
        uint8_t NetAddr[6];
        struct __packed
        {
            NCM_NotificationHeader SpeedChange;
            USBD_CDC_SpeedChangeType SpeedData;
            NCM_NotificationHeader Connection;
        } Notify;            // Буфер для уведомлений (максимальный размер + заголовок)
        uint16_t TxSequence; // Счетчик последовательности передач
        uint8_t *NthPtr;     // Указатель на NTH заголовок
        uint8_t *NdpPtr;     // Указатель на NDP заголовок (при необходимости)
    } USBD_NCM_HandleTypeDef;

    // Структуры дескрипторов
    typedef struct __packed
    {
        uint8_t bLength;
        uint8_t bDescriptorType;
        uint16_t wTotalLength;
        uint8_t bNumInterfaces;
        uint8_t bConfigurationValue;
        uint8_t iConfiguration;
        uint8_t bmAttributes;
        uint8_t bMaxPower;
    } USB_ConfigDescriptor;

    typedef struct __packed
    {
        uint8_t bLength;
        uint8_t bDescriptorType;
        uint8_t bInterfaceNumber;
        uint8_t bAlternateSetting;
        uint8_t bNumEndpoints;
        uint8_t bInterfaceClass;
        uint8_t bInterfaceSubClass;
        uint8_t bInterfaceProtocol;
        uint8_t iInterface;
    } USB_InterfaceDescriptor;

    // Класс-специфичные дескрипторы CDC
    typedef struct __packed
    {
        uint8_t bFunctionLength;
        uint8_t bDescriptorType;
        uint8_t bDescriptorSubtype;
        uint16_t bcdCDC;
    } CDC_HeaderFunctionalDescriptor;

    typedef struct __packed
    {
        uint8_t bFunctionLength;
        uint8_t bDescriptorType;
        uint8_t bDescriptorSubtype;
        uint8_t bMasterInterface;
        uint8_t bSlaveInterface;
    } CDC_UnionFunctionalDescriptor;

    typedef struct __packed
    {
        uint8_t bFunctionLength;
        uint8_t bDescriptorType;
        uint8_t bDescriptorSubtype;
        uint8_t iMACAddress;
        uint32_t bmEthernetStatistics;
        uint16_t wMaxSegmentSize;
        uint16_t wNumberMCFilters;
        uint8_t bNumberPowerFilters;
    } CDC_EthernetFunctionalDescriptor;

    typedef struct __packed
    {
        uint8_t bLength;
        uint8_t bDescriptorType;
        uint8_t bEndpointAddress;
        uint8_t bmAttributes;
        uint16_t wMaxPacketSize;
        uint8_t bInterval;
    } USB_EndpointDescriptor;

    typedef struct __packed
    {
        uint8_t bFunctionLength;
        uint8_t bDescriptorType;
        uint8_t bDescriptorSubtype;
        uint16_t bcdNcmVersion;
        uint8_t bmNetworkCapabilities;
    } NCM_FunctionalDescriptor;

    typedef struct __packed
    {
        USB_ConfigDescriptor ncmConfigDesc;
        USB_InterfaceDescriptor usbInterfaceDescriptor;
        CDC_HeaderFunctionalDescriptor ncmHeaderDesc;
        CDC_UnionFunctionalDescriptor ncmUnionDesc;
        CDC_EthernetFunctionalDescriptor EthernetFuncDesc;
        NCM_FunctionalDescriptor ncmFuncDesc;
        USB_EndpointDescriptor notificationEPDesc;
        USB_InterfaceDescriptor dataIfDesc0;
        USB_InterfaceDescriptor dataIfDesc;
        USB_EndpointDescriptor inputEPDesc;
        USB_EndpointDescriptor outEPDesc;
    } USBD_NCM_DescTypeDef;

    typedef struct _USBD_NCM_Itf
    {
        int8_t (*Init)();
        int8_t (*DeInit)();
        int8_t (*Receive)(uint8_t *Buf, uint32_t Len);
    } USBD_NCM_ItfTypeDef;

    typedef struct __packed
    {
        uint32_t dwSignature;   // Сигнатура 'NCMH' (0x484D434E)
        uint16_t wHeaderLength; // Длина заголовка в байтах
        uint16_t wSequence;     // Последовательный номер блока
        uint16_t wBlockLength;  // Общая длина блока (включая заголовки)
        uint16_t wNdpIndex;     // Смещение до NDP от начала NTH
    } NTH16_Header;

    typedef struct __packed
    {
        uint16_t Index;  // Индекс байта датаграммы в NTB
        uint16_t Length; // Размер в байтах датаграммы в NTB 
    } NCM_Datagram16;

    typedef struct __packed
    {
        uint32_t dwSignature;   // Сигнатура 'NCM0' (0x304D434E)
        uint16_t wLength;       // Длина NDP (включая эту структуру)
        uint16_t wNextNdpIndex; // Смещение следующего NDP (0 для последнего)
        NCM_Datagram16 Datagram[1];
    } NDP16_Header;

    extern USBD_ClassTypeDef USBD_NCM;
#define USBD_NCM_CLASS &USBD_NCM

    uint8_t USBD_NCM_RegisterInterface(USBD_HandleTypeDef *pdev, USBD_NCM_ItfTypeDef *fops);
    uint8_t USBD_NCM_SetTxBuffer(USBD_HandleTypeDef *pdev, uint8_t *pbuff, uint32_t length);
    uint8_t USBD_NCM_TransmitPacket(USBD_HandleTypeDef *pdev);
    uint8_t USBD_NCM_SetRxBuffer(USBD_HandleTypeDef *pdev, uint8_t *pbuff);
    uint8_t USBD_NCM_ReceivePacket(USBD_HandleTypeDef *pdev);
    uint8_t *ncmGetStrDescriptor(USBD_SpeedTypeDef speed, uint8_t idx, uint16_t *length);
    uint8_t USBD_NCM_Connect(USBD_HandleTypeDef *pdev, uint32_t bitrate);

#ifdef __cplusplus
}
#endif

#endif