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

# Сторонние библиотеки (git submodule): git submodule update --init
set(THIRD_PARTY ${CMAKE_SOURCE_DIR}/../../third_party)
set(FREERTOS_DIR ${THIRD_PARTY}/FreeRTOS-Kernel)

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
set(CMAKE_POSITION_INDEPENDENT_CODE ON)
set(CMAKE_C_ARCHIVE_CREATE "<CMAKE_AR> qcs <TARGET> <LINK_FLAGS> <OBJECTS>")
set(CMAKE_CXX_ARCHIVE_CREATE "<CMAKE_AR> qcs <TARGET> <LINK_FLAGS> <OBJECTS>")

file(GLOB_RECURSE WOLFSSL_SRC "libs/wolfSSL/src/*.c")
file(GLOB_RECURSE WOLFSSL_CRYPT "libs/wolfSSL/wolfcrypt/**.c")
file(GLOB_RECURSE WOLFSSH_SRC "libs/wolfSSH/src/*.c")

# Link directories setup
target_link_directories(${CMAKE_PROJECT_NAME} PRIVATE
    # Add user defined library search paths

)

# Add include paths
target_include_directories(${CMAKE_PROJECT_NAME} PUBLIC
    # Add user defined include paths
    app
    libs/sys/include
    libs/sdram
    libs/FreeRTOS-Plus-TCP/include
    libs/FreeRTOS-Plus-TCP/portable/NetworkInterface/include
    libs/dhcp
    libs/esp
    libs/FatFs
    libs/disks
    libs/usb/App
    libs/usb/Class
    ${THIRD_PARTY}/stm32-mw-usb-device/Core/Inc
    libs/usb/Target
    libs/ntp
    libs/term
    libs/ssh
    libs/wolfSSL
    libs/wolfSSL/wolfssl
    libs/wolfSSL/cyassl
    libs/wolfSSH
)

# Заголовки FreeRTOS и конфиги библиотек — общие для всех целей
add_library(freertos_headers INTERFACE)
target_include_directories(freertos_headers INTERFACE
    config
    ${FREERTOS_DIR}/include
    ${FREERTOS_DIR}/portable/GCC/ARM_CM4F
)

# lwmem — менеджер памяти SDRAM
add_library(lwmem STATIC ${THIRD_PARTY}/lwmem/lwmem/src/lwmem/lwmem.c)
target_include_directories(lwmem PUBLIC config ${THIRD_PARTY}/lwmem/lwmem/src/include)

add_subdirectory(libs/esp/nimble)  
add_subdirectory(libs/lua) 

# Add sources to executable
target_sources(${CMAKE_PROJECT_NAME} PUBLIC
    # Add user sources here
    app/app.c
    app/log.c
    libs/lua/lua_macros.h
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
    
    libs/FreeRTOS-Plus-TCP/FreeRTOS_ARP.c
    libs/FreeRTOS-Plus-TCP/FreeRTOS_BitConfig.c
    libs/FreeRTOS-Plus-TCP/FreeRTOS_DHCP.c
    libs/FreeRTOS-Plus-TCP/FreeRTOS_DHCPv6.c
    libs/FreeRTOS-Plus-TCP/FreeRTOS_DNS.c
    libs/FreeRTOS-Plus-TCP/FreeRTOS_DNS_Cache.c
    libs/FreeRTOS-Plus-TCP/FreeRTOS_DNS_Callback.c
    libs/FreeRTOS-Plus-TCP/FreeRTOS_DNS_Networking.c
    libs/FreeRTOS-Plus-TCP/FreeRTOS_DNS_Parser.c
    libs/FreeRTOS-Plus-TCP/FreeRTOS_ICMP.c
    libs/FreeRTOS-Plus-TCP/FreeRTOS_IP.c
    libs/FreeRTOS-Plus-TCP/FreeRTOS_IP_Timers.c
    libs/FreeRTOS-Plus-TCP/FreeRTOS_IP_Utils.c
    libs/FreeRTOS-Plus-TCP/FreeRTOS_IPv4.c
    libs/FreeRTOS-Plus-TCP/FreeRTOS_IPv4_Sockets.c
    libs/FreeRTOS-Plus-TCP/FreeRTOS_IPv4_Utils.c
    libs/FreeRTOS-Plus-TCP/FreeRTOS_IPv6.c
    libs/FreeRTOS-Plus-TCP/FreeRTOS_IPv6_Sockets.c
    libs/FreeRTOS-Plus-TCP/FreeRTOS_IPv6_Utils.c
    libs/FreeRTOS-Plus-TCP/FreeRTOS_ND.c
    libs/FreeRTOS-Plus-TCP/FreeRTOS_RA.c
    libs/FreeRTOS-Plus-TCP/FreeRTOS_Routing.c
    libs/FreeRTOS-Plus-TCP/FreeRTOS_Sockets.c
    libs/FreeRTOS-Plus-TCP/FreeRTOS_Stream_Buffer.c
    libs/FreeRTOS-Plus-TCP/FreeRTOS_TCP_IP.c
    libs/FreeRTOS-Plus-TCP/FreeRTOS_TCP_IP_IPv4.c
    libs/FreeRTOS-Plus-TCP/FreeRTOS_TCP_IP_IPv6.c
    libs/FreeRTOS-Plus-TCP/FreeRTOS_TCP_Reception.c
    libs/FreeRTOS-Plus-TCP/FreeRTOS_TCP_State_Handling.c
    libs/FreeRTOS-Plus-TCP/FreeRTOS_TCP_State_Handling_IPv4.c
    libs/FreeRTOS-Plus-TCP/FreeRTOS_TCP_State_Handling_IPv6.c
    libs/FreeRTOS-Plus-TCP/FreeRTOS_TCP_Transmission.c
    libs/FreeRTOS-Plus-TCP/FreeRTOS_TCP_Transmission_IPv4.c
    libs/FreeRTOS-Plus-TCP/FreeRTOS_TCP_Transmission_IPv6.c
    libs/FreeRTOS-Plus-TCP/FreeRTOS_TCP_Utils.c
    libs/FreeRTOS-Plus-TCP/FreeRTOS_TCP_Utils_IPv4.c
    libs/FreeRTOS-Plus-TCP/FreeRTOS_TCP_Utils_IPv6.c
    libs/FreeRTOS-Plus-TCP/FreeRTOS_TCP_WIN.c
    libs/FreeRTOS-Plus-TCP/FreeRTOS_Tiny_TCP.c
    libs/FreeRTOS-Plus-TCP/FreeRTOS_UDP_IP.c
    libs/FreeRTOS-Plus-TCP/FreeRTOS_UDP_IPv4.c
    libs/FreeRTOS-Plus-TCP/FreeRTOS_UDP_IPv6.c
    libs/FreeRTOS-Plus-TCP/portable/BufferManagement/BufferAllocation_1.c
    libs/FreeRTOS-Plus-TCP/portable/NetworkInterface/xNetworkInterface.c

    libs/dhcp/dhcpserver.c

    libs/esp/bt.c
    libs/esp/control.c
    libs/esp/driver.c  

    libs/disks/diskio.c
    libs/disks/ffsystem.c
    libs/disks/ramdisk.c
    libs/disks/sddisk.c
    libs/disks/wrappers.c

    libs/FatFs/ff.c
    libs/FatFs/ffunicode.c

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

    libs/ssh/ssh_auth.c
    libs/ssh/ssh_sftp.c
    libs/ssh/ssh_shell.c 
    libs/ssh/ssh_utils.c 
    libs/ssh/ssh_server.c

    ${WOLFSSL_SRC}
    libs/wolfSSL/ssl_util.c
    ${WOLFSSL_CRYPT}
    ${WOLFSSH_SRC}

)

# Add project symbols (macros)
target_compile_definitions(${CMAKE_PROJECT_NAME} PRIVATE
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
)

# Add linked libraries
target_link_libraries(${CMAKE_PROJECT_NAME}
    stm32cubemx

    # Add user defined libraries
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