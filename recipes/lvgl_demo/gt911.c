#include <string.h>
#include "gt911.h"
#include "app.h"
#include "FreeRTOS.h"
#include "task.h"
#include "i2c.h"

// Регистры GT911 — 16-битные адреса (даташит GT911, раздел «Register Map»)
#define REG_PRODUCT_ID 0x8140 // 4 символа ASCII, например "911"
#define REG_X_MAX 0x8048      // разрешение панели: X, Y — по 2 байта, младший первым
#define REG_STATUS 0x814E     // бит 7 — новые данные, биты 0–3 — число касаний
#define REG_POINT1 0x8150     // первое касание: X (2 байта), Y (2 байта), размер (2 байта)

#define INT_PORT GPIOB
#define INT_PIN GPIO_PIN_9
#define RST_PORT GPIOD
#define RST_PIN GPIO_PIN_4

static bool touched;
static uint16_t lastX, lastY;

static bool readReg(uint16_t reg, uint8_t *buf, uint16_t len)
{
    return HAL_I2C_Mem_Read(&hi2c1, GT911_ADDR << 1, reg, I2C_MEMADD_SIZE_16BIT, buf, len, 10) == HAL_OK;
}

static bool writeReg(uint16_t reg, uint8_t value)
{
    return HAL_I2C_Mem_Write(&hi2c1, GT911_ADDR << 1, reg, I2C_MEMADD_SIZE_16BIT, &value, 1, 10) == HAL_OK;
}

bool gt911Init(void)
{
    // Сброс: пока RST в нуле, уровень INT выбирает адрес (0 — 0x5D). После сброса INT —
    // вход: контроллер сам выдаёт на нём импульсы при касании (здесь он не используется,
    // касания опрашиваются).
    __HAL_RCC_GPIOB_CLK_ENABLE();
    __HAL_RCC_GPIOD_CLK_ENABLE();
    GPIO_InitTypeDef gpio = {.Mode = GPIO_MODE_OUTPUT_PP, .Pull = GPIO_NOPULL, .Speed = GPIO_SPEED_FREQ_LOW};
    gpio.Pin = RST_PIN;
    HAL_GPIO_WritePin(RST_PORT, RST_PIN, GPIO_PIN_RESET);
    HAL_GPIO_Init(RST_PORT, &gpio);
    gpio.Pin = INT_PIN;
    HAL_GPIO_WritePin(INT_PORT, INT_PIN, GPIO_PIN_RESET);
    HAL_GPIO_Init(INT_PORT, &gpio);
    vTaskDelay(pdMS_TO_TICKS(10));
    HAL_GPIO_WritePin(RST_PORT, RST_PIN, GPIO_PIN_SET);
    vTaskDelay(pdMS_TO_TICKS(60)); // INT держим в нуле ≥ 50 мс после сброса
    gpio.Mode = GPIO_MODE_INPUT;
    HAL_GPIO_Init(INT_PORT, &gpio);
    vTaskDelay(pdMS_TO_TICKS(50));

    uint8_t id[5] = {0}, res[4];
    if (!readReg(REG_PRODUCT_ID, id, 4) || !readReg(REG_X_MAX, res, 4))
    {
        ERR("gt911: no answer at 0x%02X\n", GT911_ADDR);
        return false;
    }
    INF("gt911: product %s, panel %ux%u\n", id, res[0] | (res[1] << 8), res[2] | (res[3] << 8));
    return true;
}

bool gt911Read(uint16_t *x, uint16_t *y)
{
    uint8_t status;
    if (readReg(REG_STATUS, &status, 1) && (status & 0x80))
    {
        // Новые данные: число касаний и координаты первого; затем сбросить флаг,
        // иначе контроллер не обновит регистры
        uint8_t p[4];
        touched = (status & 0x0F) > 0 && readReg(REG_POINT1, p, 4);
        if (touched)
        {
            lastX = p[0] | (p[1] << 8);
            lastY = p[2] | (p[3] << 8);
        }
        writeReg(REG_STATUS, 0);
    }
    *x = lastX;
    *y = lastY;
    return touched;
}
