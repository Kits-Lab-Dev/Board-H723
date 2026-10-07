#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "wifi_http.h"
#include "app.h"
#include "FreeRTOS.h"
#include "task.h"
#include "FreeRTOS_IP.h"
#include "FreeRTOS_Sockets.h"
#include "FreeRTOS_DNS.h"
#include "xNetworkInterface.h"
#include "control.h"

#define HTTP_TIMEOUT_MS 5000
#define TASK_STACK_SIZE 1024

static StackType_t taskStack[TASK_STACK_SIZE];
static StaticTask_t taskTcb;

// TCP-соединение с host:port. У имени бывает несколько адресов, и не все могут
// отвечать — пробуем по очереди, как браузер. Возвращает сокет или NULL.
static Socket_t httpConnect(const char *host, uint16_t port, int *step)
{
    // 1. Имя -> адреса. DNS-сервер выдал роутер по DHCP вместе с адресом платы
    struct freertos_addrinfo *list = NULL;
    *step = -1;
    if (FreeRTOS_getaddrinfo(host, NULL, NULL, &list) != 0 || list == NULL)
        return NULL;

    Socket_t s = NULL;
    for (struct freertos_addrinfo *ai = list; ai != NULL && s == NULL; ai = ai->ai_next)
    {
        // 2. TCP-сокет с таймаутами: без них connect и recv ждут вечно, если сервер молчит
        *step = -2;
        Socket_t t = FreeRTOS_socket(FREERTOS_AF_INET, FREERTOS_SOCK_STREAM, FREERTOS_IPPROTO_TCP);
        if (t == FREERTOS_INVALID_SOCKET)
            break;
        TickType_t timeout = pdMS_TO_TICKS(HTTP_TIMEOUT_MS);
        FreeRTOS_setsockopt(t, 0, FREERTOS_SO_RCVTIMEO, &timeout, sizeof(timeout));
        FreeRTOS_setsockopt(t, 0, FREERTOS_SO_SNDTIMEO, &timeout, sizeof(timeout));

        // 3. Соединение. Интерфейс выбирает стек: у Wi-Fi есть шлюз, у USB-сети — нет
        struct freertos_sockaddr addr = {0};
        addr.sin_family = FREERTOS_AF_INET;
        addr.sin_port = FreeRTOS_htons(port);
        addr.sin_address.ulIP_IPv4 = ai->ai_addr->sin_address.ulIP_IPv4;
        char ipDbg[16];
        FreeRTOS_inet_ntoa(addr.sin_address.ulIP_IPv4, ipDbg);
        DBG("wifi_http: %s -> %s\n", host, ipDbg);
        *step = -3;
        BaseType_t err = FreeRTOS_connect(t, &addr, sizeof(addr));
        if (err == 0)
        {
            s = t;
        }
        else
        {
            char ip[16];
            FreeRTOS_inet_ntoa(addr.sin_address.ulIP_IPv4, ip);
            WRN("wifi_http: connect %s (%s):%u error %ld\n", host, ip, port, err);
            FreeRTOS_closesocket(t);
        }
    }
    FreeRTOS_freeaddrinfo(list);
    return s;
}

// Тело в виде chunked: "<длина hex>\r\n<данные>\r\n" ... "0\r\n\r\n" — склеить куски на месте
static size_t dechunk(char *body)
{
    char *src = body, *dst = body;
    for (;;)
    {
        char *end;
        unsigned long n = strtoul(src, &end, 16);
        char *data = strstr(end, "\r\n");
        if (n == 0 || data == NULL)
            break;
        data += 2;
        size_t have = strlen(data);
        if (n > have)
            n = have; // ответ оборвался посреди куска
        memmove(dst, data, n);
        dst += n;
        src = data + n;
        if (src[0] == '\r')
            src += 2;
    }
    *dst = '\0';
    return dst - body;
}

int wifiHttpGet(const char *host, const char *path, char *buf, size_t size, wifiHttpResult *res)
{
    memset(res, 0, sizeof(*res));
    TickType_t t0 = xTaskGetTickCount();
    int ret;
    Socket_t s = httpConnect(host, 80, &ret);
    if (s == NULL)
        return ret;

    // 4. Запрос. "Connection: close" — сервер сам закроет соединение после ответа,
    // так конец ответа виден без разбора Content-Length
    char tmp[512];
    int len = snprintf(tmp, sizeof(tmp),
                       "GET %s HTTP/1.1\r\nHost: %s\r\nUser-Agent: BoardH723\r\nConnection: close\r\n\r\n",
                       path, host);
    ret = -4;
    if (FreeRTOS_send(s, tmp, len, 0) != len)
        goto out;

    // 5. Ответ: первая строка "HTTP/1.1 200 OK", дальше заголовки и тело.
    // Без буфера вызывающего данные идут во временный и только считаются
    bool keep = buf != NULL && size > 1;
    bool overflow = false;
    for (;;)
    {
        char *dst = keep ? buf + res->bytes : tmp;
        size_t room = keep ? size - 1 - res->bytes : sizeof(tmp) - 1;
        if (room == 0)
        {
            overflow = true;
            dst = tmp; // дочитать ответ до конца, чтобы соединение закрылось чисто
            room = sizeof(tmp) - 1;
        }
        BaseType_t n = FreeRTOS_recv(s, dst, room, 0);
        if (n <= 0)
            break; // 0 — таймаут, <0 — сервер закрыл соединение
        if (res->bytes == 0)
        {
            dst[n] = '\0';
            sscanf(dst, "HTTP/%*s %d", &res->status);
        }
        if (!overflow)
            res->bytes += n;
    }
    ret = res->status ? 0 : -5;

    // Тело — после пустой строки "\r\n\r\n", которой кончаются заголовки
    if (keep)
    {
        buf[res->bytes] = '\0';
        char *body = strstr(buf, "\r\n\r\n");
        if (body)
        {
            *body = '\0'; // заголовки — отдельной строкой, для поиска в них
            body += 4;
            res->body = body;
            res->bodyLen = strstr(buf, "chunked") ? dechunk(body) : strlen(body);
        }
        if (overflow)
            ret = -6;
    }

out:
    // Вежливое закрытие: shutdown, дождаться ответного FIN, потом освободить сокет
    FreeRTOS_shutdown(s, FREERTOS_SHUT_RDWR);
    for (int i = 0; i < 20 && FreeRTOS_recv(s, tmp, sizeof(tmp), 0) >= 0; i++)
        vTaskDelay(pdMS_TO_TICKS(50));
    FreeRTOS_closesocket(s);
    res->ms = (xTaskGetTickCount() - t0) * portTICK_PERIOD_MS;
    return ret;
}

bool wifiHttpJoin(const char *ssid, const char *password)
{
    if (ssid == NULL || ssid[0] == '\0')
        return false;
    wifiConnRecord rec = {
        .bssid_set = false,
        .is_wpa3_supported = true,
        .listen_interval = -1,
    };
    strncpy((char *)rec.ssid, ssid, sizeof(rec.ssid) - 1);
    strncpy((char *)rec.password, password, sizeof(rec.password) - 1);
    // Так же делает boot.lua: wifi.enable(true), wifi.setMode(WIFI_STA), wifi.connect(...)
    espFeatureEnable(ftWifi, true);
    wifiSetWiFiMode(WIFI_MODE_STA);
    if (!wifiConnect(&rec, true))
    {
        ERR("wifi_http: connect to %s failed\n", ssid);
        return false;
    }
    return true;
}

// Адрес от DHCP на интерфейсе Wi-Fi STA. Если DHCP не ответил, стек берёт
// link-local 169.254.x.x без шлюза — в интернет так не выйти
bool wifiHttpOnline(void)
{
    return FreeRTOS_IsEndPointUp(&staNetEndPoint) && staNetEndPoint.ipv4_settings.ulGatewayAddress != 0;
}

static void httpTask(void *arg)
{
    (void)arg;
    wifiHttpJoin(WIFI_HTTP_SSID, WIFI_HTTP_PASSWORD);
    for (;;)
    {
        if (!wifiHttpOnline())
        {
            vTaskDelay(pdMS_TO_TICKS(1000));
            continue;
        }
        wifiHttpResult r;
        int e = wifiHttpGet(WIFI_HTTP_HOST, WIFI_HTTP_PATH, NULL, 0, &r);
        if (e == 0)
            INF("wifi_http: GET http://%s%s -> %d, %lu bytes, %lu ms\n",
                WIFI_HTTP_HOST, WIFI_HTTP_PATH, r.status, r.bytes, r.ms);
        else
            WRN("wifi_http: GET http://%s%s failed at step %d\n", WIFI_HTTP_HOST, WIFI_HTTP_PATH, -e);
        vTaskDelay(pdMS_TO_TICKS(WIFI_HTTP_PERIOD_S * 1000));
    }
}

void wifi_http_start(void)
{
    xTaskCreateStatic(httpTask, "wifiHttp", TASK_STACK_SIZE, NULL, tskIDLE_PRIORITY + 1, taskStack, &taskTcb);
    INF("wifi_http: started, GET http://%s%s every %d s\n", WIFI_HTTP_HOST, WIFI_HTTP_PATH, WIFI_HTTP_PERIOD_S);
}
