add_library( freertos_plus_tcp_network_if STATIC )


target_include_directories( freertos_plus_tcp_network_if
  PUBLIC
    include
    ../../include
    ${CMAKE_SOURCE_DIR}/libs/usb/App
)

target_link_libraries( freertos_plus_tcp_network_if
  PRIVATE
    freertos_kernel
    freertos_plus_tcp
    freertos_plus_tcp_port
)

target_sources( freertos_plus_tcp_network_if
  PUBLIC
    xNetworkInterface.c
    include/xNetworkInterface.h
)

target_compile_options( freertos_plus_tcp_network_if
  PRIVATE
    $<$<COMPILE_LANG_AND_ID:C,Clang>:-Wno-conditional-uninitialized>
    $<$<COMPILE_LANG_AND_ID:C,Clang>:-Wno-implicit-int-conversion>
    $<$<COMPILE_LANG_AND_ID:C,Clang>:-Wno-padded>
    $<$<COMPILE_LANG_AND_ID:C,Clang>:-Wno-uninitialized>
    $<$<COMPILE_LANG_AND_ID:C,Clang>:-Wno-unused-but-set-variable>
    $<$<COMPILE_LANG_AND_ID:C,Clang>:-Wno-unused-macros>
)

