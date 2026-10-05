# Сторонние компоненты

Код KitsLab распространяется под лицензией MIT (см. [LICENSE](LICENSE)).
Сторонние компоненты сохраняют свои лицензии.

> **GPLv3.** SSH/SFTP-сервер построен на wolfSSH и wolfCrypt (часть wolfSSL),
> они распространяются под GPLv3 (или коммерческой лицензией wolfSSL Inc.).
> Прошивка, собранная с опцией `H723_WITH_SSH` (включена по умолчанию), целиком
> распространяется под GPLv3. Для закрытого проекта выключите её:
> `cmake --preset Release -DH723_WITH_SSH=OFF`. Остальное (в том числе TLS для Lua
> на Mbed TLS) — под MIT, BSD и Apache-2.0.

## Подключены как git submodule (`third_party/`)

Исходники не меняются. Настройки — в `examples/BoardH723/config/`, точечные
правки при сборке — через `patch_source` (`examples/BoardH723/cmake/patch_source.cmake`).

| Компонент | Версия | Лицензия | Где | Правки при сборке |
|---|---|---|---|---|
| [FreeRTOS-Kernel](https://github.com/FreeRTOS/FreeRTOS-Kernel) | V11.3.1 | MIT | `third_party/FreeRTOS-Kernel` | — |
| [FreeRTOS-Plus-TCP](https://github.com/FreeRTOS/FreeRTOS-Plus-TCP) | V4.4.1 | MIT | `third_party/FreeRTOS-Plus-TCP` | — |
| [lwmem](https://github.com/MaJerle/lwmem) | v2.2.4 | MIT | `third_party/lwmem` | — |
| [STM32 USB Device Library](https://github.com/STMicroelectronics/stm32-mw-usb-device) | v2.11.6 | SLA0044 (ST, только для устройств ST) | `third_party/stm32-mw-usb-device` | — |
| [Lua](https://github.com/lua/lua) | 5.4.9 | MIT | `third_party/lua` | — (настройки в `libs/lua/lua_port.h`) |
| [LuaSocket](https://github.com/lunarmodules/luasocket) | 3.1.0 | MIT | `third_party/luasocket` | `usocket.c`: неблокирующий `recv` |
| [NimBLE](https://github.com/apache/mynewt-nimble) | 1.10.0 | Apache-2.0 | `third_party/mynewt-nimble` | `npl_os_freertos.c`: `in_isr()` с учётом PRIMASK |
| [Mbed TLS](https://github.com/Mbed-TLS/mbedtls) | 3.6.7 LTS | Apache-2.0 | `third_party/mbedtls` | — (настройки в `config/mbedtls_board_config.h`; TLS для Lua и криптография NimBLE) |
| [wolfSSL](https://github.com/wolfSSL/wolfssl) (только wolfCrypt) | 5.9.4 | **GPLv3** | `third_party/wolfssl` | — (только при `H723_WITH_SSH`) |
| [wolfSSH](https://github.com/wolfSSL/wolfssh) | 1.5.0 | **GPLv3** | `third_party/wolfssh` | — (только при `H723_WITH_SSH`) |

## Лежат в репозитории

| Компонент | Версия | Лицензия | Где | Почему не submodule |
|---|---|---|---|---|
| [FatFs](http://elm-chan.org/fsw/ff/) | R0.16 | BSD-1 (ChaN) | `third_party/fatfs` | у FatFs нет официального git; файлы без изменений |
| [LuaFileSystem](https://github.com/lunarmodules/luafilesystem) | 1.8.0 | MIT | `examples/BoardH723/libs/lua/lfs` | переписан под FatFs |
| [lua-cjson](https://github.com/mpx/lua-cjson) | 2.1devel | MIT | `examples/BoardH723/libs/lua/luajson` | доработан под SDRAM |
| Порт NimBLE: `syscfg.h`, `bleuart` | на основе 1.8 | Apache-2.0 | `examples/BoardH723/libs/esp/nimble_port` | наш конфиг и сервис |
| STM32H7 HAL, CMSIS | HAL 1.11.5, CMSIS Device 1.10.6 | BSD-3-Clause / Apache-2.0 | `examples/*/Drivers` | копирует STM32CubeMX при генерации кода |
| FreeRTOS (CubeMX) | V10.3.1 | MIT | `examples/ESP-Loader/Middlewares` | копирует STM32CubeMX |
| STM32 USB Device (CubeMX) | — | SLA0044 | `examples/ESP-Loader/Middlewares` | копирует STM32CubeMX |

Конфиги `config/wolfSSL.I-CUBE-wolfSSL_conf.h` и `config/wolfSSL.I-CUBE-wolfSSH_conf.h`
созданы пакетами ST I-CUBE-wolfSSL/I-CUBE-wolfSSH, `config/myFilesystem.h` — на
основе шаблона wolfSSH (GPLv3); используются только с `H723_WITH_SSH`.

Lua-модули на SD-карте (`examples/BoardH723/app/SD/lib`) — из LuaSocket и
LuaSec 1.3.2 (MIT). C-часть LuaSec заменена своей на Mbed TLS
(`examples/BoardH723/libs/lua/ssl`), Lua-часть (`ssl.lua`, `ssl/https.lua`) — без изменений,
кроме комментария о проверке сертификатов. Корневые сертификаты
`app/SD/lib/ssl/certs/roots.pem` — из набора Mozilla (MPL-2.0).

## Свой код по мотивам

- `examples/BoardH723/libs/term` — терминал по мотивам [microrl](https://github.com/Helius/microrl).
