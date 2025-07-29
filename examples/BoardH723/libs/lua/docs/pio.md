# Документация по библиотеке GPIO для Lua

Эта библиотека предоставляет функции для работы с GPIO (General Purpose Input Output) микроконтроллера, используя HAL API. Она позволяет управлять пинами в режимах ввода/вывода, а также работать с аналоговым сигналом и прерываниями.

## Использование библиотеки

Для использования библиотеки вам нужно ее загрузить в ваш проект и зарегистрировать функции в Lua. Пример регистрации:

```lua
require("gpio")
```

### Пины

В библиотеке определены константы для пинов микроконтроллера. Ниже приведен список доступных пинов:

- `PG3`, `PG6`, `PG7`, `PC6`, `PC7`, `PC8`, `PC9`, `PA8`, `PA9`, `PA10`, `PA15`, `PC10`, `PC11`, `PC12`, `PD2`, `PD3`, `PG13`, `PB5`, `PB7`, `PE4`, `PE5`, `PE6`
- `PC1`, `PA1`, `PA2`, `PA3`, `PA4`, `PA5`, `PA6`, `PA7`, `PC4`, `PC5`, `PB0`, `PB1`, `PB2`, `PB10`, `PB11`, `PB12`, `PB13`, `PB14`, `PB15`, `PD11`, `PD12`, `PD13`

### Функции

#### pinMode(pin, mode)

Устанавливает режим работы пина.

**Параметры:**
- `pin` — номер пина.
- `mode` — режим работы (INPUT, OUTPUT, INPUT_PULLUP, INPUT_PULLDOWN, OUTPUT_OPENDRAIN).

**Пример:**

```lua
pinMode(PC13, OUTPUT)
```

#### digitalWrite(pin, value)

Устанавливает значение на выходном пине.

**Параметры:**
- `pin` — номер пина.
- `value` — значение (HIGH или LOW).

**Пример:**

```lua
digitalWrite(PC13, HIGH)
```

#### digitalRead(pin)

Читает значение с входного пина.

**Параметры:**
- `pin` — номер пина.

**Возвращаемое значение:**  
- `value` — значение (HIGH или LOW).

**Пример:**

```lua
local value = digitalRead(PE4)
```

#### togglePin(pin)

Переключает состояние выходного пина.

**Параметры:**
- `pin` — номер пина.

**Пример:**

```lua
togglePin(PC13)
```

#### analogRead(pin)

Читает аналоговое значение с пина.

**Параметры:**
- `pin` — номер пина.

**Возвращаемое значение:**  
- `value` — аналоговое значение (от 0 до 65535).

**Пример:**

```lua
local value = analogRead(PC4)
```

#### analogWrite(pin, value)

Устанавливает аналоговое значение на выходном пине.

**Параметры:**
- `pin` — номер пина.
- `value` — аналоговое значение (от 0 до PWM_RESOLUTION - 1).

**Пример:**

```lua
analogWrite(PC7, 32768) -- Устанавливает значение на середине диапазона
```

#### attachInterrupt(pin, callback, mode)

Подключает функцию обратного вызова к прерыванию.

**Параметры:**
- `pin` — номер пина.
- `callback` — функция обратного вызова.
- `mode` — режим прерывания (RISING, FALLING, CHANGE, RISING_PULLDOWN, FALLING_PULLUP).

**Пример:**

```lua
function myCallback()
    print("Interrupt triggered!")
end

attachInterrupt(PC13, myCallback, RISING)
```

#### detachInterrupt(pin)

Отключает прерывание.

**Параметры:**
- `pin` — номер пина.

**Пример:**

```lua
detachInterrupt(PC13)
```

### Пример использования

Вот простой пример, демонстрирующий использование библиотеки:

```lua
require("gpio")

-- Устанавливаем пин PC13 в режим вывода и устанавливаем HIGH
pinMode(PC13, OUTPUT)
digitalWrite(PC13, HIGH)

-- Устанавливаем пин PE4 в режим входа с подтянутым к земле резистором
pinMode(PE4, INPUT_PULLUP)

-- Читаем состояние пина PE4 и выводим результат
local value = digitalRead(PE4)
print("Value of PE4:", value)

-- Устанавливаем аналоговый выход на пин PC7 с значением 32768 (середина диапазона)
analogWrite(PC7, 32768)

-- Подключаем функцию обратного вызова к прерыванию по изменениям состояния пина PC13
function myCallback()
    print("Interrupt triggered!")
end

attachInterrupt(PC13, myCallback, CHANGE)

-- Отключаем прерывание на пине PC13
detachInterrupt(PC13)
```