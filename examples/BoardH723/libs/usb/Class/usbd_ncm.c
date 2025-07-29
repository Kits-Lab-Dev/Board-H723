#include "usbd_ncm.h"
#include "usbd_ctlreq.h"

#define NCM_NDP16_DATAGRAM_COUNT 1 // Количество датаграмм в пакете

static uint8_t USBD_NCM_Init(USBD_HandleTypeDef *pdev, uint8_t cfgidx);
static uint8_t USBD_NCM_DeInit(USBD_HandleTypeDef *pdev, uint8_t cfgidx);
static uint8_t USBD_NCM_Setup(USBD_HandleTypeDef *pdev, USBD_SetupReqTypedef *req);
static uint8_t USBD_NCM_DataIn(USBD_HandleTypeDef *pdev, uint8_t epnum);
static uint8_t USBD_NCM_DataOut(USBD_HandleTypeDef *pdev, uint8_t epnum);
static uint8_t USBD_NCM_EP0_RxReady(USBD_HandleTypeDef *pdev);
static uint8_t *USBD_NCM_GetFSCfgDesc(uint16_t *length);


#define USBD_ISTR_INTERFACES 1

#define USBD_IIF_INDEX(IFNUM, INTNUM) \
    (USBD_ISTR_INTERFACES + (IFNUM) + ((INTNUM) << 4))

#define USB_DEVICE_DESC_SIZE 0x0AU

#define USBD_CFG_MAX_NUM 1
#define USBD_MAX_EP0_SIZE 64U

#define MAC_STR_IDX 7
#define USB_DIR_MASK 0x80U

/* NCM interface class callbacks structure */
USBD_ClassTypeDef USBD_NCM =
    {
        USBD_NCM_Init,
        USBD_NCM_DeInit,
        USBD_NCM_Setup,
        NULL, /* EP0_TxSent */
        USBD_NCM_EP0_RxReady,
        USBD_NCM_DataIn,
        USBD_NCM_DataOut,
        NULL,
        NULL,
        NULL,
#ifdef USE_USBD_COMPOSITE
        NULL,
        NULL,
        NULL,
        NULL,
#else
        NULL,
        USBD_NCM_GetFSCfgDesc,
        NULL,
        NULL,
        NULL
#endif /* USE_USBD_COMPOSITE  */
};

#ifndef USE_USBD_COMPOSITE
/* USB NCM device Configuration Descriptor */
__ALIGN_BEGIN static USBD_NCM_DescTypeDef USBD_NCM_CfgDesc __ALIGN_END = {
    .ncmConfigDesc = {
        .bLength = sizeof(USB_ConfigDescriptor),
        .bDescriptorType = USB_DESC_TYPE_CONFIGURATION,
        .wTotalLength = sizeof(USBD_NCM_DescTypeDef),
        .bNumInterfaces = 2,
        .bConfigurationValue = 1,
        .iConfiguration = 0,
        .bmAttributes = 0xC0,
        .bMaxPower = 0x32},
    // Управляющий интерфейс (Control Interface)
    .usbInterfaceDescriptor = {
        .bLength = sizeof(USB_InterfaceDescriptor), 
        .bDescriptorType = USB_DESC_TYPE_INTERFACE, 
        .bInterfaceNumber = 0, 
        .bAlternateSetting = 0, 
        .bNumEndpoints = 1, 
        .bInterfaceClass = 0x02, 
        .bInterfaceSubClass = 0x0D, 
        .bInterfaceProtocol = 0x00, 
        .iInterface = 0
        },
    // Функциональные дескрипторы CDC-NCM
    .ncmHeaderDesc = {
        .bFunctionLength = 5, 
        .bDescriptorType = 0x24, 
        .bDescriptorSubtype = 0x00, 
        .bcdCDC = 0x0100
        },
    .ncmUnionDesc = {
        .bFunctionLength = 5, 
        .bDescriptorType = 0x24, 
        .bDescriptorSubtype = 0x06, 
        .bMasterInterface = 0, 
        .bSlaveInterface = 1
        },
    .EthernetFuncDesc = {
        .bFunctionLength = 13, 
        .bDescriptorType = 0x24, 
        .bDescriptorSubtype = 0x0F, 
        .iMACAddress = MAC_STR_IDX, 
        .bmEthernetStatistics = 0, 
        .wMaxSegmentSize = NCM_MAX_SEGMENT_SIZE, 
        .wNumberMCFilters = 0, 
        .bNumberPowerFilters = 0
        },
    .ncmFuncDesc = {
        .bFunctionLength = sizeof(NCM_FunctionalDescriptor),
        .bDescriptorType = 0x24,
        .bDescriptorSubtype = 0x1A,
        .bcdNcmVersion = 0x0100,
        .bmNetworkCapabilities = 0x00,
    },
    .notificationEPDesc = {
        .bLength = sizeof(USB_EndpointDescriptor),
        .bDescriptorType = USB_DESC_TYPE_ENDPOINT, 
        .bEndpointAddress = NCM_CMD_EP, 
        .bmAttributes = 0x03, 
        .wMaxPacketSize = NCM_CMD_PACKET_SIZE, 
        .bInterval = 0x10
        },
    .dataIfDesc0 = {
        .bLength = sizeof(USB_InterfaceDescriptor), 
        .bDescriptorType = USB_DESC_TYPE_INTERFACE, 
        .bInterfaceNumber = 1, 
        .bAlternateSetting = 0, 
        .bNumEndpoints = 0, 
        .bInterfaceClass = 0x0A, 
        .bInterfaceSubClass = 0x00, 
        .bInterfaceProtocol = 0x01, 
        .iInterface = 0
        },
    .dataIfDesc = {
        .bLength = sizeof(USB_InterfaceDescriptor), 
        .bDescriptorType = USB_DESC_TYPE_INTERFACE, 
        .bInterfaceNumber = 1, 
        .bAlternateSetting = 1, 
        .bNumEndpoints = 2, 
        .bInterfaceClass = 0x0A, 
        .bInterfaceSubClass = 0x00, 
        .bInterfaceProtocol = 0x01, 
        .iInterface = 0
        },
    .inputEPDesc = {
        .bLength = sizeof(USB_EndpointDescriptor), 
        .bDescriptorType = USB_DESC_TYPE_ENDPOINT, 
        .bEndpointAddress = NCM_IN_EP, 
        .bmAttributes = 0x02, 
        .wMaxPacketSize = 0x40, 
        .bInterval = 0x00
        },
    .outEPDesc = {
        .bLength = sizeof(USB_EndpointDescriptor), 
        .bDescriptorType = USB_DESC_TYPE_ENDPOINT, 
        .bEndpointAddress = NCM_OUT_EP, 
        .bmAttributes = 0x02, 
        .wMaxPacketSize = 0x40, 
        .bInterval = 0x00
        }
        };
#endif /* USE_USBD_COMPOSITE  */

typedef union
{
    uint8_t b[4];
    uint32_t dw;
} USBD_NCM_SignatureType;

// static const USBD_NCM_SignatureType nth16_sign =
    // {.b = {'N', 'C', 'M', 'H'}};

static const USBD_NCM_SignatureType ndp16_sign =
    {.b = {'N', 'C', 'M', '0'}};

uint8_t *ncmGetStrDescriptor(USBD_SpeedTypeDef speed, uint8_t idx, uint16_t *length)
{
    UNUSED(speed);
    static uint8_t USBD_MAC_STRING[26]; // 6 байт MAC * 4 символа (XX:) - последний ':' убираем + 2 байта заголовка
    extern uint8_t ncmMACAddress[6];    // Объявляем внешний массив

    if (idx == MAC_STR_IDX) // Индекс строки MAC-адреса
    {
        USBD_MAC_STRING[0] = 26;   // bLength (12 символов * 2 + 2)
        USBD_MAC_STRING[1] = 0x03; // bDescriptorType (STRING)

        // Конвертируем MAC-адрес в Unicode строку
        uint8_t *ptr = &USBD_MAC_STRING[2];
        for (int i = 0; i < 6; i++)
        {
            uint8_t nibble_hi = (ncmMACAddress[i] >> 4) & 0x0F;
            uint8_t nibble_lo = ncmMACAddress[i] & 0x0F;

            *ptr++ = (nibble_hi < 0xA) ? ('0' + nibble_hi) : ('A' + nibble_hi - 0xA);
            *ptr++ = 0x00; // Unicode high byte
            *ptr++ = (nibble_lo < 0xA) ? ('0' + nibble_lo) : ('A' + nibble_lo - 0xA);
            *ptr++ = 0x00;
        }

        *length = 26; // Фиксированная длина для MAC-адреса
        return USBD_MAC_STRING;
    }

    return NULL;
}

/**
 * @brief  USBD_NCM_Init
 *         Initialize the NCM interface
 * @param  pdev: device instance
 * @param  cfgidx: Configuration index
 * @retval status
 */
static uint8_t USBD_NCM_Init(USBD_HandleTypeDef *pdev, uint8_t cfgidx)
{
    UNUSED(cfgidx);
    USBD_NCM_HandleTypeDef *hcdc;

    // Выделяем память для структуры обработчика
    hcdc = (USBD_NCM_HandleTypeDef *)USBD_malloc(sizeof(USBD_NCM_HandleTypeDef));
    if (hcdc == NULL)
    {
        return (uint8_t)USBD_FAIL;
    }
    (void)USBD_memset(hcdc, 0, sizeof(USBD_NCM_HandleTypeDef));
    pdev->pClassDataCmsit[pdev->classId] = (void *)hcdc;
    pdev->pClassData = pdev->pClassDataCmsit[pdev->classId];
    // Инициализируем конечные точки
#ifdef USE_USBD_COMPOSITE
    // Получаем адреса EP из композитного драйвера
    NCMInEpAdd = USBD_CoreGetEPAdd(pdev, USBD_EP_IN, USBD_EP_TYPE_BULK, (uint8_t)pdev->classId);
    NCMOutEpAdd = USBD_CoreGetEPAdd(pdev, USBD_EP_OUT, USBD_EP_TYPE_BULK, (uint8_t)pdev->classId);
    NCMCmdEpAdd = USBD_CoreGetEPAdd(pdev, USBD_EP_IN, USBD_EP_TYPE_INTR, (uint8_t)pdev->classId);
#endif

    // Открываем конечные точки
    (void)USBD_LL_OpenEP(pdev, NCM_CMD_EP, USBD_EP_TYPE_INTR, NCM_CMD_PACKET_SIZE);
    pdev->ep_in[NCM_CMD_EP & 0xFU].bInterval = NCM_FS_BINTERVAL;
    
    // Инициализируем состояние буферов
    hcdc->TxState = 0U;
    hcdc->RxState = 0U;
    hcdc->altSetting = 1;
    hcdc->NthPtr = NULL;

    hcdc->Notify.SpeedChange.bmRequestType = 0xA1;
    hcdc->Notify.SpeedChange.bNotificationType = CDC_NOT_CONNECTION_SPEED_CHANGE;
    hcdc->Notify.SpeedChange.wIndex = 0;
    hcdc->Notify.SpeedChange.wLength = sizeof(hcdc->Notify.SpeedData);
    hcdc->Notify.SpeedChange.wValue = 0;
    hcdc->Notify.Connection.bmRequestType = 0xA1;
    hcdc->Notify.Connection.bNotificationType = CDC_NOT_NETWORK_CONNECTION;
    hcdc->Notify.Connection.wIndex = 0;
    hcdc->Notify.Connection.wValue = 0;
    hcdc->Notify.Connection.wLength = 0;

    // Вызываем пользовательскую инициализацию
    if (((USBD_NCM_ItfTypeDef *)pdev->pUserData[pdev->classId])->Init(hcdc) != USBD_OK)
    {
        return (uint8_t)USBD_FAIL;
    }

    return (uint8_t)USBD_OK;
}

/**
 * @brief  USBD_NCM_DeInit
 *         DeInitialize the NCM layer
 * @param  pdev: device instance
 * @param  cfgidx: Configuration index
 * @retval status
 */
static uint8_t USBD_NCM_DeInit(USBD_HandleTypeDef *pdev, uint8_t cfgidx)
{
    UNUSED(cfgidx);
    USBD_NCM_HandleTypeDef *hcdc = (USBD_NCM_HandleTypeDef *)pdev->pClassDataCmsit[pdev->classId];

    if (hcdc == NULL)
    {
        return (uint8_t)USBD_OK;
    }
    // hcdc->altSetting = cfgidx;
    // Закрываем конечные точки
    (void)USBD_LL_CloseEP(pdev, NCM_CMD_EP);
    (void)USBD_LL_CloseEP(pdev, NCM_IN_EP);
    (void)USBD_LL_CloseEP(pdev, NCM_OUT_EP);

    // Вызываем пользовательскую деинициализацию
    if (((USBD_NCM_ItfTypeDef *)pdev->pUserData[pdev->classId])->DeInit(hcdc) != USBD_OK)
    {
        return (uint8_t)USBD_FAIL;
    }

    // Освобождаем память класса
    USBD_free(pdev->pClassDataCmsit[pdev->classId]);
    pdev->pClassDataCmsit[pdev->classId] = NULL;

    return (uint8_t)USBD_OK;
}

/**
 * @brief  USBD_NCM_Setup
 *         Handle the NCM specific requests
 * @param  pdev: instance
 * @param  req: usb requests
 * @retval status
 */
static uint8_t USBD_NCM_Setup(USBD_HandleTypeDef *pdev,
                              USBD_SetupReqTypedef *req)
{
    USBD_NCM_HandleTypeDef *hcdc = (USBD_NCM_HandleTypeDef *)pdev->pClassDataCmsit[pdev->classId];
    uint16_t len;
    uint8_t *pbuf;
    uint8_t ret = USBD_OK;

    if (hcdc == NULL)
    {
        return (uint8_t)USBD_FAIL;
    }

    switch (req->bmRequest & USB_REQ_TYPE_MASK)
    {
    case USB_REQ_TYPE_STANDARD:
        switch (req->bRequest)
        {
        case USB_REQ_GET_DESCRIPTOR:
            /* Handle descriptor requests if needed */
            break;

        case USB_REQ_GET_INTERFACE:
            USBD_CtlSendData(pdev, (uint8_t *)&hcdc->altSetting, 1U);
            break;

        case USB_REQ_SET_INTERFACE:
            hcdc->altSetting = (uint8_t)(req->wValue);
            break;

        default:
            USBD_CtlError(pdev, req);
            ret = USBD_FAIL;
            break;
        }
        break;

    case USB_REQ_TYPE_CLASS:
        if (req->wLength)
        {
            if (req->bmRequest & 0x80U)
            {
                /* Handle Class GET Requests */
                switch (req->bRequest)
                {
                case CDC_GET_NTB_PARAMETERS:
                    hcdc->NCMParam.NtbFormatsSupported = 0x01; // Только NTB-16
                    hcdc->NCMParam.NtbInMaxSize = USBD_NCM_MAX_IN_SIZE;
                    hcdc->NCMParam.NdpInDivisor = 4;
                    hcdc->NCMParam.NdpInAlignment = 4;
                    hcdc->NCMParam.NdpInPayloadRemainder = 0;
                    hcdc->NCMParam.NtbOutMaxSize = USBD_NCM_MAX_OUT_SIZE;
                    hcdc->NCMParam.NdpOutDivisor = 4;
                    hcdc->NCMParam.NdpOutAlignment = 4;
                    hcdc->NCMParam.NdpOutPayloadRemainder = 0;
                    hcdc->NCMParam.NtbOutMaxDatagrams = 20;
                    hcdc->NCMParam.reserved = 0;

                    pbuf = (uint8_t *)&hcdc->NCMParam;
                    len = MIN(req->wLength, sizeof(hcdc->NCMParam));
                    break;

                case CDC_GET_NET_ADDRESS:
                    pbuf = hcdc->NetAddr;
                    len = MIN(req->wLength, 6U);
                    break;

                default:
                    USBD_CtlError(pdev, req);
                    ret = USBD_FAIL;
                    break;
                }
                USBD_CtlSendData(pdev, pbuf, len);
            }
            else
            {
                /* Handle Class SET Requests */
                switch (req->bRequest)
                {
                case CDC_SET_ENET_MULTICAST_FILTERS:
                // case CDC_SET_ETHERNET_PACKET_FILTER:
                case CDC_SET_NET_ADDRESS:
                    /* Prepare to receive control data */
                    USBD_CtlPrepareRx(pdev, hcdc->RxBuffer, req->wLength);
                    break;

                default:
                    USBD_CtlError(pdev, req);
                    ret = USBD_FAIL;
                    break;
                }
            }
        }
        else
        {
            /* Handle no-data class requests */
            switch (req->bRequest)
            {
            case CDC_SET_NTB_FORMAT:

                break;

            default:
                USBD_CtlError(pdev, req);
                ret = USBD_FAIL;
                break;
            }
        }
        break;

    case USB_REQ_TYPE_VENDOR:
        /* Handle vendor-specific requests if needed */
        break;

    default:
        USBD_CtlError(pdev, req);
        ret = USBD_FAIL;
        break;
    }

    /* Handle interface and endpoint management */
    // if ((req->bmRequest & USB_REQ_TYPE_MASK) == USB_REQ_TYPE_CLASS)
    // {
    //   ((USBD_NCM_ItfTypeDef *)pdev->pUserData[pdev->classId])->Control(hcdc);
    // }

    return ret;
}

/**
 * @brief  USBD_NCM_DataIn
 *         Data sent on non-control IN endpoint
 * @param  pdev: device instance
 * @param  epnum: endpoint number
 * @retval status
 */
static uint8_t USBD_NCM_DataIn(USBD_HandleTypeDef *pdev, uint8_t epnum)
{
    USBD_NCM_HandleTypeDef *hcdc;
    PCD_HandleTypeDef *hpcd = (PCD_HandleTypeDef *)pdev->pData;

    if (pdev->pClassDataCmsit[pdev->classId] == NULL)
    {
        return (uint8_t)USBD_FAIL;
    }

    hcdc = (USBD_NCM_HandleTypeDef *)pdev->pClassDataCmsit[pdev->classId];

    if ((pdev->ep_in[epnum & 0xFU].total_length > 0U) &&
        ((pdev->ep_in[epnum & 0xFU].total_length % hpcd->IN_ep[epnum & 0xFU].maxpacket) == 0U))
    {
        /* Update the packet total length */
        pdev->ep_in[epnum & 0xFU].total_length = 0U;

        /* Send ZLP */
        (void)USBD_LL_Transmit(pdev, epnum, NULL, 0U);
    }
    else
    {
        hcdc->TxState = 0U;

        // if (((USBD_NCM_HandleTypeDef *)pdev->pUserData[pdev->classId])->TransmitCplt != NULL)
        // {
        //   ((USBD_NCM_HandleTypeDef *)pdev->pUserData[pdev->classId])->TransmitCplt(hcdc->TxBuffer, &hcdc->TxLength, epnum);
        // }
    }
    return (uint8_t)USBD_OK;
}

/**
 * @brief  USBD_NCM_DataOut
 *         Data received on non-control Out endpoint
 * @param  pdev: device instance
 * @param  epnum: endpoint number
 * @retval status
 */
static uint8_t USBD_NCM_DataOut(USBD_HandleTypeDef *pdev, uint8_t epnum)
{
    USBD_NCM_HandleTypeDef *hcdc = (USBD_NCM_HandleTypeDef *)pdev->pClassDataCmsit[pdev->classId];

    if (pdev->pClassDataCmsit[pdev->classId] == NULL)
    {
        return (uint8_t)USBD_FAIL;
    }

    /* Get the received data length */
    hcdc->RxLength = USBD_LL_GetRxDataSize(pdev, epnum);

    // Обработка только NTH16/NDP16
    uint32_t signature = *((uint32_t *)hcdc->RxBuffer);

    if (signature == NCM_NTH16_SIGNATURE)
    {
        NTH16_Header *nth = (NTH16_Header *)hcdc->RxBuffer;

        // Проверки целостности заголовка
        if ((nth->wHeaderLength != sizeof(NTH16_Header)) ||
            (nth->wNdpIndex >= hcdc->RxLength))
        {
            return USBD_FAIL;
        }

        NDP16_Header *ndp = (NDP16_Header *)(hcdc->RxBuffer + nth->wNdpIndex);

        // Проверяем сигнатуру и длину NDP
        if ((ndp->dwSignature != NCM_NDP16_SIGNATURE) ||
            (ndp->wLength < sizeof(NDP16_Header)) ||
            ((nth->wNdpIndex + ndp->wLength) > hcdc->RxLength))
        {
            return USBD_FAIL;
        }

        uint16_t ndpIndex = nth->wNdpIndex;
        // Обрабатываем все датаграммы в NDP
        // uint16_t datagram_count = (ndp->wLength - sizeof(NDP16_Header)) / (sizeof(uint16_t) * 2);
        while ((ndpIndex >= sizeof(NTH16_Header)) &&
               (ndpIndex < nth->wBlockLength) &&
               (ndp->dwSignature == ndp16_sign.dw) &&
               (ndp->wLength > sizeof(NDP16_Header)) && ndpIndex)
        {
            uint8_t *eth_frame = (void *)nth + ndp->Datagram->Index;
            ((USBD_NCM_ItfTypeDef *)pdev->pUserData[pdev->classId])->Receive(eth_frame, ndp->Datagram->Length);

            ndpIndex = ndp->wNextNdpIndex;
            ndp = (void *)nth + ndpIndex;
        }
    }

    // Подготовка следующего приема
    USBD_NCM_ReceivePacket(pdev);

    return (uint8_t)USBD_OK;
}

/**
 * @brief  USBD_NCM_EP0_RxReady
 *         Handle EP0 Rx Ready event
 * @param  pdev: device instance
 * @retval status
 */
static uint8_t USBD_NCM_EP0_RxReady(USBD_HandleTypeDef *pdev)
{
    USBD_NCM_HandleTypeDef *hcdc = (USBD_NCM_HandleTypeDef *)pdev->pClassDataCmsit[pdev->classId];

    if (hcdc == NULL)
    {
        return (uint8_t)USBD_FAIL;
    }

    // Обрабатываем только OUT-запросы класса CDC-NCM
    if ((pdev->request.bmRequest & USB_DIR_MASK) == USB_DIRECTION_OUT)
    {
        switch (pdev->request.bRequest)
        {
        case CDC_SET_NTB_INPUT_SIZE:
            if (pdev->request.wLength == sizeof(uint32_t))
            {
                uint32_t *ntb_in = (uint32_t *)hcdc->RxBuffer;
                // Проверка допустимого размера NTB
                if (*ntb_in >= (sizeof(NTH16_Header) + sizeof(NDP16_Header)))
                {
                    hcdc->NCMParam.NtbInMaxSize = *ntb_in;
                }
            }
            break;

            // case CDC_REQ_SET_NET_ADDRESS:
            //   if (pdev->request.wLength == 6U)
            //   {
            //     memcpy(hcdc->NetAddr, hcdc->RxBuffer, 6U);
            //   }
            // break;

        default:
            break;
        }
    }

    return (uint8_t)USBD_OK;
}

/**
 * @brief  USBD_NCM_GetFSCfgDesc
 *         Return configuration descriptor
 * @param  length : pointer data length
 * @retval pointer to descriptor buffer
 */
static uint8_t *USBD_NCM_GetFSCfgDesc(uint16_t *length)
{
    *length = (uint16_t)sizeof(USBD_NCM_CfgDesc);
    return (uint8_t *)&USBD_NCM_CfgDesc;
}

/**
 * @brief  USBD_NCM_RegisterInterface
 * @param  pdev: device instance
 * @param  fops: CD  Interface callback
 * @retval status
 */
uint8_t USBD_NCM_RegisterInterface(USBD_HandleTypeDef *pdev,
                                   USBD_NCM_ItfTypeDef *fops)
{
    if (fops == NULL)
    {
        return (uint8_t)USBD_FAIL;
    }

    pdev->pUserData[pdev->classId] = fops;

    return (uint8_t)USBD_OK;
}

/**
 * @brief  USBD_NCM_SetTxBuffer
 * @param  pdev: device instance
 * @param  pbuff: Tx Buffer
 * @param  length: length of data to be sent
 * @param  ClassId: The Class ID
 * @retval status
 */
#ifdef USE_USBD_COMPOSITE
uint8_t USBD_NCM_SetTxBuffer(USBD_HandleTypeDef *pdev,
                             uint8_t *pbuff, uint32_t length, uint8_t ClassId)
{
    USBD_NCM_HandleTypeDef *hcdc = (USBD_NCM_HandleTypeDef *)pdev->pClassDataCmsit[ClassId];
#else
uint8_t USBD_NCM_SetTxBuffer(USBD_HandleTypeDef *pdev,
                             uint8_t *pbuff, uint32_t length)
{
    USBD_NCM_HandleTypeDef *hcdc = (USBD_NCM_HandleTypeDef *)pdev->pClassDataCmsit[pdev->classId];
#endif /* USE_USBD_COMPOSITE */

    if (hcdc == NULL)
    {
        return (uint8_t)USBD_FAIL;
    }

    hcdc->TxBuffer = pbuff;
    hcdc->TxLength = length;

    return (uint8_t)USBD_OK;
}

/**
 * @brief  USBD_NCM_SetRxBuffer
 * @param  pdev: device instance
 * @param  pbuff: Rx Buffer
 * @retval status
 */
uint8_t USBD_NCM_SetRxBuffer(USBD_HandleTypeDef *pdev, uint8_t *pbuff)
{
    USBD_NCM_HandleTypeDef *hcdc = (USBD_NCM_HandleTypeDef *)pdev->pClassDataCmsit[pdev->classId];

    if (hcdc == NULL)
    {
        return (uint8_t)USBD_FAIL;
    }

    hcdc->RxBuffer = pbuff;

    return (uint8_t)USBD_OK;
}

/**
 * @brief  USBD_NCM_TransmitPacket
 *         Transmit packet on IN endpoint
 * @param  pdev: device instance
 * @param  ClassId: The Class ID
 * @retval status
 */

uint8_t USBD_NCM_TransmitPacket(USBD_HandleTypeDef *pdev)
{
    USBD_NCM_HandleTypeDef *hcdc = (USBD_NCM_HandleTypeDef *)pdev->pClassData;
    USBD_StatusTypeDef ret = USBD_BUSY;
    if (hcdc == NULL)
    {
        return (uint8_t)USBD_FAIL;
    }

    if (hcdc->TxState == 0U)
    {
        /* Tx Transfer in progress */
        hcdc->TxState = 1U;

        /* Update the packet total length */
        pdev->ep_in[NCM_IN_EP & 0xFU].total_length = hcdc->TxLength;

        /* Transmit next packet */
        (void)USBD_LL_Transmit(pdev, NCM_IN_EP, hcdc->TxBuffer, hcdc->TxLength);

        ret = USBD_OK;
    }

    return (uint8_t)ret;
}

/**
 * @brief  USBD_NCM_ReceivePacket
 *         prepare OUT Endpoint for reception
 * @param  pdev: device instance
 * @retval status
 */
uint8_t USBD_NCM_ReceivePacket(USBD_HandleTypeDef *pdev)
{
    USBD_NCM_HandleTypeDef *hcdc = (USBD_NCM_HandleTypeDef *)pdev->pClassDataCmsit[pdev->classId];

#ifdef USE_USBD_COMPOSITE
    /* Get the Endpoints addresses allocated for this class instance */
    NCMOutEpAdd = USBD_CoreGetEPAdd(pdev, USBD_EP_OUT, USBD_EP_TYPE_BULK, (uint8_t)pdev->classId);
#endif /* USE_USBD_COMPOSITE */

    if (pdev->pClassDataCmsit[pdev->classId] == NULL)
    {
        return (uint8_t)USBD_FAIL;
    }

    if (pdev->dev_speed == USBD_SPEED_HIGH)
    {
        /* Prepare Out endpoint to receive next packet */
        (void)USBD_LL_PrepareReceive(pdev, NCM_OUT_EP, hcdc->RxBuffer,
                                     USBD_NCM_MAX_OUT_SIZE);
    }
    else
    {
        /* Prepare Out endpoint to receive next packet */
        (void)USBD_LL_PrepareReceive(pdev, NCM_OUT_EP, hcdc->RxBuffer,
                                     USBD_NCM_MAX_OUT_SIZE);
    }

    return (uint8_t)USBD_OK;
}

uint8_t USBD_NCM_Connect(USBD_HandleTypeDef *pdev, uint32_t bitrate)
{
    uint8_t status = USBD_FAIL;
    USBD_NCM_HandleTypeDef *hcdc = (USBD_NCM_HandleTypeDef *)pdev->pClassDataCmsit[pdev->classId];
    if (hcdc == NULL)
    {
        return (uint8_t)USBD_FAIL;
    }
    if ((hcdc->Notify.Connection.wValue == 0) && (hcdc->altSetting == 1))
    {
        // Открываем конечные точки данных
        (void)USBD_LL_OpenEP(pdev, NCM_IN_EP, USBD_EP_TYPE_BULK, 64);
        (void)USBD_LL_OpenEP(pdev, NCM_OUT_EP, USBD_EP_TYPE_BULK, 64);

        pdev->ep_in[NCM_IN_EP & 0xFU].is_used = 1U;
        pdev->ep_out[NCM_OUT_EP & 0xFU].is_used = 1U;
        pdev->ep_in[NCM_IN_EP & 0xFU].maxpacket = 64;
        pdev->ep_out[NCM_OUT_EP & 0xFU].maxpacket = 64;

        /* Prepare Out endpoint to receive next packet */
        (void)USBD_LL_PrepareReceive(pdev, NCM_OUT_EP, hcdc->RxBuffer,
                                     USBD_NCM_MAX_OUT_SIZE);

        /* Уведомление о скорости */
        hcdc->Notify.SpeedData.DLBitRate = bitrate;
        hcdc->Notify.SpeedData.ULBitRate = bitrate;
        /* Уведомление о подключении */
        hcdc->Notify.Connection.wValue = 1;
        // hcdc->Notify.SpeedChange.wValue = 1;
        status = USBD_LL_Transmit(pdev,
                                  NCM_CMD_EP,
                                  (uint8_t *)&hcdc->Notify,
                                  sizeof(hcdc->Notify));
    }

    return status;
}
