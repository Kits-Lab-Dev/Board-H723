# Документация по библиотеке lthread для Lua

## Описание

Библиотека `lthread` предоставляет функциональность многопоточности и работы с примитивами синхронизации (семафоры и мьютексы) в среде выполнения Lua, интегрированной с FreeRTOS.

## Функции

### start
Запускает новый поток выполнения Lua.
- Параметры:
  - `function`: функция, которую нужно запустить в новом потоке.
  - `stackSize` (опционально): размер стека нового потока. По умолчанию 1024 байт.
  - `priority` (опционально): приоритет нового потока. По умолчанию минимальный приоритет + 1.
- Возвращает: идентификатор потока или 0 в случае ошибки.

Пример:
```lua
local lthread = require("lthread")
local function myTask()
    print("Задача запущена!")
end
local threadId = lthread.start(myTask, 2048, 5)
```

### delay
Приостанавливает выполнение текущего потока на заданное количество миллисекунд.
- Параметры:
  - `ms`: время ожидания в миллисекундах.

Пример:
```lua
local lthread = require("lthread")
lthread.delay(1000) -- Задержка на 1 секунду
```

### semaphore
Создает новый семафор.
- Параметры:
  - `count`: начальное количество семафоров.
  - `maxCount`: максимальное количество семафоров.
- Возвращает: идентификатор семафора или nil в случае ошибки.

Пример:
```lua
local lthread = require("lthread")
local sem = lthread.semaphore(1, 1) -- Бинарный семафор
```

### wait
Ожидает доступ к семафору.
- Параметры:
  - `sem`: идентификатор семафора.
  - `timeout` (опционально): время ожидания в тиках таймера. По умолчанию бесконечно.

Пример:
```lua
local lthread = require("lthread")
if lthread.wait(sem, 100) then
    print("Доступ к семафору получен!")
end
```

### release
Освобождает семафор.
- Параметры:
  - `sem`: идентификатор семафора.

Пример:
```lua
local lthread = require("lthread")
lthread.release(sem)
```

### deleteSemaphore
Удаляет семафор.
- Параметры:
  - `sem`: идентификатор семафора.

Пример:
```lua
local lthread = require("lthread")
lthread.deleteSemaphore(sem)
```

### mutex
Создает новый мьютекс.
- Возвращает: идентификатор мьютекса или nil в случае ошибки.

Пример:
```lua
local lthread = require("lthread")
local mtx = lthread.mutex()
```

### lock
Захватывает мьютекс.
- Параметры:
  - `mtx`: идентификатор мьютекса.
  - `timeout` (опционально): время ожидания в тиках таймера. По умолчанию бесконечно.

Пример:
```lua
local lthread = require("lthread")
if lthread.lock(mtx, 100) then
    print("Мьютекс захвачен!")
end
```

### unlock
Освобождает мьютекс.
- Параметры:
  - `mtx`: идентификатор мьютекса.

Пример:
```lua
local lthread = require("lthread")
lthread.unlock(mtx)
```

### deleteMutex
Удаляет мьютекс.
- Параметры:
  - `mtx`: идентификатор мьютекса.

Пример:
```lua
local lthread = require("lthread")
lthread.deleteMutex(mtx)
```