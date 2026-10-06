#ifndef WIFI_HTTP_H
#define WIFI_HTTP_H

#include <stdint.h>

// Точка доступа Wi-Fi. Пустая строка — рецепт сам не подключается и ждёт,
// пока это сделает кто-то другой (/SD/boot.lua в прошивке с Lua). Без Lua — задать здесь.
#ifndef WIFI_HTTP_SSID
#define WIFI_HTTP_SSID ""
#endif
#ifndef WIFI_HTTP_PASSWORD
#define WIFI_HTTP_PASSWORD ""
#endif

// Что запрашивать и как часто
#define WIFI_HTTP_HOST "example.com"
#define WIFI_HTTP_PATH "/"
#define WIFI_HTTP_PERIOD_S 60

typedef struct
{
    int status;          // код ответа HTTP (200, 404...), 0 — ответа не было
    uint32_t bytes;      // принято байт вместе с заголовками
    uint32_t ms;         // время запроса (заполняется, если дошло до соединения)
} wifiHttpResult;

// Подключается к Wi-Fi (если задан WIFI_HTTP_SSID) и раз в WIFI_HTTP_PERIOD_S
// делает GET http://WIFI_HTTP_HOST/WIFI_HTTP_PATH
void wifi_http_start(void);

// Один GET-запрос по HTTP (порт 80). Тело ответа не сохраняется — только считается.
// Возвращает 0 при успехе, иначе отрицательный код шага, где случилась ошибка.
int wifiHttpGet(const char *host, const char *path, wifiHttpResult *res);

#endif
