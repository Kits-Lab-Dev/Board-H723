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
#include "control.h"
#include "driver.h"
#include "sdram.h"

#define MIN_STACK_SIZE (1024)
#define MIN_PRIO (tskIDLE_PRIORITY + 1)

LUA_LIB_FUNC static int enable(lua_State *L)
{
    if (lua_type(L, 1) != LUA_TBOOLEAN)
    {
        lua_pushnil(L);
        lua_pushstring(L, "Invalid argument type");
        return 2;
    }
    bool en = lua_toboolean(L, 1);
    vTaskDelay(100);
    if (!espFeatureEnable(ftWifi, en))
    {
        lua_pushnil(L);
        lua_pushstring(L, "Failed to enable wifi feature");
        return 2;
    }
    vTaskDelay(100);
    lua_pushboolean(L, true);
    return 1;
}

LUA_LIB_FUNC static int setMac(lua_State *L)
{
    uint8_t mac[MAC_LEN];
    esp_interface interface = luaL_checkinteger(L, 1);
    const char* mac_str = luaL_checkstring(L, 2);
    if(!stringToMac(mac_str, mac)){
        lua_pushnil(L);
        lua_pushstring(L, "Invalid MAC address format");
        return 2;
    }

    if (!wifiSetMacAddress(interface, mac))
    {
        lua_pushnil(L);
        lua_pushstring(L, "Failed to set MAC address");
        return 2;
    }
    lua_pushboolean(L, true);
    return 1;
}

LUA_LIB_FUNC static int getMac(lua_State *L)
{
    uint8_t mac[MAC_LEN];
    esp_interface interface = luaL_checkinteger(L, 1);
    if (!wifiGetMacAddress(interface, mac))
    {
        lua_pushnil(L);
        lua_pushstring(L, "Failed to get MAC address");
        return 2;
    }
    char macStr[MAC_LEN * 3 + 1];
    if(!macToString(mac, macStr)){
        lua_pushnil(L);
        lua_pushstring(L, "Failed to convert MAC address to string");
        return 2;
    }
    lua_pushlstring(L, macStr, strlen(macStr));
    return 1;
}

LUA_LIB_FUNC static int getMode(lua_State *L)
{
    wifiMode mode;
    if (!wifiGetWiFiMode(&mode))
    {
        lua_pushboolean(L, false);
        lua_pushstring(L, "Failed to get WiFi mode");
        return 2;
    }

    if (mode < WIFI_MODE_STA || mode > WIFI_MODE_APSTA)
    {
        lua_pushboolean(L, false);
        lua_pushstring(L, "Invalid WiFi mode");
        return 2;
    }
    lua_pushinteger(L, mode);
    return 1;
}

LUA_LIB_FUNC static int setMode(lua_State *L)
{
    wifiMode mode = luaL_checkinteger(L, 1);

    if (mode < WIFI_MODE_STA || mode > WIFI_MODE_APSTA)
    {
        lua_pushboolean(L, false);
        lua_pushstring(L, "Invalid WiFi mode");
        return 2;
    }
    // vTaskDelay(100);
    if (!wifiSetWiFiMode(mode))
    {
        lua_pushboolean(L, false);
        lua_pushstring(L, "Failed to set WiFi mode");
        return 2;
    }
    vTaskDelay(100);
    lua_pushboolean(L, true);
    return 1;
}

LUA_LIB_FUNC static int scan(lua_State *L)
{
    wifiAPrecord *apRecs = (wifiAPrecord *)sdramMalloc(sizeof(wifiAPrecord) * MAX_WIFI_AP_RECS);
    int n = wifiGetScanList(apRecs);
    lua_newtable(L);
    for (int i = 0; i < n; ++i)
    {
        // элемент — таблица {ssid, bssid, rssi, chnl}, как в docs/wifi.md
        lua_newtable(L);
        lua_pushstring(L, (const char *)apRecs[i].ssid);
        lua_setfield(L, -2, "ssid");
        char bssid[19];
        macToString(apRecs[i].bssid, bssid);
        lua_pushstring(L, bssid);
        lua_setfield(L, -2, "bssid");
        lua_pushinteger(L, apRecs[i].rssi);
        lua_setfield(L, -2, "rssi");
        lua_pushinteger(L, apRecs[i].chnl);
        lua_setfield(L, -2, "chnl");
        lua_rawseti(L, -2, i + 1);
    }
    sdramFree(apRecs);
    return 1;
}

LUA_LIB_FUNC static int connect(lua_State *L)
{
    const char *ssid = luaL_checkstring(L, 1);
    const char *password = luaL_optstring(L, 2, NULL);

    wifiConnRecord rec = {
        .ssid = {0},
        .bssid = {0},
        .password = {0},
        .bssid_set = false,
        .is_wpa3_supported = true,
        .listen_interval = -1};

    memcpy(rec.ssid, ssid, strlen(ssid));
    rec.ssid[strlen(ssid)] = '\0';
    if (password != NULL)
    {
        memcpy(rec.password, password, strlen(password));
        rec.password[strlen(password)] = '\0';
    }

    if (!wifiConnect(&rec, true))
    {
        lua_pushboolean(L, false);
        lua_pushstring(L, "Failed start connect to AP");
        return 2;
    }
    lua_pushboolean(L, true);
    return 1;
}

LUA_LIB_FUNC static int getAPConfig(lua_State *L)
{
    wifiAPConfig rec;
    if(!wifiGetAPConfig(&rec)){
        lua_pushnil(L);
        return 1;
    }
    lua_newtable(L);
    lua_pushstring(L, "ssid");
    lua_pushstring(L, (const char*)rec.ssid);
    lua_settable(L, -3);
    lua_pushstring(L, "bssid");
    char bssid[MAC_LEN * 3 + 1];
    macToString(rec.bssid, bssid);
    lua_pushstring(L, bssid);
    lua_settable(L, -3);
    lua_pushstring(L, "channel");
    lua_pushinteger(L, rec.chnl);
    lua_settable(L, -3);
    lua_pushstring(L, "rssi");
    lua_pushinteger(L, rec.rssi);
    lua_settable(L, -3);

    return 1;
}

LUA_LIB_FUNC static int connected(lua_State *L)
{
    if (wifiIsConnected())
    {
        lua_pushboolean(L, true);
        return 1;
    }
    else
    {
        lua_pushboolean(L, false);
        return 1;
    }
}

LUA_LIB_FUNC static int disconnect(lua_State *L)
{
    if (!wifiDisconnect())
    {
        lua_pushboolean(L, false);
        lua_pushstring(L, "Failed to disconnect from AP");
        return 2;
    }
    lua_pushboolean(L, true);
    return 1;
}

LUA_LIB_FUNC static int setPowerSaveMode(lua_State *L)
{
    wifiPowerSaveMode mode = luaL_checkinteger(L, 1);
    if (!wifiSetPowerSaveMode(mode))
    {
        lua_pushboolean(L, false);
        lua_pushstring(L, "Failed set power save mode");
        return 2;
    }
    lua_pushboolean(L, true);
    return 1;
}

// startAP(ssid [, password [, channel [, maxConn]]]): без пароля — открытая сеть,
// пароль — WPA2-PSK (8–63 символа). Клиенты получают адрес 192.168.11.x по DHCP.
LUA_LIB_FUNC static int startAP(lua_State *L)
{
    size_t ssidLen, pwdLen = 0;
    const char *ssid = luaL_checklstring(L, 1, &ssidLen);
    const char *pwd = luaL_optlstring(L, 2, NULL, &pwdLen);
    lua_Integer chnl = luaL_optinteger(L, 3, 1);
    lua_Integer maxConn = luaL_optinteger(L, 4, 4);

    if (ssidLen == 0 || ssidLen >= SSID_LENGTH)
        return luaL_argerror(L, 1, "SSID length must be 1..32");
    if (pwd != NULL && pwdLen > 0 && (pwdLen < 8 || pwdLen >= PASSWORD_LENGTH))
        return luaL_argerror(L, 2, "password length must be 8..63");
    if (chnl < 1 || chnl > 13)
        return luaL_argerror(L, 3, "channel must be 1..13");
    if (maxConn < 1 || maxConn > 10)
        return luaL_argerror(L, 4, "maxConn must be 1..10");

    wifiSoftAPConfig cfg = {0};
    memcpy(cfg.ssid, ssid, ssidLen);
    if (pwd != NULL && pwdLen > 0)
    {
        memcpy(cfg.pwd, pwd, pwdLen);
        cfg.auth_mode = AuthWPA2_PSK;
    }
    else
    {
        cfg.auth_mode = AuthOpen;
    }
    cfg.chnl = (uint8_t)chnl;
    cfg.max_conn = (uint8_t)maxConn;
    cfg.ssid_hidden = 0;
    cfg.bw = WIFI_BW_HT20;

    if (!wifiStartSoftAP(&cfg))
    {
        lua_pushboolean(L, false);
        lua_pushstring(L, "Failed to start soft AP");
        return 2;
    }
    lua_pushboolean(L, true);
    return 1;
}

LUA_LIB_FUNC static int stopAP(lua_State *L)
{
    if (!wifiStopSoftAP())
    {
        lua_pushboolean(L, false);
        lua_pushstring(L, "Failed to stop soft AP");
        return 2;
    }
    lua_pushboolean(L, true);
    return 1;
}

// apClients(): таблица {mac, rssi} подключённых к точке доступа клиентов
LUA_LIB_FUNC static int apClients(lua_State *L)
{
    wifiConnectedStantion *recs = (wifiConnectedStantion *)sdramMalloc(CONTROL_DATA_LEN);
    if (recs == NULL)
        return luaL_error(L, "out of memory");
    uint32_t n = wifiGetConnectedStaList(recs);
    lua_newtable(L);
    for (uint32_t i = 0; i < n; ++i)
    {
        lua_newtable(L);
        char mac[MAC_LEN * 3 + 1];
        macToString(recs[i].mac, mac);
        lua_pushstring(L, mac);
        lua_setfield(L, -2, "mac");
        lua_pushinteger(L, recs[i].rssi);
        lua_setfield(L, -2, "rssi");
        lua_rawseti(L, -2, i + 1);
    }
    sdramFree(recs);
    return 1;
}

static const luaL_Reg tab_funcs[] = {

    {"enable", enable},
    {"setMac", setMac},
    {"getMac", getMac},
    {"getMode", getMode},
    {"setMode", setMode},
    {"scan", scan},
    {"connect", connect},
    {"getAPConfig", getAPConfig},
    {"connected", connected},
    {"disconnect", disconnect},
    {"setPowerSaveMode", setPowerSaveMode},
    {"startAP", startAP},
    {"stopAP", stopAP},
    {"apClients", apClients},
    
    {NULL, NULL}};

LUAMOD_API int luaopen_lwifi(lua_State *L)
{
    lua_pushinteger(L, ESP_STA);
    lua_setglobal(L, "WIFI_STA");
    lua_pushinteger(L, ESP_AP);
    lua_setglobal(L, "WIFI_AP");
    lua_pushinteger(L, WIFI_MODE_STA);
    lua_setglobal(L, "WIFI_MODE_STA");
    lua_pushinteger(L, WIFI_MODE_AP);
    lua_setglobal(L, "WIFI_MODE_AP");
    lua_pushinteger(L, WIFI_MODE_APSTA);
    lua_setglobal(L, "WIFI_MODE_APSTA");

    lua_pushinteger(L, AuthOpen);
    lua_setglobal(L, "WIFI_OPEN");
    lua_pushinteger(L, AuthWEP);
    lua_setglobal(L, "WIFI_WEP");
    lua_pushinteger(L, AuthWPA_PSK);
    lua_setglobal(L, "WIFI_WPA_PSK");
    lua_pushinteger(L, AuthWPA2_PSK);
    lua_setglobal(L, "WIFI_WPA2_PSK");
    lua_pushinteger(L, AuthWPA2_ENTERPRISE);
    lua_setglobal(L, "WIFI_WPA2_ENTERPRISE");
    lua_pushinteger(L, AuthWPA3_PSK);
    lua_setglobal(L, "WIFI_WPA3_PSK");
    lua_pushinteger(L, AuthWPA2_WPA3_PSK);
    lua_setglobal(L, "WIFI_WPA2_WPA3_PSK");

    lua_pushinteger(L, WIFI_PS_NONE);
    lua_setglobal(L, "WIFI_PS_NONE");
    lua_pushinteger(L, WIFI_PS_MIN_MODEM);
    lua_setglobal(L, "WIFI_PS_MIN");
    lua_pushinteger(L, WIFI_PS_MAX_MODEM);
    lua_setglobal(L, "WIFI_PS_MAX");

    luaL_newlib(L, tab_funcs);
    return 1;
}