# Board-H723

Проект для отладочной платы `Доска-H723` на базе микроконтроллера STM32H723.

## Описание

Этот проект представляет собой базовую конфигурацию для разработки на отладочной плате с микроконтроллером STM32H723. Проект включает в себя все необходимые файлы для сборки и отладки.

## Структура проекта

- `app/` - Исходный код приложения
- `config/` - Настройки библиотек (FreeRTOS, FreeRTOS+TCP, FatFs, lwmem, Mbed TLS, wolfCrypt, wolfSSH)
- `libs/` - Наши библиотеки
- `cmake/patch_source.cmake` - Точечные правки сторонних файлов при сборке
- `Core/`, `Drivers/` - Файлы сгенерированые STM32CubeMX
- `../../third_party/` - Сторонние библиотеки (git submodule), см. [THIRD_PARTY.md](../../THIRD_PARTY.md)

## Требования

- [STM32CubeCLT 1.18.0](https://www.st.com/en/development-tools/stm32cubeclt.html) ([без VPN](https://disk.yandex.ru/d/Fjw2cZVs_fMvog))
- [STM32CubeMX](https://www.st.com/en/development-tools/stm32cubemx.html)
- [VSCode](https://code.visualstudio.com/)
- Расширение [CMake Tools](https://marketplace.visualstudio.com/items?itemName=ms-vscode.cmake-tools) для VScode

## Состав проекта

- `libs/esp` Драйвер для [esp32c2phy](https://github.com/Kits-Lab-Dev/esp32c2phy). Управление WiFi. BLE - стек [NimBLE](https://github.com/apache/mynewt-nimble) (`third_party/mynewt-nimble`, наш конфиг — `libs/esp/nimble_port`).
- `libs/net` Сетевой драйвер FreeRTOS+TCP для Wi-Fi (ESP32-C2) и USB-NCM.
- [FreeRTOS](https://github.com/FreeRTOS/FreeRTOS-Kernel) ОС реального времени (`third_party/FreeRTOS-Kernel`).
- [FreeRTOS-Plus-TCP](https://github.com/FreeRTOS/FreeRTOS-Plus-TCP) TCP/IP стек (`third_party/FreeRTOS-Plus-TCP`).
- `libs/dhcp` DHCP сервер, выдает IP адреса клиентам WiFi AP и устройствам подключенным по USB-NCM
- `libs/disks` Библиотеки для работы с файловой системой (uSD-карта и Ram-Диск). Обертки для функций стандартных библиотек
- [FatFs](http://elm-chan.org/fsw/ff/) Библиотека для работы с файловой системой FAT (`third_party/fatfs`)
- `libs/sdram` Библиотека для работы с микросхемой памяти SDRAM, включает в себя менеджер памяти [lwmem](https://github.com/MaJerle/lwmem)
- `libs/ssh` SSH - сервер с поддержкой SFTP на основе [wolfSSH](https://github.com/wolfSSL/wolfssh) и wolfCrypt из [wolfSSL](https://github.com/wolfSSL/wolfssl) (GPLv3, опция `H723_WITH_SSH`)
- `libs/tls` Порт [Mbed TLS](https://github.com/Mbed-TLS/mbedtls) на плату: аппаратный RNG, время RTC, общий генератор случайных чисел
- `libs/term` Терминал разработанный по мотивам [microrl](https://github.com/Helius/microrl)
- `libs/lua` Интерпретатор скриптового языка [Lua](https://www.lua.org/)
  - `libs/lua/pio` GPIO для Lua. [pio doc](libs/lua/docs/pio.md)
  - `libs/lua/serial` Serial для Lua. [Serial doc](libs/lua/docs/serial.md)
  - `libs/lua/spi` SPI для Lua. [SPI doc](libs/lua/docs/spi.md)
  - `libs/lua/wire` I2C для Lua. [I2C doc](libs/lua/docs/i2c.md)
  - `libs/lua/lthread` Библиотека многопоточности для Lua. [Lua Thread doc](libs/lua/docs/lthread.md)
  - `libs/lua/wifi` Библиотека управления WiFi для Lua. [Lua Thread doc](libs/lua/docs/wifi.md)
  - `libs/lua/rtos` Библиотека для работы с FreeRTOS и системными ресурсами. [Lua RTOS doc](libs/lua/docs/rtos.md)
  - [LuaSocket](https://lunarmodules.github.io/luasocket/) Библиотека для работы с сокетами (`third_party/luasocket`)
  - `ssl` — HTTPS-клиент на Mbed TLS с интерфейсом [LuaSec](https://github.com/lunarmodules/luasec/wiki) (`libs/lua/ssl`, опция `H723_WITH_TLS`): TLS 1.2, SNI, ALPN, проверка сертификата сервера (`verify = "peer"`, `cafile = "/SD/lib/ssl/certs/roots.pem"`)

## SSH и SFTP

К плате можно подключиться по SSH и SFTP используя USB-NCM (USB-Ethernet) или Wi-Fi.
Для первого подключения можно использовать вход по паролю, а затем настроить вход по ключу. Пароль по умолчанию для SSH указан в [CMakeLists.txt](/CMakeLists.txt) проекта. Если на SD-карте `(/SD/.ssh/mcu.key.pub)` расположен исправный ключ, то вход по паролю будет отключен.

### Шаги для генерации пары ключей

1. **Откройте терминал**:
   Запустите команду или окно терминала, в котором вы сможете выполнять команды OpenSSH.
2. **Используйте команду ssh-keygen**:
   Выполните следующую команду в вашем терминале:

   ```bash
   ssh-keygen -t rsa -b 4096
   ```

   В этой команде:
   - `-t rsa` указывает тип ключа (RSA).
   - `-b 4096` задает длину ключа в битах.
3. **Укажите путь для сохранения ключей**:
   По умолчанию SSH будет сохранять ключи в папке `~/.ssh/`, но вы можете указать другой путь:

   ```bash
   Enter file in which to save the key (/home/your_username/.ssh/id_rsa): [Press enter]
   ```
4. **Установите парольную фразу (опционально)**:
   Ввод парольной фразы добавляет дополнительный уровень безопасности к вашему ключу, так как она потребуется при каждом использовании ключа.

   ```bash
   Enter passphrase (empty for no passphrase): [Type a passphrase]
   Enter same passphrase again: [Repeat the passphrase]
   ```
5. **Подтверждение создания ключей**:
   После выполнения этих шагов, SSH создаст пару ключей в указанной папке.

### Использование ключей

1. **Публичный ключ**:
   Содержимое файла `mcu.key.pub` (или другого указанного вами имени) является публичным ключом, который можно добавить на удаленный сервер для аутентификации.
   Скопируйте файл в `/SD/.ssh/mcu.key.pub` на sd-карте платы.
2. **Приватный ключ**:
   Приватный ключ (обычно `mcu.key`) должен храниться в безопасности на вашем локальном компьютере и никогда не передаваться кому-либо.

### Проверка подключения

Теперь вы можете попробовать подключиться к плате, используя команду:

```bash
ssh root@<IP-адрес> -i ~/.ssh/mcu.key
```

## Команды терминала

В SSH-терминале доступны следующие команды:

### `lua <script>` или `lua -f <script.lua>`

- **Описание**: Запуск Lua-скриптов.
- **Синтаксис**:
  - `lua <script>` — выполнение строки скрипта.
  - `lua -f <script.lua>` — загрузка скрипта из файла.
- **Примечание**: Требует наличия Lua-библиотеки.

### `ls [-l] [dir]`

- **Описание**: Вывод содержимого директории.
- **Синтаксис**:
  - `ls [dir]` — список файлов в текущей директории.
  - `ls -l [dir]` — расширенный вывод (не реализован).
- **Примечание**: Цветовая индикация: синий — директории, зеленый — исполняемые файлы.

### `reboot`

- **Описание**: Перезагрузка системы.
- **Синтаксис**: `reboot [dfu|esploader]`
- **Примечание**: `reboot` — сброс через NVIC; `reboot dfu` — в системный загрузчик USB DFU
  (прошивка через `STM32_Programmer_CLI -c port=usb1`, см. `tools/dfu_flash.py`);
  `reboot esploader` — запуск `/SD/utils/ESPLoader.bin` для прошивки ESP32-C2.

### `exit`

- **Описание**: Завершение SSH-сессии.
- **Синтаксис**: `exit`

### `help`

- **Описание**: Вывод списка доступных команд.
- **Синтаксис**: `help`

### `cd <dir>`

- **Описание**: Смена текущей директории.
- **Синтаксис**:
  - `cd <dir>` — переход в указанную директорию.
  - `cd ..` — возврат в родительскую директорию.

### `pwd`

- **Описание**: Вывод текущей директории.
- **Синтаксис**: `pwd`

### `mkdir <dir>`

- **Описание**: Создание директории.
- **Синтаксис**: `mkdir <directory>`
- **Примечание**: Создает директорию в текущем пути или абсолютном пути.

### `rm <file>`

- **Описание**: Удаление файла.
- **Синтаксис**: `rm <file>`
- **Примечание**: Нет поддержки рекурсивного удаления директорий.

### `cp <src> <dst>`

- **Описание**: Копирование файла.
- **Синтаксис**: `cp <source> <destination>`

### `cat <file>`

- **Описание**: Вывод содержимого файла.
- **Синтаксис**: `cat <file>`

### `echo [text]`

- **Описание**: Вывод текста.
- **Синтаксис**: `echo [text]`
- **Пример**: `echo Hello World`

### `uname [-a]`

- **Описание**: Вывод информации о системе.
- **Синтаксис**:
  - `uname` — краткая информация.
  - `uname -a` — расширенная информация.

### `touch <file>`

- **Описание**: Создание пустого файла.
- **Синтаксис**: `touch <file>`

### `top`

- **Описание**: Вывод статистики ресурсов системы.
- **Синтаксис**: `top`
- **Примечание**: Нажатие `Ctrl+C` останавливает отображение.

### `initsddisk`

- **Описание**: Инициализация SD-карты.
- **Синтаксис**: `initsddisk`
- **Примечание**: Требует подтверждения (`y/n`) для форматирования.

### `ip`

- **Описание**: Вывод сетевых настроек.
- **Синтаксис**: `ip`
- **Примечание**: Показывает IP-адреса для USB-NCM, WiFi-STA и WiFi-AP.