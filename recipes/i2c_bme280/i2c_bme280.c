#include "i2c_bme280.h"
#include "app.h"
#include "FreeRTOS.h"
#include "task.h"

#define I2C_TIMEOUT_MS 10
#define READ_PERIOD_MS 2000
#define TASK_STACK_SIZE 512

// Регистры BME280/BMP280
#define REG_ID 0xD0         // 0x60 — BME280, 0x58 — BMP280
#define REG_RESET 0xE0
#define REG_CALIB_TP 0x88   // 26 байт: T1..P9, H1
#define REG_CALIB_H 0xE1    // 7 байт: H2..H6
#define REG_CTRL_HUM 0xF2
#define REG_CTRL_MEAS 0xF4
#define REG_CONFIG 0xF5
#define REG_DATA 0xF7       // 8 байт: давление, температура, влажность

static StackType_t taskStack[TASK_STACK_SIZE];
static StaticTask_t taskTcb;

static bool readRegs(bme280 *dev, uint8_t reg, uint8_t *buf, uint16_t len)
{
    return HAL_I2C_Mem_Read(dev->i2c, dev->addr << 1, reg, I2C_MEMADD_SIZE_8BIT, buf, len,
                            I2C_TIMEOUT_MS) == HAL_OK;
}

static bool writeReg(bme280 *dev, uint8_t reg, uint8_t val)
{
    return HAL_I2C_Mem_Write(dev->i2c, dev->addr << 1, reg, I2C_MEMADD_SIZE_8BIT, &val, 1,
                             I2C_TIMEOUT_MS) == HAL_OK;
}

int i2cScan(I2C_HandleTypeDef *i2c, uint8_t *found, int max)
{
    int n = 0;
    // 0x00–0x07 и 0x78–0x7F зарезервированы стандартом I2C
    for (uint8_t a = 0x08; a < 0x78 && n < max; a++)
    {
        // Адрес + бит записи: если устройство есть, оно отвечает ACK
        if (HAL_I2C_IsDeviceReady(i2c, a << 1, 1, I2C_TIMEOUT_MS) == HAL_OK)
            found[n++] = a;
    }
    return n;
}

bool bme280Init(bme280 *dev, I2C_HandleTypeDef *i2c, uint8_t addr)
{
    dev->i2c = i2c;
    dev->addr = addr;
    uint8_t id;
    if (!readRegs(dev, REG_ID, &id, 1) || (id != 0x60 && id != 0x58))
        return false;
    dev->hasHumidity = id == 0x60;

    writeReg(dev, REG_RESET, 0xB6);
    vTaskDelay(pdMS_TO_TICKS(5)); // после сброса датчик копирует калибровку из NVM ~2 мс

    uint8_t c[26];
    if (!readRegs(dev, REG_CALIB_TP, c, sizeof(c)))
        return false;
    // Коэффициенты little-endian: младший байт первым
    dev->T1 = (uint16_t)(c[1] << 8 | c[0]);
    dev->T2 = (int16_t)(c[3] << 8 | c[2]);
    dev->T3 = (int16_t)(c[5] << 8 | c[4]);
    dev->P1 = (uint16_t)(c[7] << 8 | c[6]);
    dev->P2 = (int16_t)(c[9] << 8 | c[8]);
    dev->P3 = (int16_t)(c[11] << 8 | c[10]);
    dev->P4 = (int16_t)(c[13] << 8 | c[12]);
    dev->P5 = (int16_t)(c[15] << 8 | c[14]);
    dev->P6 = (int16_t)(c[17] << 8 | c[16]);
    dev->P7 = (int16_t)(c[19] << 8 | c[18]);
    dev->P8 = (int16_t)(c[21] << 8 | c[20]);
    dev->P9 = (int16_t)(c[23] << 8 | c[22]);
    dev->H1 = c[25];

    if (dev->hasHumidity)
    {
        uint8_t h[7];
        if (!readRegs(dev, REG_CALIB_H, h, sizeof(h)))
            return false;
        dev->H2 = (int16_t)(h[1] << 8 | h[0]);
        dev->H3 = h[2];
        dev->H4 = (int16_t)((int8_t)h[3] * 16 | (h[4] & 0x0F)); // 12 бит со знаком
        dev->H5 = (int16_t)((int8_t)h[5] * 16 | (h[4] >> 4));
        dev->H6 = (int8_t)h[6];
        // ctrl_hum вступает в силу только после записи ctrl_meas — пишем его первым
        writeReg(dev, REG_CTRL_HUM, 0x01); // влажность ×1
    }
    // Простой 1000 мс между измерениями, фильтр выключен
    writeReg(dev, REG_CONFIG, 0xA0);
    // Температура ×1, давление ×1, нормальный режим (меряет сам по кругу)
    return writeReg(dev, REG_CTRL_MEAS, 0x27);
}

// Формулы компенсации — из даташита BME280 (раздел 4.2.3), целочисленные
bool bme280Read(bme280 *dev, bme280Data *out)
{
    uint8_t d[8];
    if (!readRegs(dev, REG_DATA, d, sizeof(d))) // одним чтением: значения из одного измерения
        return false;
    int32_t adcP = (int32_t)(d[0] << 12 | d[1] << 4 | d[2] >> 4);
    int32_t adcT = (int32_t)(d[3] << 12 | d[4] << 4 | d[5] >> 4);
    int32_t adcH = (int32_t)(d[6] << 8 | d[7]);

    int32_t v1 = ((((adcT >> 3) - ((int32_t)dev->T1 << 1))) * dev->T2) >> 11;
    int32_t v2 = (((((adcT >> 4) - (int32_t)dev->T1) * ((adcT >> 4) - (int32_t)dev->T1)) >> 12) * dev->T3) >> 14;
    int32_t tFine = v1 + v2; // «точная» температура, нужна для давления и влажности
    out->temp = (tFine * 5 + 128) >> 8;

    int64_t p1 = (int64_t)tFine - 128000;
    int64_t p2 = p1 * p1 * dev->P6;
    p2 += (p1 * dev->P5) << 17;
    p2 += (int64_t)dev->P4 << 35;
    p1 = ((p1 * p1 * dev->P3) >> 8) + ((p1 * dev->P2) << 12);
    p1 = ((((int64_t)1 << 47) + p1) * dev->P1) >> 33;
    if (p1 == 0)
    {
        out->pressure = 0; // защита от деления на ноль
    }
    else
    {
        int64_t p = 1048576 - adcP;
        p = (((p << 31) - p2) * 3125) / p1;
        int64_t q1 = ((int64_t)dev->P9 * (p >> 13) * (p >> 13)) >> 25;
        int64_t q2 = ((int64_t)dev->P8 * p) >> 19;
        p = ((p + q1 + q2) >> 8) + ((int64_t)dev->P7 << 4);
        out->pressure = (uint32_t)(p >> 8); // формула даёт Па × 256
    }

    out->humidity = 0;
    if (dev->hasHumidity)
    {
        int32_t h = tFine - 76800;
        h = (((((adcH << 14) - ((int32_t)dev->H4 << 20) - ((int32_t)dev->H5 * h)) + 16384) >> 15) *
             (((((((h * dev->H6) >> 10) * (((h * (int32_t)dev->H3) >> 11) + 32768)) >> 10) + 2097152) *
                   dev->H2 + 8192) >> 14));
        h -= (((((h >> 15) * (h >> 15)) >> 7) * (int32_t)dev->H1) >> 4);
        h = h < 0 ? 0 : h;
        h = h > 419430400 ? 419430400 : h;
        out->humidity = (uint32_t)(h >> 12);
    }
    return true;
}

static void sensorTask(void *arg)
{
    (void)arg;
    uint8_t found[16];
    int n = i2cScan(&hi2c1, found, 16);
    if (n == 0)
        WRN("i2c_bme280: no devices on I2C1 (PB8 SCL, PB7 SDA)\n");
    for (int i = 0; i < n; i++)
        INF("i2c_bme280: device at 0x%02X\n", found[i]);

    bme280 dev;
    for (;;)
    {
        if (bme280Init(&dev, &hi2c1, 0x76) || bme280Init(&dev, &hi2c1, 0x77))
            break;
        vTaskDelay(pdMS_TO_TICKS(5000)); // датчик могут подключить позже
    }
    INF("i2c_bme280: %s at 0x%02X\n", dev.hasHumidity ? "BME280" : "BMP280", dev.addr);
    vTaskDelay(pdMS_TO_TICKS(100)); // первое измерение готово через ~10 мс

    for (;;)
    {
        bme280Data d;
        if (bme280Read(&dev, &d))
            INF("i2c_bme280: %ld.%02ld C, %lu Pa (%lu mmHg), %lu.%lu %%RH\n",
                d.temp / 100, (d.temp < 0 ? -d.temp : d.temp) % 100, d.pressure,
                d.pressure * 100 / 13332, d.humidity >> 10, ((d.humidity & 1023) * 10) >> 10);
        else
            WRN("i2c_bme280: read failed\n");
        vTaskDelay(pdMS_TO_TICKS(READ_PERIOD_MS));
    }
}

void i2c_bme280_start(void)
{
    // На плате нет подтяжек на PB7/PB8: у модулей датчиков они обычно свои (4,7–10 кОм).
    // Встроенные ~40 кОм держат линии в 1, если к разъёму ничего не подключено.
    GPIO_InitTypeDef gpio = {
        .Pin = GPIO_PIN_7 | GPIO_PIN_8,
        .Mode = GPIO_MODE_AF_OD,
        .Pull = GPIO_PULLUP,
        .Speed = GPIO_SPEED_FREQ_LOW,
        .Alternate = GPIO_AF4_I2C1,
    };
    HAL_GPIO_Init(GPIOB, &gpio);
    xTaskCreateStatic(sensorTask, "bme280", TASK_STACK_SIZE, NULL, tskIDLE_PRIORITY + 1, taskStack, &taskTcb);
    INF("i2c_bme280: started\n");
}
