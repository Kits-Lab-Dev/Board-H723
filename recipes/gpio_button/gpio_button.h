#ifndef GPIO_BUTTON_H
#define GPIO_BUTTON_H

#include <stdint.h>

// Кнопка BOOT/USER (PC13) по прерыванию, светодиод PG3 переключается коротким нажатием
void gpio_button_start(void);

// Сколько раз нажата кнопка с момента старта
uint32_t gpioButtonPresses(void);

#endif
