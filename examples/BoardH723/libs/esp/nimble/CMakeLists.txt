add_library(nimble STATIC)

target_include_directories(stm32cubemx INTERFACE ${MX_Include_Dirs})
target_compile_definitions(stm32cubemx INTERFACE ${MX_Defines_Syms})

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


        ${CMAKE_SOURCE_DIR}/libs/esp/nimble/include
        ${CMAKE_SOURCE_DIR}/libs/esp/nimble/transport/include
        ${CMAKE_SOURCE_DIR}/libs/esp/nimble/transport/common/hci_h4/include
        ${CMAKE_SOURCE_DIR}/libs/esp/nimble/transport/common/hci_ipc/include
        ${CMAKE_SOURCE_DIR}/libs/esp/nimble/ext/tinycrypt/include
        ${CMAKE_SOURCE_DIR}/libs/esp/nimble/drivers/native/include
        ${CMAKE_SOURCE_DIR}/libs/esp/nimble/controller/include
        ${CMAKE_SOURCE_DIR}/libs/esp/nimble/host/services/ans/include
        ${CMAKE_SOURCE_DIR}/libs/esp/nimble/host/services/bas/include
        ${CMAKE_SOURCE_DIR}/libs/esp/nimble/host/services/dis/include
        ${CMAKE_SOURCE_DIR}/libs/esp/nimble/host/services/gap/include
        ${CMAKE_SOURCE_DIR}/libs/esp/nimble/host/services/gatt/include
        ${CMAKE_SOURCE_DIR}/libs/esp/nimble/host/services/ias/include
        ${CMAKE_SOURCE_DIR}/libs/esp/nimble/host/services/ipss/include
        ${CMAKE_SOURCE_DIR}/libs/esp/nimble/host/services/lls/include
        ${CMAKE_SOURCE_DIR}/libs/esp/nimble/host/services/tps/include
        ${CMAKE_SOURCE_DIR}/libs/esp/nimble/host/include
        ${CMAKE_SOURCE_DIR}/libs/esp/nimble/host/mesh/include
        ${CMAKE_SOURCE_DIR}/libs/esp/nimble/host/audio/include
        ${CMAKE_SOURCE_DIR}/libs/esp/nimble/host/store/ram/include
        ${CMAKE_SOURCE_DIR}/libs/esp/nimble/host/store/config/include
        ${CMAKE_SOURCE_DIR}/libs/esp/nimble/host/util/include
        ${CMAKE_SOURCE_DIR}/libs/esp/nimble/porting/nimble/include
        ${CMAKE_SOURCE_DIR}/libs/esp/nimble/porting/npl/freertos/include
        ${CMAKE_SOURCE_DIR}/libs/esp/nimble/porting/bleuart/include
)

file(GLOB_RECURSE NIMBLE_DRIVERS_SOURCES "drivers/native/src/*.c")
file(GLOB_RECURSE NIMBLE_TINYCRYPT_SOURCES "ext/tinycrypt/src/*.c")
file(GLOB_RECURSE NIMBLE_HOST_SOURCES "host/src/*.c")
file(GLOB_RECURSE NIMBLE_HOST_MESH_SOURCES "host/mesh/**/*.c")
file(GLOB_RECURSE NIMBLE_HOST_SERVICES_SOURCES "host/services/**/*.c")
file(GLOB_RECURSE NIMBLE_HOST_STORE_SOURCES "host/store/**/*.c")
file(GLOB_RECURSE NIMBLE_PORTING_SOURCES "porting/**/*.c")
file(GLOB_RECURSE NIMBLE_TRANSPORT_SOURCES "transport/**/*.c")

target_sources(nimble PUBLIC 
    ${NIMBLE_DRIVERS_SOURCES}
    ${NIMBLE_TINYCRYPT_SOURCES}
    ${NIMBLE_HOST_SOURCES}
    ${NIMBLE_HOST_MESH_SOURCES}
    ${NIMBLE_HOST_SERVICES_SOURCES}
    ${NIMBLE_HOST_STORE_SOURCES}
    ${NIMBLE_PORTING_SOURCES}
    ${NIMBLE_TRANSPORT_SOURCES}
)