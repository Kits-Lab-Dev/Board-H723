#ifndef GT911_H
#define GT911_H

#include <stdbool.h>
#include <stdint.h>

// Сенсорная панель Goodix GT911 на I2C1: SCL PB8 (J1-26), SDA PB7 (J1-23),
// INT PB9 (J1-25), RST PD4 (J1-17)
#define GT911_ADDR 0x5D // адрес выбирается уровнем INT при сбросе: 0 — 0x5D

// Сброс, выбор адреса, чтение ID и разрешения панели. false — контроллер не ответил.
bool gt911Init(void);

// Последнее касание: true — палец на панели, x, y — координаты в пикселях панели
bool gt911Read(uint16_t *x, uint16_t *y);

#endif
