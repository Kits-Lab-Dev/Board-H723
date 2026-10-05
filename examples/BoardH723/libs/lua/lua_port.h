/*
 * Настройка Lua под плату. Подключается ко всем исходникам luaengine
 * флагом -include (см. libs/lua/CMakeLists.txt), поэтому оригинальные
 * файлы Lua в third_party/lua не меняются: luaconf.h и llimits.h
 * проверяют эти макросы через #if !defined(...).
 */
#ifndef LUA_PORT_H
#define LUA_PORT_H

#include <limits.h>
#include <stddef.h>
#include <string.h>
#include <sys/types.h>
#include <FreeRTOS.h>

#include "app.h"

#define LUA_OUT
#define LUA_PATH_DEFAULT "?.lua;" "/lib/?.lua;" "/ram/lib/?.lua;" "/sd/lib/?.lua;" "/sd/?.lua"
#define LUA_USE_CTYPE 1

/* Блокировки и вывод — реализованы в luaengine.c */
void luaLock(void *L);
void luaUnlock(void *L);
int luaPrint(const char *msg, size_t l, void *L);
int luaGChar(void *f);
void luaUserStateThread(void *L, void *L1);
void luaUserStateFree(void *L, void *L1);

#define lua_lock(x) luaLock(x)
#define lua_unlock(x) luaUnlock(x)
#define lua_writestring(s, l) LUA_PRINT(s, l)
#define lua_writeline() lua_writestring("\r\n", 2)
#define lua_writestringerror(s, p) ERR(s, p)
#define luai_userstatethread(L, L1) LUA_USERSTATETHREAD(L, L1)
#define luai_userstatefree(L, L1) LUA_USERSTATEFREE(L, L1)

/* Атрибут функций модулей (можно переопределить, например, для размещения в RAM) */
#define LUA_LIB_FUNC

#define lstat stat

/* luasec: заглушка в libs/tls/ssl_util.c */
int SSL_up_ref(void *ssl);

#ifndef LLONG_MAX
#define LLONG_MAX __LONG_LONG_MAX__
#endif
#ifndef LLONG_MIN
#define LLONG_MIN __LONG_LONG_MIN__
#endif

#endif /* LUA_PORT_H */
