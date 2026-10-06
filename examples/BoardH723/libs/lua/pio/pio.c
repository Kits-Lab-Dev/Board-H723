#include <lua.h>
#include <lauxlib.h>
#include <lualib.h>
#include "stm32h7xx_hal_gpio.h"
#include "stm32h7xx_hal_adc.h"
#include "stm32h7xx_hal_tim.h"

#include "FreeRTOS.h"
#include "task.h"
#include "semphr.h"
#include "queue.h"
#include "app.h"
#include "stdbool.h"

// Константы для режимов пинов
#define INPUT 0
#define OUTPUT 1
#define INPUT_PULLUP 2
#define INPUT_PULLDOWN 3
#define OUTPUT_OPENDRAIN 4

// Константы для значений
#define HIGH 1
#define LOW 0

// Добавляем определения для аналоговых функций
#define ADC_RESOLUTION 65536 // 16-bit ADC
#define PWM_RESOLUTION 65536 // 16-bit PWM

// Добавляем определения для режимов прерываний
#define RISING 1
#define FALLING 2
#define CHANGE 3
#define RISING_PULLDOWN 4
#define FALLING_PULLUP 5

#define PG3 0
#define PG6 1
#define PG7 2
#define PC6 3
#define PC7 4
#define PC8 5
#define PC9 6
#define PA8 7
#define PA9 8
#define PA10 9
#define PA15 10
#define PC10 11
#define PC11 12
#define PC12 13
#define PD2 14
#define PD3 15
#define PG13 16
#define PB5 17
#define PB7 18
#define PE4 19
#define PE5 20
#define PE6 21
#define PC1 22
#define PA1 23
#define PA2 24
#define PA3 25
#define PA4 26
#define PA5 27
#define PA6 28
#define PA7 29
#define PC4 30
#define PC5 31
#define PB0 32
#define PB1 33
#define PB2 34
#define PB10 35
#define PB11 36
#define PB12 37
#define PB13 38
#define PB14 39
#define PB15 40
#define PD11 41
#define PD12 42
#define PD13 43

// Структура для хранения информации о пине
typedef struct
{
    GPIO_TypeDef *port;
    uint16_t pin;
} GPIO_Map;

typedef struct
{
    GPIO_TypeDef *port;
    uint16_t pin;
    uint16_t pinFromMap;
    lua_State *L;
    int callback;
    int mode;
} EXTI_Map;

typedef struct
{
    ADC_TypeDef *adc;
    uint16_t pinFromMap;
    uint32_t channel;
} ADC_Map;

typedef struct
{
    TIM_TypeDef *tim;
    uint16_t pinFromMap;
    uint32_t channel;
    uint8_t AF;
} PWM_Map;

static const PWM_Map pwmMap[] = {
    {TIM2, PA1, TIM_CHANNEL_2, GPIO_AF1_TIM2},
    {TIM2, PA2, TIM_CHANNEL_3, GPIO_AF1_TIM2},
    {TIM2, PA3, TIM_CHANNEL_4, GPIO_AF1_TIM2},
    {TIM2, PA5, TIM_CHANNEL_1, GPIO_AF1_TIM2},
    {TIM3, PA6, TIM_CHANNEL_1, GPIO_AF2_TIM3},
    {TIM3, PA7, TIM_CHANNEL_2, GPIO_AF2_TIM3},
    {TIM3, PB0, TIM_CHANNEL_3, GPIO_AF2_TIM3},
    {TIM3, PB1, TIM_CHANNEL_4, GPIO_AF2_TIM3},
    {TIM4, PD12, TIM_CHANNEL_1, GPIO_AF2_TIM4},
    {TIM4, PD13, TIM_CHANNEL_2, GPIO_AF2_TIM4},
    {TIM8, PC6, TIM_CHANNEL_1, GPIO_AF3_TIM8},
    {TIM8, PC7, TIM_CHANNEL_2, GPIO_AF3_TIM8},
    {TIM8, PC8, TIM_CHANNEL_3, GPIO_AF3_TIM8},
    {TIM8, PC9, TIM_CHANNEL_4, GPIO_AF3_TIM8},
    {TIM1, PA8, TIM_CHANNEL_1, GPIO_AF1_TIM1},
    {TIM1, PA9, TIM_CHANNEL_2, GPIO_AF1_TIM1},
    {TIM1, PA10, TIM_CHANNEL_3, GPIO_AF1_TIM1}};

static bool isInitTim1, isInitTim2, isInitTim3, isInitTim4, isInitTim8;

static inline bool isInitTim(TIM_TypeDef *tim)
{
    switch ((int)tim)
    {
    case (int)TIM1:
        return isInitTim1;
    case (int)TIM2:
        return isInitTim2;
    case (int)TIM3:
        return isInitTim3;
    case (int)TIM4:
        return isInitTim4;
    case (int)TIM8:
        return isInitTim8;
    default:
        return false;
    }
}

static inline void setInitTim(TIM_TypeDef *tim)
{
    switch ((int)tim)
    {
    case (int)TIM1:
        isInitTim1 = true;
        break;
    case (int)TIM2:
        isInitTim2 = true;
        break;
    case (int)TIM3:
        isInitTim3 = true;
        break;
    case (int)TIM4:
        isInitTim4 = true;
        break;
    case (int)TIM8:
        isInitTim8 = true;
        break;
    }
}

static inline void setDeInitTim(TIM_TypeDef *tim)
{
    switch ((int)tim)
    {
    case (int)TIM1:
        isInitTim1 = false;
        break;
    case (int)TIM2:
        isInitTim2 = false;
        break;
    case (int)TIM3:
        isInitTim3 = false;
        break;
    case (int)TIM4:
        isInitTim4 = false;
        break;
    case (int)TIM8:
        isInitTim8 = false;
        break;
    }
}

static const ADC_Map adcMap[] = {
    {ADC2, PC1, ADC_CHANNEL_11},
    // {ADC1, PA1, ADC_CHANNEL_17}, // FIXME: Не работает на ADC1
    {ADC2, PA2, ADC_CHANNEL_14},
    {ADC2, PA3, ADC_CHANNEL_15},
    {ADC2, PA4, ADC_CHANNEL_18},
    {ADC2, PA5, ADC_CHANNEL_19},
    {ADC2, PA6, ADC_CHANNEL_3},
    {ADC2, PA7, ADC_CHANNEL_7},
    {ADC2, PC4, ADC_CHANNEL_4},
    {ADC2, PC5, ADC_CHANNEL_8},
    {ADC2, PB0, ADC_CHANNEL_9},
    {ADC2, PB0, ADC_CHANNEL_5},
};

static const GPIO_Map gpioMap[] = {
    // left side
    {GPIOG, GPIO_PIN_3},
    {GPIOG, GPIO_PIN_6},
    {GPIOG, GPIO_PIN_7},
    {GPIOC, GPIO_PIN_6},
    {GPIOC, GPIO_PIN_7},
    {GPIOC, GPIO_PIN_8},
    {GPIOC, GPIO_PIN_9},
    {GPIOA, GPIO_PIN_8},
    {GPIOA, GPIO_PIN_9},
    {GPIOA, GPIO_PIN_10},
    {GPIOA, GPIO_PIN_15},
    {GPIOC, GPIO_PIN_10},
    {GPIOC, GPIO_PIN_11},
    {GPIOC, GPIO_PIN_12},
    {GPIOD, GPIO_PIN_2},
    {GPIOD, GPIO_PIN_3},
    {GPIOG, GPIO_PIN_13},
    {GPIOB, GPIO_PIN_5},
    {GPIOB, GPIO_PIN_7},
    {GPIOE, GPIO_PIN_4},
    {GPIOE, GPIO_PIN_5},
    {GPIOE, GPIO_PIN_6},

    // right side
    {GPIOC, GPIO_PIN_1}, // 22
    {GPIOA, GPIO_PIN_1},
    {GPIOA, GPIO_PIN_2},
    {GPIOA, GPIO_PIN_3},
    {GPIOA, GPIO_PIN_4},
    {GPIOA, GPIO_PIN_5},
    {GPIOA, GPIO_PIN_6},
    {GPIOA, GPIO_PIN_7},
    {GPIOC, GPIO_PIN_4}, // 30
    {GPIOC, GPIO_PIN_5},
    {GPIOB, GPIO_PIN_0},
    {GPIOB, GPIO_PIN_1},
    {GPIOB, GPIO_PIN_2},
    {GPIOB, GPIO_PIN_10}, // 35
    {GPIOB, GPIO_PIN_11},
    {GPIOB, GPIO_PIN_12},
    {GPIOB, GPIO_PIN_13},
    {GPIOB, GPIO_PIN_14},
    {GPIOB, GPIO_PIN_15},
    {GPIOD, GPIO_PIN_11},
    {GPIOD, GPIO_PIN_12},
    {GPIOD, GPIO_PIN_13},
};

static EXTI_Map extiMap[] = {
    // left side
    {GPIOC, GPIO_PIN_7, PC7, NULL, LUA_NOREF, 0},
    {GPIOC, GPIO_PIN_8, PC8, NULL, LUA_NOREF, 0},
    {GPIOC, GPIO_PIN_9, PC9, NULL, LUA_NOREF, 0},
    {GPIOC, GPIO_PIN_10, PC10, NULL, LUA_NOREF, 0},
    {GPIOC, GPIO_PIN_11, PC11, NULL, LUA_NOREF, 0},
    {GPIOC, GPIO_PIN_12, PC12, NULL, LUA_NOREF, 0},
    {GPIOD, GPIO_PIN_2, PD2, NULL, LUA_NOREF, 0},
    {GPIOG, GPIO_PIN_13, PG13, NULL, LUA_NOREF, 0},

    // right side
    {GPIOC, GPIO_PIN_1, PC1, NULL, LUA_NOREF, 0},
    {GPIOC, GPIO_PIN_4, PC4, NULL, LUA_NOREF, 0},
    {GPIOC, GPIO_PIN_5, PC5, NULL, LUA_NOREF, 0},
    {GPIOB, GPIO_PIN_0, PB0, NULL, LUA_NOREF, 0},
    {GPIOB, GPIO_PIN_14, PB14, NULL, LUA_NOREF, 0},
    {GPIOB, GPIO_PIN_15, PB15, NULL, LUA_NOREF, 0}};

#define LUA_EXTI_TASK_STACK_SIZE (1024 * 4)
#define LUA_EXTI_TASK_PRIORITY (tskIDLE_PRIORITY + 2)
static StackType_t taskStack[LUA_EXTI_TASK_STACK_SIZE] __ITCMRAM;
static StaticTask_t taskBuffer __ITCMRAM;
static QueueHandle_t extiQueue;
static uint8_t queueStorage[sizeof(extiMap) / sizeof(EXTI_Map) * sizeof(uint16_t)] __ITCMRAM;
static StaticQueue_t staticQueue;

static ADC_Map *getADC(uint16_t pin)
{
    for (uint32_t i = 0; i < sizeof(adcMap) / sizeof(ADC_Map); i++)
    {
        if (adcMap[i].pinFromMap == pin)
            return (ADC_Map *)&adcMap[i];
    }
    return NULL;
}

static PWM_Map *getTIM(uint16_t pin)
{
    for (uint32_t i = 0; i < sizeof(pwmMap) / sizeof(PWM_Map); i++)
    {
        if (pwmMap[i].pinFromMap == pin)
            return (PWM_Map *)&pwmMap[i];
    }
    return NULL;
}

static EXTI_Map *getEXTI(uint16_t pin)
{
    for (uint32_t i = 0; i < sizeof(extiMap) / sizeof(EXTI_Map); i++)
    {
        if (extiMap[i].pinFromMap == pin)
            return (EXTI_Map *)&extiMap[i];
    }
    return NULL;
}

// Функция для включения тактирования порта
static void enableGPIOClock(GPIO_TypeDef *port)
{
    if (port == GPIOA)
        __HAL_RCC_GPIOA_CLK_ENABLE();
    else if (port == GPIOB)
        __HAL_RCC_GPIOB_CLK_ENABLE();
    else if (port == GPIOC)
        __HAL_RCC_GPIOC_CLK_ENABLE();
    else if (port == GPIOD)
        __HAL_RCC_GPIOD_CLK_ENABLE();
    else if (port == GPIOE)
        __HAL_RCC_GPIOE_CLK_ENABLE();
    else if (port == GPIOF)
        __HAL_RCC_GPIOF_CLK_ENABLE();
    else if (port == GPIOG)
        __HAL_RCC_GPIOG_CLK_ENABLE();
    else if (port == GPIOH)
        __HAL_RCC_GPIOH_CLK_ENABLE();
    else if (port == GPIOJ)
        __HAL_RCC_GPIOJ_CLK_ENABLE();
    else if (port == GPIOK)
        __HAL_RCC_GPIOK_CLK_ENABLE();
}

static GPIO_Map *getGPIOMap(uint16_t pin_number)
{
    if (pin_number >= (sizeof(gpioMap) / sizeof(GPIO_Map)))
        return NULL;

    return (GPIO_Map *)&gpioMap[pin_number];
}

// pinMode(pin, mode)
static int l_pinMode(lua_State *L)
{
    int pin = luaL_checkinteger(L, 1);
    int mode = luaL_checkinteger(L, 2);

    GPIO_Map *gpio = getGPIOMap(pin);
    if (!gpio)
    {
        return luaL_error(L, "Invalid pin number");
    }

    // Включаем тактирование порта
    enableGPIOClock(gpio->port);

    GPIO_InitTypeDef GPIO_InitStruct = {0};
    GPIO_InitStruct.Pin = gpio->pin;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_HIGH;

    switch (mode)
    {
    case INPUT:
        GPIO_InitStruct.Mode = GPIO_MODE_INPUT;
        GPIO_InitStruct.Pull = GPIO_NOPULL;
        break;
    case OUTPUT:
        GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
        GPIO_InitStruct.Pull = GPIO_NOPULL;
        break;
    case INPUT_PULLUP:
        GPIO_InitStruct.Mode = GPIO_MODE_INPUT;
        GPIO_InitStruct.Pull = GPIO_PULLUP;
        break;
    case INPUT_PULLDOWN:
        GPIO_InitStruct.Mode = GPIO_MODE_INPUT;
        GPIO_InitStruct.Pull = GPIO_PULLDOWN;
        break;
    case OUTPUT_OPENDRAIN:
        GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_OD;
        GPIO_InitStruct.Pull = GPIO_NOPULL;
        break;
    default:
        return luaL_error(L, "Invalid mode");
    }

    HAL_GPIO_Init(gpio->port, &GPIO_InitStruct);
    return 0;
}

// digitalWrite(pin, value)
static int l_digitalWrite(lua_State *L)
{
    int pin = luaL_checkinteger(L, 1);
    int value = luaL_checkinteger(L, 2);

    GPIO_Map *gpio = getGPIOMap(pin);
    if (!gpio)
    {
        return luaL_error(L, "Invalid pin number");
    }

    HAL_GPIO_WritePin(gpio->port, gpio->pin, value == HIGH ? GPIO_PIN_SET : GPIO_PIN_RESET);
    return 0;
}

// digitalRead(pin)
static int l_digitalRead(lua_State *L)
{
    int pin = luaL_checkinteger(L, 1);

    GPIO_Map *gpio = getGPIOMap(pin);
    if (!gpio)
    {
        return luaL_error(L, "Invalid pin number");
    }

    GPIO_PinState state = HAL_GPIO_ReadPin(gpio->port, gpio->pin);
    lua_pushinteger(L, state == GPIO_PIN_SET ? HIGH : LOW);
    return 1;
}

// togglePin(pin)
static int l_togglePin(lua_State *L)
{
    int pin = luaL_checkinteger(L, 1);

    GPIO_Map *gpio = getGPIOMap(pin);
    if (!gpio)
    {
        return luaL_error(L, "Invalid pin number");
    }

    HAL_GPIO_TogglePin(gpio->port, gpio->pin);
    return 0;
}

static int adcClockEn = 0;

void HAL_ADC_MspInit(ADC_HandleTypeDef *adcHandle)
{
    if (adcHandle->Instance == ADC1 || adcHandle->Instance == ADC2)
    {
        adcClockEn++;
        if (adcClockEn == 1)
            __HAL_RCC_ADC12_CLK_ENABLE();
    }
    else if (adcHandle->Instance == ADC3)
    {
        __HAL_RCC_ADC3_CLK_ENABLE();
    }
}

void HAL_ADC_MspDeInit(ADC_HandleTypeDef *adcHandle)
{
    if (adcHandle->Instance == ADC1 || adcHandle->Instance == ADC2)
    {
        if (--adcClockEn == 0)
            __HAL_RCC_ADC12_CLK_DISABLE();
    }
    else if (adcHandle->Instance == ADC3)
    {
        __HAL_RCC_ADC3_CLK_DISABLE();
    }
}

// analogRead(pin)
static int l_analogRead(lua_State *L)
{
    int pin = luaL_checkinteger(L, 1);

    // Конфигурация ADC
    ADC_HandleTypeDef hadc = {0};
    ADC_ChannelConfTypeDef sConfig = {0};
    ADC_MultiModeTypeDef multimode = {0};

    // Базовая инициализация ADC
    ADC_Map *adc = getADC(pin);
    if (!adc)
    {
        return luaL_error(L, "Invalid pin number for analog read");
    }

    hadc.Instance = adc->adc;
    if (hadc.Instance != ADC1 && hadc.Instance != ADC2)
    {
        return luaL_error(L, "Invalid pin number for analog read");
    }

    hadc.Init.ClockPrescaler = ADC_CLOCK_ASYNC_DIV1;
    hadc.Init.Resolution = ADC_RESOLUTION_16B;
    hadc.Init.ScanConvMode = ADC_SCAN_DISABLE;
    hadc.Init.EOCSelection = ADC_EOC_SINGLE_CONV;
    hadc.Init.LowPowerAutoWait = DISABLE;
    hadc.Init.ContinuousConvMode = DISABLE;
    hadc.Init.NbrOfConversion = 1;
    hadc.Init.DiscontinuousConvMode = DISABLE;
    hadc.Init.ExternalTrigConv = ADC_SOFTWARE_START;
    hadc.Init.ExternalTrigConvEdge = ADC_EXTERNALTRIGCONVEDGE_NONE;
    hadc.Init.ConversionDataManagement = ADC_CONVERSIONDATA_DR;
    hadc.Init.Overrun = ADC_OVR_DATA_PRESERVED;
    hadc.Init.LeftBitShift = ADC_LEFTBITSHIFT_NONE;
    hadc.Init.OversamplingMode = DISABLE;
    hadc.Init.Oversampling.Ratio = 1;
    if (HAL_ADC_Init(&hadc) != HAL_OK)
    {
        return luaL_error(L, "ADC init failed");
    }

    // multimode.Mode = ADC_MODE_INDEPENDENT;
    // if (HAL_ADCEx_MultiModeConfigChannel(&hadc, &multimode) != HAL_OK)
    // {
    // return luaL_error(L, "ADC init failed");
    // }

    // Конфигурация канала ADC
    sConfig.Channel = adc->channel;
    sConfig.Rank = ADC_REGULAR_RANK_1;
    sConfig.SamplingTime = ADC_SAMPLETIME_1CYCLE_5;
    sConfig.SingleDiff = ADC_SINGLE_ENDED;
    sConfig.OffsetNumber = ADC_OFFSET_NONE;
    sConfig.Offset = 0;
    sConfig.OffsetSignedSaturation = DISABLE;

    if (HAL_ADC_ConfigChannel(&hadc, &sConfig) != HAL_OK)
    {
        return luaL_error(L, "ADC channel config failed");
    }

    // __HAL_RCC_ADC12_CLK_ENABLE();
    GPIO_Map *gpio = getGPIOMap(pin);
    HAL_GPIO_DeInit(gpio->port, gpio->pin);

    enableGPIOClock(gpio->port);
    GPIO_InitTypeDef GPIO_InitStruct = {0};
    GPIO_InitStruct.Pin = gpio->pin;
    GPIO_InitStruct.Mode = GPIO_MODE_ANALOG;
    GPIO_InitStruct.Pull = GPIO_NOPULL;
    HAL_GPIO_Init(gpio->port, &GPIO_InitStruct);

    HAL_ADCEx_Calibration_Start(&hadc, ADC_CALIB_OFFSET, ADC_SINGLE_ENDED);

    // Запуск преобразования
    HAL_ADC_Start(&hadc);
    HAL_ADC_PollForConversion(&hadc, 100);
    uint32_t value = HAL_ADC_GetValue(&hadc);

    HAL_ADC_Stop(&hadc);
    HAL_ADC_DeInit(&hadc);
    HAL_GPIO_DeInit(gpio->port, gpio->pin);

    lua_pushinteger(L, value);
    return 1;
}

// analogWrite(pin, value)
static int l_analogWrite(lua_State *L)
{
    int pin = luaL_checkinteger(L, 1);
    int value = luaL_checkinteger(L, 2);

    GPIO_Map *gpio = getGPIOMap(pin);
    if (!gpio)
    {
        return luaL_error(L, "Invalid pin number");
    }

    // Конфигурация PWM
    TIM_HandleTypeDef htim = {0};
    TIM_OC_InitTypeDef sConfigOC = {0};

    PWM_Map *pwm = getTIM(pin);
    if (!pwm)
    {
        return luaL_error(L, "Invalid pin number for PWM");
    }

    // Базовая инициализация таймера
    htim.Instance = pwm->tim; // Нужно выбрать подходящий таймер
    if (!isInitTim(pwm->tim))
    {
        htim.Init.Prescaler = 0;
        htim.Init.CounterMode = TIM_COUNTERMODE_UP;
        htim.Init.Period = PWM_RESOLUTION - 1;
        htim.Init.ClockDivision = TIM_CLOCKDIVISION_DIV1;
        htim.Init.AutoReloadPreload = TIM_AUTORELOAD_PRELOAD_DISABLE;
        if (HAL_TIM_Base_Init(&htim) != HAL_OK)
        {
            return luaL_error(L, "Error initializing PWM timer");
        }
        TIM_ClockConfigTypeDef sClockSourceConfig = {0};
        sClockSourceConfig.ClockSource = TIM_CLOCKSOURCE_INTERNAL;
        if (HAL_TIM_ConfigClockSource(&htim, &sClockSourceConfig) != HAL_OK)
        {
            return luaL_error(L, "Error configuring PWM timer clock source");
        }

        if (HAL_TIM_PWM_Init(&htim) != HAL_OK)
        {
            return luaL_error(L, "PWM init failed");
        }

        TIM_MasterConfigTypeDef sMasterConfig = {0};
        sMasterConfig.MasterOutputTrigger = TIM_TRGO_RESET;
        sMasterConfig.MasterSlaveMode = TIM_MASTERSLAVEMODE_DISABLE;
        if (HAL_TIMEx_MasterConfigSynchronization(&htim, &sMasterConfig) != HAL_OK)
        {
            return luaL_error(L, "Error configuring PWM timer master mode");
        }
        setInitTim(pwm->tim);
    }
    enableGPIOClock(gpio->port);
    GPIO_InitTypeDef GPIO_InitStruct = {0};
    GPIO_InitStruct.Pin = gpio->pin;
    GPIO_InitStruct.Mode = GPIO_MODE_AF_PP;
    GPIO_InitStruct.Pull = GPIO_NOPULL;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_HIGH;
    GPIO_InitStruct.Alternate = pwm->AF;

    HAL_GPIO_Init(gpio->port, &GPIO_InitStruct);

    // Конфигурация PWM канала
    sConfigOC.OCMode = TIM_OCMODE_PWM1;
    sConfigOC.Pulse = value;
    sConfigOC.OCPolarity = TIM_OCPOLARITY_HIGH;
    sConfigOC.OCFastMode = TIM_OCFAST_DISABLE;
    if (HAL_TIM_PWM_ConfigChannel(&htim, &sConfigOC, pwm->channel) != HAL_OK)
    {
        return luaL_error(L, "PWM channel config failed");
    }
    TIM_CHANNEL_STATE_SET_ALL(&htim, HAL_TIM_CHANNEL_STATE_READY);
    // Запуск PWM
    HAL_TIM_PWM_Start(&htim, pwm->channel);

    return 0;
}

// Вспомогательная функция для получения номера прерывания
static IRQn_Type getEXTIIRQn(uint16_t pin)
{
    switch (pin)
    {
    case GPIO_PIN_0:
        return EXTI0_IRQn;
    case GPIO_PIN_1:
        return EXTI1_IRQn;
    case GPIO_PIN_2:
        return EXTI2_IRQn;
    case GPIO_PIN_3:
        return EXTI3_IRQn;
    case GPIO_PIN_4:
        return EXTI4_IRQn;
    case GPIO_PIN_5:
    case GPIO_PIN_6:
    case GPIO_PIN_7:
    case GPIO_PIN_8:
    case GPIO_PIN_9:
        return EXTI9_5_IRQn;
    default:
        return EXTI15_10_IRQn;
    }
}

// attachInterrupt(pin, callback, mode)
static int l_attachInterrupt(lua_State *L)
{
    int pin = luaL_checkinteger(L, 1);
    if (!lua_isfunction(L, 2))
    {
        return luaL_error(L, "Second argument must be a function");
    }
    int mode = luaL_checkinteger(L, 3);

    EXTI_Map *exti = getEXTI(pin);
    if (!exti)
    {
        return luaL_error(L, "Invalid pin number");
    }

    // Сохраняем callback функцию в registry
    lua_pushvalue(L, 2);
    int callback = luaL_ref(L, LUA_REGISTRYINDEX);

    // Сохраняем состояние Lua и ссылку на callback
    exti->L = L;
    exti->callback = callback;
    exti->mode = mode;

    // Конфигурируем GPIO для прерывания
    enableGPIOClock(exti->port);
    GPIO_InitTypeDef GPIO_InitStruct = {0};
    GPIO_InitStruct.Pin = exti->pin;
    GPIO_InitStruct.Pull = GPIO_NOPULL;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_HIGH;

    switch (mode)
    {
    case RISING:
        GPIO_InitStruct.Mode = GPIO_MODE_IT_RISING;
        break;
    case FALLING:
        GPIO_InitStruct.Mode = GPIO_MODE_IT_FALLING;
        break;
    case CHANGE:
        GPIO_InitStruct.Mode = GPIO_MODE_IT_RISING_FALLING;
        break;
    case RISING_PULLDOWN:
        GPIO_InitStruct.Mode = GPIO_MODE_IT_RISING;
        GPIO_InitStruct.Pull = GPIO_PULLDOWN;
        break;
    case FALLING_PULLUP:
        GPIO_InitStruct.Mode = GPIO_MODE_IT_FALLING;
        GPIO_InitStruct.Pull = GPIO_PULLUP;
        break;
    default:
        return luaL_error(L, "Invalid interrupt mode");
    }

    HAL_GPIO_Init(exti->port, &GPIO_InitStruct);

    // Включаем прерывание EXTI
    IRQn_Type irqn = getEXTIIRQn(exti->pin);
    HAL_NVIC_SetPriority(irqn, 5, 0);
    HAL_NVIC_EnableIRQ(irqn);

    return 0;
}

// detachInterrupt(pin)
static int l_detachInterrupt(lua_State *L)
{
    int pin = luaL_checkinteger(L, 1);

    EXTI_Map *exti = getEXTI(pin);
    if (!exti)
    {
        return luaL_error(L, "Invalid pin number");
    }

    // Отключаем прерывание
    IRQn_Type irqn = getEXTIIRQn(exti->pin);
    HAL_NVIC_DisableIRQ(irqn);

    // Освобождаем callback
    if (exti->callback != LUA_NOREF)
    {
        luaL_unref(L, LUA_REGISTRYINDEX, exti->callback);
        exti->callback = LUA_NOREF;
        exti->L = NULL;
    }

    // Деинициализируем GPIO
    HAL_GPIO_DeInit(exti->port, exti->pin);

    return 0;
}

/* Прерывание вывода, на который Lua повесил pio.attachInterrupt: в очередь для extiTask.
 * Вызывается из HAL_GPIO_EXTI_Callback (app/exti.c), если вывод не забрали ESP и приложение. */
bool pioGpioExti(uint16_t pin)
{
    if (extiQueue == NULL)
        return false;
    xQueueSendFromISR(extiQueue, &pin, NULL);
    return true;
}

void extiTask(void *)
{
    uint16_t pin;
    for (;;)
    {
        if (xQueueReceive(extiQueue, &pin, portMAX_DELAY) == pdPASS)
        {
            for (uint16_t i = 0; i < sizeof(extiMap) / sizeof(EXTI_Map); i++)
            {
                if (extiMap[i].pin == pin)
                {
                    if (extiMap[i].callback != LUA_NOREF)
                    {
                        lua_State *threadL = lua_newthread(extiMap[i].L);
                        lua_rawgeti(extiMap[i].L, LUA_REGISTRYINDEX, extiMap[i].callback);
                        lua_xmove(extiMap[i].L, threadL, 1);
                        if (lua_isfunction(threadL, -1))
                        {
                            if (lua_pcall(threadL, 0, 0, 0) != LUA_OK)
                            {
                                ERR("%s\n", lua_tostring(threadL, -1));
                                lua_pop(threadL, 1);
                            }
                        }
                        lua_closethread(threadL, extiMap[i].L);
                        break;
                    }
                }
            }
        }
    }
}

// Регистрация функций
static const struct luaL_Reg gpio_lib[] = {
    {"pinMode", l_pinMode},
    {"digitalWrite", l_digitalWrite},
    {"digitalRead", l_digitalRead},
    {"togglePin", l_togglePin},
    {"analogRead", l_analogRead},
    {"analogWrite", l_analogWrite},
    {"attachInterrupt", l_attachInterrupt},
    {"detachInterrupt", l_detachInterrupt},
    {NULL, NULL}};

// Инициализация модуля
int luaopen_gpio(lua_State *L)
{
    // Добавляем константы в таблицу
    lua_pushinteger(L, INPUT);
    lua_setglobal(L, "INPUT");
    lua_pushinteger(L, OUTPUT);
    lua_setglobal(L, "OUTPUT");
    lua_pushinteger(L, INPUT_PULLUP);
    lua_setglobal(L, "INPUT_PULLUP");
    lua_pushinteger(L, INPUT_PULLDOWN);
    lua_setglobal(L, "INPUT_PULLDOWN");
    lua_pushinteger(L, OUTPUT_OPENDRAIN);
    lua_setglobal(L, "OUTPUT_OD");
    lua_pushinteger(L, HIGH);
    lua_setglobal(L, "HIGH");
    lua_pushinteger(L, LOW);
    lua_setglobal(L, "LOW");

    // left side
    lua_pushinteger(L, PG3);
    lua_setglobal(L, "PG3");
    lua_pushinteger(L, PG6);
    lua_setglobal(L, "PG6");
    lua_pushinteger(L, PG7);
    lua_setglobal(L, "PG7");
    lua_pushinteger(L, PC6);
    lua_setglobal(L, "PC6");
    lua_pushinteger(L, PC7);
    lua_setglobal(L, "PC7");
    lua_pushinteger(L, PC8);
    lua_setglobal(L, "PC8");
    lua_pushinteger(L, PC9);
    lua_setglobal(L, "PC9");
    lua_pushinteger(L, PA8);
    lua_setglobal(L, "PA8");
    lua_pushinteger(L, PA9);
    lua_setglobal(L, "PA9");
    lua_pushinteger(L, PA10);
    lua_setglobal(L, "PA10");
    lua_pushinteger(L, PA15);
    lua_setglobal(L, "PA15");
    lua_pushinteger(L, PC10);
    lua_setglobal(L, "PC10");
    lua_pushinteger(L, PC11);
    lua_setglobal(L, "PC11");
    lua_pushinteger(L, PC12);
    lua_setglobal(L, "PC12");
    lua_pushinteger(L, PD2);
    lua_setglobal(L, "PD2");
    lua_pushinteger(L, PD3);
    lua_setglobal(L, "PD3");
    lua_pushinteger(L, PG13);
    lua_setglobal(L, "PG13");
    lua_pushinteger(L, PB5);
    lua_setglobal(L, "PB5");
    lua_pushinteger(L, PB7);
    lua_setglobal(L, "PB7");
    lua_pushinteger(L, PE4);
    lua_setglobal(L, "PE4");
    lua_pushinteger(L, PE5);
    lua_setglobal(L, "PE5");
    lua_pushinteger(L, PE6);
    lua_setglobal(L, "PE6");

    // right side
    lua_pushinteger(L, PC1);
    lua_setglobal(L, "PC1");
    lua_pushinteger(L, PA1);
    lua_setglobal(L, "PA1");
    lua_pushinteger(L, PA2);
    lua_setglobal(L, "PA2");
    lua_pushinteger(L, PA3);
    lua_setglobal(L, "PA3");
    lua_pushinteger(L, PA4);
    lua_setglobal(L, "PA4");
    lua_pushinteger(L, PA5);
    lua_setglobal(L, "PA5");
    lua_pushinteger(L, PA6);
    lua_setglobal(L, "PA6");
    lua_pushinteger(L, PA7);
    lua_setglobal(L, "PA7");
    lua_pushinteger(L, PC4);
    lua_setglobal(L, "PC4");
    lua_pushinteger(L, PC5);
    lua_setglobal(L, "PC5");
    lua_pushinteger(L, PB0);
    lua_setglobal(L, "PB0");
    lua_pushinteger(L, PB1);
    lua_setglobal(L, "PB1");
    lua_pushinteger(L, PB2);
    lua_setglobal(L, "PB2");
    lua_pushinteger(L, PB10);
    lua_setglobal(L, "PB10");
    lua_pushinteger(L, PB11);
    lua_setglobal(L, "PB11");
    lua_pushinteger(L, PB12);
    lua_setglobal(L, "PB12");
    lua_pushinteger(L, PB13);
    lua_setglobal(L, "PB13");
    lua_pushinteger(L, PB14);
    lua_setglobal(L, "PB14");
    lua_pushinteger(L, PB15);
    lua_setglobal(L, "PB15");
    lua_pushinteger(L, PD11);
    lua_setglobal(L, "PD11");
    lua_pushinteger(L, PD12);
    lua_setglobal(L, "PD12");
    lua_pushinteger(L, PD13);
    lua_setglobal(L, "PD13");

    // Добавляем константы для режимов прерываний
    lua_pushinteger(L, RISING);
    lua_setglobal(L, "RISING");
    lua_pushinteger(L, FALLING);
    lua_setglobal(L, "FALLING");
    lua_pushinteger(L, CHANGE);
    lua_setglobal(L, "CHANGE");
    lua_pushinteger(L, RISING_PULLDOWN);
    lua_setglobal(L, "RISING_PULLDOWN");
    lua_pushinteger(L, FALLING_PULLUP);
    lua_setglobal(L, "FALLING_PULLUP");

    for (uint32_t i = 0; i < sizeof(gpio_lib) / sizeof(luaL_Reg); i++)
    {
        lua_pushcfunction(L, gpio_lib[i].func);
        lua_setglobal(L, gpio_lib[i].name);
    }

    extiQueue = xQueueCreateStatic(sizeof(extiMap) / sizeof(EXTI_Map), sizeof(uint16_t), queueStorage, &staticQueue);
    xTaskCreateStatic(extiTask, "extiTask", LUA_EXTI_TASK_STACK_SIZE, NULL, LUA_EXTI_TASK_PRIORITY, taskStack, &taskBuffer);

    return 1;
}

/**  Пример

local Led = PC11
pinMode(Led, OUTPUT)
digitalWrite(Led, HIGH)

pinMode(PE4, INPUT_PULLUP)
local value = digitalRead(PE4)

**/