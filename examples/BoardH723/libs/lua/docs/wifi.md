# Документация по библиотеке Lua для управления Wi-Fi

## Описание

Библиотека `lwifi` предоставляет функции для управления модулем Wi-Fi, включая включение/выключение Wi-Fi, настройку параметров соединения, сканирование доступных сетей и управление режимами работы.

## Функции библиотеки

### enable
**Описание:** Включает или выключает модуль Wi-Fi.
- **Синтаксис:** `lwifi.enable(enable_flag)`
- **Аргументы:**
  - `enable_flag` (boolean): true для включения, false для отключения.
- **Возвращаемое значение:** boolean. true при успешном выполнении, nil и сообщение об ошибке при неудаче.

**Пример:**
```lua
local success, msg = lwifi.enable(true)
if not success then print(msg) end
```

### setMac
**Описание:** Устанавливает MAC-адрес для указанного интерфейса Wi-Fi.
- **Синтаксис:** `lwifi.setMac(interface, mac_address)`
- **Аргументы:**
  - `interface` (integer): Интерфейс Wi-Fi (ESP_STA или ESP_AP).
  - `mac_address` (string): MAC-адрес в формате "XX:XX:XX:XX:XX:XX".
- **Возвращаемое значение:** boolean. true при успешном выполнении, nil и сообщение об ошибке при неудаче.

**Пример:**
```lua
local success, msg = lwifi.setMac(WIFI_STA, "00:1A:2B:3C:4D:5E")
if not success then print(msg) end
```

### getMac
**Описание:** Получает MAC-адрес для указанного интерфейса Wi-Fi.
- **Синтаксис:** `lwifi.getMac(interface)`
- **Аргументы:**
  - `interface` (integer): Интерфейс Wi-Fi (ESP_STA или ESP_AP).
- **Возвращаемое значение:** string с MAC-адресом при успешном выполнении, nil и сообщение об ошибке при неудаче.

**Пример:**
```lua
local mac, msg = lwifi.getMac(WIFI_STA)
if not mac then print(msg) else print("MAC:", mac) end
```

### getMode
**Описание:** Получает текущий режим работы Wi-Fi.
- **Синтаксис:** `lwifi.getMode()`
- **Возвращаемое значение:** integer с кодом режима (WIFI_MODE_STA, WIFI_MODE_AP или WIFI_MODE_APSTA) при успешном выполнении, false и сообщение об ошибке при неудаче.

**Пример:**
```lua
local mode, msg = lwifi.getMode()
if not mode then print(msg) else print("Mode:", mode) end
```

### setMode
**Описание:** Устанавливает режим работы Wi-Fi.
- **Синтаксис:** `lwifi.setMode(mode)`
- **Аргументы:**
  - `mode` (integer): Режим работы Wi-Fi (WIFI_MODE_STA, WIFI_MODE_AP или WIFI_MODE_APSTA).
- **Возвращаемое значение:** boolean. true при успешном выполнении, false и сообщение об ошибке при неудаче.

**Пример:**
```lua
local success, msg = lwifi.setMode(WIFI_MODE_STA)
if not success then print(msg) end
```

### scan
**Описание:** Сканирует доступные Wi-Fi сети.
- **Синтаксис:** `lwifi.scan()`
- **Возвращаемое значение:** таблица с информацией о найденных сетях.

**Пример:**
```lua
local aps = lwifi.scan()
for i, ap in ipairs(aps) do
    print("SSID:", ap.ssid, "BSSID:", ap.bssid, "RSSI:", ap.rssi, "Channel:", ap.chnl)
end
```

### connect
**Описание:** Подключается к указанной Wi-Fi сети.
- **Синтаксис:** `lwifi.connect(ssid, password)`
- **Аргументы:**
  - `ssid` (string): SSID сети.
  - `password` (string, опционально): Пароль для подключения.
- **Возвращаемое значение:** boolean. true при успешном выполнении, false и сообщение об ошибке при неудаче.

**Пример:**
```lua
local success, msg = lwifi.connect("MyNetwork", "password123")
if not success then print(msg) end
```

### getAPConfig
**Описание:** Получает текущие настройки точки доступа.
- **Синтаксис:** `lwifi.getAPConfig()`
- **Возвращаемое значение:** таблица с параметрами точки доступа при успешном выполнении, nil при отсутствии конфигурации.

**Пример:**
```lua
local config = lwifi.getAPConfig()
if config then print("SSID:", config.ssid, "BSSID:", config.bssid, "Channel:", config.channel, "RSSI:", config.rssi) end
```

### connected
**Описание:** Проверяет подключен ли модуль к Wi-Fi сети.
- **Синтаксис:** `lwifi.connected()`
- **Возвращаемое значение:** boolean. true если подключено, false в противном случае.

**Пример:**
```lua
if lwifi.connected() then print("Connected") else print("Not connected") end
```

### disconnect
**Описание:** Отключает модуль от текущей Wi-Fi сети.
- **Синтаксис:** `lwifi.disconnect()`
- **Возвращаемое значение:** boolean. true при успешном выполнении, false и сообщение об ошибке при неудаче.

**Пример:**
```lua
local success, msg = lwifi.disconnect()
if not success then print(msg) end
```

### setPowerSaveMode
**Описание:** Устанавливает режим энергосбережения для Wi-Fi.
- **Синтаксис:** `lwifi.setPowerSaveMode(mode)`
- **Аргументы:**
  - `mode` (integer): Режим энергосбережения (WIFI_PS_NONE, WIFI_PS_MIN_MODEM или WIFI_PS_MAX_MODEM).
- **Возвращаемое значение:** boolean. true при успешном выполнении, false и сообщение об ошибке при неудаче.

**Пример:**
```lua
local success, msg = lwifi.setPowerSaveMode(WIFI_PS_MIN_MODEM)
if not success then print(msg) end
```

## Глобальные переменные

### WIFI_STA
Значение для интерфейса станции (клиент).

### WIFI_AP
Значение для интерфейса точки доступа.

### WIFI_MODE_STA
Режим работы как клиент.

### WIFI_MODE_AP
Режим работы как точка доступа.

### WIFI_MODE_APSTA
Режим работы как клиент и точка доступа одновременно.