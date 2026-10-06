# Рецепты для Доски-H723

Рецепт — небольшой законченный модуль на C: `recipes/<имя>/<имя>.c` + `.h` и README
с объяснением, как он устроен. Каждый проверен на плате; в README — настоящий вывод лога.
Рецепты подключаются к основному проекту `examples/BoardH723` без правки его кода.

| Рецепт | Что показывает |
|---|---|
| [gpio_button](gpio_button/README.md) | кнопка BOOT по прерыванию, подавление дребезга, светодиод |
| [uart_dma](uart_dma/README.md) | приём UART через DMA в кольцевой буфер, команды из терминала |
| [i2c_bme280](i2c_bme280/README.md) | сканер шины I2C, датчик BME280/BMP280: температура, давление, влажность |
| [wifi_http](wifi_http/README.md) | подключение к Wi-Fi, HTTP GET на сокетах FreeRTOS+TCP |
| [sd_file](sd_file/README.md) | дозапись в файл на SD, замер скорости записи и чтения |
| [rtos_queue](rtos_queue/README.md) | две задачи FreeRTOS и очередь между ними |
| [lcd_ltdc](lcd_ltdc/README.md) | дисплей RGB 800×480: LTDC, DMA2D, двойной буфер в SDRAM, картинка с SD |

## Как включить

Список рецептов через `;` в опции `H723_RECIPES`:

```bash
cd examples/BoardH723
cmake --preset Release -DH723_RECIPES="gpio_button;sd_file"
cmake --build build/Release
python ../../tools/dfu_flash.py
```

CMake добавляет в сборку все `.c` из папок рецептов и генерирует `build/Release/recipes_start.c`,
который вызывает `<имя>_start()` каждого рецепта. `appTask` (`app/app.c`) вызывает его, когда
уже работают USB-сеть, SDRAM, ESP32-C2, SD-карта, Lua и SSH. Выключить рецепты —
`-DH723_RECIPES=""`.

Вывод рецептов — в лог USART1 115200 (PB14, на разъёме отладчика J6 и на J2).

## Свой рецепт

1. Папка `recipes/my_thing/` с `my_thing.c`, где есть `void my_thing_start(void)`.
2. `my_thing_start()` только создаёт задачу FreeRTOS и сразу возвращается: `appTask` на ней не задерживается.
3. `cmake --preset Release -DH723_RECIPES="my_thing"`.

Правила, которые соблюдают все рецепты:

- Задачи, очереди и стеки — статически (`xTaskCreateStatic`): их размер виден при сборке, а не
  выясняется при нехватке кучи во время работы.
- Буферы для DMA — не в обычных `static`-массивах: они попадают в DTCM, куда DMA1/DMA2 доступа
  не имеют. Подходят `pvPortMalloc` (RAM_D1) или `sdramMalloc` (SDRAM), с выравниванием по 32 байта
  и очисткой/инвалидацией кэша.
- Прерывания, из которых зовутся функции `...FromISR`, — с приоритетом 5 и ниже по важности
  (число ≥ 5).
- Свободные выводы и периферия — в `docs/board.md` репозитория.
