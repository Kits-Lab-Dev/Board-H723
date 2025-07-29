#ifndef LUAENGINE_H
#define LUAENGINE_H

#ifdef __cplusplus
extern "C"
{
#endif

#include <stdio.h>
#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"
#include "semphr.h"
#include "lua.h"
#include "lauxlib.h"
#include "lualib.h"
#include "stdbool.h"
#include "term.h"

// Определение структуры контекста клиента
typedef struct LuaContextData {
    lua_State *L;
    SemaphoreHandle_t mutex;
    term_t * trm;
} LuaContextData;


lua_State * luaInit();
void luaDeinit(lua_State * L);

LuaContextData* luaGetUD(lua_State *L);

bool luaRun(lua_State *L, const char *script);
bool luaCall(lua_State *L, const char *code);
bool luaRunFile(lua_State *L, const char *filename);

#ifdef __cplusplus
}
#endif

#endif