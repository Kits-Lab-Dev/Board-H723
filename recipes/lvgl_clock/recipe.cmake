# Зависимости рецепта lvgl_clock (подключается из examples/BoardH723/CMakeLists.txt)

if("lvgl_demo" IN_LIST H723_RECIPES OR "lcd_ltdc" IN_LIST H723_RECIPES OR "wifi_http" IN_LIST H723_RECIPES)
    message(FATAL_ERROR "lvgl_clock включает в себя lvgl_demo, lcd_ltdc и wifi_http — вместе с ними его не собирают")
endif()

# LVGL, драйвер дисплея и тача — из рецепта lvgl_demo (его экран не запускается)
include(${RECIPES_DIR}/lvgl_demo/recipe.cmake)
target_sources(${CMAKE_PROJECT_NAME} PRIVATE
    ${RECIPES_DIR}/lvgl_demo/lvgl_port.c
    ${RECIPES_DIR}/lvgl_demo/gt911.c
)
target_include_directories(${CMAKE_PROJECT_NAME} PRIVATE ${RECIPES_DIR}/lvgl_demo)

# Wi-Fi и HTTP-запросы — из рецепта wifi_http (его задача не запускается)
target_sources(${CMAKE_PROJECT_NAME} PRIVATE ${RECIPES_DIR}/wifi_http/wifi_http.c)
target_include_directories(${CMAKE_PROJECT_NAME} PRIVATE ${RECIPES_DIR}/wifi_http)
