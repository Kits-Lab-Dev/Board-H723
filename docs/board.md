# Доска-H723: железо для прошивки

Источник — схема `hardware/sch.pdf` и `examples/BoardH723/BoardH723.ioc`.
Если картинка распиновки где-то расходится с этой таблицей, верна схема.

## Микроконтроллер и такты

- STM32H723ZGT6, LQFP144: Cortex-M7, Flash 1 МБ, ОЗУ 564 КБ, кэш I/D по 32 КБ.
- HSE 25 МГц, LSE 32,768 кГц (RTC).
- PLL1: 25 / 5 × 110 = 550 МГц — ядро (SYSCLK). HCLK 275 МГц, APB1–APB4 137,5 МГц.
- Ядра периферии: FMC 275 МГц, SPI1–5 125 МГц (PLL3Q), ADC и LTDC 25 МГц (PLL3R), SDMMC 100 МГц (PLL2),
  USB 48 МГц (HSI48).
- Таймбаза HAL — TIM7 (SysTick занят FreeRTOS).

## Память

| Область | Адрес | Размер | Что там в проекте `BoardH723` |
|---|---|---|---|
| ITCM | 0x00000000 | 64 КБ | ~41 КБ: стек `appTask` и другие статические буферы (`__ITCMRAM`); адрес 0 закрыт — ловит обращение по NULL |
| DTCM | 0x20000000 | 128 КБ | `.data` + `.bss` (~83 КБ), стек прерываний |
| Flash | 0x08000000 | 1 МБ | прошивка: ~805 КБ со всем, 629 КБ без SSH, 530 КБ без SSH и TLS |
| RAM_D1 (AXI) | 0x24000000 | 320 КБ | целиком куча FreeRTOS (`configTOTAL_HEAP_SIZE`); в работе занято ~150 КБ (сеть, SSH-сессия, буфер SD 32 КБ) — `top` в SSH |
| RAM_D2 | 0x30000000 | 32 КБ | сетевые буферы FreeRTOS+TCP (`ucNetworkPackets`), занята на ~98 % |
| RAM_D3 | 0x38000000 | 16 КБ | буферы SPI-обмена с ESP32-C2 и пулы NimBLE, занята на ~90 % |
| SDRAM | 0xC0000000 | 32 МБ | MT48LC16M16A2 через FMC (16 бит): `sdramMalloc()` (lwmem), RAM-диск `/RAM` 4 МБ, Lua |

Новые большие буферы — в SDRAM через `sdramMalloc()`. Буферы DMA, которым важна задержка
(SD-карта, быстрый UART), лучше в куче FreeRTOS (`pvPortMalloc`, AXI SRAM): шину SDRAM делят
LTDC, DMA2D и процессор, и периферия может не успеть получить или отдать данные.
MPU: SDRAM — кэшируемая, **не Shareable** (иначе FMC отдаёт мусор при невыровненном
чтении). Если DMA пишет в кэшируемую память, нужны `SCB_CleanDCache_by_Addr` /
`SCB_InvalidateDCache_by_Addr`.

## Занятые выводы и периферия

Эти выводы разведены на плате к своим микросхемам — менять их нельзя без переделки платы
(и прошивки ESP32-C2 для линий ESP).

| Блок | Выводы | Периферия, DMA |
|---|---|---|
| SDRAM | PC0, PC2_C, PC3_C, PD0, PD1, PD8–PD10, PD14, PD15, PE0, PE1, PE7–PE15, PF0–PF5, PF11–PF15, PG0–PG2, PG4, PG5, PG8, PG15 | FMC |
| microSD | PD6 (CK), PD7 (CMD), PG9–PG12 (D0–D3), PD5 (DET) | SDMMC2, 4 бита, 50 МГц; настраивает `libs/disks/sddisk.c` |
| ESP32-C2, данные | PF6 (CS), PF7 (SCK), PF8 (MISO), PF9 (MOSI), PE3 (DATA_READY), PB6 (HANDSHAKE) | SPI5, DMA1 Stream0 (RX) / Stream1 (TX), EXTI3, EXTI6 |
| ESP32-C2, управление | PF10 (EN), PE2 (BOOT) | GPIO |
| ESP32-C2, UART | PB4 → RXD, PB3 ← TXD | UART7 (прошивка ESP через ESP-Loader); идут и на разъём J1 через перемычки JP3/JP4 |
| USB | PA11 (D−), PA12 (D+) | USB_OTG_HS во встроенном FS PHY: USB-NCM (сеть) |
| Лог, VCP отладчика | PB14 (TX), PB15 (RX) | USART1 115200, DMA1 Stream2 (TX); выведены на SWD-разъём и J2 |
| SWD | PA13 (SWDIO), PA14 (SWCLK) | отладчик |
| Кварцы | PH0, PH1, PC14, PC15 | HSE, LSE |
| Кнопка BOOT/USER | BOOT0 и PC13 | при сбросе с нажатой — системный загрузчик (DFU); в прошивке долгое нажатие (~1 с) — сброс, короткое свободно (рецепт `gpio_button`) |
| Светодиод | PG3 | мигает 2 Гц из idle-задачи (выключается `appLedHeartbeat = false`); выведен на J2 |

Ещё занято прошивкой: RTC (`RTC_BKP_DR31` — флаг перехода в загрузчик), RNG, CRC, DTS
(температура кристалла), ADC3 (внутренний датчик температуры), TIM7.

Настроены в `.ioc`, но свободны для своих задач (их использует Lua-модуль `pio`, если запущен Lua):

- I2C1 — PB8 (SCL), PB7 (SDA)
- FDCAN2 — PB12 (RX), PB13 (TX); нужен внешний трансивер
- ШИМ: TIM2 — PA5, PA1, PA2, PA3; TIM3 — PA6, PA7, PB0, PB1; TIM4 — PD12, PD13;
  TIM8 — PC6–PC9; TIM1 — PA8

## Разъёмы

J1 и J2 — 2×20, шаг 2,54 мм. Номер вывода — как на схеме (нечётные с одной стороны).

| J1 | | | J2 | | |
|---|---|---|---|---|---|
| 1 GND | 2 GND | | 1 GND | 2 GND |
| 3 3V3 | 4 3V3 | | 3 3V3 | 4 3V3 |
| 5 PC7 | 6 PC6 | | 5 RST | 6 GND |
| 7 PC9 | 8 PC8 | | 7 PG7 | 8 PG6 |
| 9 PA9 | 10 PA8 | | 9 PG3 (LED) | 10 PD13 |
| 11 PA15 | 12 PA10 | | 11 PD12 | 12 PD11 |
| 13 PC11 | 14 PC10 | | 13 PB15 (лог RX) | 14 PB14 (лог TX) |
| 15 PD2 | 16 PC12 | | 15 PB13 | 16 PB12 |
| 17 PD4 | 18 PD3 | | 17 PB11 | 18 PB10 |
| 19 GND | 20 GND | | 19 GND | 20 GND |
| 21 PG14 | 22 PG13 | | 21 PB2 | 22 PB1 |
| 23 PB7 | 24 PB5 | | 23 PB0 | 24 PC5 |
| 25 PB9 | 26 PB8 | | 25 PC4 | 26 PA7 |
| 27 PE4 | 28 PE5 | | 27 PA6 | 28 PA5 |
| 29 PE6 | 30 PB4 / ESP RXD | | 29 PA4 | 30 PA3 |
| 31 PB3 / ESP TXD | 32 ESP IO18 | | 31 PC1 | 32 PA2 |
| 33 ESP IO0 | 34 ESP IO5 | | 33 PA1 | 34 PA0 |
| 35 3V3 | 36 VBAT | | 35 VREF+ | 36 3V3 |
| 37 5V | 38 5V | | 37 5V | 38 5V |
| 39 GND | 40 GND | | 39 GND | 40 GND |

- J6 (SWD, 2×4): 1 3V3, 2 SWDIO, 3 GND, 4 SWCLK, 5 GND, 6 —, 7 PB15 (USART1 RX), 8 PB14 (USART1 TX).
  Отладчик «Лобзик» подключается сюда и сразу даёт лог USART1.
- Перемычки на плате: JP1 (VREF+ ↔ 3V3, разомкнута), JP2 (VBAT ↔ 3V3, разомкнута),
  JP3/JP4 (UART ESP32-C2 ↔ PB4/PB3, замкнуты).
- Питание: USB-C 5 В → SY8089 → 3,3 В. 5V на разъёмах — это VBUS USB.

## Сеть и загрузчик

- USB-C — USB-NCM: плата 192.168.4.1, ПК получает 192.168.4.2 по DHCP. Драйвер NCM
  встроен в Windows 10/11, Linux, macOS.
- Wi-Fi точка доступа платы — 192.168.11.1, клиенты 192.168.11.x.
- Переход в системный загрузчик USB DFU без кнопки: UDP-пакет `dfu` на 192.168.4.1:7123
  (только из USB-сети), команда `reboot dfu` в SSH, `rtos.reboot("dfu")` в Lua. Прошивка
  кладёт адрес ROM-загрузчика (0x1FF09800) в `RTC_BKP_DR31` и сбрасывается; `bootloader()`
  в `app/app.c` прыгает туда до инициализации периферии. Готовый скрипт — `tools/dfu_flash.py`.
