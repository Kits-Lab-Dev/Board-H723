cmake_minimum_required(VERSION 3.22)

#
# This file is generated only once,
# and is not re-generated if converter is called multiple times.
#
# User is free to modify the file as much as necessary
#

# Setup compiler settings
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS ON)

# Define the build type
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE "Debug")
endif()

# Set the project name
set(CMAKE_PROJECT_NAME BoardH723)

# Include toolchain file
include("BoardH723.cmake")

# Enable compile command to ease indexing with e.g. clangd
set(CMAKE_EXPORT_COMPILE_COMMANDS TRUE)

# SSH/SFTP-сервер построен на wolfSSH и wolfCrypt (GPLv3). Если он включён,
# прошивка целиком распространяется под GPLv3 (см. LICENSE и THIRD_PARTY.md в корне репозитория).
# Для закрытого проекта выключите его: cmake --preset Release -DH723_WITH_SSH=OFF
option(H723_WITH_SSH "wolfSSH + wolfCrypt (GPLv3): SSH/SFTP-сервер с терминалом" ON)
if(H723_WITH_SSH)
    message(STATUS "ВНИМАНИЕ: включён SSH на wolfSSH (GPLv3) — прошивка распространяется под GPLv3")
endif()
option(H723_WITH_TLS "Модуль ssl для Lua (HTTPS) на Mbed TLS (Apache-2.0)" ON)

# Сторонние библиотеки (git submodule): git submodule update --init
set(THIRD_PARTY ${CMAKE_SOURCE_DIR}/../../third_party)
set(FREERTOS_DIR ${THIRD_PARTY}/FreeRTOS-Kernel)
set(FREERTOS_TCP_DIR ${THIRD_PARTY}/FreeRTOS-Plus-TCP/source)
set(WOLFSSL_DIR ${THIRD_PARTY}/wolfssl)
set(WOLFSSH_DIR ${THIRD_PARTY}/wolfssh)
set(MBEDTLS_DIR ${THIRD_PARTY}/mbedtls)

# Core project settings
project(${CMAKE_PROJECT_NAME}) 
message("Build type: " ${CMAKE_BUILD_TYPE})

# Enable CMake support for ASM and C languages
enable_language(C ASM)

# Create an executable object type
add_executable(${CMAKE_PROJECT_NAME})

# Add commands to convert ELF to BIN and HEX
add_custom_command(TARGET ${CMAKE_PROJECT_NAME} POST_BUILD
    COMMAND ${CMAKE_OBJCOPY} -O binary ${CMAKE_BINARY_DIR}/${CMAKE_PROJECT_NAME}.elf ${CMAKE_BINARY_DIR}/${CMAKE_PROJECT_NAME}.bin
    COMMAND ${CMAKE_OBJCOPY} -O ihex ${CMAKE_BINARY_DIR}/${CMAKE_PROJECT_NAME}.elf ${CMAKE_BINARY_DIR}/${CMAKE_PROJECT_NAME}.hex
    COMMENT "Converting ELF to BIN and HEX formats"
)

# Add STM32CubeMX generated sources 
add_subdirectory(cmake/stm32cubemx)

# Удаляем startup_stm32h723xx.s и добавляем BoardH723.s
set_source_files_properties(${CMAKE_SOURCE_DIR}/startup_stm32h723xx.s PROPERTIES HEADER_FILE_ONLY TRUE)
target_sources(${CMAKE_PROJECT_NAME} PRIVATE
    ${CMAKE_SOURCE_DIR}/BoardH723.s
)

# Оптимизация сборки библиотек
# PIC не включать: стартовый код не копирует .got в RAM, глобальные адреса в библиотеках становятся нулевыми
set(CMAKE_C_ARCHIVE_CREATE "<CMAKE_AR> qcs <TARGET> <LINK_FLAGS> <OBJECTS>")
set(CMAKE_CXX_ARCHIVE_CREATE "<CMAKE_AR> qcs <TARGET> <LINK_FLAGS> <OBJECTS>")


# Link directories setup
target_link_directories(${CMAKE_PROJECT_NAME} PRIVATE
    # Add user defined library search paths

)

# Add include paths
# Пути заголовков прошивки — общие для основной цели и сторонних библиотек
add_library(board_includes INTERFACE)
target_include_directories(board_includes INTERFACE
    # Add user defined include paths
    app
    libs/sys/include
    libs/sdram
    libs/net
    libs/dhcp
    libs/esp
    ${THIRD_PARTY}/fatfs/source
    libs/disks
    libs/usb/App
    libs/usb/Class
    ${THIRD_PARTY}/stm32-mw-usb-device/Core/Inc
    libs/usb/Target
    libs/ntp
    libs/term
    libs/ssh
    libs/tls
    ${WOLFSSL_DIR}
    ${WOLFSSL_DIR}/wolfssl
    ${WOLFSSH_DIR}
)

# Определения проекта — общие для прошивки и библиотек (luaengine, nimble, ...)
add_compile_definitions(
    # Add user defined symbols
    _GNU_SOURCE
    _POSIX_C_SOURCE=200809L 

    ADC_VER_V5_X
    APP_DEBUG
    APP_DEBUG_LEVEL=4
    malloc=pvPortMalloc
    free=vPortFree
    realloc=pvPortRealloc
    calloc=pvPortCalloc 

    # SSH_SERVER_STACK_SDRAM
    SSH_PASSWORD="123"
    SSH_BUFFER_SIZE=4096

    WITH_LUA
    $<$<BOOL:${H723_WITH_TLS}>:WITH_TLS>
    $<$<BOOL:${H723_WITH_SSH}>:WITH_SSH>
)

# Заголовки FreeRTOS, FreeRTOS+TCP и конфиги библиотек — общие для всех целей
add_library(freertos_headers INTERFACE)
target_include_directories(freertos_headers INTERFACE
    config
    ${FREERTOS_DIR}/include
    ${FREERTOS_DIR}/portable/GCC/ARM_CM4F
    ${FREERTOS_TCP_DIR}/include
    ${FREERTOS_TCP_DIR}/portable/Compiler/GCC
    libs/net
)

# lwmem — менеджер памяти SDRAM
add_library(lwmem STATIC ${THIRD_PARTY}/lwmem/lwmem/src/lwmem/lwmem.c)
target_include_directories(lwmem PUBLIC config ${THIRD_PARTY}/lwmem/lwmem/src/include)

# Mbed TLS (Apache-2.0) — TLS для Lua (libs/lua/ssl) и криптография NimBLE.
# Настройки — config/mbedtls_board_config.h, порт на плату — libs/tls/tls_port.c.
file(GLOB MBEDTLS_SOURCES ${MBEDTLS_DIR}/library/*.c)
add_library(mbedtls OBJECT ${MBEDTLS_SOURCES} libs/tls/tls_port.c)
target_include_directories(mbedtls PUBLIC ${MBEDTLS_DIR}/include libs/tls config)
target_compile_definitions(mbedtls PUBLIC MBEDTLS_CONFIG_FILE="mbedtls_board_config.h")
target_link_libraries(mbedtls PRIVATE board_includes freertos_headers lwmem stm32cubemx)
target_link_libraries(${CMAKE_PROJECT_NAME} mbedtls)

# wolfSSH и wolfCrypt (GPLv3) — SSH/SFTP-сервер; libs/ssh — наш сервер поверх него.
# Из wolfSSL берётся только wolfCrypt (WOLFCRYPT_ONLY в config/wolfSSL.I-CUBE-wolfSSL_conf.h).
if(H723_WITH_SSH)
    file(GLOB WOLFCRYPT_SOURCES ${WOLFSSL_DIR}/wolfcrypt/src/*.c)
    add_library(wolfcrypt OBJECT ${WOLFCRYPT_SOURCES})
    target_link_libraries(wolfcrypt PRIVATE board_includes freertos_headers lwmem stm32cubemx)
    target_link_libraries(${CMAKE_PROJECT_NAME} wolfcrypt)

    file(GLOB WOLFSSH_SOURCES ${WOLFSSH_DIR}/src/*.c)
    add_library(wolfssh OBJECT ${WOLFSSH_SOURCES})
    target_link_libraries(wolfssh PRIVATE board_includes freertos_headers lwmem stm32cubemx)
    target_link_libraries(${CMAKE_PROJECT_NAME} wolfssh)
    target_sources(${CMAKE_PROJECT_NAME} PRIVATE
        libs/ssh/ssh_auth.c
        libs/ssh/ssh_sftp.c
        libs/ssh/ssh_shell.c
        libs/ssh/ssh_utils.c
        libs/ssh/ssh_server.c
    )
endif()

add_subdirectory(libs/esp/nimble_port)
add_subdirectory(libs/lua) 

# Add sources to executable
target_sources(${CMAKE_PROJECT_NAME} PUBLIC
    # Add user sources here
    app/app.c
    app/log.c
    libs/sdram/sdram.c

    libs/sys/socket.c 
    libs/sys/statvfs.c 
    libs/sys/utils.c 

    ${FREERTOS_DIR}/croutine.c
    ${FREERTOS_DIR}/event_groups.c
    ${FREERTOS_DIR}/list.c
    ${FREERTOS_DIR}/queue.c
    ${FREERTOS_DIR}/stream_buffer.c
    ${FREERTOS_DIR}/tasks.c
    ${FREERTOS_DIR}/timers.c
    ${FREERTOS_DIR}/portable/GCC/ARM_CM4F/port.c
    ${FREERTOS_DIR}/portable/MemMang/heap_4.c
    
    ${FREERTOS_TCP_DIR}/FreeRTOS_ARP.c
    ${FREERTOS_TCP_DIR}/FreeRTOS_BitConfig.c
    ${FREERTOS_TCP_DIR}/FreeRTOS_DHCP.c
    ${FREERTOS_TCP_DIR}/FreeRTOS_DHCPv6.c
    ${FREERTOS_TCP_DIR}/FreeRTOS_DNS.c
    ${FREERTOS_TCP_DIR}/FreeRTOS_DNS_Cache.c
    ${FREERTOS_TCP_DIR}/FreeRTOS_DNS_Callback.c
    ${FREERTOS_TCP_DIR}/FreeRTOS_DNS_Networking.c
    ${FREERTOS_TCP_DIR}/FreeRTOS_DNS_Parser.c
    ${FREERTOS_TCP_DIR}/FreeRTOS_ICMP.c
    ${FREERTOS_TCP_DIR}/FreeRTOS_IP.c
    ${FREERTOS_TCP_DIR}/FreeRTOS_IP_Timers.c
    ${FREERTOS_TCP_DIR}/FreeRTOS_IP_Utils.c
    ${FREERTOS_TCP_DIR}/FreeRTOS_IPv4.c
    ${FREERTOS_TCP_DIR}/FreeRTOS_IPv4_Sockets.c
    ${FREERTOS_TCP_DIR}/FreeRTOS_IPv4_Utils.c
    ${FREERTOS_TCP_DIR}/FreeRTOS_IPv6.c
    ${FREERTOS_TCP_DIR}/FreeRTOS_IPv6_Sockets.c
    ${FREERTOS_TCP_DIR}/FreeRTOS_IPv6_Utils.c
    ${FREERTOS_TCP_DIR}/FreeRTOS_ND.c
    ${FREERTOS_TCP_DIR}/FreeRTOS_RA.c
    ${FREERTOS_TCP_DIR}/FreeRTOS_Routing.c
    ${FREERTOS_TCP_DIR}/FreeRTOS_Sockets.c
    ${FREERTOS_TCP_DIR}/FreeRTOS_Stream_Buffer.c
    ${FREERTOS_TCP_DIR}/FreeRTOS_TCP_IP.c
    ${FREERTOS_TCP_DIR}/FreeRTOS_TCP_IP_IPv4.c
    ${FREERTOS_TCP_DIR}/FreeRTOS_TCP_IP_IPv6.c
    ${FREERTOS_TCP_DIR}/FreeRTOS_TCP_Reception.c
    ${FREERTOS_TCP_DIR}/FreeRTOS_TCP_State_Handling.c
    ${FREERTOS_TCP_DIR}/FreeRTOS_TCP_State_Handling_IPv4.c
    ${FREERTOS_TCP_DIR}/FreeRTOS_TCP_State_Handling_IPv6.c
    ${FREERTOS_TCP_DIR}/FreeRTOS_TCP_Transmission.c
    ${FREERTOS_TCP_DIR}/FreeRTOS_TCP_Transmission_IPv4.c
    ${FREERTOS_TCP_DIR}/FreeRTOS_TCP_Transmission_IPv6.c
    ${FREERTOS_TCP_DIR}/FreeRTOS_TCP_Utils.c
    ${FREERTOS_TCP_DIR}/FreeRTOS_TCP_Utils_IPv4.c
    ${FREERTOS_TCP_DIR}/FreeRTOS_TCP_Utils_IPv6.c
    ${FREERTOS_TCP_DIR}/FreeRTOS_TCP_WIN.c
    ${FREERTOS_TCP_DIR}/FreeRTOS_Tiny_TCP.c
    ${FREERTOS_TCP_DIR}/FreeRTOS_UDP_IP.c
    ${FREERTOS_TCP_DIR}/FreeRTOS_UDP_IPv4.c
    ${FREERTOS_TCP_DIR}/FreeRTOS_UDP_IPv6.c
    ${FREERTOS_TCP_DIR}/portable/BufferManagement/BufferAllocation_1.c
    libs/net/xNetworkInterface.c

    libs/dhcp/dhcpserver.c

    libs/esp/bt.c
    libs/esp/control.c
    libs/esp/driver.c  

    libs/disks/diskio.c
    libs/disks/ffsystem.c
    libs/disks/ramdisk.c
    libs/disks/sddisk.c
    libs/disks/wrappers.c

    ${THIRD_PARTY}/fatfs/source/ff.c
    ${THIRD_PARTY}/fatfs/source/ffunicode.c

    libs/usb/App/usb_device.c
    libs/usb/App/usbd_desc.c
    libs/usb/App/usbd_ncm_if.c
    libs/usb/Class/usbd_ncm.c
    ${THIRD_PARTY}/stm32-mw-usb-device/Core/Src/usbd_core.c
    ${THIRD_PARTY}/stm32-mw-usb-device/Core/Src/usbd_ctlreq.c
    ${THIRD_PARTY}/stm32-mw-usb-device/Core/Src/usbd_ioreq.c
    libs/usb/Target/usbd_conf.c

    libs/ntp/ntpclient.c

    libs/term/term.c



)


# Add linked libraries
target_link_libraries(${CMAKE_PROJECT_NAME}
    stm32cubemx

    # Add user defined libraries
    board_includes
    freertos_headers
    lwmem
    nimble 
    luaengine
) 

# Debug output
message(STATUS "CMAKE_BINARY_DIR: ${CMAKE_BINARY_DIR}")

# Generate VS Code launch
configure_file(
    ${CMAKE_SOURCE_DIR}/launch.json.in
    ${CMAKE_SOURCE_DIR}/.vscode/launch.json
    @ONLY
)

# Generate VS Code tasks
configure_file(
    ${CMAKE_SOURCE_DIR}/tasks.json.in
    ${CMAKE_SOURCE_DIR}/.vscode/tasks.json
    @ONLY
)