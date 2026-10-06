#include <stdio.h>
#include <string.h>
#include "sd_file.h"
#include "app.h"
#include "FreeRTOS.h"
#include "task.h"
#include "ff.h"
#include "rtc.h"
#include "sddisk.h"
#include "sdram.h"

#define SD_FILE_DIR "/SD/recipes"
#define SD_FILE_LOG SD_FILE_DIR "/log.txt"
#define SD_FILE_BENCH SD_FILE_DIR "/bench.bin"
#define SD_FILE_PERIOD_S 10
#define BENCH_SIZE_KB 4096
#define BENCH_BLOCK (32 * 1024) // крупные блоки: драйвер шлёт до 64 секторов одной командой

#define TASK_STACK_SIZE 1024 // FIL и printf — стек побольше

static StackType_t taskStack[TASK_STACK_SIZE];
static StaticTask_t taskTcb;

// Пути вида /SD/... и /RAM/... понимают и stdio (fopen), и FatFs (f_open):
// stdio-обёртки — в libs/disks/wrappers.c
bool sdFileAppend(const char *path, const char *line)
{
    FILE *f = fopen(path, "a");
    if (!f)
        return false;
    bool ok = fputs(line, f) >= 0;
    // fclose сбрасывает буфер и обновляет каталог: без него после сброса
    // питания строка может пропасть
    ok = (fclose(f) == 0) && ok;
    return ok;
}

bool sdFileBenchmark(const char *path, uint32_t sizeKB, uint32_t *writeKBps, uint32_t *readKBps)
{
    // Буфер в SDRAM: во внутренних ОЗУ 32 КБ свободных нет
    uint8_t *buf = sdramMalloc(BENCH_BLOCK);
    if (!buf)
        return false;
    for (int i = 0; i < BENCH_BLOCK; i++)
        buf[i] = (uint8_t)i;

    FIL f;
    UINT n;
    bool ok = false;
    uint32_t blocks = sizeKB * 1024 / BENCH_BLOCK;

    if (f_open(&f, path, FA_CREATE_ALWAYS | FA_WRITE) != FR_OK)
        goto out;
    TickType_t t0 = xTaskGetTickCount();
    for (uint32_t i = 0; i < blocks; i++)
    {
        if (f_write(&f, buf, BENCH_BLOCK, &n) != FR_OK || n != BENCH_BLOCK)
        {
            f_close(&f);
            goto out;
        }
    }
    f_close(&f);
    TickType_t dt = xTaskGetTickCount() - t0;
    *writeKBps = sizeKB * 1000 / (dt ? dt : 1);

    if (f_open(&f, path, FA_READ) != FR_OK)
        goto out;
    t0 = xTaskGetTickCount();
    ok = true;
    for (uint32_t i = 0; i < blocks && ok; i++)
    {
        ok = f_read(&f, buf, BENCH_BLOCK, &n) == FR_OK && n == BENCH_BLOCK && buf[1] == 1 &&
             buf[BENCH_BLOCK - 1] == (uint8_t)(BENCH_BLOCK - 1);
    }
    f_close(&f);
    dt = xTaskGetTickCount() - t0;
    *readKBps = sizeKB * 1000 / (dt ? dt : 1);
    f_unlink(path);
out:
    sdramFree(buf);
    return ok;
}

static void sdTask(void *arg)
{
    (void)arg;
    // Карту монтирует appTask (sdCardDetectLoop) — ждём её
    while (!sdDiskIsMounted())
        vTaskDelay(pdMS_TO_TICKS(500));

    FRESULT r = f_mkdir(SD_FILE_DIR);
    if (r != FR_OK && r != FR_EXIST)
    {
        ERR("sd_file: mkdir %s: FatFs error %d\n", SD_FILE_DIR, r);
        vTaskDelete(NULL);
    }

    uint32_t w, rd;
    if (sdFileBenchmark(SD_FILE_BENCH, BENCH_SIZE_KB, &w, &rd))
        INF("sd_file: %d KB write %lu KB/s, read %lu KB/s\n", BENCH_SIZE_KB, w, rd);
    else
        ERR("sd_file: benchmark failed\n");

    for (uint32_t n = 1;; n++)
    {
        if (!sdDiskIsMounted())
        {
            vTaskDelay(pdMS_TO_TICKS(1000));
            continue;
        }
        // Время — из RTC (его ставит NTP при доступе в интернет). HAL требует
        // читать сначала время, потом дату: чтение времени фиксирует теневые регистры
        RTC_TimeTypeDef t;
        RTC_DateTypeDef d;
        HAL_RTC_GetTime(&hrtc, &t, RTC_FORMAT_BIN);
        HAL_RTC_GetDate(&hrtc, &d, RTC_FORMAT_BIN);
        char line[80];
        snprintf(line, sizeof(line), "20%02u-%02u-%02u %02u:%02u:%02u #%lu uptime %lu s\n",
                 d.Year, d.Month, d.Date, t.Hours, t.Minutes, t.Seconds, n,
                 xTaskGetTickCount() / configTICK_RATE_HZ);
        if (sdFileAppend(SD_FILE_LOG, line))
        {
            FILINFO fi;
            f_stat(SD_FILE_LOG, &fi);
            INF("sd_file: %s += %s", SD_FILE_LOG, line);
            INF("sd_file: size %lu bytes\n", (uint32_t)fi.fsize);
        }
        else
        {
            ERR("sd_file: append %s failed\n", SD_FILE_LOG);
        }
        vTaskDelay(pdMS_TO_TICKS(SD_FILE_PERIOD_S * 1000));
    }
}

void sd_file_start(void)
{
    xTaskCreateStatic(sdTask, "sdFile", TASK_STACK_SIZE, NULL, tskIDLE_PRIORITY + 1, taskStack, &taskTcb);
    INF("sd_file: started, log %s every %d s\n", SD_FILE_LOG, SD_FILE_PERIOD_S);
}
