/* Настройки LVGL 9 для Доски-H723 (рецепт lvgl_demo).
 * Здесь только отличия от значений по умолчанию — остальное в
 * third_party/lvgl/include/lvgl/config/lv_conf_internal.h. */
#ifndef LV_CONF_H
#define LV_CONF_H

/* Дисплей RGB565, как кадровые буферы LTDC */
#define LV_COLOR_DEPTH 16

/* Память LVGL — в SDRAM: lv_malloc_core и др. в recipes/lvgl_demo/lvgl_port.c */
#define LV_USE_STDLIB_MALLOC LV_STDLIB_CUSTOM

/* Один поток: всё, что трогает интерфейс, — из задачи LVGL */
#define LV_USE_OS LV_OS_NONE

/* Период перерисовки и опроса тача, мс (кадр LTDC — 23 мс) */
#define LV_DEF_REFR_PERIOD 20

/* Форматы пикселей программного рисования: экран RGB565, картинки ARGB8888,
 * буквы A8. Остальные выключены — каждый формат стоит десятки КБ Flash. */
#define LV_DRAW_SW_SUPPORT_RGB565_SWAPPED 0
#define LV_DRAW_SW_SUPPORT_RGB565A8 0
#define LV_DRAW_SW_SUPPORT_RGB888 0
#define LV_DRAW_SW_SUPPORT_XRGB8888 0
#define LV_DRAW_SW_SUPPORT_ARGB8888_PREMULTIPLIED 0
#define LV_DRAW_SW_SUPPORT_L8 0
#define LV_DRAW_SW_SUPPORT_AL88 0
#define LV_DRAW_SW_SUPPORT_I1 0

/* Буферы рисования выровнены по строке кэша Cortex-M7 */
#define LV_DRAW_BUF_ALIGN 32

/* Заливки и копии картинок — через DMA2D (сам выгружает и инвалидирует кэш) */
#define LV_USE_DRAW_DMA2D 1
#define LV_DRAW_DMA2D_HAL_INCLUDE "stm32h7xx_hal.h"

/* Лог LVGL — в лог платы (lvgl_port.c) */
#define LV_USE_LOG 1
#define LV_LOG_LEVEL LV_LOG_LEVEL_WARN

/* Файлы с SD-карты: "S:fonts/x.ttf" → /SD/fonts/x.ttf */
#define LV_USE_FS_STDIO 1
#define LV_FS_STDIO_LETTER 'S'
#define LV_FS_STDIO_PATH "/SD/"

/* Шрифты TTF с кириллицей: файл с карты загружается в SDRAM, буквы любого размера */
#define LV_USE_TINY_TTF 1
#define LV_TINY_TTF_CACHE_GLYPH_CNT 256

/* Встроенный шрифт — запасной, если на карте нет TTF (только латиница) */
#define LV_FONT_MONTSERRAT_14 1

#endif /* LV_CONF_H */
