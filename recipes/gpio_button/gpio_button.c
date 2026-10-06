#include "gpio_button.h"
#include "app.h"
#include "FreeRTOS.h"
#include "task.h"

#define BUTTON_PORT BOOT_BTN_GPIO_Port // PC13: кнопка на 3,3 В, подтяжка 10 кОм к GND на плате
#define BUTTON_PIN BOOT_BTN_Pin
#define LED_PORT LED_GPIO_Port         // PG3: светодиод D2, горит при 1
#define LED_PIN_ LED_Pin

#define DEBOUNCE_MS 20      // дребезг контактов затихает за 5–15 мс
#define SHORT_PRESS_MS 800  // дольше — не считаем нажатием (~1 с — сброс платы, см. app.c)
#define BUTTON_IRQ_PRIORITY 6 // не выше configLIBRARY_MAX_SYSCALL_INTERRUPT_PRIORITY (5):
                              // из такого прерывания можно звать ...FromISR

#define TASK_STACK_SIZE 384

static StackType_t taskStack[TASK_STACK_SIZE];
static StaticTask_t taskTcb;
static TaskHandle_t task;
static volatile uint32_t presses;
static bool ledOn;

uint32_t gpioButtonPresses(void)
{
    return presses;
}

// Прерывание по обоим фронтам. В ISR только будим задачу: дребезг даёт пачку
// прерываний, разбирать их здесь нельзя — нужна пауза, а в ISR ждать нельзя.
bool appGpioExti(uint16_t pin)
{
    if (pin != BUTTON_PIN)
        return false;
    BaseType_t woken = pdFALSE;
    if (task)
        vTaskNotifyGiveFromISR(task, &woken);
    portYIELD_FROM_ISR(woken);
    return true;
}

static void buttonTask(void *arg)
{
    (void)arg;
    GPIO_PinState stable = HAL_GPIO_ReadPin(BUTTON_PORT, BUTTON_PIN);
    TickType_t pressedAt = 0;
    for (;;)
    {
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
        // Ждём, пока контакты успокоятся, и сбрасываем прерывания, пришедшие за это время
        vTaskDelay(pdMS_TO_TICKS(DEBOUNCE_MS));
        ulTaskNotifyTake(pdTRUE, 0);
        GPIO_PinState now = HAL_GPIO_ReadPin(BUTTON_PORT, BUTTON_PIN);
        if (now == stable)
        {
            DBG("gpio_button: bounce, ignored\n");
            continue;
        }
        stable = now;
        if (now == GPIO_PIN_SET)
        {
            pressedAt = xTaskGetTickCount();
            continue;
        }
        uint32_t ms = (xTaskGetTickCount() - pressedAt) * portTICK_PERIOD_MS;
        if (ms > SHORT_PRESS_MS)
            continue;
        presses++;
        // Состояние храним сами: IDR догоняет запись в выход через несколько тактов,
        // и HAL_GPIO_ReadPin сразу после переключения может вернуть старое значение
        ledOn = !ledOn;
        HAL_GPIO_WritePin(LED_PORT, LED_PIN_, ledOn ? GPIO_PIN_SET : GPIO_PIN_RESET);
        INF("gpio_button: press %lu (%lu ms), LED %s\n", presses, ms, ledOn ? "on" : "off");
    }
}

void gpio_button_start(void)
{
    // Светодиод забираем у мигалки app.c
    appLedHeartbeat = false;
    HAL_GPIO_WritePin(LED_PORT, LED_PIN_, GPIO_PIN_RESET);

    task = xTaskCreateStatic(buttonTask, "button", TASK_STACK_SIZE, NULL, tskIDLE_PRIORITY + 2,
                             taskStack, &taskTcb);

    // CubeMX настроил PC13 простым входом — переводим в режим прерывания.
    // Обработчик EXTI15_10_IRQHandler и HAL_GPIO_EXTI_Callback — в libs/lua/pio/pio.c,
    // оттуда прерывание приходит в appGpioExti() выше.
    GPIO_InitTypeDef gpio = {
        .Pin = BUTTON_PIN,
        .Mode = GPIO_MODE_IT_RISING_FALLING,
        .Pull = GPIO_NOPULL,
    };
    HAL_GPIO_Init(BUTTON_PORT, &gpio);
    HAL_NVIC_SetPriority(EXTI15_10_IRQn, BUTTON_IRQ_PRIORITY, 0);
    HAL_NVIC_EnableIRQ(EXTI15_10_IRQn);
    INF("gpio_button: started, press BOOT to toggle LED\n");
}
