#ifndef LVGL_DEMO_H
#define LVGL_DEMO_H

// Шрифты с SD-карты (PT Sans с кириллицей, архив sd-card.zip)
#define LVGL_DEMO_FONT "/SD/fonts/PT_Sans-Web-Regular.ttf"
#define LVGL_DEMO_FONT_BOLD "/SD/fonts/PT_Sans-Web-Bold.ttf"

// Задача LVGL: экран с показаниями платы, графиком температуры и переключателем светодиода
void lvgl_demo_start(void);

#endif
