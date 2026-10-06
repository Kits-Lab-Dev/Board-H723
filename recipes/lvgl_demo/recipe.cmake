# Зависимости рецепта lvgl_demo (подключается из examples/BoardH723/CMakeLists.txt)

# LVGL 9 (MIT) — third_party/lvgl без изменений, настройки — config/lv_conf.h
set(LVGL_DIR ${THIRD_PARTY}/lvgl)
file(GLOB_RECURSE LVGL_SOURCES ${LVGL_DIR}/src/*.c)
add_library(lvgl OBJECT ${LVGL_SOURCES})
target_include_directories(lvgl PUBLIC
    ${LVGL_DIR}
    ${LVGL_DIR}/include
    ${LVGL_DIR}/include/lvgl
    ${CMAKE_SOURCE_DIR}/config
)
target_compile_definitions(lvgl PUBLIC LV_CONF_INCLUDE_SIMPLE)
# По размеру: заливки и копии делает DMA2D, процессору остаются буквы и края
target_compile_options(lvgl PRIVATE -Os)
# stm32h7xx_hal.h — для блока DMA2D в LVGL (регистры и SCB)
target_link_libraries(lvgl PRIVATE stm32cubemx board_includes freertos_headers)
target_link_libraries(${CMAKE_PROJECT_NAME} lvgl)

# Драйвер дисплея — из рецепта lcd_ltdc (его пример lcd_ltdc_start не запускается)
target_sources(${CMAKE_PROJECT_NAME} PRIVATE ${RECIPES_DIR}/lcd_ltdc/lcd_ltdc.c)
target_include_directories(${CMAKE_PROJECT_NAME} PRIVATE ${RECIPES_DIR}/lcd_ltdc)
