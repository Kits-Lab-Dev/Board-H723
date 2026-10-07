#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "lvgl_clock.h"
#include "lvgl_port.h"
#include "wifi_http.h"
#include "app.h"
#include "FreeRTOS.h"
#include "task.h"
#include "ff.h"
#include "sddisk.h"
#include "ntpclient.h"
#include "xNetworkInterface.h"

#define UI_STACK_SIZE 2048  // слов; рисование LVGL и разбор TTF
#define NET_STACK_SIZE 1024 // слов; ответ сервера — в статическом буфере
#define MAX_BUBBLES 10

static StackType_t uiStack[UI_STACK_SIZE], netStack[NET_STACK_SIZE];
static StaticTask_t uiTcb, netTcb;

// ---------- Данные от сетевой задачи к задаче LVGL ----------

typedef struct
{
    char place[64];  // город
    int temp, feels; // °C, округлено
    int tmin, tmax;  // на сегодня
    int humidity;    // %
    int wind10;      // м/с × 10
    int code;        // код погоды WMO
    bool isDay;
    char time[6];    // время наблюдения "08:30" по местному времени
} Weather;

// Сетевая задача пишет, задача LVGL читает раз в секунду. Копирование — в критической
// секции: структура небольшая, мьютекс не нужен.
static struct
{
    uint32_t seq;      // меняется при каждой записи — LVGL перерисовывает только новое
    bool valid;        // погода получена хотя бы раз
    Weather weather;
    int32_t utcOffset; // часовой пояс места, секунды от UTC
    char status[160]; // строка состояния внизу экрана
} shared = {.utcOffset = TIME_ZONE * 3600};

static void setStatus(const char *text)
{
    taskENTER_CRITICAL();
    snprintf(shared.status, sizeof(shared.status), "%s", text);
    shared.seq++;
    taskEXIT_CRITICAL();
    INF("lvgl_clock: %s\n", text);
}

// ---------- Настройки с карты ----------

typedef struct
{
    char ssid[33], password[65];
    char city[64];
    float lat, lon;
    bool hasCoords;
    int32_t utcOffset; // секунды; hasOffset = false — пояс по координатам от open-meteo
    bool hasOffset;
} Settings;

static Settings cfg;

static char *trim(char *s)
{
    while (*s == ' ' || *s == '\t')
        s++;
    char *e = s + strlen(s);
    while (e > s && (e[-1] == ' ' || e[-1] == '\t' || e[-1] == '\r' || e[-1] == '\n'))
        *--e = '\0';
    return s;
}

// Строки "ключ = значение"; ";" и "#" в начале строки — комментарий
static bool loadSettings(Settings *s)
{
    static FIL f; // FIL с буфером сектора — больше 0,5 КБ, не для стека
    if (f_open(&f, LVGL_CLOCK_SETTINGS, FA_READ) != FR_OK)
        return false;
    memset(s, 0, sizeof(*s));
    char line[128];
    bool hasLat = false, hasLon = false;
    while (f_gets(line, sizeof(line), &f))
    {
        char *eq = strchr(line, '=');
        char *k = trim(line);
        if (strncmp(k, "\xEF\xBB\xBF", 3) == 0) // метка UTF-8, её ставит Блокнот Windows
            k = trim(k + 3);
        if (*k == ';' || *k == '#' || eq == NULL)
            continue;
        *eq = '\0';
        k = trim(k);
        char *v = trim(eq + 1);
        if (*v == '\0')
            continue;
        if (strcmp(k, "ssid") == 0)
            strncpy(s->ssid, v, sizeof(s->ssid) - 1);
        else if (strcmp(k, "password") == 0)
            strncpy(s->password, v, sizeof(s->password) - 1);
        else if (strcmp(k, "city") == 0)
            strncpy(s->city, v, sizeof(s->city) - 1);
        else if (strcmp(k, "latitude") == 0)
            s->lat = strtof(v, NULL), hasLat = true;
        else if (strcmp(k, "longitude") == 0)
            s->lon = strtof(v, NULL), hasLon = true;
        else if (strcmp(k, "utc_offset") == 0)
            s->utcOffset = (int32_t)(strtof(v, NULL) * 3600), s->hasOffset = true; // часы, бывает 5.5
        else
            WRN("lvgl_clock: unknown key '%s' in %s\n", k, LVGL_CLOCK_SETTINGS);
    }
    f_close(&f);
    s->hasCoords = hasLat && hasLon;
    return true;
}

// ---------- Ответы open-meteo (JSON) ----------

// Ответы короткие и одинаковые по устройству, поэтому без библиотеки JSON: ищется
// "ключ": после начала нужного объекта (section, например "\"current\":{").
static const char *jsonFind(const char *json, const char *section, const char *key)
{
    const char *p = json;
    if (section && (p = strstr(json, section)) == NULL)
        return NULL;
    char pat[40];
    snprintf(pat, sizeof(pat), "\"%s\":", key);
    p = strstr(p, pat);
    return p ? p + strlen(pat) : NULL;
}

static bool jsonNumber(const char *json, const char *section, const char *key, float *v)
{
    const char *p = jsonFind(json, section, key);
    if (p == NULL)
        return false;
    if (*p == '[') // в "daily" значения — массивы по дням, берём первый день
        p++;
    char *end;
    *v = strtof(p, &end);
    return end != p;
}

// Строка без экранирования: open-meteo отдаёт названия в UTF-8 как есть
static bool jsonString(const char *json, const char *section, const char *key, char *out, size_t size)
{
    const char *p = jsonFind(json, section, key);
    if (p == NULL || *p != '"')
        return false;
    p++;
    size_t n = 0;
    while (p[n] && p[n] != '"' && n < size - 1)
        n++;
    memcpy(out, p, n);
    out[n] = '\0';
    return true;
}

static int roundf2i(float v)
{
    return (int)(v < 0 ? v - 0.5f : v + 0.5f);
}

// Координата "55.7520" без printf с плавающей точкой (в newlib-nano его нет)
static void fmtCoord(char *buf, size_t size, float v)
{
    long x = (long)(v < 0 ? v * 10000 - 0.5f : v * 10000 + 0.5f);
    snprintf(buf, size, "%s%ld.%04ld", x < 0 ? "-" : "", labs(x) / 10000, labs(x) % 10000);
}

// Город в адресе запроса: UTF-8 побайтно в %XX
static void urlEncode(char *out, size_t size, const char *s)
{
    size_t n = 0;
    for (; *s && n + 4 < size; s++)
    {
        unsigned char c = (unsigned char)*s;
        if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || strchr("-_.~", c))
            out[n++] = c;
        else
            n += snprintf(out + n, size - n, "%%%02X", c);
    }
    out[n] = '\0';
}

static char http[4096]; // ответ сервера целиком; прогноз — около 1,2 КБ

// Город → координаты: geocoding-api.open-meteo.com
static bool geocode(Settings *s, char *place, size_t placeSize)
{
    char name[3 * sizeof(s->city)], path[256];
    urlEncode(name, sizeof(name), s->city);
    snprintf(path, sizeof(path), "/v1/search?name=%s&count=1&language=ru&format=json", name);
    wifiHttpResult r;
    if (wifiHttpGet("geocoding-api.open-meteo.com", path, http, sizeof(http), &r) != 0 || r.status != 200 ||
        r.body == NULL)
    {
        setStatus("Погода: нет ответа от geocoding-api.open-meteo.com");
        return false;
    }
    float lat, lon;
    if (!jsonNumber(r.body, "\"results\":", "latitude", &lat) || !jsonNumber(r.body, "\"results\":", "longitude", &lon))
    {
        char msg[160];
        snprintf(msg, sizeof(msg), "Город «%s» не найден — проверьте %s", s->city, LVGL_CLOCK_SETTINGS);
        setStatus(msg);
        return false;
    }
    s->lat = lat;
    s->lon = lon;
    s->hasCoords = true;
    jsonString(r.body, "\"results\":", "name", place, placeSize);
    char la[16], lo[16];
    fmtCoord(la, sizeof(la), lat);
    fmtCoord(lo, sizeof(lo), lon);
    INF("lvgl_clock: %s -> %s, %s (%lu ms)\n", place, la, lo, r.ms);
    return true;
}

// Тот же запрос /v1/forecast понимают два сервера open-meteo. У некоторых провайдеров
// адрес api.open-meteo.com недоступен — тогда работает второй; удачный запоминается.
static const char *const weatherHosts[] = {"api.open-meteo.com", "historical-forecast-api.open-meteo.com"};
static int weatherHost;

// Текущая погода и прогноз на сегодня
static bool fetchWeather(const Settings *s, Weather *w, int32_t *utcOffset)
{
    char la[16], lo[16], path[320];
    fmtCoord(la, sizeof(la), s->lat);
    fmtCoord(lo, sizeof(lo), s->lon);
    snprintf(path, sizeof(path),
             "/v1/forecast?latitude=%s&longitude=%s"
             "&current=temperature_2m,relative_humidity_2m,apparent_temperature,weather_code,wind_speed_10m,is_day"
             "&daily=temperature_2m_max,temperature_2m_min&forecast_days=1&wind_speed_unit=ms&timezone=auto",
             la, lo);
    wifiHttpResult r;
    bool ok = false;
    for (int i = 0; i < 2 && !ok; i++)
    {
        ok = wifiHttpGet(weatherHosts[weatherHost], path, http, sizeof(http), &r) == 0 && r.status == 200 && r.body;
        if (!ok)
            weatherHost ^= 1;
    }
    if (!ok)
    {
        setStatus("Погода: нет ответа от open-meteo.com");
        return false;
    }
    const char *cur = "\"current\":{", *day = "\"daily\":{";
    float temp, feels, hum, code, wind, isDay, tmin, tmax, offset;
    if (!jsonNumber(r.body, cur, "temperature_2m", &temp) || !jsonNumber(r.body, cur, "weather_code", &code) ||
        !jsonNumber(r.body, NULL, "utc_offset_seconds", &offset))
    {
        setStatus("Погода: непонятный ответ api.open-meteo.com");
        return false;
    }
    w->temp = roundf2i(temp);
    w->code = (int)code;
    w->feels = jsonNumber(r.body, cur, "apparent_temperature", &feels) ? roundf2i(feels) : w->temp;
    w->humidity = jsonNumber(r.body, cur, "relative_humidity_2m", &hum) ? roundf2i(hum) : -1;
    w->wind10 = jsonNumber(r.body, cur, "wind_speed_10m", &wind) ? roundf2i(wind * 10) : -1;
    w->isDay = jsonNumber(r.body, cur, "is_day", &isDay) ? isDay != 0 : true;
    w->tmin = jsonNumber(r.body, day, "temperature_2m_min", &tmin) ? roundf2i(tmin) : w->temp;
    w->tmax = jsonNumber(r.body, day, "temperature_2m_max", &tmax) ? roundf2i(tmax) : w->temp;
    char t[20] = "";
    jsonString(r.body, cur, "time", t, sizeof(t)); // "2026-10-07T08:30"
    snprintf(w->time, sizeof(w->time), "%.5s", strlen(t) >= 16 ? t + 11 : "");
    *utcOffset = (int32_t)offset;
    INF("lvgl_clock: weather %d C (feels %d), code %d, humidity %d %%, wind %d.%d m/s, UTC%+ld s, %s %lu ms\n",
        w->temp, w->feels, w->code, w->humidity, w->wind10 / 10, w->wind10 % 10, (long)*utcOffset,
        weatherHosts[weatherHost], r.ms);
    return true;
}

static void netTask(void *arg)
{
    (void)arg;
    for (int i = 0; i < 10 && !sdDiskIsMounted(); i++)
        vTaskDelay(pdMS_TO_TICKS(500));
    if (!loadSettings(&cfg))
    {
        setStatus("Нет файла " LVGL_CLOCK_SETTINGS " — образец в sd-card.zip");
        vTaskDelete(NULL);
    }
    if (cfg.hasOffset)
    {
        taskENTER_CRITICAL();
        shared.utcOffset = cfg.utcOffset;
        taskEXIT_CRITICAL();
    }
    if (!cfg.hasCoords && cfg.city[0] == '\0')
        setStatus("Погода: в " LVGL_CLOCK_SETTINGS " не задан город (city) или координаты");

    char msg[160];
    if (cfg.ssid[0])
    {
        snprintf(msg, sizeof(msg), "Wi-Fi: подключение к %s…", cfg.ssid);
        setStatus(msg);
        wifiHttpJoin(cfg.ssid, cfg.password);
    }
    else
    {
        setStatus("Wi-Fi: ssid в " LVGL_CLOCK_SETTINGS " пуст, ждём подключения");
    }

    Weather w = {0};
    snprintf(w.place, sizeof(w.place), "%s", cfg.city);
    bool online = false;
    for (;;)
    {
        if (!wifiHttpOnline())
        {
            online = false;
            vTaskDelay(pdMS_TO_TICKS(1000));
            continue;
        }
        if (!online)
        {
            online = true;
            char ip[16];
            FreeRTOS_inet_ntoa(staNetEndPoint.ipv4_settings.ulIPAddress, ip);
            snprintf(msg, sizeof(msg), "Wi-Fi: %s, адрес %s", cfg.ssid[0] ? cfg.ssid : "подключено", ip);
            setStatus(msg);
        }
        if (!cfg.hasCoords && (cfg.city[0] == '\0' || !geocode(&cfg, w.place, sizeof(w.place))))
        {
            vTaskDelay(pdMS_TO_TICKS(LVGL_CLOCK_RETRY_S * 1000));
            continue;
        }
        int32_t offset;
        if (!fetchWeather(&cfg, &w, &offset))
        {
            vTaskDelay(pdMS_TO_TICKS(LVGL_CLOCK_RETRY_S * 1000));
            continue;
        }
        taskENTER_CRITICAL();
        shared.weather = w;
        shared.valid = true;
        if (!cfg.hasOffset)
            shared.utcOffset = offset;
        shared.seq++;
        taskEXIT_CRITICAL();
        vTaskDelay(pdMS_TO_TICKS(LVGL_CLOCK_WEATHER_PERIOD_S * 1000));
    }
}

// ---------- Экран ----------

static const lv_font_t *fontSmall, *fontText, *fontTemp, *fontTime, *fontSec;
static lv_obj_t *bubbleLayer, *timeLabel, *secLabel, *dateLabel, *statusLabel;
static lv_obj_t *placeLabel, *tempLabel, *descLabel, *detailLabel, *timeOfLabel, *icon;

// Иконка погоды из кругов и прямоугольников LVGL
typedef enum
{
    SKY_CLEAR,
    SKY_PARTLY,
    SKY_CLOUDY,
    SKY_FOG,
    SKY_RAIN,
    SKY_SNOW,
    SKY_STORM,
} Sky;

// Коды погоды WMO, которые отдаёт open-meteo
static const char *weatherText(int code, Sky *sky)
{
    static const struct
    {
        uint8_t code;
        Sky sky;
        const char *text;
    } table[] = {
        {0, SKY_CLEAR, "Ясно"},
        {1, SKY_PARTLY, "Преимущественно ясно"},
        {2, SKY_PARTLY, "Переменная облачность"},
        {3, SKY_CLOUDY, "Пасмурно"},
        {45, SKY_FOG, "Туман"},
        {48, SKY_FOG, "Туман с изморозью"},
        {51, SKY_RAIN, "Слабая морось"},
        {53, SKY_RAIN, "Морось"},
        {55, SKY_RAIN, "Сильная морось"},
        {56, SKY_RAIN, "Ледяная морось"},
        {57, SKY_RAIN, "Ледяная морось"},
        {61, SKY_RAIN, "Небольшой дождь"},
        {63, SKY_RAIN, "Дождь"},
        {65, SKY_RAIN, "Сильный дождь"},
        {66, SKY_RAIN, "Ледяной дождь"},
        {67, SKY_RAIN, "Ледяной дождь"},
        {71, SKY_SNOW, "Небольшой снег"},
        {73, SKY_SNOW, "Снег"},
        {75, SKY_SNOW, "Сильный снег"},
        {77, SKY_SNOW, "Снежные зёрна"},
        {80, SKY_RAIN, "Небольшой ливень"},
        {81, SKY_RAIN, "Ливень"},
        {82, SKY_RAIN, "Сильный ливень"},
        {85, SKY_SNOW, "Снегопад"},
        {86, SKY_SNOW, "Сильный снегопад"},
        {95, SKY_STORM, "Гроза"},
        {96, SKY_STORM, "Гроза с градом"},
        {99, SKY_STORM, "Гроза с градом"},
    };
    for (size_t i = 0; i < sizeof(table) / sizeof(table[0]); i++)
        if (table[i].code == code)
        {
            *sky = table[i].sky;
            return table[i].text;
        }
    *sky = SKY_CLOUDY;
    return "—";
}

static lv_obj_t *shape(lv_obj_t *parent, int x, int y, int w, int h, int radius, uint32_t color)
{
    lv_obj_t *o = lv_obj_create(parent);
    lv_obj_remove_style_all(o);
    lv_obj_set_clickable(o, false);
    lv_obj_set_pos(o, x, y);
    lv_obj_set_size(o, w, h);
    lv_obj_set_style_radius(o, radius, 0);
    lv_obj_set_style_bg_color(o, lv_color_hex(color), 0);
    lv_obj_set_style_bg_opa(o, LV_OPA_COVER, 0);
    return o;
}

static void cloud(lv_obj_t *parent, int x, int y, uint32_t color)
{
    shape(parent, x + 10, y + 14, 40, 40, LV_RADIUS_CIRCLE, color);
    shape(parent, x + 34, y, 50, 50, LV_RADIUS_CIRCLE, color);
    shape(parent, x, y + 30, 96, 30, 15, color);
}

static void drawIcon(Sky sky, bool isDay)
{
    lv_obj_clean(icon);
    uint32_t sun = isDay ? 0xFFC53D : 0xE6E1C5; // ночью вместо солнца — луна
    switch (sky)
    {
    case SKY_CLEAR:
        shape(icon, 22, 12, 72, 72, LV_RADIUS_CIRCLE, sun);
        break;
    case SKY_PARTLY:
        shape(icon, 40, 4, 60, 60, LV_RADIUS_CIRCLE, sun);
        cloud(icon, 4, 30, 0xF2F4F8);
        break;
    case SKY_CLOUDY:
        cloud(icon, 10, 18, 0xC5CBD6);
        break;
    case SKY_FOG:
        for (int i = 0; i < 4; i++)
            shape(icon, 10 + (i % 2) * 12, 20 + i * 16, 86, 8, 4, 0xB8BFCC);
        break;
    case SKY_RAIN:
    case SKY_STORM:
        cloud(icon, 10, 0, 0xC5CBD6);
        for (int i = 0; i < 4; i++)
            shape(icon, 22 + i * 20, 68, 5, 18, 3, 0x5AA9FF);
        if (sky == SKY_STORM)
            shape(icon, 52, 62, 10, 30, 2, 0xFFD43B);
        break;
    case SKY_SNOW:
        cloud(icon, 10, 0, 0xE3E8EF);
        for (int i = 0; i < 4; i++)
            shape(icon, 22 + i * 20, 70 + (i % 2) * 10, 10, 10, LV_RADIUS_CIRCLE, 0xFFFFFF);
        break;
    }
}

// Температура со знаком: «+9°», «−3°» (настоящий минус U+2212)
static void fmtTemp(char *buf, size_t size, int t)
{
    snprintf(buf, size, "%s%d°", t > 0 ? "+" : t < 0 ? "−" : "", abs(t));
}

static void showWeather(const Weather *w)
{
    Sky sky;
    const char *text = weatherText(w->code, &sky);
    drawIcon(sky, w->isDay);
    char t[16], feels[16], lo[16], hi[16];
    fmtTemp(t, sizeof(t), w->temp);
    fmtTemp(feels, sizeof(feels), w->feels);
    fmtTemp(lo, sizeof(lo), w->tmin);
    fmtTemp(hi, sizeof(hi), w->tmax);
    lv_label_set_text(placeLabel, w->place);
    lv_label_set_text(tempLabel, t);
    lv_label_set_text(descLabel, text);
    lv_label_set_text_fmt(detailLabel,
                          "Ощущается как %s\nСегодня от %s до %s\nВлажность %d %%\nВетер %d,%d м/с", feels, lo,
                          hi, w->humidity, w->wind10 / 10, w->wind10 % 10);
    lv_label_set_text_fmt(timeOfLabel, "Данные на %s", w->time);
}

static const char *const weekdays[] = {"воскресенье", "понедельник", "вторник", "среда",
                                       "четверг",     "пятница",     "суббота"};
static const char *const months[] = {"января", "февраля", "марта",    "апреля",  "мая",    "июня",
                                     "июля",   "августа", "сентября", "октября", "ноября", "декабря"};

// Раз в 250 мс: часы (перерисовка — только когда сменилась секунда) и новые данные из сети
static void update(lv_timer_t *timer)
{
    LV_UNUSED(timer);
    static uint32_t seenSeq = (uint32_t)-1;
    static time_t shown;
    static int shownMin = -1, shownDay = -1;

    Weather w;
    bool valid;
    int32_t offset;
    char status[sizeof(shared.status)];
    taskENTER_CRITICAL();
    uint32_t seq = shared.seq;
    w = shared.weather;
    valid = shared.valid;
    offset = shared.utcOffset;
    memcpy(status, shared.status, sizeof(status));
    taskEXIT_CRITICAL();

    if (seq != seenSeq)
    {
        seenSeq = seq;
        lv_label_set_text(statusLabel, status);
        if (valid)
            showWeather(&w);
        shownMin = -1; // пояс мог смениться — перерисовать часы
    }

    // RTC идёт по поясу TIME_ZONE (его ставит NTP-клиент) — переводим в пояс места
    time_t rtc = get_time(NULL);
    if (rtc == shown && shownMin >= 0)
        return;
    shown = rtc;
    if (rtc < 1700000000) // часы ещё не получили время из сети (год 2023 и раньше)
    {
        lv_label_set_text(timeLabel, "--:--");
        lv_label_set_text(secLabel, "");
        lv_label_set_text(dateLabel, "ждём время из интернета (NTP)");
        return;
    }
    time_t local = rtc - TIME_ZONE * 3600 + offset;
    struct tm tm;
    gmtime_r(&local, &tm);
    lv_label_set_text_fmt(secLabel, "%02d", tm.tm_sec);
    if (tm.tm_min != shownMin)
    {
        shownMin = tm.tm_min;
        lv_label_set_text_fmt(timeLabel, "%02d:%02d", tm.tm_hour, tm.tm_min);
        lv_obj_update_layout(timeLabel);
        lv_obj_align_to(secLabel, timeLabel, LV_ALIGN_OUT_RIGHT_BOTTOM, 8, -22);
    }
    if (tm.tm_mday != shownDay)
    {
        shownDay = tm.tm_mday;
        lv_label_set_text_fmt(dateLabel, "%s, %d %s", weekdays[tm.tm_wday], tm.tm_mday, months[tm.tm_mon]);
    }
}

// ---------- Пузыри на фоне ----------

static void animY(void *obj, int32_t v)
{
    lv_obj_set_y(obj, v);
}

static void animX(void *obj, int32_t v)
{
    lv_obj_set_x(obj, v);
}

// Пузырь со случайным размером, цветом и скоростью всплывает от (x, y) за верх экрана,
// покачиваясь из стороны в сторону, и удаляется в конце анимации
static void bubble(int32_t x, int32_t y)
{
    if (lv_obj_get_child_count(bubbleLayer) >= MAX_BUBBLES)
        return;
    int32_t size = lv_rand(12, 48);
    lv_color_t color = lv_color_hsv_to_rgb(lv_rand(0, 359), 55, 100);
    lv_obj_t *b = lv_obj_create(bubbleLayer);
    lv_obj_remove_style_all(b);
    lv_obj_set_clickable(b, false);
    lv_obj_set_size(b, size, size);
    lv_obj_set_pos(b, x - size / 2, y);
    lv_obj_set_style_radius(b, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(b, color, 0);
    lv_obj_set_style_bg_opa(b, LV_OPA_20, 0);
    lv_obj_set_style_border_color(b, color, 0);
    lv_obj_set_style_border_width(b, 2, 0);
    lv_obj_set_style_border_opa(b, LV_OPA_70, 0);

    // Вверх: маленькие всплывают быстрее
    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, b);
    lv_anim_set_exec_cb(&a, animY);
    lv_anim_set_values(&a, y, -size);
    lv_anim_set_duration(&a, (y + size) * (lv_rand(18, 30) + size / 2));
    lv_anim_set_completed_cb(&a, lv_obj_delete_anim_completed_cb);
    lv_anim_start(&a);

    // Покачивание по горизонтали — туда и обратно, пока пузырь жив
    int32_t sway = lv_rand(8, 30);
    lv_anim_init(&a);
    lv_anim_set_var(&a, b);
    lv_anim_set_exec_cb(&a, animX);
    lv_anim_set_values(&a, x - size / 2 - sway, x - size / 2 + sway);
    lv_anim_set_duration(&a, lv_rand(1200, 2600));
    lv_anim_set_reverse_duration(&a, lv_rand(1200, 2600));
    lv_anim_set_repeat_count(&a, LV_ANIM_REPEAT_INFINITE);
    lv_anim_set_path_cb(&a, lv_anim_path_ease_in_out);
    lv_anim_start(&a);
}

// Случайный момент и место: в среднем пузырь раз в секунду
static void bubbleTimer(lv_timer_t *t)
{
    LV_UNUSED(t);
    if (lv_rand(0, 2) == 0)
        bubble(lv_rand(20, LV_HOR_RES - 20), LV_VER_RES);
}

// Касание — горсть пузырей из этой точки
static void touched(lv_event_t *e)
{
    lv_point_t p;
    lv_indev_get_point(lv_indev_active(), &p);
    LV_UNUSED(e);
    for (int i = 0; i < 4; i++)
        bubble(p.x + (int32_t)lv_rand(0, 60) - 30, p.y + (int32_t)lv_rand(0, 30) - 15);
}

static lv_obj_t *label(lv_obj_t *parent, const lv_font_t *font, uint32_t color, int x, int y)
{
    lv_obj_t *l = lv_label_create(parent);
    lv_obj_set_style_text_font(l, font, 0);
    lv_obj_set_style_text_color(l, lv_color_hex(color), 0);
    lv_obj_set_pos(l, x, y);
    lv_label_set_text(l, "");
    return l;
}

static void buildScreen(void)
{
    lv_obj_t *scr = lv_screen_active();
    lv_obj_set_scrollable(scr, false);
    lv_obj_set_style_bg_color(scr, lv_color_hex(0x10182E), 0);
    lv_obj_set_style_bg_grad_color(scr, lv_color_hex(0x2B1A44), 0);
    lv_obj_set_style_bg_grad_dir(scr, LV_GRAD_DIR_VER, 0);
    lv_obj_add_event_cb(scr, touched, LV_EVENT_PRESSED, NULL);

    // Слой пузырей — первым, чтобы всё остальное рисовалось поверх
    bubbleLayer = lv_obj_create(scr);
    lv_obj_remove_style_all(bubbleLayer);
    lv_obj_set_size(bubbleLayer, LV_PCT(100), LV_PCT(100));
    lv_obj_set_clickable(bubbleLayer, false);
    lv_obj_set_scrollable(bubbleLayer, false);

    dateLabel = label(scr, fontText, 0xC9D1E3, 40, 50);
    timeLabel = label(scr, fontTime, 0xFFFFFF, 34, 110);
    secLabel = label(scr, fontSec, 0x8FB8FF, 0, 0);
    statusLabel = label(scr, fontSmall, 0x8A94A8, 40, 430);
    lv_obj_set_width(statusLabel, 450);
    lv_label_set_long_mode(statusLabel, LV_LABEL_LONG_MODE_DOTS);

    // Карточка погоды справа: полупрозрачная, пузыри видны сквозь неё
    lv_obj_t *card = lv_obj_create(scr);
    lv_obj_remove_style_all(card);
    lv_obj_set_pos(card, 510, 24);
    lv_obj_set_size(card, 266, 432);
    lv_obj_set_style_radius(card, 18, 0);
    lv_obj_set_style_bg_color(card, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_bg_opa(card, LV_OPA_10, 0);
    lv_obj_set_clickable(card, false);
    lv_obj_set_scrollable(card, false);

    placeLabel = label(card, fontText, 0xFFFFFF, 20, 16);
    lv_obj_set_width(placeLabel, 230);
    lv_label_set_long_mode(placeLabel, LV_LABEL_LONG_MODE_DOTS);
    lv_label_set_text(placeLabel, "Погода");
    icon = lv_obj_create(card);
    lv_obj_remove_style_all(icon);
    lv_obj_set_pos(icon, 12, 64);
    lv_obj_set_size(icon, 116, 96);
    lv_obj_set_clickable(icon, false);
    lv_obj_set_scrollable(icon, false);
    tempLabel = label(card, fontTemp, 0xFFFFFF, 0, 0);
    lv_obj_align(tempLabel, LV_ALIGN_TOP_RIGHT, -16, 78); // ширина меняется с числом — по правому краю
    descLabel = label(card, fontText, 0xFFFFFF, 20, 172);
    lv_obj_set_width(descLabel, 230);
    lv_label_set_long_mode(descLabel, LV_LABEL_LONG_MODE_WRAP);
    detailLabel = label(card, fontSmall, 0xC9D1E3, 20, 250);
    lv_obj_set_style_text_line_space(detailLabel, 6, 0);
    lv_label_set_text(detailLabel, "ждём данные…");
    timeOfLabel = label(card, fontSmall, 0x8A94A8, 20, 392);

    update(NULL);
    lv_timer_create(update, 250, NULL);
    lv_timer_create(bubbleTimer, 330, NULL);
}

static void uiTask(void *arg)
{
    (void)arg;
    if (!lvglPortInit())
        vTaskDelete(NULL);

    for (int i = 0; i < 10 && !sdDiskIsMounted(); i++)
        vTaskDelay(pdMS_TO_TICKS(500));
    fontSmall = lvglLoadFont(LVGL_CLOCK_FONT, 22);
    fontText = lvglLoadFont(LVGL_CLOCK_FONT, 30);
    fontTemp = lvglLoadFont(LVGL_CLOCK_FONT_BOLD, 60);
    fontTime = lvglLoadFont(LVGL_CLOCK_FONT_BOLD, 140);
    fontSec = lvglLoadFont(LVGL_CLOCK_FONT_BOLD, 50);
    if (!fontSmall || !fontText || !fontTemp || !fontTime || !fontSec)
    {
        WRN("lvgl_clock: no fonts on SD card, built-in Latin only\n");
        fontSmall = fontText = fontTemp = fontTime = fontSec = &lv_font_montserrat_14;
    }

    // Случайность пузырей: зерно — счётчик тактов ядра, он к этому моменту уже «разбежался»
    CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
    DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;
    lv_rand_set_seed(DWT->CYCCNT ^ xTaskGetTickCount());

    buildScreen();
    // 30 кадров в секунду вместо 50: медленным пузырям хватает, а каждый кадр с пузырями —
    // это перерисовка кусков фона и крупных цифр под ними
    lv_timer_set_period(lv_display_get_refr_timer(lv_display_get_default()), 33);
    INF("lvgl_clock: screen ready\n");

    for (;;)
    {
        uint32_t next = lv_timer_handler();
        vTaskDelay(pdMS_TO_TICKS(next < 1 ? 1 : next > 10 ? 10 : next));
    }
}

void lvgl_clock_start(void)
{
    xTaskCreateStatic(uiTask, "lvglClock", UI_STACK_SIZE, NULL, tskIDLE_PRIORITY + 1, uiStack, &uiTcb);
    xTaskCreateStatic(netTask, "clockNet", NET_STACK_SIZE, NULL, tskIDLE_PRIORITY + 1, netStack, &netTcb);
    INF("lvgl_clock: started\n");
}
