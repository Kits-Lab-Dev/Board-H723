#include <string.h>
#include "lvgl_port.h"
#include "lcd_ltdc.h"
#include "gt911.h"
#include "app.h"
#include "FreeRTOS.h"
#include "task.h"
#include "ff.h"
#include "sdram.h"

// ---------- Память LVGL — в SDRAM (LV_USE_STDLIB_MALLOC = LV_STDLIB_CUSTOM) ----------

void lv_mem_init(void)
{
}

void lv_mem_deinit(void)
{
}

lv_mem_pool_t lv_mem_add_pool(void *mem, size_t bytes)
{
    LV_UNUSED(mem);
    LV_UNUSED(bytes);
    return NULL;
}

void lv_mem_remove_pool(lv_mem_pool_t pool)
{
    LV_UNUSED(pool);
}

void *lv_malloc_core(size_t size)
{
    return sdramMalloc(size);
}

void *lv_realloc_core(void *p, size_t new_size)
{
    return sdramRealloc(p, new_size);
}

void lv_free_core(void *p)
{
    sdramFree(p);
}

void lv_mem_monitor_core(lv_mem_monitor_t *mon)
{
    LV_UNUSED(mon);
}

lv_result_t lv_mem_test_core(void)
{
    return LV_RESULT_OK;
}

// ---------- Время, лог ----------

static uint32_t tick(void)
{
    return xTaskGetTickCount() * portTICK_PERIOD_MS;
}

static void logPrint(lv_log_level_t level, const char *buf)
{
    LV_UNUSED(level);
    WRN("lvgl: %s", buf);
}

// ---------- Дисплей ----------

// Режим DIRECT: LVGL рисует прямо в кадровые буферы LTDC и сама переносит изменённые
// области из показанного буфера в следующий. Здесь остаётся показать готовый буфер.
static void flush(lv_display_t *disp, const lv_area_t *area, uint8_t *px)
{
    LV_UNUSED(area);
    if (lv_display_flush_is_last(disp))
    {
        // Процессор рисовал через кэш (write-back) — выгрузить в SDRAM всё, что лежит в
        // кэше: LTDC читает память мимо него. Весь кэш (32 КБ) быстрее, чем буфер построчно.
        SCB_CleanDCache();
        lcdShow((uint16_t *)px);
    }
    lv_display_flush_ready(disp);
}

// ---------- Тач ----------

static void touchRead(lv_indev_t *indev, lv_indev_data_t *data)
{
    LV_UNUSED(indev);
    uint16_t x, y;
    bool pressed = gt911Read(&x, &y);
    data->point.x = x;
    data->point.y = y;
    data->state = pressed ? LV_INDEV_STATE_PRESSED : LV_INDEV_STATE_RELEASED;
}

bool lvglPortInit(void)
{
    if (!lcdInit())
    {
        ERR("lvgl: no SDRAM for frame buffers\n");
        return false;
    }
    lv_init();
    lv_tick_set_cb(tick);
    lv_log_register_print_cb(logPrint);

    lv_display_t *disp = lv_display_create(LCD_WIDTH, LCD_HEIGHT);
    lv_display_set_color_format(disp, LV_COLOR_FORMAT_RGB565);
    lv_display_set_buffers(disp, lcdFrameBuffer(1), lcdFrameBuffer(0), LCD_WIDTH * LCD_HEIGHT * 2,
                           LV_DISPLAY_RENDER_MODE_DIRECT);
    lv_display_set_flush_cb(disp, flush);

    if (gt911Init())
    {
        lv_indev_t *touch = lv_indev_create();
        lv_indev_set_type(touch, LV_INDEV_TYPE_POINTER);
        lv_indev_set_read_cb(touch, touchRead);
    }
    return true;
}

// ---------- Шрифты с SD-карты ----------

#define MAX_FONT_FILES 4

static struct
{
    char path[64];
    uint8_t *data;
    UINT size;
} fontFiles[MAX_FONT_FILES];

static uint8_t *loadFile(const char *path, UINT *size)
{
    for (int i = 0; i < MAX_FONT_FILES; i++)
        if (fontFiles[i].data && strcmp(fontFiles[i].path, path) == 0)
        {
            *size = fontFiles[i].size;
            return fontFiles[i].data;
        }

    static FIL f; // FIL с буфером сектора — больше 0,5 КБ, не для стека
    if (f_open(&f, path, FA_READ) != FR_OK)
        return NULL;
    UINT len = f_size(&f), got = 0;
    uint8_t *data = sdramMalloc(len);
    // Крупными блоками: драйвер SD передаёт до 32 КБ одной командой
    for (UINT n; data && got < len; got += n)
        if (f_read(&f, data + got, len - got < 32768 ? len - got : 32768, &n) != FR_OK || n == 0)
            break;
    f_close(&f);
    if (!data || got != len)
    {
        sdramFree(data);
        return NULL;
    }
    for (int i = 0; i < MAX_FONT_FILES; i++)
        if (!fontFiles[i].data)
        {
            strncpy(fontFiles[i].path, path, sizeof(fontFiles[i].path) - 1);
            fontFiles[i].data = data;
            fontFiles[i].size = len;
            break;
        }
    *size = len;
    return data;
}

lv_font_t *lvglLoadFont(const char *path, int size)
{
    UINT len;
    uint8_t *data = loadFile(path, &len);
    if (!data)
    {
        WRN("lvgl: no font %s\n", path);
        return NULL;
    }
    return lv_tiny_ttf_create_data(data, len, size);
}
