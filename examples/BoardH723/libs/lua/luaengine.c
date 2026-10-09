#ifdef WITH_LUA

#include "luaengine.h"
#include "FreeRTOS.h"
#include "semphr.h"
#include "sdram.h"

#include <time.h>
#include <sys/time.h>

#include "lfs.h"
#include <luasocket.h>
#include "stdbool.h"
#include <stdio.h>
#include <unistd.h>
#include "app.h"

#include "lstate.h"

int _gettimeofday(struct timeval *tv, void *tzvp)
{
    UNUSED(tzvp);
    uint64_t t = 0;                        // get uptime in nanoseconds
    tv->tv_sec = t / 1000000000;           // convert to seconds
    tv->tv_usec = (t % 1000000000) / 1000; // get remaining microseconds
    return 0;                              // return non-zero for error
}

int luaopen_custom_io(lua_State *L);

#define LUA_CJSON "cjson"
int luaopen_cjson(lua_State *l);
#define LUA_THREAD "thread"
int luaopen_lthread(lua_State *L);
#define LUA_SOCK "lsocket"
int luaopen_lsocket(lua_State *L);
#define LUA_RTOS "rtos"
int luaopen_rtos(lua_State *L);
#define LUA_WIFI "wifi"
int luaopen_lwifi(lua_State *L);
#define LUA_BLE "ble"
int luaopen_ble(lua_State *L);

#define LAU_LFS "lfs"

#define LUA_GPIO "pio"
#define LUA_SERIAL "serial"
#define LUA_I2C "wire"
#define LUA_SPI "spi"
#define LUA_CAN "can"

#define LUA_SOCKET "socket.core"
#define LUA_SSL "ssl.core"
#define LUA_SSL_CONTEXT "ssl.context"
#define LUA_SSL_x509 "ssl.x509"
#define LUA_SSL_CONGIG "ssl.config"
#define LUA_MIME "mime"

int luaopen_gpio(lua_State *L);

int luaopen_serial(lua_State *L);
int luaopen_i2c(lua_State *L);
int luaopen_spi(lua_State *L);
int luaopen_can(lua_State *L);

#ifdef WITH_TLS
int luaopen_ssl_core(lua_State *L);
int luaopen_ssl_context(lua_State *L);
int luaopen_ssl_x509(lua_State *L);
int luaopen_ssl_config(lua_State *L);
#endif

int luaopen_mime_core(lua_State *L);

static void *lua_alloc(void *ud, void *ptr, size_t osize, size_t nsize)
{
    (void)ud;
    (void)osize;
    if (ptr == NULL && nsize == 0)
    {
        return NULL;
    }
    return sdramRealloc(ptr, nsize);
}

static const luaL_Reg loadedlibs[] = {
    {LUA_GNAME, luaopen_base},
    {LUA_LOADLIBNAME, luaopen_package},
    // {LUA_COLIBNAME, luaopen_coroutine},
    {LUA_TABLIBNAME, luaopen_table},
    {LUA_IOLIBNAME, luaopen_io},
    {LUA_OSLIBNAME, luaopen_os},
    {LUA_STRLIBNAME, luaopen_string},
    {LUA_MATHLIBNAME, luaopen_math},
    {LUA_UTF8LIBNAME, luaopen_utf8},
    {LUA_CJSON, luaopen_cjson},
    {LUA_SOCKET, luaopen_socket_core},
    {LUA_MIME, luaopen_mime_core},
    {LUA_RTOS, luaopen_rtos},
    {LUA_THREAD, luaopen_lthread},
    {LAU_LFS, luaopen_lfs},
    {LUA_WIFI, luaopen_lwifi},
    // {LUA_BLE, luaopen_ble},
    {LUA_GPIO, luaopen_gpio},
    {LUA_SERIAL, luaopen_serial},
    {LUA_I2C, luaopen_i2c},
    {LUA_SPI, luaopen_spi},
    {LUA_CAN, luaopen_can},
#ifdef WITH_TLS
    {LUA_SSL, luaopen_ssl_core},
    {LUA_SSL_CONTEXT, luaopen_ssl_context},
    {LUA_SSL_x509, luaopen_ssl_x509},
    {LUA_SSL_CONGIG, luaopen_ssl_config},
#endif

    {NULL, NULL}};

lua_State *luaInit()
{
    LuaContextData *ctx = pvPortMalloc(sizeof(LuaContextData));
    memset(ctx, 0, sizeof(LuaContextData));
    ctx->mutex = xSemaphoreCreateMutex();
    ctx->L = lua_newstate(lua_alloc, ctx);
    const luaL_Reg *lib;
    for (lib = loadedlibs; lib->func; lib++)
    {
        luaL_requiref(ctx->L, lib->name, lib->func, 1);
        lua_pop(ctx->L, 1);
    }
    return ctx->L;
}

void luaDeinit(lua_State *L)
{
    LuaContextData *ctx = (LuaContextData *)(G(L)->ud);
    lua_close(L);
    vSemaphoreDelete(ctx->mutex);
    vPortFree(ctx);
}

LuaContextData *luaGetUD(lua_State *L)
{
    return (LuaContextData *)(G(L)->ud);
}

bool luaRun(lua_State *L, const char *script)
{
    if (luaL_dostring(L, script) != LUA_OK)
    {
        ERR("Lua error: %s\n", lua_tostring(L, -1));
        return false;
    }
    return true;
}

bool luaCall(lua_State *L, const char *code)
{
    const char *msg;
    bool ret = true;
    if (L == NULL)
    {
        return false;
    }
    if (luaL_loadstring(L, code) != LUA_OK)
    {
        msg = "\033[31mLua error: \033[0m";
        luaPrint(msg, strlen(msg), L);
        msg = lua_tostring(L, -1);
        luaPrint(msg, strlen(msg), L);
        msg = "\r\n";
        luaPrint(msg, strlen(msg), L);
        ret = false;
    }
    if (ret && lua_pcall(L, 0, LUA_MULTRET, 0) != LUA_OK)
    {
        msg = "\033[31mLua error: \033[0m";
        luaPrint(msg, strlen(msg), L);
        msg = lua_tostring(L, -1);
        luaPrint(msg, strlen(msg), L);
        msg = "\r\n";
        luaPrint(msg, strlen(msg), L);
        ret = false;
    }
    return ret;
}

bool luaRunFile(lua_State *L, const char *filename)
{
    const char *msg;
    bool ret = true;
    if (L == NULL)
    {
        return false;
    }
    if (luaL_dofile(L, filename) != LUA_OK)
    {
        msg = "\033[31mLua error: \033[0m";
        luaPrint(msg, strlen(msg), L);
        msg = lua_tostring(L, -1);
        luaPrint(msg, strlen(msg), L);
        msg = "\r\n";
        luaPrint(msg, strlen(msg), L);
        ret = false;
    }
    return ret;
}

void luaLock(void *L)
{
    LuaContextData *ctx = (LuaContextData *)(G(((lua_State *)L))->ud);
    xMutexTake(ctx->mutex, portMAX_DELAY);
}

void luaUnlock(void *L)
{
    LuaContextData *ctx = (LuaContextData *)(G(((lua_State *)L))->ud);
    xMutexGive(ctx->mutex);
}

int luaPrint(const char *msg, size_t l, void *L)
{
    UNUSED(l);
    LuaContextData *ctx = (LuaContextData *)(G(((lua_State *)L))->ud);
    if (ctx->trm != NULL)
    {
        return ctx->trm->output(ctx->trm, msg);
    }
    return mcuPrint(msg);
}

int c_getc(lua_State *L)
{
    LuaContextData *ctx = (LuaContextData *)(G(((lua_State *)L))->ud);
    if (ctx->trm != NULL)
    {
        char c;
        int ret = ctx->trm->read(ctx->trm, &c, 1);
        if (ret == 1)
            return c;
    }
    return -1;
}
int c_read(lua_State *L, char *p, size_t n)
{
    LuaContextData *ctx = (LuaContextData *)(G(((lua_State *)L))->ud);
    if (ctx->trm != NULL)
    {
        size_t total_read = 0;
        while (total_read < n)
        {
            int ret = ctx->trm->read(ctx->trm, p + total_read, n - total_read);
            if (ret > 0)
            {
                total_read += ret;
            }
            else if (ret < 0)
            {
                return -1;
            }
            else
            {
                break; // EOF или нет данных
            }
        }
        return total_read;
    }
    return -1;
}

void luaUserStateThread(void *L, void *L1)
{
    G(((lua_State *)L1))->ud = G(((lua_State *)L))->ud;
}

void luaUserStateFree(void *L, void *L1)
{
    UNUSED(L);
    UNUSED(L1);
}

#endif