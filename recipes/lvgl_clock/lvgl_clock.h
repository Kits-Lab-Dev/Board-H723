#ifndef LVGL_CLOCK_H
#define LVGL_CLOCK_H

// Настройки на SD-карте: сеть Wi-Fi, город или координаты, часовой пояс (образец — в sd-card.zip)
#define LVGL_CLOCK_SETTINGS "/SD/clock.ini"

// Шрифты с SD-карты — те же, что у lvgl_demo
#define LVGL_CLOCK_FONT "/SD/fonts/PT_Sans-Web-Regular.ttf"
#define LVGL_CLOCK_FONT_BOLD "/SD/fonts/PT_Sans-Web-Bold.ttf"

// Погода — open-meteo.com (бесплатно, без ключа, по HTTP)
#define LVGL_CLOCK_WEATHER_PERIOD_S (15 * 60) // данные у open-meteo обновляются раз в 15 минут
#define LVGL_CLOCK_RETRY_S 30                 // повтор после ошибки сети

// Часы, дата и погода на дисплее 800×480; на фоне всплывают пузыри, касание добавляет новые
void lvgl_clock_start(void);

#endif
