#include <string.h>
#include "tcp_speed.h"
#include "app.h"
#include "FreeRTOS.h"
#include "task.h"
#include "FreeRTOS_IP.h"
#include "FreeRTOS_Sockets.h"
#include "xNetworkInterface.h"
#include "control.h"

#define TASK_STACK_SIZE 1024
#define CHUNK 4096
// Окна TCP в сегментах MSS. Приём — 32: больше ПК шлёт пачками, очередь SPI на ESP
// переполняется, и TCP на ПК после потерь сбрасывает скорость. Отправка — 44 (64 КБ,
// предел без масштабирования окна)
#define TCP_SPEED_RX_WIN 32
#define TCP_SPEED_TX_WIN 44

static StackType_t rxStack[TASK_STACK_SIZE], txStack[TASK_STACK_SIZE], startStack[512];
static StaticTask_t rxTcb, txTcb, startTcb;
static uint8_t txData[CHUNK];

static void wifiConnectSta(void)
{
    if (strlen(TCP_SPEED_SSID) == 0)
        return;
    wifiConnRecord rec = {
        .bssid_set = false,
        .is_wpa3_supported = true,
        .listen_interval = -1,
    };
    strncpy((char *)rec.ssid, TCP_SPEED_SSID, sizeof(rec.ssid) - 1);
    strncpy((char *)rec.password, TCP_SPEED_PASSWORD, sizeof(rec.password) - 1);
    espFeatureEnable(ftWifi, true);
    wifiSetWiFiMode(WIFI_MODE_STA);
    if (!wifiConnect(&rec, true))
        ERR("tcp_speed: connect to %s failed\n", TCP_SPEED_SSID);
    // Энергосбережение ESP (по умолчанию MIN_MODEM): радио спит между маяками точки
    // доступа, и приём идёт рывками. Для скорости — выключить
    wifiPowerSaveMode ps = WIFI_PS_MAX_MODEM;
    wifiGetPowerSaveMode(&ps);
    wifiSetPowerSaveMode(WIFI_PS_NONE);
    INF("tcp_speed: power save %d -> %d\n", ps, WIFI_PS_NONE);
}

static Socket_t listenOn(uint16_t port)
{
    Socket_t s = FreeRTOS_socket(FREERTOS_AF_INET, FREERTOS_SOCK_STREAM, FREERTOS_IPPROTO_TCP);
    if (s == FREERTOS_INVALID_SOCKET)
        return NULL;
    // Буферы и окна TCP; принятые сокеты наследуют их от слушающего
    WinProperties_t win = {
        .lTxBufSize = TCP_SPEED_TX_WIN * ipconfigTCP_MSS,
        .lTxWinSize = TCP_SPEED_TX_WIN,
        .lRxBufSize = TCP_SPEED_RX_WIN * ipconfigTCP_MSS,
        .lRxWinSize = TCP_SPEED_RX_WIN,
    };
    FreeRTOS_setsockopt(s, 0, FREERTOS_SO_WIN_PROPERTIES, &win, sizeof(win));
    struct freertos_sockaddr addr = {0};
    addr.sin_family = FREERTOS_AF_INET;
    addr.sin_port = FreeRTOS_htons(port);
    if (FreeRTOS_bind(s, &addr, sizeof(addr)) != 0 || FreeRTOS_listen(s, 1) != 0)
    {
        FreeRTOS_closesocket(s);
        return NULL;
    }
    return s;
}

static void report(const char *dir, uint32_t bytes, TickType_t t0)
{
    uint32_t ms = (xTaskGetTickCount() - t0) * portTICK_PERIOD_MS;
    if (ms == 0)
        ms = 1;
    // Мбит/с с одним знаком после запятой: bytes * 8 / ms / 1000
    uint32_t kbit = (uint32_t)((uint64_t)bytes * 8 / ms);
    INF("tcp_speed: %s %lu bytes in %lu ms, %lu.%lu Mbit/s\n", dir, bytes, ms, kbit / 1000, (kbit % 1000) / 100);
}

// Порт TCP_SPEED_RX_PORT: принять всё и выбросить. Данные не копируются —
// FREERTOS_ZERO_COPY отдаёт указатель прямо в буфер сокета
static void rxTask(void *arg)
{
    (void)arg;
    Socket_t ls = listenOn(TCP_SPEED_RX_PORT);
    for (;;)
    {
        struct freertos_sockaddr peer;
        socklen_t len = sizeof(peer);
        Socket_t c = FreeRTOS_accept(ls, &peer, &len);
        if (c == NULL || c == FREERTOS_INVALID_SOCKET)
            continue;
        uint32_t bytes = 0;
        TickType_t t0 = xTaskGetTickCount();
        for (;;)
        {
            uint8_t *p;
            BaseType_t n = FreeRTOS_recv(c, &p, CHUNK, FREERTOS_ZERO_COPY);
            if (n < 0)
                break;
            if (n > 0)
            {
                bytes += n;
                FreeRTOS_ReleaseTCPPayloadBuffer(c, p, n);
            }
        }
        report("rx", bytes, t0);
        FreeRTOS_closesocket(c);
    }
}

// Порт TCP_SPEED_TX_PORT: слать поток, пока ПК не закроет соединение
static void txTask(void *arg)
{
    (void)arg;
    for (int i = 0; i < CHUNK; i++)
        txData[i] = (uint8_t)i;
    Socket_t ls = listenOn(TCP_SPEED_TX_PORT);
    for (;;)
    {
        struct freertos_sockaddr peer;
        socklen_t len = sizeof(peer);
        Socket_t c = FreeRTOS_accept(ls, &peer, &len);
        if (c == NULL || c == FREERTOS_INVALID_SOCKET)
            continue;
        uint32_t bytes = 0;
        TickType_t t0 = xTaskGetTickCount();
        for (;;)
        {
            BaseType_t n = FreeRTOS_send(c, txData, CHUNK, 0);
            if (n < 0)
                break;
            bytes += n;
        }
        report("tx", bytes, t0);
        FreeRTOS_closesocket(c);
    }
}

static void startTask(void *arg)
{
    (void)arg;
    wifiConnectSta();
    while (!FreeRTOS_IsEndPointUp(&staNetEndPoint) || staNetEndPoint.ipv4_settings.ulIPAddress == 0)
        vTaskDelay(pdMS_TO_TICKS(500));
    INF("tcp_speed: listening, rx port %d, tx port %d\n", TCP_SPEED_RX_PORT, TCP_SPEED_TX_PORT);
    xTaskCreateStatic(rxTask, "tcp_rx", TASK_STACK_SIZE, NULL, tskIDLE_PRIORITY + 2, rxStack, &rxTcb);
    xTaskCreateStatic(txTask, "tcp_tx", TASK_STACK_SIZE, NULL, tskIDLE_PRIORITY + 2, txStack, &txTcb);
    vTaskDelete(NULL);
}

void tcp_speed_start(void)
{
    xTaskCreateStatic(startTask, "tcp_speed", 512, NULL, tskIDLE_PRIORITY + 1, startStack, &startTcb);
}
