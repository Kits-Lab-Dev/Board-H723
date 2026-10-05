add_library(luaengine OBJECT)

# Ядро Lua — third_party/lua без изменений; настройки платы в lua_port.h
set(LUA_DIR ${CMAKE_SOURCE_DIR}/../../third_party/lua)
target_compile_options(luaengine PRIVATE "SHELL:-include ${CMAKE_CURRENT_SOURCE_DIR}/lua_port.h")

# LuaSocket — third_party/luasocket без изменений.
set(LUASOCKET_DIR ${CMAKE_SOURCE_DIR}/../../third_party/luasocket)
# luasec подключает заголовки как <luasocket/xxx.h>
set(LUASOCKET_INCLUDE ${CMAKE_CURRENT_BINARY_DIR}/luasocket_include)
file(GLOB LUASOCKET_HEADERS ${LUASOCKET_DIR}/src/*.h)
file(COPY ${LUASOCKET_HEADERS} DESTINATION ${LUASOCKET_INCLUDE}/luasocket)
# Неблокирующий recv: наш POSIX-слой (libs/sys) пока не поддерживает
# fcntl(O_NONBLOCK), поэтому флаг передаётся прямо в recv.
include(${CMAKE_SOURCE_DIR}/cmake/patch_source.cmake)
patch_source(${LUASOCKET_DIR}/src/usocket.c ${CMAKE_CURRENT_BINARY_DIR}/patched/usocket.c
    FIND "long taken = (long) recv(*ps, data, count, 0);"
    REPLACE "long taken = (long) recv(*ps, data, count, FREERTOS_MSG_DONTWAIT);")

# LuaSec — third_party/luasec, работает поверх совместимого с OpenSSL API wolfSSL.
set(LUASEC_DIR ${CMAKE_SOURCE_DIR}/../../third_party/luasec)
set(LUASEC_PATCHED ${CMAKE_CURRENT_BINARY_DIR}/luasec)
# wolfSSL реализует API OpenSSL 1.1, но OPENSSL_VERSION_NUMBER у него меньше
target_compile_definitions(luaengine PRIVATE LSEC_API_OPENSSL_1_1_0)
# Заголовки рядом с правлеными копиями, чтобы "ssl.h" не взялся из wolfssl/
file(GLOB LUASEC_HEADERS ${LUASEC_DIR}/src/*.h)
file(COPY ${LUASEC_HEADERS} DESTINATION ${LUASEC_PATCHED})
# Свои функции ввода-вывода для TLS-сокета (libs/tls/ssl_util.c)
patch_source(${LUASEC_DIR}/src/ssl.c ${LUASEC_PATCHED}/ssl.c
    FIND "    SSL_set_connect_state(ssl->ssl);\n"
    REPLACE "    SSL_set_connect_state(ssl->ssl);\n  { extern void SSLSetIO(void *ssl); SSLSetIO(ssl->ssl); }\n")
# SHA-1 в wolfSSL отключён (NO_SHA): дайджест по умолчанию — SHA-256
patch_source(${LUASEC_DIR}/src/x509.c ${LUASEC_PATCHED}/x509.c
    FIND "if (!str)\n    digest = EVP_sha1();"
    REPLACE "if (!str)\n    digest = EVP_sha256();"
    FIND "if (!strcmp(str, \"sha1\"))\n      digest = EVP_sha1();\n    else if"
    REPLACE "if")

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
        ${CMAKE_SOURCE_DIR}/../../third_party/wolfssl
        ${CMAKE_SOURCE_DIR}/../../third_party/wolfssl/wolfssl
        ${CMAKE_SOURCE_DIR}/libs/esp
        ${CMAKE_SOURCE_DIR}/../../third_party/fatfs/source
        ${CMAKE_SOURCE_DIR}/config
        ${CMAKE_SOURCE_DIR}/libs/term
        ${CMAKE_SOURCE_DIR}/libs/disks
        ${CMAKE_SOURCE_DIR}/libs/lua
        ${LUA_DIR}
        luajson
        lfs
        ${LUASEC_PATCHED}
        lsocket
        ${LUASOCKET_DIR}/src
        ${LUASOCKET_INCLUDE}
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

    ${LUASOCKET_DIR}/src/auxiliar.c
    ${LUASOCKET_DIR}/src/buffer.c
    ${LUASOCKET_DIR}/src/compat.c
    ${LUASOCKET_DIR}/src/except.c
    ${LUASOCKET_DIR}/src/inet.c
    ${LUASOCKET_DIR}/src/io.c
    ${LUASOCKET_DIR}/src/luasocket.c
    ${LUASOCKET_DIR}/src/mime.c
    ${LUASOCKET_DIR}/src/options.c
    ${LUASOCKET_DIR}/src/select.c
    ${LUASOCKET_DIR}/src/serial.c
    ${LUASOCKET_DIR}/src/tcp.c
    ${LUASOCKET_DIR}/src/timeout.c
    ${LUASOCKET_DIR}/src/udp.c
    ${LUASOCKET_DIR}/src/unix.c
    ${LUASOCKET_DIR}/src/unixdgram.c
    ${LUASOCKET_DIR}/src/unixstream.c
    ${CMAKE_CURRENT_BINARY_DIR}/patched/usocket.c

    ${LUASEC_DIR}/src/config.c
    ${LUASEC_DIR}/src/context.c
    ${LUASEC_DIR}/src/ec.c
    ${LUASEC_DIR}/src/options.c
    ${LUASEC_PATCHED}/ssl.c
    ${LUASEC_PATCHED}/x509.c
)
