# Board-H723 Project

## Описание

Репозиторий содержит базовую конфигурацию для разработки на отладочной плате `Доска-H723` на базе микроконтроллера STM32H723. Включает в себя проекты для работы с STM32H723 и ESP32С2, файловыми системами, SSH/SFTP, терминалом и другими компонентами.

## Получение исходников

Сторонние библиотеки подключены как git submodule, поэтому после клонирования их нужно скачать:

```bash
git clone https://github.com/Kits-Lab-Dev/Board-H723.git
cd Board-H723
git submodule update --init
```

На Windows перед клонированием включите длинные пути, иначе часть файлов wolfSSL не создастся:

```bash
git config --global core.longpaths true
```

Флаг `--recursive` не нужен: вложенные submodule библиотек (тесты, порты для других платформ) для сборки не используются.

## Структура директорий

- `third_party/` - Сторонние библиотеки (git submodule, без изменений) и FatFs, см. [THIRD_PARTY.md](THIRD_PARTY.md)
- `examples/BoardH723/` - Основной проект для Board-H723
  - `app/` - Исходный код приложения
  - `config/` - Настройки библиотек (FreeRTOSConfig.h, FreeRTOSIPConfig.h, ffconf.h, Mbed TLS, wolfSSL/wolfSSH и др.)
  - `libs/` - Наши библиотеки (ESP, сеть, SSH, Lua-модули, диски и др.)
  - `cmake/patch_source.cmake` - Точечные правки сторонних файлов при сборке (оригиналы не меняются)
  - `Core/`, `Drivers/` - Сгенерированные файлы STM32CubeMX
- `examples/ESP-Loader/` - Загрузчик прошивки для ESP32
- `hardware/` - Схемы и PCB платы (pcb.pdf, sch.pdf)

## Сборка

```bash
cd examples/BoardH723
cmake --preset Release
cmake --build build/Release
```

SSH/SFTP-сервер построен на wolfSSH и wolfCrypt, которые распространяются под **GPLv3**.
По умолчанию он включён, и прошивка целиком распространяется под GPLv3. Для закрытого проекта его можно выключить
(TLS для Lua — на Mbed TLS под Apache-2.0 и остаётся):

```bash
cmake --preset Release -DH723_WITH_SSH=OFF
```

### 1. examples/**BoardH723**

- FreeRTOS - многопоточная операционная система реального времени
- Драйвер SPI для работы с ESP32 (WiFi и BLE). Проект для [ESP32-C2](https://github.com/Kits-Lab-Dev/esp32c2phy)
  - Скорость передачи данных по SPI до 60 Мбит/с
  - DMA для передачи данных по SPI
  - Интеграция с TCP/IP стеком (FreeRTOS-Plus-TCP)
  - Совместимость с Unix сокетами
  - Поддержка одновременной работы WiFi(AP, STA, AP+STA) и BLE
- Работа с SD-картой и RAM-диском (FatFs)
- Поддержка SSH с возможностью входа по ключу
- SFTP с доступом к файловой системе (SD-карта, RAM-диск)
- Интерпретатор Lua с поддержкой:
  - GPIO, Serial, SPI, I2C
  - Многопоточность (LThread)
  - Сетевые сокеты (LuaSocket) и HTTPS (модуль `ssl` с интерфейсом LuaSec на Mbed TLS)
  - Файловая система (FatFs)

### 2. examples/**ESP-Loader**

- Загрузка прошивки в ESP32
- Файл `ESPLoader.bin` может храниться на SD-карте

## Что потребуется

- [STM32CubeCLT 1.18.0](https://www.st.com/en/development-tools/stm32cubeclt.html) ([без VPN](https://disk.yandex.ru/d/Fjw2cZVs_fMvog))
- [STM32CubeMX](https://www.st.com/en/development-tools/stm32cubemx.html)
- [VSCode](https://code.visualstudio.com/)
- Расширение [CMake Tools](https://marketplace.visualstudio.com/items?itemName=ms-vscode.cmake-tools)
- Для SSH: генерация ключей через `ssh-keygen`

## Прошивка

По USB без кнопок (плата подключена USB-C, в ней работает эта прошивка; нужен `STM32_Programmer_CLI` из STM32CubeCLT):

```bash
python tools/dfu_flash.py
```

Скрипт переводит плату в USB DFU UDP-пакетом и прошивает `examples/BoardH723/build/Release/BoardH723.hex`.
Если плата не отвечает — зажмите BOOT, нажмите RESET, отпустите BOOT и запустите скрипт снова.

Описание железа для прошивки (память, такты, занятые выводы, разъёмы) — [docs/board.md](docs/board.md),
инструкции для ИИ-агентов — [AGENTS.md](AGENTS.md).

## Как использовать

1. Подключите плату через USB или Wi-Fi
2. Используйте SSH для доступа (пароль по умолчанию в `CMakeLists.txt`)
3. Загружайте скрипты Lua через терминал или SFTP на SD-карту
4. Используйте команды в терминале (ls, cd, lua, reboot и др.)

## Дополнительная информация

- `hardware/pcb.pdf` - Схема платы
- `hardware/sch.pdf` - Эскиз платы

Дополнительную информацию см. в README подкаталогов.

## Лицензия

Код KitsLab — MIT, см. [LICENSE](LICENSE). Сторонние компоненты сохраняют свои лицензии, см. [THIRD_PARTY.md](THIRD_PARTY.md).