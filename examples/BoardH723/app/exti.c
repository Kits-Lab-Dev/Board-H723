#include "app.h"
#include "main.h"

/* Прерывания EXTI всех выводов. Получатели по порядку: драйвер ESP32-C2 (свои линии),
 * приложение (appGpioExti, переопределяют рецепты), Lua (pioGpioExti, pio.attachInterrupt). */

void ESP_EXTI_Callback(void);

/* Есть в libs/lua/pio/pio.c, если прошивка собрана с Lua */
__weak bool pioGpioExti(uint16_t pin)
{
    UNUSED(pin);
    return false;
}

/* Приложение переопределяет функцию и возвращает true для своих выводов —
 * тогда прерывание не уходит в Lua */
__weak bool appGpioExti(uint16_t pin)
{
    UNUSED(pin);
    return false;
}

void HAL_GPIO_EXTI_Callback(uint16_t GPIO_Pin)
{
    if ((GPIO_Pin == ESP_HANDSHAKE_Pin) || (GPIO_Pin == ESP_DATA_READY_Pin))
    {
        ESP_EXTI_Callback();
        return;
    }
    if (appGpioExti(GPIO_Pin))
        return;
    pioGpioExti(GPIO_Pin);
}

void EXTI0_IRQHandler(void)
{
    HAL_GPIO_EXTI_IRQHandler(GPIO_PIN_0);
}

void EXTI1_IRQHandler(void)
{
    HAL_GPIO_EXTI_IRQHandler(GPIO_PIN_1);
}

void EXTI2_IRQHandler(void)
{
    HAL_GPIO_EXTI_IRQHandler(GPIO_PIN_2);
}

void EXTI3_IRQHandler(void)
{
    HAL_GPIO_EXTI_IRQHandler(GPIO_PIN_3);
}

void EXTI4_IRQHandler(void)
{
    HAL_GPIO_EXTI_IRQHandler(GPIO_PIN_4);
}

void EXTI9_5_IRQHandler(void)
{
    for (uint16_t pin = GPIO_PIN_5; pin <= GPIO_PIN_9; pin = pin << 1)
    {
        HAL_GPIO_EXTI_IRQHandler(pin);
    }
}

void EXTI15_10_IRQHandler(void)
{
    // uint32_t: у uint16_t после GPIO_PIN_15 (0x8000) сдвиг даёт 0 и цикл не кончается
    for (uint32_t pin = GPIO_PIN_10; pin <= GPIO_PIN_15; pin = pin << 1)
    {
        HAL_GPIO_EXTI_IRQHandler(pin);
    }
}
