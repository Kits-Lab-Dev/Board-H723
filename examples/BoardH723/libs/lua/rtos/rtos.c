#include "lprefix.h"
#include <limits.h>
#include <stddef.h>
#include <string.h>

#include "lua.h"
#include "lauxlib.h"
#include "lualib.h"
#include "ramdisk.h"
#include "sdram.h"
#include "stdint.h"
#include "app.h"
#include "dts.h"
#include "rtc.h"
#include "FreeRTOS.h"
#include "task.h"
// #include "adc.h"

#define V_REF 1.216f
#define V_BAT_RATIO 4.0f
static int getFreeMem(lua_State *L)
{
    lua_pushinteger(L, sdramGetFreeMem());
    return 1;
}

static int getUseCpu(lua_State *L)
{
    lua_pushnumber(L, 100.0f - CPU_IDLE);
    return 1;
}

static int getUseMem(lua_State *L)
{
    lua_pushinteger(L, sdramGetUseMem());
    return 1;
}

static int getTotalMem(lua_State *L)
{
    lua_pushinteger(L, SDRAM_BANK1_SIZE);
    return 1;
}

static int getTemp(lua_State *L)
{
    int32_t temp;
    HAL_DTS_GetTemperature(&hdts, &temp);
    lua_pushinteger(L, temp);
    return 1;
}

static int getDateTime(lua_State *L)
{
    RTC_DateTypeDef sDate;
    RTC_TimeTypeDef sTime;
    HAL_RTC_GetDate(&hrtc, &sDate, RTC_FORMAT_BIN);
    vTaskDelay(1);
    HAL_RTC_GetTime(&hrtc, &sTime, RTC_FORMAT_BIN);
    char str[32];
    sprintf(str, "%d-%02d-%02dT%02d:%02d:%02d", sDate.Date, sDate.Month, sDate.Year, sTime.Hours, sTime.Minutes, sTime.Seconds);
    lua_pushlstring(L, str, strlen(str));
    return 1;
}

static int setDateTime(lua_State *L)
{
    RTC_DateTypeDef sDate;
    RTC_TimeTypeDef sTime;
    const char *datetime = luaL_checkstring(L, 1);
    if (sscanf(datetime, "%hhu-%hhu-%hhuT%hhu:%hhu:%hhu", &sDate.Date, &sDate.Month, &sDate.Year, &sTime.Hours, &sTime.Minutes, &sTime.Seconds) != 6)
    {
        lua_pushboolean(L, 0);
        return 1;
    }
    sDate.WeekDay = 1;
    sTime.DayLightSaving = RTC_DAYLIGHTSAVING_NONE;
    sTime.StoreOperation = RTC_STOREOPERATION_SET;

    HAL_RTC_SetDate(&hrtc, &sDate, RTC_FORMAT_BIN);
    vTaskDelay(1);
    HAL_RTC_SetTime(&hrtc, &sTime, RTC_FORMAT_BIN);
    lua_pushboolean(L, 1);
    return 1;
}

static int getV(lua_State *L)
{
    // HAL_ADCEx_Calibration_Start(&hadc1, ADC_CALIB_OFFSET, ADC_SINGLE_ENDED);
    // uint16_t adc[3];
    // HAL_ADC_Start_DMA(&hadc1, (uint32_t *)adc, 3);
    // vTaskDelay(2);
    // float ref = V_REF / (float)adc[2];
    // float vbat = (float)adc[0] * ref * V_BAT_RATIO;
    // float v = ref*4096.0f;
    // lua_pushnumber(L, v);
    // lua_pushnumber(L, vbat);
    return 2;
}

// rtos.reboot(), rtos.reboot("dfu") — в системный загрузчик USB DFU,
// rtos.reboot("esploader") — в ESP-Loader для прошивки ESP32-C2
static int reboot(lua_State *L)
{
    const char *mode = luaL_optstring(L, 1, "");
    if (strcmp(mode, "dfu") == 0)
        rebootToDFU();
    if (strcmp(mode, "esploader") == 0)
    {
        appStartEspLoader();
        return 0;
    }
    NVIC_SystemReset();
    return 0;
}

static int getFreeMemRTOS(lua_State *L)
{
    HeapStats_t heap;
    vPortGetHeapStats(&heap);
    lua_pushinteger(L, heap.xAvailableHeapSpaceInBytes);
    return 1;
}

int c_getc(lua_State *L);
int c_read(lua_State *L, char* p, size_t n);

static int read_line(lua_State *L, int chop)
{
    luaL_Buffer b;
    int c;
    luaL_buffinit(L, &b);
    do
    {                                     /* may need to read several chunks to get whole line */
        char *buff = luaL_prepbuffer(&b); /* preallocate buffer space */
        int i = 0;

        while (i < LUAL_BUFFERSIZE && (c = c_getc(L)) != EOF && c != '\n' && c != '\r')
            buff[i++] = c; /* read up to end of line or buffer limit */

        luaL_addsize(&b, i);
    } while (c != EOF && c != '\n' && c != '\r'); /* repeat until end of line */
    if (!chop && (c == '\n' || c == '\r'))  /* want a newline and have one? */
        luaL_addchar(&b, c); /* add ending newline to result */
    luaL_pushresult(&b);     /* close buffer */
    /* return ok if read something (either a newline or something else) */
    return (c == '\n' || c == '\r' || lua_rawlen(L, -1) > 0);
}

static void read_all(lua_State *L)
{
    size_t nr;
    luaL_Buffer b;
    luaL_buffinit(L, &b);
    do
    { /* read file in chunks of LUAL_BUFFERSIZE bytes */
        char *p = luaL_prepbuffer(&b);
        nr = c_read(L, p, LUAL_BUFFERSIZE);
        luaL_addsize(&b, nr);
    } while (nr == LUAL_BUFFERSIZE);
    luaL_pushresult(&b); /* close buffer */
}

static int read_chars(lua_State *L, size_t n)
{
    size_t nr; /* number of chars actually read */
    char *p;
    luaL_Buffer b;
    luaL_buffinit(L, &b);
    p = luaL_prepbuffsize(&b, n); /* prepare buffer to read whole block */
    nr = c_read(L, p, n);      /* try to read 'n' chars */
    luaL_addsize(&b, nr);
    luaL_pushresult(&b); /* close buffer */
    return (nr > 0);     /* true iff read something */
}

#if !defined (L_MAXLENNUM)
#define L_MAXLENNUM     200
#endif

static int g_read(lua_State *L, int first)
{
    int nargs = lua_gettop(L);
    int n, success;

    if (nargs == 0)
    { /* no arguments? */
        success = read_line(L, 1);
        n = first + 1; /* to return 1 result */
    }
    else
    {
        /* ensure stack space for all results and for auxlib's buffer */
        luaL_checkstack(L, nargs + LUA_MINSTACK, "too many arguments");
        success = 1;
        for (n = first; nargs-- && success; n++)
        {
            if (lua_type(L, n) == LUA_TNUMBER)
            {
                size_t l = (size_t)luaL_checkinteger(L, n);
                // success = (l == 0) ? test_eof(L) : read_chars(L, l);
                success = read_chars(L, l);
            }
            else
            {
                const char *p = luaL_checkstring(L, n);
                if (*p == '*')
                    p++; /* skip optional '*' (for compatibility) */
                switch (*p)
                {
                case 'n': /* number */
                    // success = read_number(L, f);
                    break;
                case 'l': /* line */
                    success = read_line(L, 1);
                    break;
                case 'L': /* line with end-of-line */
                    success = read_line(L, 0);
                    break;
                case 'a':        /* file */
                    read_all(L); /* read entire file */
                    success = 1; /* always success */
                    break;
                default:
                    return luaL_argerror(L, n, "invalid format");
                }
            }
        }
    }
    if (!success)
    {
        lua_pop(L, 1);    /* remove last result */
        luaL_pushfail(L); /* push nil instead */
    }
    return n - first;
}

static int console_read(lua_State *L)
{
    return g_read(L, 1);
}

static int console_write(lua_State *L)
{
}

/* }====================================================== */

static const luaL_Reg tab_funcs[] = {

    {"freeMemory", getFreeMem},
    {"freeRTOSMemory", getFreeMemRTOS},
    {"usageCpu", getUseCpu},
    {"totalMemory", getTotalMem},
    {"usageMemory", getUseMem},
    {"temperature", getTemp},
    {"time", getDateTime},
    {"setTime", setDateTime},
    {"voltage", getV},
    {"reboot", reboot},
    {"read", console_read},
    {"write", console_write},
    {NULL, NULL}};

LUAMOD_API int luaopen_rtos(lua_State *L)
{
    luaL_newlib(L, tab_funcs);
    return 1;
}