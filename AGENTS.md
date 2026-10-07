# Доска-H723 — инструкции для ИИ-агента

Прошивка и примеры для отладочной платы «Доска-H723» (KitsLab): STM32H723ZGT6 550 МГц,
SDRAM 32 МБ, microSD, Wi-Fi/BLE на ESP32-C2, USB-C. Основной язык — C; Lua — дополнительный
интерпретатор на плате для быстрых опытов.

Железо (такты, память, занятые выводы и периферия, разъёмы J1/J2) — [docs/board.md](docs/board.md).
Прочитай его, прежде чем выбирать выводы, таймеры, DMA или место под буферы.

## Где что лежит

- `examples/BoardH723/` — основной проект (STM32CubeMX + CMake/Ninja, FreeRTOS).
  - `app/app.c` — точка входа приложения: `appTask` поднимает USB, SDRAM, сеть, ESP32-C2, SD, Lua, SSH.
  - `libs/` — код KitsLab: `esp` (драйвер ESP32-C2, BLE), `net` (сетевые интерфейсы Wi-Fi и USB-NCM),
    `disks` (FatFs: `/SD`, `/RAM`), `sdram` (`sdramMalloc`), `ssh`, `lua`, `dhcp`, `ntp`, `tls`, `sys` (POSIX-слой).
  - `config/` — настройки библиотек (FreeRTOSConfig.h, FreeRTOSIPConfig.h, ffconf.h, Mbed TLS, wolfSSL).
  - `cmake/patch_source.cmake` — точечные правки сторонних файлов при сборке.
  - `app/SD/` — содержимое SD-карты (`boot.lua`, библиотеки Lua, `utils/ESPLoader.bin`).
    Архив для карты — `python tools/make_sd_zip.py` (→ `build/sd-card.zip`, он же в релизах GitHub);
    распаковать в корень карты FAT32. На живую плату файлы можно залить по SFTP в `/SD/`.
  - `Core/` — код CubeMX (`main.c`, `*_it.c`, инициализация периферии), см. правила ниже.
- `examples/ESP-Loader/` — мост USB-UART для прошивки ESP32-C2 через STM32.
- `recipes/` — готовые проверенные модули на C (кнопка, UART+DMA, I2C-датчик, Wi-Fi+HTTP, SD, задачи
  FreeRTOS) с объяснением; подключаются опцией `-DH723_RECIPES="имя;имя"`, см. `recipes/README.md`.
  Начинай свою задачу с ближайшего рецепта.
- `tools/dfu_flash.py` — прошивка по USB без кнопок.
- `hardware/` — схема и плата в PDF.

**Не читай без нужды:** `examples/BoardH723/Drivers/` (HAL и CMSIS от ST), `third_party/`
(сторонние библиотеки — git submodule, их не правим), `build/`. Это сотни тысяч строк; нужное
место ищи поиском по имени функции.

## Сборка

Нужны [STM32CubeCLT](https://www.st.com/en/development-tools/stm32cubeclt.html) (компилятор
`arm-none-eabi-gcc`, CMake, Ninja, `STM32_Programmer_CLI` — всё должно быть в PATH), git и Python 3.

```bash
git submodule update --init          # один раз; на Windows до клонирования: git config --global core.longpaths true
cd examples/BoardH723
cmake --preset Release
cmake --build build/Release
```

На Windows клонируй в короткий путь (вроде `C:\src\Board-H723`): при длинном CMake
предупреждает о пределе 250 символов на путь объектного файла.

Результат — `build/Release/BoardH723.elf` / `.hex` / `.bin` (ELF с отладочной информацией).
Опции: `-DH723_WITH_SSH=OFF` убирает SSH/SFTP (wolfSSH под GPLv3 — с ним вся прошивка GPLv3),
`-DH723_WITH_TLS=OFF` убирает HTTPS для Lua, `-DH723_WITH_LUA=OFF` — Lua целиком (вместе с HTTPS
для него; прошивка с SSH — 482 КБ вместо 811 КБ, место под LVGL). `-DH723_IDLE_SLEEP=ON` —
сон ядра командой WFI, когда задачам нечего делать (кристалл в простое 51 °C вместо 66 °C; загрузка
в `top` считается по тактам ядра). Собирается GCC 13 (STM32CubeCLT)
и GCC 14 (Arm GNU Toolchain).
В нашем коде (`app/`, `libs/`, `Core/`, `recipes/`) неявное объявление функции, смешение указателя
с целым и `return` без значения — ошибки (`-Werror=...` в `CMakeLists.txt`): объявляй функции в
заголовках и подключай их. Предупреждения из `third_party/` — не наши, их не чиним.

## Прошивка

**По USB, без кнопок и отладчика** (плата подключена USB-C и в ней работает эта прошивка):

```bash
python tools/dfu_flash.py            # по умолчанию examples/BoardH723/build/Release/BoardH723.hex
```

Скрипт шлёт UDP-пакет `dfu` на 192.168.4.1:7123, плата перезагружается в системный загрузчик
STM32 (USB DFU), скрипт пишет прошивку через `STM32_Programmer_CLI`, запускает её и ждёт ping
(около 40 с). Вручную то же самое:

```bash
python -c "import socket; socket.socket(2,2).sendto(b'dfu', ('192.168.4.1', 7123))"
STM32_Programmer_CLI -c port=usb1 -w build/Release/BoardH723.hex -s 0x08000000
```

Если плата не отвечает (нет USB-сети, прошивка сломана или старая): попроси человека зажать
кнопку BOOT, нажать RESET (или переподключить USB) и отпустить BOOT — плата войдёт в DFU, затем
снова запусти скрипт. На Windows для DFU нужен драйвер STM32 DFU (ставится со STM32CubeProgrammer;
устройство в диспетчере — «STM32 BOOTLOADER»).

**Через отладчик «Лобзик»** (SWD-разъём J6, заодно даёт лог USART1): навык для агентов —
[github.com/Kits-Lab-Dev/lobzik-skill](https://github.com/Kits-Lab-Dev/lobzik-skill). С ним доступны
чтение памяти и регистров на живой плате и разбор HardFault.

**Модуль ESP32-C2** (Wi-Fi/BLE). Его прошивка — проект
[esp32c2phy](https://github.com/Kits-Lab-Dev/esp32c2phy), собирается ESP-IDF 5.4 (на 5.5 нет
`ble_hci_trans.h`). Прошивается через STM32: `reboot esploader` в SSH, `rtos.reboot("esploader")`
в Lua или UDP-пакет `esploader` на 192.168.4.1:7123 (работает и без SSH и Lua) — плата запускает
мост ESP-Loader с `/SD/utils/ESPLoader.bin`, переводит ESP32-C2 в загрузчик, и на ПК появляется
USB-UART (VID 0483, PID 5740). Дальше esptool:

```bash
cd esp32c2phy/build
python -m esptool --chip esp32c2 -p COM5 -b 921600 --after no-reset-stub write-flash \
  --flash-mode dio --flash-freq 60m --flash-size 2MB \
  0x0 bootloader/bootloader.bin 0x8000 partition_table/partition-table.bin 0x10000 esp32phyInterface.bin
```

Загрузчик ESP включается один раз при запуске моста, DTR/RTS мост не передаёт: на каждый сеанс
esptool — заново RESET платы и `reboot esploader`. 921600 бод — запись 650 КБ за 10 с, `read-flash`
всех 2 МБ за 30 с (2 Мбод тоже работает). После прошивки — RESET: основная прошивка перезапускает
ESP. Проверка: в логе `Loaded app from partition at offset 0x10000`, Wi-Fi STA получает адрес, плата
видна по BLE как `Mynewt_BLEuart`. Скорость Wi-Fi — рецепт `recipes/tcp_speed`.

## Проверка на плате

Проверяй результат на плате, а не «должно работать»:

- `ping 192.168.4.1` — плата в USB-сети (ПК получает 192.168.4.2 по DHCP).
- Лог — USART1 115200 8N1: PB14 (TX) / PB15 (RX), выведены на J6 (VCP Лобзика) и J2.
  Макросы лога — `INF`, `WRN`, `ERR` из `app/log.h`.
- SSH/SFTP: `ssh root@192.168.4.1`. Пароль — первая строка `/SD/.ssh/password`; файла нет — плата
  при загрузке создаёт случайный, сохраняет туда и печатает в лог (`SSH: new password for root: ...`).
  Сменить — записать свой в этот файл (или удалить файл) и перезагрузить. Если на SD есть
  `/SD/.ssh/mcu.key.pub` — вход только по этому ключу, пароль выключен. Без карты войти нельзя.
  Пароль не знаешь и лога нет — спроси человека, он прочитает файл с карты. Команды оболочки: `help`, `ls`, `cat`, `top`,
  `ip`, `reboot`, `reboot dfu`, `reboot esploader`, `lua -f /SD/x.lua`. Оболочка режет аргументы по пробелам и не понимает
  кавычки — Lua запускай файлом. В Git Bash аргументы вида `/SD/x` превращаются в пути Windows —
  для SFTP и SSH из скриптов ставь `MSYS_NO_PATHCONV=1`.

## Правила

- **CubeMX.** В сгенерированных файлах (`Core/`, `BoardH723.cmake`, `cmake/stm32cubemx/`) правь только
  внутри блоков `/* USER CODE BEGIN ... */ ... /* USER CODE END ... */` — остальное перезапишется при
  перегенерации. Такты, пины, MPU, DMA и периферию меняй в `BoardH723.ioc` (STM32CubeMX) и
  перегенерируй. MPU-регион SDRAM должен остаться **не Shareable**.
- `MX_SDMMC2_SD_Init` не вызывается намеренно — SD настраивает `libs/disks/sddisk.c`.
- Выводы SDRAM, microSD, ESP32-C2 и USB разведены на плате — не переназначай их (см. docs/board.md).
- Внутренние ОЗУ почти заняты: большие буферы бери из SDRAM (`sdramMalloc`) или кучи FreeRTOS
  (`pvPortMalloc`; `malloc` уже переопределён на неё).
- Сторонние библиотеки не правим и не копируем к себе: настройки — в `config/`, неизбежные правки —
  в `cmake/patch_source.cmake`.
- Свой модуль — проще всего как рецепт (`recipes/<имя>/`, см. `recipes/README.md`). Иначе — в `app/` или `libs/<имя>/`; файлы добавляй в `target_sources` и
  `board_includes` в `examples/BoardH723/CMakeLists.txt`. Новые задачи FreeRTOS создавай из `appTask`
  после инициализации нужных подсистем.
- Лицензия: код KitsLab — MIT; сторонние компоненты — свои лицензии, см. `THIRD_PARTY.md`.
