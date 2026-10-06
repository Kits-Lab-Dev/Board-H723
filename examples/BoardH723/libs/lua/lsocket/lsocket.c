#include "lprefix.h"
#include <limits.h>
#include <stddef.h>
#include <string.h>

#include "lua.h"

#include "lauxlib.h"
#include "lualib.h"
#include "sdram.h"
#include "FreeRTOS_IP.h"
#include "FreeRTOS_Sockets.h"
#include "sys/socket.h"

LUA_LIB_FUNC static int l_socket(lua_State *L)
{
    int domain = luaL_checkinteger(L, 1);
    int type = luaL_checkinteger(L, 2);
    int protocol = luaL_checkinteger(L, 3);
    int sock = socket(domain, type, protocol);
    lua_pushinteger(L, sock);
    return 1;
}

LUA_LIB_FUNC static int l_bind(lua_State *L)
{
    int sock = (int)luaL_checkinteger(L, 1);
    struct freertos_sockaddr address;
    address.sin_family = FREERTOS_AF_INET;
    address.sin_port = FreeRTOS_htons((uint16_t)luaL_checkinteger(L, 3));
    address.sin_address.ulIP_IPv4 = FreeRTOS_inet_addr(luaL_checkstring(L, 2));
    int result = bind(sock, &address, sizeof(address));
    lua_pushinteger(L, result);
    return 1;
}

LUA_LIB_FUNC static int l_listen(lua_State *L)
{
    int sock = (int)luaL_checkinteger(L, 1);
    int backlog = luaL_checkinteger(L, 2);
    int result = listen(sock, backlog);
    lua_pushinteger(L, result);
    return 1;
}

LUA_LIB_FUNC static int l_accept(lua_State *L)
{
    int sock = (int)luaL_checkinteger(L, 1);
    int client_sock = accept(sock, NULL, NULL);
    lua_pushinteger(L, client_sock);
    return 1;
}

LUA_LIB_FUNC static int l_read(lua_State *L)
{
    int sock = (int)luaL_checkinteger(L, 1);
    size_t len = luaL_checkinteger(L, 2);
    char *buffer = (char *)sdramMalloc(len);
    int received = recv(sock, buffer, len, 0);
    if (received > 0)
    {
        lua_newtable(L);
        for (int i = 0; i < received; ++i)
        {
            lua_pushinteger(L, buffer[i]);
            lua_rawseti(L, -2, i + 1);
        }
    }
    else
    {
        lua_pushnil(L);
    }
    sdramFree(buffer);
    return 1;
}

LUA_LIB_FUNC static int l_read_str(lua_State *L)
{
    int sock = (int)luaL_checkinteger(L, 1);
    size_t len = luaL_checkinteger(L, 2);
    char *buffer = (char *)sdramMalloc(len);
    int received = recv(sock, buffer, len, 0);
    if (received > 0)
    {
        lua_pushlstring(L, buffer, received);
    }
    else
    {
        lua_pushnil(L);
    }
    sdramFree(buffer);
    return 1;
}

LUA_LIB_FUNC static int l_send(lua_State *L)
{
    int sent = 0;
    int sock = (int)luaL_checkinteger(L, 1);
    if (lua_type(L, 2) == LUA_TSTRING)
    {
        const char *str = luaL_checkstring(L, 2);
        sent = send(sock, str, strlen(str), 0);
    }
    else
    {
        luaL_checktype(L, 2, LUA_TTABLE);
        size_t len = lua_rawlen(L, 2);

        char *data = (char *)sdramMalloc(len);
        for (size_t i = 0; i < len; ++i)
        {
            lua_rawgeti(L, 2, i + 1);
            data[i] = (char)lua_tointeger(L, -1);
            lua_pop(L, 1);
        }
        sent = send(sock, data, len, 0);
        sdramFree(data);
    }
    lua_pushinteger(L, sent);
    return 1;
}
// UDP
LUA_LIB_FUNC static int l_readfrom(lua_State *L)
{
    int sock = (int)luaL_checkinteger(L, 1);
    size_t len = luaL_checkinteger(L, 2);
    char *buffer = (char *)sdramMalloc(len);
    struct freertos_sockaddr address;
    int received = recvfrom(sock, buffer, len, 0, &address, NULL);
    if (received > 0)
    {
        lua_newtable(L);        // Создаем таблицу для хранения данных
        char addrbuf[32];              
        lua_pushstring(L, FreeRTOS_inet_ntoa(address.sin_address.ulIP_IPv4, addrbuf)); // Добавляем IP-адрес в таблицу
        lua_setfield(L, -2, "ip");                               // Устанавливаем поле "ip" для IP-адреса
        lua_pushinteger(L, address.sin_port); // Добавляем порт в таблицу
        lua_setfield(L, -2, "port");          // Устанавливаем поле "port" для порта
        lua_newtable(L);                      // Создаем вложенную таблицу для данных
        for (int i = 0; i < received; i++)
        {
            lua_pushinteger(L, buffer[i]); // Добавляем каждый байт данных в вложенную таблицу
            lua_rawseti(L, -2, i + 1);     // Устанавливаем байт данных в вложенной таблице
        }
        lua_setfield(L, -2, "data");          // Устанавливаем поле "data" для вложенной таблицы данных
    }
    else
    {
        lua_pushnil(L);
    }
    sdramFree(buffer);
    return 1; 
}

LUA_LIB_FUNC static int l_read_str_from(lua_State *L)
{
    int sock = (int)luaL_checkinteger(L, 1);
    size_t len = luaL_checkinteger(L, 2);
    char *buffer = (char *)sdramMalloc(len);
    struct freertos_sockaddr address;
    int received = recvfrom(sock, buffer, len, 0, &address, NULL);
    if (received > 0)
    {
        lua_newtable(L);    // Создаем таблицу для хранения данных
        char addrbuf[32];              
        lua_pushstring(L, FreeRTOS_inet_ntoa(address.sin_address.ulIP_IPv4, addrbuf)); // Добавляем IP-адрес в таблицу
        lua_setfield(L, -2, "ip");                    // Создаем таблицу для хранения данных
        lua_pushinteger(L, address.sin_port); // Добавляем порт в таблицу
        lua_setfield(L, -2, "port");          // Устанавливаем поле "port" для порта
        lua_pushlstring(L, buffer, received); // Добавляем полученные данные в таблицу
        lua_setfield(L, -2, "data");     
    }
    else
    {
        lua_pushnil(L);
    }
    sdramFree(buffer);
    return 1;
}

LUA_LIB_FUNC static int l_sendto(lua_State *L)
{
    int sent = 0;
    int sock = (int)luaL_checkinteger(L, 1);
    struct freertos_sockaddr address;
    address.sin_family = FREERTOS_SOCK_DGRAM;
    address.sin_port = FreeRTOS_htons((uint16_t)luaL_checkinteger(L, 4));
    address.sin_address.ulIP_IPv4 = FreeRTOS_inet_addr(luaL_checkstring(L, 3));
    if (lua_type(L, 2) == LUA_TSTRING)
    {
        const char *str = luaL_checkstring(L, 2);
        sent = sendto(sock, str, strlen(str), 0, &address, sizeof(address));
    }
    else
    {
        size_t len = luaL_checkinteger(L, 2);
        luaL_checktype(L, 2, LUA_TTABLE);

        char *data = (char *)sdramMalloc(len);
        for (size_t i = 0; i < len; ++i)
        {
            lua_rawgeti(L, 2, i + 1);
            data[i] = (char)lua_tointeger(L, -1);
            lua_pop(L, 1);
        }
        sent = sendto(sock, data, len, 0, &address, sizeof(address));
        sdramFree(data);
    }
    lua_pushinteger(L, sent);
    return 1;
}
// end UDP

LUA_LIB_FUNC static int l_connect(lua_State *L)
{
    struct freertos_sockaddr address;
    address.sin_family = FREERTOS_AF_INET;
    address.sin_port = FreeRTOS_htons((uint16_t)luaL_checkinteger(L, 3));
    address.sin_address.ulIP_IPv4 = FreeRTOS_inet_addr(luaL_checkstring(L, 2));
    int sock = (int)luaL_checkinteger(L, 1);
    int result = connect(sock, &address, sizeof(address));
    lua_pushinteger(L, result);
    return 1;
}

LUA_LIB_FUNC static int l_close(lua_State *L)
{
    int sock = (int)luaL_checkinteger(L, 1);
    int result = close(sock);
    lua_pushinteger(L, result);
    return 1;
}

LUA_LIB_FUNC static int l_localIP(lua_State *L)
{
    Socket_t sock = (Socket_t)(intptr_t)luaL_checkinteger(L, 1);
    const char *separator = ".";
    if (lua_gettop(L) > 1)
    {
        separator = luaL_checkstring(L, 2);
    }
    uint32_t addr = sock->pxEndPoint->ipv4_settings.ulIPAddress;

    char ip_str[16];
    snprintf(ip_str, sizeof(ip_str), "%lu%s%lu%s%lu%s%lu",
             (addr >> 0) & 0xFF, separator,
             (addr >> 8) & 0xFF, separator,
             (addr >> 16) & 0xFF, separator,
             (addr >> 24) & 0xFF);
    lua_pushstring(L, ip_str);
    return 1;
}

LUA_LIB_FUNC static int l_isNetUp(lua_State *L)
{
    lua_pushboolean(L, FreeRTOS_IsNetworkUp() == pdTRUE);
    return 1;
}

/* }====================================================== */

static const luaL_Reg tab_funcs[] = {
    {"socket", l_socket}, // Создание сокета
    {"bind", l_bind}, // Привязка сокета к адресу и порту
    {"listen", l_listen}, // Прослушивание входящих соединений
    {"accept", l_accept}, // Принятие входящего соединения
    {"recv", l_read}, // Получение данных из сокета
    {"read", l_read_str}, // Чтение строки из сокета
    {"send", l_send}, // Отправка данных в сокет
    {"recvfrom", l_readfrom}, // Получение данных из UDP сокета
    {"readfrom", l_read_str_from}, // Чтение строки из UDP сокета
    {"sendto", l_sendto}, // Отправка данных в UDP сокет
    {"connect", l_connect}, // Установка соединения с удаленным сокетом
    {"close", l_close}, // Закрытие сокета
    {"localIP", l_localIP}, // Получение локального IP-адреса
    {"isnetup", l_isNetUp}, // Проверка состояния сети
    {NULL, NULL}};

LUAMOD_API int luaopen_lsocket(lua_State *L)
{
    lua_pushinteger(L, FREERTOS_AF_INET); // Установка семейства адресов для сокета (IPv4)
    lua_setglobal(L, "AF_INET");
    lua_pushinteger(L, FREERTOS_SOCK_STREAM); // Установка типа сокета (потоковый сокет, TCP)
    lua_setglobal(L, "SOCK_STREAM");
    lua_pushinteger(L, FREERTOS_SOCK_DGRAM); // Установка типа сокета (датаграммный сокет, UDP)
    lua_setglobal(L, "SOCK_DGRAM");
    lua_pushinteger(L, FREERTOS_SOCK_DEPENDENT_PROTO); // Установка протокола по умолчанию для сокета
    lua_setglobal(L, "IPPROTO_IP");
    lua_pushinteger(L, FREERTOS_IPPROTO_TCP); // Установка протокола TCP для сокета
    lua_setglobal(L, "IPPROTO_TCP");
    lua_pushinteger(L, FREERTOS_IPPROTO_UDP); // Установка протокола UDP для сокета
    lua_setglobal(L, "IPPROTO_UDP");
    luaL_newlib(L, tab_funcs);
    return 1;
}

/*
** Example usage in Lua
**
** This example demonstrates how to use the lsocket library to create a simple HTTP server.
** It listens on port 80 and responds with a "Hello, World!" message to any incoming request.
**
** Lua code:
**
-- Create a socket for the HTTP server
local http_server_socket = socket.socket(socket.AF_INET, socket.SOCK_STREAM, socket.IPPROTO_IP)
if http_server_socket < 0 then
    print("Failed to create socket for HTTP server")
    return
end

-- Bind the socket to address and port
local bind_result = socket.bind(http_server_socket, "0.0.0.0", 80)
if bind_result ~= 0 then
    print("Failed to bind socket for HTTP server")
    socket.close(http_server_socket)
    return
end

-- Listen for incoming connections
local listen_result = socket.listen(http_server_socket, 5)
if listen_result ~= 0 then
    print("Failed to listen on port for HTTP server")
    socket.close(http_server_socket)
    return
end

print("HTTP server started and listening on port 80")

-- Loop to accept incoming connections
while true do
    local client_socket = socket.accept(http_server_socket)
    if client_socket > 0 then
        print("HTTP client connected: " .. client_socket)
        -- Handle client request and send response
        local request = socket.read(client_socket, 1024)
        if request then
            print("Received data from HTTP client: " .. request)
            local response = "HTTP/1.1 200 OK\r\nContent-Type: text/plain\r\r\n\nHello, World!"
            local send_result = socket.send(client_socket, response)
            if send_result < 0 then
                print("Failed to send response to HTTP client")
            end
        else
            socket.close(client_socket)
            print("HTTP client disconnected: " .. client_socket)
        end
    end
end

-- Close the HTTP server socket
local close_result = socket.close(http_server_socket)
if close_result < 0 then
    print("Failed to close HTTP server socket")
end
**/

/*
-- Создание UDP сокета
local udp_socket = socket.udp()
if not udp_socket then
    print("Не удалось создать UDP сокет")
    return
end

-- Привязка сокета к адресу и порту
local bind_result = socket.bind(udp_socket, "0.0.0.0", 12345)
if bind_result ~= 0 then
    print("Не удалось привязать UDP сокет")
    socket.close(udp_socket)
    return
end

print("UDP сервер запущен и слушает на порту 12345")

-- Цикл для приема входящих сообщений
while true do
    local datagram = socket.readfrom(udp_socket, 1024)
    if datagram then
        print("Получено сообщение от " .. datagram.ip .. ":" .. datagram.port .. " - " .. datagram.data)
        -- Отправка ответа клиенту
        local response = "Привет, клиент!"
        local send_result = socket.sendto(udp_socket, response, datagram.ip, datagram.port)
        if send_result < 0 then
            print("Не удалось отправить ответ клиенту")
        end
    end
end

-- Закрытие UDP сокета
local close_result = socket.close(udp_socket)
if close_result < 0 then
    print("Не удалось закрыть UDP сокет")
end
*/


