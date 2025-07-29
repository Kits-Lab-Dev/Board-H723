# Документация по библиотеке SPI для Lua

## Описание

Библиотека `spi` позволяет управлять модулями SPI (Serial Peripheral Interface) в микроконтроллере на базе STM32H7xx. Она предоставляет функции для инициализации, отправки, приема данных по SPI, а также завершения работы с этим интерфейсом.

## Поддерживаемые модули SPI

- **SPI1**
- **SPI3**

### Пины для подключения к SPI

| Модуль | MOSI (Master Out Slave In) | MISO (Master In Slave Out) | SCK (Serial Clock) | NSS (Slave Select) | CS2  | CS3  | CS4  |
|--------|----------------------------|----------------------------|--------------------|------------------|------|------|------|
| SPI1   | GPIOA.7                    | GPIOA.6                    | GPIOA.5            | GPIOA.4          | GPIOA.3 | GPIOA.2 | GPIOA.1 |
| SPI3   | GPIOC.12                   | GPIOC.11                   | GPIOC.10           | GPIOA.15         | GPIOC.10 | GPIOA.9  | GPIOA.8 |

## Функции

### Инициализация SPI
```lua
SPI:begin(csCount, mode, speed)
```
- **csCount** (опционально): количество линий выбора устройства (CS). По умолчанию 1.
- **mode** (опционально): режим работы SPI (0, 1, 2 или 3). По умолчанию 0.
- **speed** (опционально): скорость передачи данных. Может быть одной из предопределённых констант `SPI62MHZ`, `SPI31MHZ` и т.д. По умолчанию `SPI_1_875MHZ`.

Пример:
```lua
SPI:begin(1, SPI_MODE0, SPI_468KHZ)
```

### Отправка данных по SPI
```lua
SPI:write(data, cs)
```
- **data**: данные для отправки (строка или таблица байт).
- **cs** (опционально): номер линии выбора устройства. По умолчанию 1.

Пример:
```lua
local out_data = {0x51, 0x52, 0x53}
SPI:write(out_data)
```

### Получение данных по SPI
```lua
SPI:read(length, cs)
```
- **length**: количество байт для приема.
- **cs** (опционально): номер линии выбора устройства. По умолчанию 1.

Пример:
```lua
local in_data = SPI:read(3)
for i = 1, #in_data do
    print(in_data[i])
end
```

### Передача-прием данных через SPI
```lua
SPI:transfer(data, cs)
```
- **data**: данные для отправки (строка или таблица байт).
- **cs** (опционально): номер линии выбора устройства. По умолчанию 1.

Пример:
```lua
local out_data = {0x51, 0x52, 0x53}
local in_data = SPI:transfer(out_data)
for i = 1, #in_data do
    print(in_data[i])
end
```

### Завершение работы SPI
```lua
SPI:end()
```
Завершает работу с модулем SPI и освобождает ресурсы.

Пример:
```lua
SPI:end()
```

## Константы

- **Скорости передачи данных:**
  - `SPI62MHZ`
  - `SPI31MHZ`
  - `SPI15_5MHZ`
  - `SPI7_5MHZ`
  - `SPI3_75MHZ`
  - `SPI1_875MHZ`
  - `SPI937KHZ`
  - `SPI468KHZ`

- **Режимы работы:**
  - `SPI_MODE0`
  - `SPI_MODE1`
  - `SPI_MODE2`
  - `SPI_MODE3`

## Пример использования

```lua
-- Инициализация SPI с одной линией выбора устройства, режимом 0 и скоростью 468 кГц
SPI:begin(1, SPI_MODE0, SPI_468KHZ)

-- Отправка данных
local out_data = {0x51, 0x52, 0x53}
SPI:write(out_data)

-- Прием данных (например, 3 байта)
local in_data = SPI:read(3)
for i = 1, #in_data do
    print(in_data[i])
end

-- Передача-прием данных
out_data = {0x54, 0x55, 0x56}
in_data = SPI:transfer(out_data)
for i = 1, #in_data do
    print(in_data[i])
end

-- Завершение работы с SPI
SPI:end()
```