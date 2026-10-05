# NimBLE (Apache-2.0) — BLE-хост поверх контроллера ESP32-C2 (HCI по SPI).
# Стек и порт (porting/nimble, porting/npl/freertos) берутся из
# third_party/mynewt-nimble без изменений. Здесь только наше:
#   include/syscfg/syscfg.h   — конфиг NimBLE (MYNEWT_VAL_*)
#   include/console/console.h — заглушка консоли для NPL
#   bleuart/                  — сервис BLE UART
# HCI-транспорт к ESP32-C2 — libs/esp/bt.c.
add_library(nimble OBJECT)

set(NIMBLE_DIR ${CMAKE_SOURCE_DIR}/../../third_party/mynewt-nimble)
set(NIMBLE_STACK ${NIMBLE_DIR}/nimble)
set(NIMBLE_PORTING ${NIMBLE_DIR}/porting)
set(NIMBLE_SERVICES ans bas dis gap gatt ias ipss lls tps)

target_link_libraries(nimble
    stm32cubemx
    freertos_headers
    lwmem
)

target_include_directories(nimble
    PUBLIC
        ${CMAKE_SOURCE_DIR}/libs/sdram
        ${CMAKE_SOURCE_DIR}/app
        ${CMAKE_SOURCE_DIR}/libs/esp

        ${CMAKE_CURRENT_SOURCE_DIR}/include
        ${CMAKE_CURRENT_SOURCE_DIR}/bleuart/include

        ${NIMBLE_PORTING}/nimble/include
        ${NIMBLE_PORTING}/npl/freertos/include

        ${NIMBLE_STACK}/include
        ${NIMBLE_STACK}/transport/include
        ${NIMBLE_STACK}/transport/common/hci_h4/include
        ${NIMBLE_STACK}/transport/common/hci_ipc/include
        ${NIMBLE_DIR}/ext/tinycrypt/include
        ${NIMBLE_STACK}/drivers/native/include
        ${NIMBLE_STACK}/controller/include
        ${NIMBLE_STACK}/host/include
        ${NIMBLE_STACK}/host/mesh/include
        ${NIMBLE_STACK}/host/audio/include
        ${NIMBLE_STACK}/host/store/ram/include
        ${NIMBLE_STACK}/host/store/config/include
        ${NIMBLE_STACK}/host/util/include
)
foreach(service ${NIMBLE_SERVICES})
    target_include_directories(nimble PUBLIC ${NIMBLE_STACK}/host/services/${service}/include)
endforeach()

# in_isr(): учитываем и замаскированные прерывания (PRIMASK), а не только
# активный обработчик — NimBLE вызывается из кода с выключенными прерываниями.
include(${CMAKE_SOURCE_DIR}/cmake/patch_source.cmake)
set(NIMBLE_PATCHED ${CMAKE_CURRENT_BINARY_DIR}/patched)
patch_source(${NIMBLE_PORTING}/npl/freertos/src/npl_os_freertos.c ${NIMBLE_PATCHED}/npl_os_freertos.c
    FIND "return (SCB->ICSR & SCB_ICSR_VECTACTIVE_Msk) != 0;"
    REPLACE "uint32_t ipsr, primask;\n    __asm volatile (\"MRS %0, ipsr\" : \"=r\" (ipsr));\n    __asm volatile (\"MRS %0, primask\" : \"=r\" (primask) :: \"memory\");\n    return (ipsr != 0U) || (primask != 0U);")

file(GLOB NIMBLE_PORTING_SOURCES
    ${NIMBLE_PORTING}/nimble/src/*.c
    ${NIMBLE_PORTING}/npl/freertos/src/*.c
)
list(FILTER NIMBLE_PORTING_SOURCES EXCLUDE REGEX "/(hal_timer|npl_os_freertos)\\.c$")

file(GLOB NIMBLE_STACK_SOURCES
    ${NIMBLE_STACK}/drivers/native/src/*.c
    ${NIMBLE_DIR}/ext/tinycrypt/src/*.c
    ${NIMBLE_STACK}/host/src/*.c
    ${NIMBLE_STACK}/transport/src/*.c
    ${NIMBLE_STACK}/transport/common/hci_h4/src/*.c
    ${NIMBLE_STACK}/transport/common/hci_ipc/src/*.c
)
file(GLOB_RECURSE NIMBLE_HOST_EXTRA_SOURCES
    ${NIMBLE_STACK}/host/mesh/src/*.c
    ${NIMBLE_STACK}/host/store/*.c
)
foreach(service ${NIMBLE_SERVICES})
    file(GLOB_RECURSE service_sources ${NIMBLE_STACK}/host/services/${service}/src/*.c)
    list(APPEND NIMBLE_HOST_EXTRA_SOURCES ${service_sources})
endforeach()

target_sources(nimble PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/bleuart/src/bleuart.c
    ${NIMBLE_PATCHED}/npl_os_freertos.c
    ${NIMBLE_PORTING_SOURCES}
    ${NIMBLE_STACK_SOURCES}
    ${NIMBLE_HOST_EXTRA_SOURCES}
)
