#ifndef LVGL_PORT_H
#define LVGL_PORT_H

#include "lvgl.h"

// LVGL на Доске-H723: дисплей LTDC (два кадровых буфера в SDRAM), тач GT911,
// память LVGL в SDRAM, файлы с SD-карты по букве S: ("S:fonts/x.ttf" → /SD/fonts/x.ttf).
// Вызывать из задачи, которая дальше крутит lv_timer_handler(): LVGL однопоточная.
bool lvglPortInit(void);

// Шрифт TTF с карты нужного размера. Файл читается в SDRAM один раз, следующие размеры
// берут те же данные. NULL — файла нет (тогда подойдёт встроенный lv_font_montserrat_*).
lv_font_t *lvglLoadFont(const char *path, int size);

#endif
