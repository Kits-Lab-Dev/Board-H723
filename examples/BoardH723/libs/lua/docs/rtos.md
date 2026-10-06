# Модуль RTOS

Модуль RTOS предоставляет функции для работы с операционной системой реального времени FreeRTOS и системными ресурсами.

## Функции

### freeMemory()
```lua
mem = rtos.freeMemory()
```
Возвращает количество свободной памяти в SDRAM в байтах.

### freeRTOSMemory()
```lua
mem = rtos.freeRTOSMemory()
```
Возвращает количество свободной памяти в куче FreeRTOS в байтах.

### usageCpu()
```lua
usage = rtos.usageCpu()
```
Возвращает процент использования CPU (0-100).

### totalMemory()
```lua
total = rtos.totalMemory()
```
Возвращает общий размер SDRAM в байтах.

### usageMemory()
```lua
used = rtos.usageMemory()
```
Возвращает количество используемой памяти в SDRAM в байтах.

### temperature()
```lua
temp = rtos.temperature()
```
Возвращает текущую температуру системы в градусах Цельсия.

### time()
```lua
datetime = rtos.time()
```
Возвращает текущую дату и время в формате "DD-MM-YYThh:mm:ss".

### setTime(datetime)
```lua
success = rtos.setTime("DD-MM-YYThh:mm:ss")
```
Устанавливает системное время. Возвращает `true` при успехе, `false` при ошибке.

### voltage()
```lua
v, vbat = rtos.voltage()
```
Возвращает напряжение питания системы и батареи (в настоящее время не реализовано).

### reboot([mode])
```lua
rtos.reboot()
rtos.reboot("dfu")
```
Перезагружает систему. С `"dfu"` — в системный загрузчик STM32 (USB DFU) для прошивки через `STM32_Programmer_CLI -c port=usb1`,
с `"esploader"` — в ESP-Loader (`/SD/utils/ESPLoader.bin`) для прошивки ESP32-C2 по USB.

### read()
```lua
data = rtos.read([format])
```
Читает данные из консоли. Поддерживает следующие форматы:
- `"n"` - число
- `"l"` - строка без символа новой строки
- `"L"` - строка с символом новой строки
- `"a"` - все данные
- число - указанное количество байт

### write()
```lua
rtos.write(data)
```
Записывает данные в консоль (в настоящее время не реализовано).

## Примеры использования

```lua
-- Получение системной информации
local free_mem = rtos.freeMemory()
local cpu_usage = rtos.usageCpu()
local temp = rtos.temperature()

-- Работа с датой и временем
local current_time = rtos.time()
rtos.setTime("01-01-24T12:00:00")

-- Перезагрузка системы
rtos.reboot()

-- Работа с консолью
-- Чтение строки
local line = rtos.read("l")  -- читает строку без символа новой строки
local line_with_nl = rtos.read("L")  -- читает строку с символом новой строки

-- Чтение числа
local number = rtos.read("n")

-- Чтение определенного количества байт
local bytes = rtos.read(10)  -- читает 10 байт

-- Чтение всех данных
local all_data = rtos.read("a")

-- Запись в консоль
rtos.write("Привет, мир!\n")
rtos.write("Текущая температура: " .. rtos.temperature() .. "°C\n")
``` 