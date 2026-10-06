#ifndef TCP_SPEED_H
#define TCP_SPEED_H

// Точка доступа Wi-Fi: задаётся опциями CMake -DTCP_SPEED_SSID=... -DTCP_SPEED_PASSWORD=...
// Пустая строка — рецепт сам не подключается и ждёт, пока Wi-Fi поднимет кто-то другой
#ifndef TCP_SPEED_SSID
#define TCP_SPEED_SSID ""
#endif
#ifndef TCP_SPEED_PASSWORD
#define TCP_SPEED_PASSWORD ""
#endif

// Порты: на первый ПК шлёт данные (плата принимает и выбрасывает),
// со второго плата шлёт поток, пока ПК не закроет соединение
#define TCP_SPEED_RX_PORT 5001
#define TCP_SPEED_TX_PORT 5002

// Подключается к Wi-Fi (если задан TCP_SPEED_SSID) и слушает оба порта
void tcp_speed_start(void);

#endif
