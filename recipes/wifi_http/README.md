# wifi_http — Wi-Fi и HTTP-запрос

Подключается к точке доступа Wi-Fi и раз в минуту делает `GET http://example.com/`: адрес по
DNS, TCP-соединение, запрос, код ответа и объём. Только сокеты FreeRTOS+TCP, без библиотек.

## Включить

Имя и пароль сети — в `wifi_http.h`:

```c
#define WIFI_HTTP_SSID "my_wifi"
#define WIFI_HTTP_PASSWORD "my_password"
```

Если оставить пустыми, рецепт сам не подключается и ждёт, пока Wi-Fi поднимет кто-то другой —
`/SD/boot.lua` (`wifi.connect(...)`) в прошивке с Lua. В прошивке без Lua (`-DH723_WITH_LUA=OFF`)
имя и пароль нужно задать здесь.

```bash
cmake --preset Release -DH723_RECIPES="wifi_http"
```

## Вывод на плате

Wi-Fi поднял `boot.lua`, плата получила адрес 192.168.1.50. Строки `->` с адресом — отладочный
уровень лога (`DBG`):

```
wifi_http: started, GET http://example.com/ every 60 s
IP Address: 192.168.1.50
wifi_http: example.com -> 8.6.112.6
wifi_http: GET http://example.com/ -> 200, 932 bytes, 3160 ms
wifi_http: example.com -> 8.6.112.6
wifi_http: GET http://example.com/ -> 200, 929 bytes, 146 ms
...
wifi_http: example.com -> 8.6.112.0
wifi_http: connect example.com (8.6.112.0):80 error -116
wifi_http: example.com -> 8.47.69.0
wifi_http: GET http://example.com/ -> 200, 930 bytes, 5417 ms
```

Первый запрос после подключения дольше (3 с). В конце — перебор адресов: первый не ответил за 5 с
(`-116` — таймаут), сработал второй.

При ошибке рецепт пишет, на каком шаге: 1 — DNS, 2 — сокет, 3 — соединение (с адресом и кодом
ошибки), 4 — отправка, 5 — нет ответа HTTP.

## Как это работает

- **Wi-Fi.** ESP32-C2 на плате — сетевой адаптер: TCP/IP работает на STM32 (FreeRTOS+TCP), а ESP
  передаёт кадры Ethernet по SPI. Подключение — те же вызовы, что в Lua: `espFeatureEnable(ftWifi)`,
  `wifiSetWiFiMode(WIFI_MODE_STA)`, `wifiConnect()` из `libs/esp/control.h`. `autoReconnect = true` —
  ESP сам переподключается при обрыве.
- **Адрес.** Плата получает IP по DHCP от роутера. Задача ждёт, пока интерфейс поднят и у него
  есть шлюз: если DHCP не ответил, стек сам назначает адрес 169.254.x.x без шлюза, и с ним в
  интернет не выйти.
- **Три сети одновременно.** У платы есть USB-сеть (192.168.4.1), точка доступа (192.168.11.1) и
  Wi-Fi-клиент. Пакет в интернет уходит через тот интерфейс, где есть шлюз, — Wi-Fi-клиент.
- **DNS и несколько адресов.** `FreeRTOS_getaddrinfo` спрашивает DNS-сервер, выданный роутером, и
  возвращает список адресов. У крупных сайтов адресов несколько, и какой-то может не отвечать —
  `httpConnect` пробует их по очереди. Кэш DNS хранит до 4 адресов на имя
  (`ipconfigDNS_CACHE_ADDRESSES_PER_ENTRY` в `config/FreeRTOSIPConfig.h`), поэтому повторный запрос
  из кэша отдаёт весь список, а не один последний адрес ответа — он может оказаться мёртвым.
  Ошибки FreeRTOS+TCP — отрицательные `pdFREERTOS_ERRNO_*`.
- **Таймауты.** `FREERTOS_SO_RCVTIMEO`/`SNDTIMEO` по 5 с: без них `recv` ждёт вечно, если
  сервер замолчал.
- **Конец ответа.** Заголовок `Connection: close` — сервер закрывает соединение после ответа, и
  `FreeRTOS_recv` возвращает ошибку: так конец ответа виден без разбора `Content-Length`.
- **Тело ответа.** Если передать `wifiHttpGet` буфер, ответ принимается в него целиком; тело
  начинается после пустой строки `"\r\n\r\n"` и возвращается в `res.body`. Ответ с
  `Transfer-Encoding: chunked` (куски «длина в hex, данные») склеивается в обычную строку.
  Не поместился в буфер — код `-6`, начало ответа при этом в буфере есть.
- **Закрытие.** `FreeRTOS_shutdown`, дождаться ответного закрытия, затем `FreeRTOS_closesocket`.
  Если сразу звать `closesocket`, соединение у сервера может остаться висеть.

## Переделать под себя

- Другой адрес — `WIFI_HTTP_HOST`, `WIFI_HTTP_PATH`. Пример разбора тела (JSON с погодой) и
  подключения к сети из своего кода (`wifiHttpJoin`, `wifiHttpOnline`) — рецепт
  [lvgl_clock](../lvgl_clock/README.md).
- Отправка данных на сервер (POST) — тот же код, в запросе `POST`, заголовки `Content-Type` и
  `Content-Length` и тело после пустой строки.
- HTTPS — через Mbed TLS (`libs/tls`, его уже использует Lua-модуль `ssl`); в рецепт не входит.
