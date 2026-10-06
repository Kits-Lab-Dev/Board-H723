#include <stdio.h>
#include "lvgl_demo.h"
#include "lvgl_port.h"
#include "app.h"
#include "FreeRTOS.h"
#include "task.h"
#include "dts.h"
#include "sddisk.h"
#include "sdram.h"
#include "xNetworkInterface.h"

#define TASK_STACK_SIZE 2048 // слов; рисование LVGL и разбор TTF
#define CHART_POINTS 120     // 2 минуты по точке в секунду

static StackType_t taskStack[TASK_STACK_SIZE];
static StaticTask_t taskTcb;

static const lv_font_t *fontText, *fontTitle, *fontValue;
static lv_obj_t *tempLabel, *memLabel, *netLabel, *uptimeLabel, *touchLabel;

// Маленький график: подпись слева, текущее значение справа, линия за 2 минуты
typedef struct
{
    lv_obj_t *value;
    lv_obj_t *chart;
    lv_chart_series_t *series;
} MiniChart;

static MiniChart tempChart, cpuChart, ramChart, sdramChart;

// Карточка: подпись мелко, под ней значение; возвращает метку значения
static lv_obj_t *card(lv_obj_t *parent, int y, int h, const char *title, const lv_font_t *valueFont)
{
    lv_obj_t *c = lv_obj_create(parent);
    lv_obj_set_pos(c, 20, y);
    lv_obj_set_size(c, 310, h);
    lv_obj_set_scrollable(c, false);

    lv_obj_t *t = lv_label_create(c);
    lv_label_set_text(t, title);
    lv_obj_set_style_text_font(t, fontText, 0);
    lv_obj_set_style_text_color(t, lv_palette_main(LV_PALETTE_GREY), 0);

    lv_obj_t *v = lv_label_create(c);
    lv_obj_set_style_text_font(v, valueFont, 0);
    lv_obj_align(v, LV_ALIGN_TOP_LEFT, 0, 30);
    return v;
}

static void miniChart(MiniChart *m, lv_obj_t *parent, int x, int y, const char *title, lv_palette_t color,
                      int32_t min, int32_t max)
{
    lv_obj_t *box = lv_obj_create(parent);
    lv_obj_set_pos(box, x, y);
    lv_obj_set_size(box, 210, 150);
    lv_obj_set_scrollable(box, false);
    lv_obj_set_style_pad_all(box, 10, 0);

    lv_obj_t *t = lv_label_create(box);
    lv_label_set_text(t, title);
    lv_obj_set_style_text_font(t, fontText, 0);
    lv_obj_set_style_text_color(t, lv_palette_main(LV_PALETTE_GREY), 0);

    m->value = lv_label_create(box);
    lv_obj_set_style_text_font(m->value, fontText, 0);
    lv_obj_align(m->value, LV_ALIGN_TOP_RIGHT, 0, 0);

    m->chart = lv_chart_create(box);
    lv_obj_set_size(m->chart, lv_pct(100), 96);
    lv_obj_align(m->chart, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_set_style_pad_all(m->chart, 2, 0);
    lv_chart_set_type(m->chart, LV_CHART_TYPE_LINE);
    lv_chart_set_point_count(m->chart, CHART_POINTS);
    lv_chart_set_axis_range(m->chart, LV_CHART_AXIS_PRIMARY_Y, min, max);
    lv_chart_set_div_line_count(m->chart, 3, 0);
    lv_obj_set_style_size(m->chart, 0, 0, LV_PART_INDICATOR); // без точек — только линия
    m->series = lv_chart_add_series(m->chart, lv_palette_main(color), LV_CHART_AXIS_PRIMARY_Y);
}

static void miniChartAdd(MiniChart *m, int32_t v, const char *unit)
{
    lv_label_set_text_fmt(m->value, "%ld %s", (long)v, unit);
    lv_chart_set_next_value(m->chart, m->series, v);
}

static void ledChanged(lv_event_t *e)
{
    lv_obj_t *sw = lv_event_get_target(e);
    bool on = lv_obj_has_state(sw, LV_STATE_CHECKED);
    appLedHeartbeat = false; // светодиодом управляет экран, а не мигание из idle-задачи
    HAL_GPIO_WritePin(LED_GPIO_Port, LED_Pin, on ? GPIO_PIN_SET : GPIO_PIN_RESET);
}

static void ipText(char *buf, size_t size, NetworkEndPoint_t *ep)
{
    uint32_t ip = ep ? ep->ipv4_settings.ulIPAddress : 0;
    if (ip == 0)
        snprintf(buf, size, "—");
    else
        FreeRTOS_inet_ntoa(ip, buf);
}

// Раз в секунду: показания платы и точки на графиках
static void update(lv_timer_t *t)
{
    LV_UNUSED(t);
    int32_t temp = 0;
    HAL_DTS_GetTemperature(&hdts, &temp);
    lv_label_set_text_fmt(tempLabel, "%ld °C", (long)temp);
    miniChartAdd(&tempChart, temp, "°C");

    // Загрузка процессора — как в команде top: доля времени вне idle-задачи (app.c)
    miniChartAdd(&cpuChart, (int32_t)(100.0f - CPU_IDLE + 0.5f), "%");

    size_t heapFree = xPortGetFreeHeapSize();
    uint32_t sdUsed = sdramGetUseMem(), sdFree = sdramGetFreeMem();
    miniChartAdd(&ramChart, (int32_t)((configTOTAL_HEAP_SIZE - heapFree) * 100 / configTOTAL_HEAP_SIZE), "%");
    miniChartAdd(&sdramChart, (int32_t)((uint64_t)sdUsed * 100 / (sdUsed + sdFree)), "%");
    lv_label_set_text_fmt(memLabel, "ОЗУ (куча FreeRTOS): %u КБ\nSDRAM: %lu МБ", (unsigned)(heapFree / 1024),
                          sdFree / (1024 * 1024));

    char usb[16], wifi[16], ap[16];
    ipText(usb, sizeof(usb), &ncmNetEndPoint);
    ipText(wifi, sizeof(wifi), &staNetEndPoint);
    ipText(ap, sizeof(ap), &apNetEndPoint);
    lv_label_set_text_fmt(netLabel, "USB: %s\nWi-Fi: %s\nТочка доступа: %s", usb, wifi, ap);

    uint32_t s = xTaskGetTickCount() / configTICK_RATE_HZ;
    lv_label_set_text_fmt(uptimeLabel, "работает %lu:%02lu:%02lu", s / 3600, s / 60 % 60, s % 60);
}

// Координаты касания — проверка тача
static void touchShow(lv_timer_t *t)
{
    LV_UNUSED(t);
    lv_indev_t *indev = lv_indev_get_next(NULL);
    if (!indev)
        return;
    lv_point_t p;
    lv_indev_get_point(indev, &p);
    if (lv_indev_get_state(indev) == LV_INDEV_STATE_PRESSED)
        lv_label_set_text_fmt(touchLabel, "Касание: %ld, %ld", (long)p.x, (long)p.y);
}

static void buildScreen(void)
{
    lv_obj_t *scr = lv_screen_active();
    lv_obj_set_scrollable(scr, false);

    lv_obj_t *title = lv_label_create(scr);
    lv_label_set_text(title, "Доска-H723");
    lv_obj_set_style_text_font(title, fontTitle, 0);
    lv_obj_set_pos(title, 20, 14);

    uptimeLabel = lv_label_create(scr);
    lv_obj_set_style_text_font(uptimeLabel, fontText, 0);
    lv_obj_align(uptimeLabel, LV_ALIGN_TOP_RIGHT, -20, 24);

    tempLabel = card(scr, 70, 100, "Температура кристалла", fontValue);
    memLabel = card(scr, 180, 110, "Свободно памяти", fontText);
    netLabel = card(scr, 300, 140, "Сеть", fontText);

    miniChart(&tempChart, scr, 350, 70, "Температура", LV_PALETTE_RED, 30, 90);
    miniChart(&cpuChart, scr, 570, 70, "Процессор", LV_PALETTE_BLUE, 0, 100);
    miniChart(&ramChart, scr, 350, 230, "ОЗУ", LV_PALETTE_GREEN, 0, 100);
    miniChart(&sdramChart, scr, 570, 230, "SDRAM", LV_PALETTE_ORANGE, 0, 100);

    lv_obj_t *sw = lv_switch_create(scr);
    lv_obj_set_pos(sw, 350, 400);
    lv_obj_set_size(sw, 80, 40);
    lv_obj_add_event_cb(sw, ledChanged, LV_EVENT_VALUE_CHANGED, NULL);
    lv_obj_t *swl = lv_label_create(scr);
    lv_label_set_text(swl, "Светодиод D2");
    lv_obj_set_style_text_font(swl, fontText, 0);
    lv_obj_align_to(swl, sw, LV_ALIGN_OUT_RIGHT_MID, 16, 0);

    touchLabel = lv_label_create(scr);
    lv_label_set_text(touchLabel, "Коснитесь экрана");
    lv_obj_set_style_text_font(touchLabel, fontText, 0);
    lv_obj_set_pos(touchLabel, 600, 410);

    update(NULL);
    lv_timer_create(update, 1000, NULL);
    lv_timer_create(touchShow, 50, NULL);
}

static void lvglTask(void *arg)
{
    (void)arg;
    if (!lvglPortInit())
        vTaskDelete(NULL);

    // Шрифты — с карты; без неё встроенные (без кириллицы)
    for (int i = 0; i < 10 && !sdDiskIsMounted(); i++)
        vTaskDelay(pdMS_TO_TICKS(500));
    TickType_t t0 = xTaskGetTickCount();
    fontText = lvglLoadFont(LVGL_DEMO_FONT, 20);
    fontTitle = lvglLoadFont(LVGL_DEMO_FONT_BOLD, 34);
    fontValue = lvglLoadFont(LVGL_DEMO_FONT_BOLD, 40);
    if (fontText && fontTitle && fontValue)
    {
        INF("lvgl_demo: fonts loaded in %lu ms\n", xTaskGetTickCount() - t0);
    }
    else
    {
        WRN("lvgl_demo: no fonts on SD card, built-in Latin only\n");
        fontText = &lv_font_montserrat_14;
        fontTitle = fontValue = &lv_font_montserrat_14;
    }

    lv_display_t *disp = lv_display_get_default();
    lv_display_set_theme(disp, lv_theme_default_init(disp, lv_palette_main(LV_PALETTE_BLUE),
                                                     lv_palette_main(LV_PALETTE_RED), true, fontText));
    buildScreen();

    // Полная перерисовка экрана — по счётчику тактов ядра
    CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
    DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;
    uint32_t c0 = DWT->CYCCNT;
    lv_obj_invalidate(lv_screen_active());
    lv_refr_now(NULL);
    INF("lvgl_demo: screen ready, full redraw %lu us\n", (DWT->CYCCNT - c0) / (SystemCoreClock / 1000000));

    for (;;)
    {
        uint32_t next = lv_timer_handler();
        vTaskDelay(pdMS_TO_TICKS(next < 1 ? 1 : next > 10 ? 10 : next));
    }
}

void lvgl_demo_start(void)
{
    xTaskCreateStatic(lvglTask, "lvgl", TASK_STACK_SIZE, NULL, tskIDLE_PRIORITY + 1, taskStack, &taskTcb);
    INF("lvgl_demo: started\n");
}
