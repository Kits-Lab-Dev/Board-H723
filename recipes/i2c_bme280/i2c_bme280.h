#ifndef I2C_BME280_H
#define I2C_BME280_H

#include <stdbool.h>
#include <stdint.h>
#include "i2c.h"

typedef struct
{
    I2C_HandleTypeDef *i2c;
    uint8_t addr;   // 7-битный адрес: 0x76 (SDO на GND) или 0x77 (SDO на 3,3 В)
    bool hasHumidity; // BME280 — да, BMP280 — нет
    // Калибровочные коэффициенты из памяти датчика (даташит BME280, раздел 4.2.2)
    uint16_t T1;
    int16_t T2, T3;
    uint16_t P1;
    int16_t P2, P3, P4, P5, P6, P7, P8, P9;
    uint8_t H1, H3;
    int16_t H2, H4, H5;
    int8_t H6;
} bme280;

typedef struct
{
    int32_t temp;      // °C × 100
    uint32_t pressure; // Па
    uint32_t humidity; // %RH × 1024 (0, если датчик BMP280)
} bme280Data;

// Датчик на I2C1 (PB8 SCL, PB7 SDA): скан шины, затем чтение раз в 2 с
void i2c_bme280_start(void);

// Адреса всех устройств, ответивших на шине. Возвращает их число.
int i2cScan(I2C_HandleTypeDef *i2c, uint8_t *found, int max);

// Проверить ID, прочитать калибровку, включить измерения раз в секунду
bool bme280Init(bme280 *dev, I2C_HandleTypeDef *i2c, uint8_t addr);
bool bme280Read(bme280 *dev, bme280Data *out);

#endif
