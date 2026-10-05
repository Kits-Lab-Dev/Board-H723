add_library(luaengine OBJECT)

# Ядро Lua — third_party/lua без изменений; настройки платы в lua_port.h
set(LUA_DIR ${CMAKE_SOURCE_DIR}/../../third_party/lua)
target_compile_options(luaengine PRIVATE "SHELL:-include ${CMAKE_CURRENT_SOURCE_DIR}/lua_port.h")

target_include_directories(stm32cubemx INTERFACE ${MX_Include_Dirs})
target_compile_definitions(stm32cubemx INTERFACE ${MX_Defines_Syms})

target_link_libraries(luaengine
    stm32cubemx
    freertos_headers
    lwmem
    nimble
)

target_include_directories(luaengine
    PUBLIC
        ${CMAKE_SOURCE_DIR}/app
        ${CMAKE_SOURCE_DIR}/libs/sys/include
        ${CMAKE_SOURCE_DIR}/libs/sdram
        ${CMAKE_SOURCE_DIR}/libs/wolfSSL
        ${CMAKE_SOURCE_DIR}/libs/wolfSSL/wolfssl
        ${CMAKE_SOURCE_DIR}/libs/esp
        ${CMAKE_SOURCE_DIR}/../../third_party/fatfs/source
        ${CMAKE_SOURCE_DIR}/config
        ${CMAKE_SOURCE_DIR}/libs/term
        ${CMAKE_SOURCE_DIR}/libs/disks
        ${CMAKE_SOURCE_DIR}/libs/lua
        ${LUA_DIR}
        luajson
        lfs
        luasec
        lsocket
        luasocket
)

target_sources( luaengine
  PRIVATE
    luaengine.c
    luarun.c
    ble/ble.c
    can/can.c
    lfs/lfs.c
    lsocket/lsocket.c
    lthread/lthread.c
    luajson/fpconv.c
    luajson/lua_cjson.c
    luajson/strbuf.c
    luasec/config.c
    luasec/context.c
    luasec/ec.c
    luasec/options.c
    luasec/ssl.c
    luasec/x509.c
    luasocket/auxiliar.c
    luasocket/buffer.c
    luasocket/compat.c
    luasocket/except.c
    luasocket/inet.c
    luasocket/io.c
    luasocket/luasocket.c
    luasocket/mime.c
    luasocket/options.c
    luasocket/select.c
    luasocket/serial.c
    luasocket/tcp.c
    luasocket/timeout.c
    luasocket/udp.c
    luasocket/unix.c
    luasocket/unixdgram.c
    luasocket/unixstream.c
    luasocket/usocket.c
    pio/pio.c
    rtos/rtos.c 
    serial/serial.c
    spi/spi.c
    wifi/wifi.c
    wire/wire.c

    ${LUA_DIR}/lapi.c
    ${LUA_DIR}/lauxlib.c
    ${LUA_DIR}/lbaselib.c
    ${LUA_DIR}/lcode.c
    ${LUA_DIR}/lcorolib.c
    ${LUA_DIR}/lctype.c
    ${LUA_DIR}/ldblib.c
    ${LUA_DIR}/ldebug.c
    ${LUA_DIR}/ldo.c
    ${LUA_DIR}/ldump.c
    ${LUA_DIR}/lfunc.c
    ${LUA_DIR}/lgc.c
    ${LUA_DIR}/linit.c
    ${LUA_DIR}/liolib.c
    ${LUA_DIR}/llex.c
    ${LUA_DIR}/lmathlib.c
    ${LUA_DIR}/lmem.c
    ${LUA_DIR}/loadlib.c
    ${LUA_DIR}/lobject.c
    ${LUA_DIR}/lopcodes.c
    ${LUA_DIR}/loslib.c
    ${LUA_DIR}/lparser.c
    ${LUA_DIR}/lstate.c
    ${LUA_DIR}/lstring.c
    ${LUA_DIR}/lstrlib.c
    ${LUA_DIR}/ltable.c
    ${LUA_DIR}/ltablib.c
    ${LUA_DIR}/ltm.c
    ${LUA_DIR}/lundump.c
    ${LUA_DIR}/lutf8lib.c
    ${LUA_DIR}/lvm.c
    ${LUA_DIR}/lzio.c
)
