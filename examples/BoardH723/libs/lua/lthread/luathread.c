#include "lprefix.h"
#include <limits.h>
#include <stddef.h>
#include <string.h>

#include "lua.h"

#include "lauxlib.h"
#include "lualib.h"
#include "ramdisk.h"

#include "FreeRTOS.h"
#include "task.h"
#include "semphr.h"
#include "queue.h"
#include "app.h"
#include "luathread.h"

#define MIN_STACK_SIZE (1024)
#define MIN_PRIO (tskIDLE_PRIORITY + 1)

typedef struct ThreadArg
{
    lua_State *L;
    lua_State *threadL;
} ThreadArg;

typedef struct TreadParams
{
    TaskHandle_t id;
    uint32_t stackSize;
    uint32_t priority;
} ThreadParams;

LUA_LIB_FUNC static void task(void *arg)
{
    INF("task start, id: %d\r\n", (int)xTaskGetCurrentTaskHandle());
    ThreadArg *a = (ThreadArg *)arg;
    lua_State *L = a->L;
    lua_State *tL = a->threadL;
    vPortFree(a);
    int top = lua_gettop(tL);
    for (int i = 1; i <= top; i++)
    {
        const char *type = lua_typename(tL, lua_type(tL, i));
        printf("Stack position %d: %s\n", i, type);
    }
    if (lua_isfunction(tL, 1))
    {
        if (lua_pcall(tL, top - 1, 0, 0) != LUA_OK)
        {
            ERR("%s\n", lua_tostring(tL, -1));
            lua_pop(tL, 1);
        }
    }
    else if (lua_isstring(tL, 2))
    {
        if (luaL_dostring(tL, lua_tostring(tL, 2)) != LUA_OK)
        {
            ERR("%s\n", lua_tostring(tL, -1));
            lua_pop(tL, 1);
        }
    }
    else
    {
        ERR("not a function or string\n");
    }

    lua_closethread(tL, L);
    TaskHandle_t id = xTaskGetCurrentTaskHandle();
    INF("task stop, id: %d\r\n", (int)id);
    vTaskDelete(id);
}

LUA_LIB_FUNC static int takeSemaphore(lua_State *L)
{
    if (!lua_istable(L, 1) || !lua_getfield(L, 1, "__semaphore"))
    {
        lua_pushstring(L, "Invalid semaphore object");
        return lua_error(L);
    }
    int t = portMAX_DELAY;
    if (lua_isinteger(L, 2))
        t = luaL_checkinteger(L, 2);
    SemaphoreHandle_t sem = (SemaphoreHandle_t)luaL_checkinteger(L, 1);
    BaseType_t res = xSemaphoreTake(sem, t);
    lua_pushboolean(L, res == pdTRUE);
    return 1;
}

LUA_LIB_FUNC static int giveSemaphore(lua_State *L)
{
    if (!lua_istable(L, 1) || !lua_getfield(L, 1, "__semaphore"))
    {
        lua_pushstring(L, "Invalid semaphore object");
        return lua_error(L);
    }
    SemaphoreHandle_t sem = (SemaphoreHandle_t)luaL_checkinteger(L, 1);
    BaseType_t res = xSemaphoreGive(sem);
    lua_pushboolean(L, res == pdTRUE);
    return 1;
}

LUA_LIB_FUNC static int deleteSemaphore(lua_State *L)
{
    if (!lua_istable(L, 1) || !lua_getfield(L, 1, "__semaphore"))
    {
        lua_pushstring(L, "Invalid semaphore object");
        return lua_error(L);
    }
    SemaphoreHandle_t sem = (SemaphoreHandle_t)luaL_checkinteger(L, 1);
    vSemaphoreDelete(sem);
    return 0;
}

static const luaL_Reg semaphoreTabFuncs[] = {
    {"take", takeSemaphore},
    {"give", giveSemaphore},
    {"__gc", deleteSemaphore},
    {NULL, NULL}};

LUA_LIB_FUNC int newSemaphore(lua_State *L)
{
    SemaphoreHandle_t Semaphore = xSemaphoreCreateMutex();
    if (Semaphore == NULL)
    {
        lua_pushnil(L);
        return 1;
    }
    // Создаем новую таблицу для объекта
    luaL_newmetatable(L, "semaphore");

    // Устанавливаем параметры в таблицу
    lua_pushinteger(L, (int)Semaphore);
    lua_setfield(L, -2, "__semaphore");

    // Получаем таблицу функций из threadTabFuncs
    const luaL_Reg *funcs = semaphoreTabFuncs;
    for (; funcs->name != NULL; ++funcs)
    {
        lua_pushcfunction(L, funcs->func);
        lua_setfield(L, -2, funcs->name);
    }
    luaL_setmetatable(L, "semaphore");
    return 1;
}

LUA_LIB_FUNC static int takeMutex(lua_State *L)
{
    if (!lua_istable(L, 1) || !lua_getfield(L, 1, "__mutex"))
    {
        lua_pushstring(L, "Invalid mutex object. Mutex must be a table with __mutex field.");
        return lua_error(L);
    }
    SemaphoreHandle_t mutex = (SemaphoreHandle_t)lua_tointeger(L, -1);
    int t = portMAX_DELAY;
    if (lua_isinteger(L, 2))
        t = luaL_checkinteger(L, 2);
    BaseType_t res = xSemaphoreTake(mutex, t);
    lua_pushboolean(L, res == pdTRUE);
    return 1;
}

LUA_LIB_FUNC static int giveMutex(lua_State *L)
{
    if (!lua_istable(L, 1) || !lua_getfield(L, 1, "__mutex"))
    {
        lua_pushstring(L, "Invalid mutex object. Mutex must be a table with __mutex field.");
        return lua_error(L);
    }
    SemaphoreHandle_t mutex = (SemaphoreHandle_t)lua_tointeger(L, -1);
    BaseType_t res = xSemaphoreGive(mutex);
    lua_pushboolean(L, res == pdTRUE);
    return 1;
}

LUA_LIB_FUNC static int deleteMutex(lua_State *L)
{
    if (!lua_istable(L, 1) || !lua_getfield(L, 1, "__mutex"))
    {
        lua_pushstring(L, "Invalid mutex object. Mutex must be a table with __mutex field.");
        return lua_error(L);
    }
    SemaphoreHandle_t mutex = (SemaphoreHandle_t)lua_tointeger(L, -1);
    vSemaphoreDelete(mutex);
    return 0;
}

static const luaL_Reg mutexTabFuncs[] = {
    {"take", takeMutex},
    {"give", giveMutex},
    {"__gc", deleteMutex},
    {NULL, NULL}};

LUA_LIB_FUNC int newMutex(lua_State *L)
{
    SemaphoreHandle_t mutex = xSemaphoreCreateMutex();
    if (mutex == NULL)
    {
        lua_pushnil(L);
        return 1;
    }
    // Создаем новую таблицу для объекта
    luaL_newmetatable(L, "mutex");

    // Устанавливаем параметры в таблицу
    lua_pushinteger(L, (int)mutex);
    lua_setfield(L, -2, "__mutex");

    // Получаем таблицу функций из threadTabFuncs
    const luaL_Reg *funcs = mutexTabFuncs;
    for (; funcs->name != NULL; ++funcs)
    {
        lua_pushcfunction(L, funcs->func);
        lua_setfield(L, -2, funcs->name);
    }
    luaL_setmetatable(L, "mutex");
    return 1;
}

LUA_LIB_FUNC static int startThread(lua_State *L)
{
    // Проверяем количество аргументов (минимум 1 - функция)
    if (!lua_isstring(L, 2) && !lua_isfunction(L, 2))
    {
        lua_pushstring(L, "Error thread arg");
        return lua_error(L);
    }

    // Проверяем, является ли объект таблицей с полем __params
    if (!lua_istable(L, 1) || !lua_getfield(L, 1, "__params"))
    {
        lua_pushstring(L, "The second argument is not a thread object");
        return lua_error(L);
    }

    // Получаем указатель на структуру ThreadParams
    ThreadParams *params = (ThreadParams *)lua_touserdata(L, -1);

    ThreadArg *arg = (ThreadArg *)pvPortMalloc(sizeof(ThreadArg));
    arg->L = L;
    arg->threadL = lua_newthread(arg->L);

    lua_remove(L, 1);
    lua_pop(L, 2);
    int top = lua_gettop(L);
    for (int i = 1; i <= top; i++)
    {
        const char *type = lua_typename(L, lua_type(L, i));
        printf("Stack position %d: %s\n", i, type);
    }
   
    lua_xmove(arg->L, arg->threadL, lua_gettop(L));

    BaseType_t res = xTaskCreate(task, "lua_task", params->stackSize, arg, params->priority, &params->id);
    if (res != pdTRUE)
    {
        ERR("error create thread");
        lua_pushinteger(L, 0);
        return 1;
    }

    lua_pushboolean(L, true);
    return 1;
}

LUA_LIB_FUNC static int stopThread(lua_State *L)
{
    // Проверяем, является ли объект таблицей с полем __params
    if (!lua_istable(L, 1) || !lua_getfield(L, 1, "__params"))
    {
        lua_pushstring(L, "The second argument is not a thread object");
        return lua_error(L);
    }

    // Получаем указатель на структуру ThreadParams
    ThreadParams *params = (ThreadParams *)lua_touserdata(L, -1);
    vTaskDelete(params->id);
    return 0;
}

static const luaL_Reg threadTabFuncs[] = {

    {"start", startThread},
    {"stop", stopThread},
    {NULL, NULL}};

LUA_LIB_FUNC int newThread(lua_State *L)
{
    ThreadParams *params = (ThreadParams *)lua_newuserdata(L, sizeof(ThreadParams));
    if (lua_gettop(L) == 3)
    {
        params->stackSize = luaL_checkinteger(L, 1);
        params->priority = luaL_checkinteger(L, 2);
    }
    else
    {
        params->stackSize = MIN_STACK_SIZE;
        params->priority = MIN_PRIO;
    }
    // Создаем новую таблицу для объекта
    lua_newtable(L);

    // Устанавливаем параметры в таблицу
    lua_pushlightuserdata(L, params);
    lua_setfield(L, -2, "__params");

    // Устанавливаем метатаблицу для нового объекта
    luaL_setmetatable(L, "thread");

    // Получаем таблицу функций из threadTabFuncs
    const luaL_Reg *funcs = threadTabFuncs;

    // Вставляем функции в таблицу объекта
    for (; funcs->name != NULL; ++funcs)
    {
        lua_pushcfunction(L, funcs->func);
        lua_setfield(L, -2, funcs->name);
    }
    return 1;
}

LUA_LIB_FUNC int delayThread(lua_State *L)
{
    int ms = luaL_checkinteger(L, 1);
    vTaskDelay(ms);
    return 0;
}