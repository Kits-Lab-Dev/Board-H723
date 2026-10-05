#include "control.h"
#include "string.h"
#include "xNetworkInterface.h"
#include "app.h"
#include "timers.h"

static wifiConnRecord wifiAP;
static bool reconnectWiFi = false;

#define EVENT_TIMEOUT 5000

bool espEnabled = false;

#define CONTROL_QUEUE_COUNT 20


static QueueHandle_t controlQueue[CONTROL_QUEUE_COUNT] = {NULL};

static bool addQueue(QueueHandle_t q){
    for(int i=0; i<CONTROL_QUEUE_COUNT; i++){
        if(controlQueue[i] == NULL){
            controlQueue[i] = q;
            return true;
        }
    }
    return false;
}

static bool removeQueue(QueueHandle_t q){
    for(int i=0; i<CONTROL_QUEUE_COUNT; i++){
        if(controlQueue[i] == q){
            controlQueue[i] = NULL;
            return true;
        }
    }
    return false;
}

static bool checkQueue(QueueHandle_t q){
    for(int i=0; i<CONTROL_QUEUE_COUNT; i++){
        if(controlQueue[i] == q){
            return true;
        }
    }
    return false;
}

void controlInit()
{
}

// Переподключение нельзя делать прямо в control_rx_process: wifiConnect ждёт ответ ESP,
// а принимает его та же задача драйвера — она блокируется на EVENT_TIMEOUT и канал встаёт
static void reconnectDeferred(void *, uint32_t)
{
    if (reconnectWiFi)
        wifiConnect(&wifiAP, true);
}

esp_err_t control_rx_process(uint8_t *data, uint16_t len)
{
    ControlMsg *msg = (ControlMsg *)data;
    pControlMsg resp;
    resp.data = (uint8_t *)pvPortMalloc(msg->len);
    if (!resp.data)
        return ESP_FAIL;

    resp.id = msg->id;
    resp.type = msg->type;
    resp.len = msg->len;
    memcpy(resp.data, msg->data, msg->len);
    resp.free_fn = vPortFree;

    switch (msg->id)
    {
    case Event_StationConnectedToAP:
        vNetworkNotifyIFUp(ESP_STA);
        break;
    case Event_StationDisconnectFromAP:
        vNetworkNotifyIFDown(ESP_STA);
        if (reconnectWiFi)
        {
            xTimerPendFunctionCall(reconnectDeferred, NULL, 0, 0);
        }
        break;
    case Event_StationConnectedToESPSoftAP:

        break;
    case Event_StationDisconnectFromESPSoftAP:

        break;
    default:
        if (checkQueue((QueueHandle_t)msg->queue))      
            xQueueSend((QueueHandle_t)msg->queue, &resp, 10);
        break;
    }

    return ESP_OK;
}

static bool sendEvent(pControlMsg *msg, pControlMsg *resp)
{
    p_spi_buf spi_buf;
    spi_buf.data = (uint8_t *)pvPortMalloc(sizeof(ControlMsg) - CONTROL_DATA_LEN + msg->len + 1);
    if (!spi_buf.data || !resp)
    {
        vPortFree(msg->data);
        return false;
    }
    ControlMsg *m = (ControlMsg *)spi_buf.data;
    spi_buf.len = sizeof(ControlMsg);
    spi_buf.type = ESP_CONTROL;
    m->id = msg->id;
    m->type = msg->type;
    m->len = msg->len;
    QueueHandle_t queue = xQueueCreate(1, sizeof(pControlMsg));
    if(!addQueue(queue))
        return false;
    m->queue = (uint32_t)queue;
    if (!m->queue)
    {
        vPortFree(msg->data);
        vPortFree(spi_buf.data);
        return false;
    }
    memcpy(m->data, msg->data, msg->len);
    vPortFree(msg->data);
    spi_buf.is_released = true;
    spi_buf.free_data_fn = vPortFree;
    if (!spiDriverWrite(&spi_buf))
    {
        vPortFree(spi_buf.data);
        return false;
    }
    bool ret = xQueueReceive(queue, resp, EVENT_TIMEOUT) == pdTRUE;
    removeQueue(queue);
    if (msg->id == resp->id - Resp_Base + Req_Base)
        vQueueDelete(queue);
    return ret;
}

bool wifiGetMacAddress(esp_interface interface, uint8_t *mac)
{
    if (!espEnabled)
        return false;

    pControlMsg msg, resp;
    msg.id = Req_GetMACAddress;
    msg.type = interface; // ESP_STA, ESP_AP
    msg.len = sizeof(esp_interface);
    msg.data = (uint8_t *)pvPortMalloc(1); // очистка в sendEvent
    msg.data[0] = (uint8_t)interface;

    bool ret = sendEvent(&msg, &resp);
    if (!ret)
        return false;

    if (resp.id == Resp_GetMACAddress && resp.type == interface)
    {
        memcpy(mac, resp.data, MAC_LEN);
    }
    if (resp.free_fn)
    {
        resp.free_fn(resp.data);
    }
    return true;
}

bool wifiSetMacAddress(esp_interface interface, uint8_t *mac)
{
    if (!espEnabled)
        return false;
    pControlMsg msg, resp;
    msg.id = Req_SetMACAddress;
    msg.type = interface; // ESP_STA, ESP_AP
    msg.len = MAC_LEN;
    msg.data = (uint8_t *)pvPortMalloc(MAC_LEN); // очистка в sendEvent
    memcpy(msg.data, mac, MAC_LEN);

    bool ret = sendEvent(&msg, &resp);
    if (!ret)
        return false;
    if (resp.id == Resp_SetMACAddress && resp.type == interface)
    {
        if ((int)resp.data[0] != 0)
            ret = false;
    }
    if (resp.free_fn)
    {
        resp.free_fn(resp.data);
    }
    return ret;
}

bool wifiGetWiFiMode(wifiMode *mode)
{
    if (!espEnabled)
        return false;
    pControlMsg msg, resp = {0};
    msg.id = Req_GetWifiMode;
    msg.type = 0;
    msg.len = 1;
    msg.data = (uint8_t *)pvPortMalloc(1); // очистка в sendEvent
    msg.data[0] = 0;

    bool ret = sendEvent(&msg, &resp);
    if (!ret)
        return false;

    ret = false;
    if (resp.id == Resp_GetWifiMode && resp.type == 0)
    {
        ret = true;
        memcpy(mode, resp.data, sizeof(wifiMode));
    }
    if (resp.free_fn)
    {
        resp.free_fn(resp.data);
    }
    return ret;
}

bool wifiSetWiFiMode(wifiMode mode)
{
    if (!espEnabled)
        return false;
    pControlMsg msg, resp;
    msg.id = Req_SetWifiMode;
    msg.type = 0;
    msg.len = 1;
    msg.data = (uint8_t *)pvPortMalloc(msg.len); // очистка в sendEvent
    msg.data[0] = mode;

    bool ret = sendEvent(&msg, &resp);
    if (!ret)
        return false;

    ret = false;
    if (resp.id == Resp_SetWifiMode && resp.type == 0)
    {
        ret = *(int *)resp.data == 0;
    }
    if (resp.free_fn)
    {
        resp.free_fn(resp.data);
    }
    return ret;
}

uint32_t wifiGetScanList(wifiAPrecord *recs)
{
    if (!espEnabled)
        return false;
    pControlMsg msg, resp;
    msg.id = Req_GetAPScanList;
    msg.type = 0;
    msg.len = 1;
    msg.data = (uint8_t *)pvPortMalloc(msg.len); // очистка в sendEvent

    int ret = 0;
    if (!sendEvent(&msg, &resp))
        return 0;

    if (resp.id == Resp_GetAPScanList && resp.type == 0)
    {
        if (resp.len >= sizeof(wifiAPrecord))
        {
            memcpy(recs, resp.data, resp.len);
            ret = resp.len / sizeof(wifiAPrecord);
        }
    }
    if (resp.free_fn)
    {
        resp.free_fn(resp.data);
    }
    return ret;
}

bool wifiGetAPConfig(wifiAPConfig *rec)
{
    if (!espEnabled)
        return false;
    pControlMsg msg, resp;
    msg.id = Req_GetAPConfig;
    msg.type = 0;
    msg.len = 1;
    msg.data = (uint8_t *)pvPortMalloc(msg.len); // очистка в sendEvent

    bool ret = sendEvent(&msg, &resp);
    if (!ret)
        return false;

    if (resp.id == Resp_GetAPConfig && resp.type == 0)
    {
        if (resp.len == sizeof(wifiAPConfig))
        {
            memcpy(rec, resp.data, resp.len);
        }
        else
        {
            ret = false;
        }
    }
    if (resp.free_fn)
    {
        resp.free_fn(resp.data);
    }
    return ret;
}

bool wifiConnect(wifiConnRecord *rec, bool autoReconnect)
{
    if (!espEnabled)
        return false;
    pControlMsg msg, resp;
    msg.id = Req_ConnectAP;
    msg.type = ESP_AP;
    msg.len = sizeof(wifiConnRecord);
    msg.data = (uint8_t *)pvPortMalloc(sizeof(wifiConnRecord));
    memcpy(msg.data, rec, sizeof(wifiConnRecord));

    bool ret = sendEvent(&msg, &resp);
    if (!ret)
        return false;
    if (resp.id == Resp_ConnectAP && resp.type == 0)
    {
        ret = *(int *)resp.data == 0;
    }
    if (resp.free_fn)
    {
        resp.free_fn(resp.data);
    }
    if (ret)
    {
        if (autoReconnect)
        {
            memcpy(&wifiAP, rec, sizeof(wifiAP));
            reconnectWiFi = true;
        }
        else
            autoReconnect = false;
    }
    return ret;
}

bool wifiDisconnect()
{
    if (!espEnabled)
        return false;
    pControlMsg msg, resp;
    msg.id = Req_DisconnectAP;
    msg.type = ESP_AP;
    msg.len = 1;
    msg.data = (uint8_t *)pvPortMalloc(msg.len); // очистка в sendEvent

    bool ret = sendEvent(&msg, &resp);
    if (!ret)
        return false;

    if (resp.id == Resp_DisconnectAP && resp.type == 0)
    {
        ret = *(int *)resp.data == 0;
    }
    if (resp.free_fn)
    {
        resp.free_fn(resp.data);
    }
    return ret;
}

bool wifiIsConnected()
{
    return xGetPhyLinkStatus(&staNetInterface) == pdTRUE;
}

bool wifiGetSoftAPConfig(wifiSoftAPConfig *rec)
{
    if (!espEnabled)
        return false;
    pControlMsg msg, resp;
    msg.id = Req_GetSoftAPConfig;
    msg.type = ESP_AP;
    msg.len = sizeof(wifiSoftAPConfig);
    msg.data = (uint8_t *)pvPortMalloc(msg.len); // очистка в sendEvent

    bool ret = sendEvent(&msg, &resp);
    if (!ret)
        return false;

    ret = false;
    if (resp.id == Resp_GetSoftAPConfig && resp.len == sizeof(wifiSoftAPConfig))
    {
        memcpy(rec, resp.data, resp.len);
        ret = true;
    }
    if (resp.free_fn)
    {
        resp.free_fn(resp.data);
    }
    return ret;
}

bool wifiStartSoftAP(wifiSoftAPConfig *cfg)
{
    if (!espEnabled)
        return false;
    pControlMsg msg, resp;
    msg.id = Req_StartSoftAP;
    msg.type = ESP_AP;
    msg.len = sizeof(wifiSoftAPConfig);
    msg.data = (uint8_t *)pvPortMalloc(msg.len); // очистка в sendEvent
    memcpy(msg.data, cfg, msg.len);

    bool ret = sendEvent(&msg, &resp);
    if (!ret)
        return false;
    vNetworkNotifyIFUp(ESP_AP);
    if (resp.type == ESP_AP)
    {
        ret = *(int *)resp.data == 0;            
    }
    if (resp.free_fn)
    {
        resp.free_fn(resp.data);
    }
    return ret;
}

uint32_t wifiGetConnectedStaList(wifiConnectedStantion *rec)
{
    if (!espEnabled)
        return 0;
    pControlMsg msg, resp;
    msg.id = Req_GetSoftAPConnectedSTAList;
    msg.type = ESP_AP;
    msg.len = 1;
    msg.data = (uint8_t *)pvPortMalloc(msg.len); // очистка в sendEvent

    uint32_t ret = 0;
    if (!sendEvent(&msg, &resp))
        return 0;

    if (resp.id == Resp_GetSoftAPConnectedSTAList && resp.type == ESP_AP)
    {
        if (resp.len >= sizeof(wifiConnectedStantion))
        {
            memcpy(rec, resp.data, resp.len);
            ret = resp.len / sizeof(wifiConnectedStantion);
        }
    }
    if (resp.free_fn)
    {
        resp.free_fn(resp.data);
    }
    return ret;
}

bool wifiStopSoftAP()
{
    if (!espEnabled)
        return false;
    pControlMsg msg, resp;
    msg.id = Req_StartSoftAP;
    msg.type = ESP_AP;
    msg.len = 1;
    msg.data = (uint8_t *)pvPortMalloc(msg.len); // очистка в sendEvent

    bool ret = sendEvent(&msg, &resp);
    if (!ret)
        return false;
    vNetworkNotifyIFDown(ESP_AP);
    if (resp.id == Resp_StopSoftAP && resp.type == ESP_AP)
    {
        ret = *(int *)resp.data == 0;
    }
    if (resp.free_fn)
    {
        resp.free_fn(resp.data);
    }
    return ret;
}

bool wifiSetPowerSaveMode(wifiPowerSaveMode mode)
{
    if (!espEnabled)
        return false;
    pControlMsg msg, resp;
    msg.id = Req_SetPowerSaveMode;
    msg.type = ESP_AP;
    msg.len = 1;
    msg.data = (uint8_t *)pvPortMalloc(msg.len); // очистка в sendEvent
    msg.data[0] = mode;

    bool ret = sendEvent(&msg, &resp);
    if (!ret)
        return false;

    if (resp.id == Resp_SetPowerSaveMode)
    {
        ret = *(int *)resp.data == 0;
    }
    if (resp.free_fn)
    {
        resp.free_fn(resp.data);
    }
    return ret;
}

bool wifiGetPowerSaveMode(wifiPowerSaveMode *mode)
{
    if (!espEnabled)
        return false;
    pControlMsg msg, resp;
    msg.id = Req_GetPowerSaveMode;
    msg.type = ESP_AP;
    msg.len = 1;
    msg.data = (uint8_t *)pvPortMalloc(msg.len); // очистка в sendEvent

    bool ret = sendEvent(&msg, &resp);
    if (!ret)
        return false;

    if (resp.id == Resp_GetPowerSaveMode && resp.len == 1)
    {
        *mode = msg.data[0];
        ret = true;
    }
    if (resp.free_fn)
    {
        resp.free_fn(resp.data);
    }
    return ret;
}

bool wifiSetMaxTxPower(int8_t power)
{
    if (!espEnabled)
        return false;
    pControlMsg msg, resp;
    msg.id = Req_SetWifiMaxTxPower;
    msg.type = ESP_AP;
    msg.len = 1;
    msg.data = (uint8_t *)pvPortMalloc(msg.len); // очистка в sendEvent

    int8_t p = power < MIN_TX_POWER ? MIN_TX_POWER : power > MAX_TX_POWER ? MAX_TX_POWER
                                                                          : power;
    msg.data[0] = p;

    bool ret = sendEvent(&msg, &resp);
    if (!ret)
        return false;

    if (resp.id == Resp_SetWifiMaxTxPower && resp.len == 4)
    {
        ret = *(int *)resp.data == 0;
    }
    if (resp.free_fn)
    {
        resp.free_fn(resp.data);
    }
    return ret;
}

bool wifiGetMaxTxPower(int8_t *power)
{
    if (!espEnabled)
        return false;
    pControlMsg msg, resp;
    msg.id = Req_GetWifiCurrTxPower;
    msg.type = ESP_AP;
    msg.len = 1;
    msg.data = (uint8_t *)pvPortMalloc(msg.len); // очистка в sendEvent

    bool ret = sendEvent(&msg, &resp);
    if (!ret)
        return false;

    if (resp.id == Resp_GetWifiCurrTxPower && resp.len == 1)
    {
        *power = msg.data[0];
        ret = true;
    }
    if (resp.free_fn)
    {
        resp.free_fn(resp.data);
    }
    return ret;
}

bool wifiGetFirmwareVersion(char *fw)
{
    if (!espEnabled)
        return false;
    pControlMsg msg, resp;
    msg.id = Req_GetFwVersion;
    msg.type = ESP_AP;
    msg.len = 1;
    msg.data = (uint8_t *)pvPortMalloc(msg.len); // очистка в sendEvent

    bool ret = sendEvent(&msg, &resp);
    if (!ret)
        return false;

    if (resp.id == Resp_GetFwVersion)
    {
        memcpy(fw, msg.data, resp.len);
        ret = true;
    }
    if (resp.free_fn)
    {
        resp.free_fn(resp.data);
    }
    return ret;
}

bool espFeatureEnable(espFeature ft, bool en)
{
    if (!espEnabled)
        return false;
    pControlMsg msg, resp;
    msg.id = Req_EnableDisable;
    msg.type = ESP_STA;
    msg.len = 2;
    msg.data = (uint8_t *)pvPortMalloc(msg.len); // очистка в sendEvent
    msg.data[0] = ft;
    msg.data[1] = en ? 1 : 0;

    bool ret = sendEvent(&msg, &resp);
    if (!ret)
        return false;

    if (resp.id == Resp_EnableDisable && resp.len == 4)
    {
        ret = *(int *)resp.data == 0;
        ret = true;
    }
    if (resp.free_fn)
    {
        resp.free_fn(resp.data);
    }

    if (ft == ftWifi && ret)
    {
        if (en)
        {
            networkInterfaceUpdateMAC();
        }
        vNetworkNotifyIFDown(ESP_STA);
        vNetworkNotifyIFDown(ESP_AP);
    }

    return ret;
}

bool espEnable(bool enable)
{
    if (enable)
    {
        if (espEnabled)
            return true;

        HAL_GPIO_WritePin(ESP_BOOT_GPIO_Port, ESP_BOOT_Pin, GPIO_PIN_SET);
        HAL_GPIO_WritePin(ESP_EN_GPIO_Port, ESP_EN_Pin, GPIO_PIN_RESET);
        vTaskDelay(100);
        HAL_GPIO_WritePin(ESP_EN_GPIO_Port, ESP_EN_Pin, GPIO_PIN_SET);
        vTaskDelay(2000);
        espEnabled = true;
    }
    else
    {
        HAL_GPIO_WritePin(ESP_EN_GPIO_Port, ESP_EN_Pin, GPIO_PIN_RESET);
        if (espEnabled)
        {
            vNetworkNotifyIFDown(ESP_STA);
            vNetworkNotifyIFDown(ESP_AP);
        }
    }
    espEnabled = enable;
    return true;
}

bool espInit()
{
    controlInit();
    return spiDriverInit();
}