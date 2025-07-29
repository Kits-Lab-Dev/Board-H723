#include "lprefix.h"
#include <limits.h>
#include <stddef.h>
#include <string.h>

#include "lua.h"

#include "lauxlib.h"
#include "lualib.h"

#include "FreeRTOS.h"
#include "task.h"
#include "semphr.h"
#include "queue.h"
#include "app.h"

#define MIN_STACK_SIZE (1024)
#define MIN_PRIO (tskIDLE_PRIORITY + 1)

typedef struct ThreadArg
{
    lua_State *L;
    int func;
    lua_State *threadL;
} ThreadArg;

LUA_LIB_FUNC void task(void *arg)
{
    INF("task start, id: %d\r\n", (int)xTaskGetCurrentTaskHandle());
    ThreadArg *a = (ThreadArg *)arg;
    lua_State *L = a->L;
    lua_State *threadL = a->threadL;
    vPortFree(a);

    luaL_checktype(threadL, 1, LUA_TFUNCTION);
    if (lua_isfunction(threadL, -1))
    {
        if (lua_pcall(threadL, 0, 0, 0) != LUA_OK)
        {
            ERR("%s\n", lua_tostring(a->threadL, -1));
            lua_pop(threadL, 1);
        }
    }
    else
        WRN("arg is not a function\r\n");

    lua_closethread(threadL, L);

    TaskHandle_t id = xTaskGetCurrentTaskHandle();
    INF("task stop, id: %d\r\n", (int)id);
    vTaskDelete(id);
}

LUA_LIB_FUNC static int startThread(lua_State *L)
{
    int stackSize = MIN_STACK_SIZE;
    int priority = MIN_PRIO;
    if (lua_gettop(L) == 3)
    {
        stackSize = luaL_checkinteger(L, 2);
        priority = luaL_checkinteger(L, 3);
    }
    luaL_checktype(L, 1, LUA_TFUNCTION);
    int func = luaL_ref(L, LUA_REGISTRYINDEX);
    ThreadArg *arg = (ThreadArg *)pvPortMalloc(sizeof(ThreadArg));
    arg->L = L;
    arg->func = func;
    arg->threadL = lua_newthread(arg->L);
    // arg->thread_ref = luaL_ref(L, LUA_REGISTRYINDEX);

    lua_rawgeti(arg->L, LUA_REGISTRYINDEX, func);
    lua_settop(arg->L, 1);
    lua_xmove(arg->L, arg->threadL, 1);
    TaskHandle_t id;
    BaseType_t res = xTaskCreate(task, "lua_task", stackSize, arg, priority, &id);
    if (res != pdTRUE)
    {
        // luaL_unref(L, LUA_REGISTRYINDEX, arg->thread_ref);
        vPortFree(arg);
        ERR("error create thread");
        lua_pushinteger(L, 0);
        return 1;
    }
    lua_pushinteger(L, (int)id);
    return 1;
}

LUA_LIB_FUNC static int delay(lua_State *L)
{
    int ms = luaL_checkinteger(L, 1);
    vTaskDelay(ms);
    return 0;
}

LUA_LIB_FUNC static int newSemaphore(lua_State *L)
{
    int count = luaL_checkinteger(L, 1);
    int maxCount = luaL_checkinteger(L, 2);
    SemaphoreHandle_t sem = xSemaphoreCreateCounting(maxCount, count);
    if (sem == NULL)
    {
        lua_pushnil(L);
        return 1;
    }
    lua_pushinteger(L, (int)sem);
    return 1;
}

LUA_LIB_FUNC static int takeSemaphore(lua_State *L)
{
    SemaphoreHandle_t sem = (SemaphoreHandle_t)(uintptr_t)luaL_checkinteger(L, 1);
    int t = luaL_optinteger(L, 2, portMAX_DELAY);
    BaseType_t res = xSemaphoreTake(sem, t);
    lua_pushboolean(L, res == pdTRUE);
    return 1;
}

LUA_LIB_FUNC static int giveSemaphore(lua_State *L)
{
    SemaphoreHandle_t sem = (SemaphoreHandle_t)(uintptr_t)luaL_checkinteger(L, 1);
    BaseType_t res = xSemaphoreGive(sem);
    lua_pushboolean(L, res == pdTRUE);
    return 1;
}

LUA_LIB_FUNC static int deleteSemaphore(lua_State *L)
{
    SemaphoreHandle_t sem = (SemaphoreHandle_t)(uintptr_t)luaL_checkinteger(L, 1);
    vSemaphoreDelete(sem);
    return 0;
}
LUA_LIB_FUNC static int newMutex(lua_State *L)
{
    SemaphoreHandle_t mutex = xSemaphoreCreateMutex();
    if (mutex == NULL)
    {
        lua_pushnil(L);
        return 1;
    }
    lua_pushinteger(L, (int)mutex);
    return 1;
}

LUA_LIB_FUNC static int takeMutex(lua_State *L)
{
    SemaphoreHandle_t mutex = (SemaphoreHandle_t)(uintptr_t)luaL_checkinteger(L, 1);
    int t = luaL_optinteger(L, 2, portMAX_DELAY);
    BaseType_t res = xSemaphoreTake(mutex, t);
    lua_pushboolean(L, res == pdTRUE);
    return 1;
}

LUA_LIB_FUNC static int giveMutex(lua_State *L)
{
    SemaphoreHandle_t mutex = (SemaphoreHandle_t)(uintptr_t)luaL_checkinteger(L, 1);
    BaseType_t res = xSemaphoreGive(mutex);
    lua_pushboolean(L, res == pdTRUE);
    return 1;
}

LUA_LIB_FUNC static int deleteMutex(lua_State *L)
{
    SemaphoreHandle_t mutex = (SemaphoreHandle_t)(uintptr_t)luaL_checkinteger(L, 1);
    vSemaphoreDelete(mutex);
    return 0;
}

/* }====================================================== */

static const luaL_Reg tab_funcs[] = {

    {"start", startThread},
    {"mutex", newMutex},
    {"lock", takeMutex},
    {"unlock", giveMutex},
    {"deleteMutex", deleteMutex},
    {"semaphore", newSemaphore},
    {"wait", takeSemaphore},
    {"release", giveSemaphore},
    {"deleteSemaphore", deleteSemaphore},
    {"delay", delay},
    {NULL, NULL}};

LUAMOD_API int luaopen_lthread(lua_State *L)
{
    luaL_newlib(L, tab_funcs);
    return 1;
}
