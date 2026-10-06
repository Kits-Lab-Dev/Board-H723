# Имя и пароль Wi-Fi для рецепта tcp_speed: -DTCP_SPEED_SSID=... -DTCP_SPEED_PASSWORD=...
set(TCP_SPEED_SSID "" CACHE STRING "Wi-Fi SSID для рецепта tcp_speed")
set(TCP_SPEED_PASSWORD "" CACHE STRING "Пароль Wi-Fi для рецепта tcp_speed")
set_source_files_properties(${RECIPES_DIR}/tcp_speed/tcp_speed.c PROPERTIES COMPILE_DEFINITIONS
    "TCP_SPEED_SSID=\"${TCP_SPEED_SSID}\";TCP_SPEED_PASSWORD=\"${TCP_SPEED_PASSWORD}\"")
