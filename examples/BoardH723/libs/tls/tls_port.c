/* Порт Mbed TLS на плату: источник энтропии, время и общий генератор
 * случайных чисел. Настройки Mbed TLS — config/mbedtls_board_config.h. */
#include "tls_port.h"

#include <string.h>
#include <time.h>

#include "FreeRTOS.h"
#include "semphr.h"
#include "task.h"
#include "rng.h"

#include "mbedtls/ctr_drbg.h"
#include "mbedtls/entropy.h"
#include "mbedtls/platform_time.h"
#include "mbedtls/platform_util.h"

time_t get_time(time_t *puxTime); /* libs/ntp/ntpclient.c: часы RTC */

/* Аппаратный RNG. RNG инициализирован в main() (MX_RNG_Init) и не выключается:
 * порт STM32 в wolfCrypt вызывает HAL_RNG_DeInit и гасит тактирование RNG,
 * поэтому wolfCrypt берёт seed отсюда (CUSTOM_RAND_GENERATE_SEED). */
int hw_random(unsigned char *output, unsigned int len)
{
    unsigned int n = 0;
    while (n < len)
    {
        uint32_t v;
        /* HAL_BUSY — RNG сейчас читает другая задача, пробуем снова */
        HAL_StatusTypeDef st = HAL_RNG_GenerateRandomNumber(&hrng, &v);
        if (st == HAL_BUSY)
        {
            taskYIELD();
            continue;
        }
        if (st != HAL_OK)
            break;
        unsigned int k = len - n < sizeof(v) ? len - n : sizeof(v);
        memcpy(output + n, &v, k);
        n += k;
    }
    return n == len ? 0 : -1;
}

/* Энтропия для Mbed TLS (MBEDTLS_ENTROPY_HARDWARE_ALT) */
int mbedtls_hardware_poll(void *data, unsigned char *output, size_t len, size_t *olen)
{
    (void)data;
    if (hw_random(output, len) != 0)
    {
        *olen = 0;
        return MBEDTLS_ERR_ENTROPY_SOURCE_FAILED;
    }
    *olen = len;
    return 0;
}

/* Время для проверки срока действия сертификатов (MBEDTLS_PLATFORM_TIME_ALT) */
static mbedtls_time_t rtc_time(mbedtls_time_t *t)
{
    time_t now = get_time(NULL);
    if (t != NULL)
        *t = now;
    return now;
}

mbedtls_ms_time_t mbedtls_ms_time(void)
{
    return (mbedtls_ms_time_t)xTaskGetTickCount() * portTICK_PERIOD_MS;
}

struct tm *mbedtls_platform_gmtime_r(const mbedtls_time_t *tt, struct tm *tm_buf)
{
    time_t t = (time_t)*tt;
    return gmtime_r(&t, tm_buf);
}

/* Общий генератор CTR_DRBG. Его вызывают задачи Lua (TLS) и NimBLE,
 * поэтому доступ — под мьютексом. */
static mbedtls_entropy_context entropy;
static mbedtls_ctr_drbg_context drbg;
static SemaphoreHandle_t drbgLock;
static int drbgReady;

static int tls_init(void)
{
    if (drbgReady)
        return 0;

    taskENTER_CRITICAL();
    if (drbgLock == NULL)
        drbgLock = xSemaphoreCreateMutex();
    taskEXIT_CRITICAL();
    if (drbgLock == NULL)
        return MBEDTLS_ERR_CTR_DRBG_ENTROPY_SOURCE_FAILED;

    int ret = 0;
    xSemaphoreTake(drbgLock, portMAX_DELAY);
    if (!drbgReady)
    {
        mbedtls_platform_set_time(rtc_time);
        mbedtls_entropy_init(&entropy);
        mbedtls_ctr_drbg_init(&drbg);
        static const unsigned char pers[] = "BoardH723";
        ret = mbedtls_ctr_drbg_seed(&drbg, mbedtls_entropy_func, &entropy, pers, sizeof(pers) - 1);
        if (ret == 0)
            drbgReady = 1;
    }
    xSemaphoreGive(drbgLock);
    return ret;
}

int tls_random(void *p_rng, unsigned char *output, size_t len)
{
    (void)p_rng;
    int ret = tls_init();
    if (ret != 0)
        return ret;
    xSemaphoreTake(drbgLock, portMAX_DELAY);
    ret = mbedtls_ctr_drbg_random(&drbg, output, len);
    xSemaphoreGive(drbgLock);
    return ret;
}
