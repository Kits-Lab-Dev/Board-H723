#include "lua.h"
#include "lauxlib.h"
#include "lualib.h"
#include "stm32h7xx_hal.h"
#include "stm32h7xx_hal_i2c.h"
#include "stm32h7xx_ll_i2c.h"
#include "FreeRTOS.h"
#include "semphr.h"
#include "string.h"
#include "stdbool.h"
#include "sdram.h"
#include "i2c.h"

// Структура для хранения состояния I2C
typedef struct
{
    I2C_TypeDef *instance;
    I2C_HandleTypeDef *hi2c;
    GPIO_TypeDef *port;
    uint16_t scl;
    uint16_t sda;
    uint8_t AF;
    IRQn_Type irqn;
    uint8_t initialized;
    SemaphoreHandle_t sem;
    SemaphoreHandle_t mutex;
} I2C_Map;

static I2C_Map i2cMap[] = {
    {I2C4, NULL, GPIOD, GPIO_PIN_12, GPIO_PIN_13, GPIO_AF4_I2C4, I2C4_EV_IRQn, 0, NULL, NULL},
    {I2C1, NULL, GPIOB, GPIO_PIN_8, GPIO_PIN_9, GPIO_AF4_I2C1, I2C1_EV_IRQn, 0, NULL, NULL}};

static const char *i2cNames[] = {
    "Wire",  // I2C4
    "Wire1", // I2C1 инициализирован для работы с IP5306
};

#define IIC4 0
#define COUNT_I2C_PORTS (sizeof(i2cMap) / sizeof(I2C_Map))

static inline I2C_Map *getI2C(I2C_HandleTypeDef *hi2c)
{
    for (uint8_t i = 0; i < COUNT_I2C_PORTS; i++)
    {
        if (i2cMap[i].instance == hi2c->Instance)
            return &i2cMap[i];
    }
    return NULL;
}

static inline I2C_Map *setI2C(I2C_HandleTypeDef *hi2c)
{
    for (uint8_t i = 0; i < COUNT_I2C_PORTS; i++)
    {
        if (i2cMap[i].instance == hi2c->Instance)
        {
            i2cMap[i].hi2c = hi2c;
            return &i2cMap[i];
        }
    }
    return NULL;
}

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

static void enableI2CClock(I2C_TypeDef *icc)
{
    if (icc == I2C4)
        __HAL_RCC_I2C4_CLK_ENABLE();
    else if (icc == I2C1)
        __HAL_RCC_I2C1_CLK_ENABLE();
    else if (icc == I2C2)
        __HAL_RCC_I2C2_CLK_ENABLE();
    else if (icc == I2C3)
        __HAL_RCC_I2C3_CLK_ENABLE();
    else if (icc == I2C5)
        __HAL_RCC_I2C5_CLK_ENABLE();
}

#define I2C_100KHZ 0x60404E72
#define I2C_400KHZ 0x00D049FB
#define I2C_1MHZ 0x00601A5C

void I2C4_EV_IRQHandler(void)
{
    I2C_HandleTypeDef *hi2c = i2cMap[IIC4].hi2c;
    if (hi2c)
        HAL_I2C_EV_IRQHandler(i2cMap[IIC4].hi2c);
}

static void txrxCmplt(I2C_HandleTypeDef *hi2c)
{
    I2C_Map *i2c = getI2C(hi2c);
    if (i2c)
    {
        xSemaphoreGiveFromISR(i2c->sem, NULL);
    }
}

// Инициализация I2C
static int li2c_begin(lua_State *L)
{
    I2C_HandleTypeDef *hi2c = (I2C_HandleTypeDef *)luaL_checkudata(L, 1, "I2C");
    I2C_Map *i2c = getI2C(hi2c);
    if (i2c == NULL)
    {
        return luaL_error(L, "I2C device not found");
    }

    if (i2c->instance == I2C1)
    {
        i2c->initialized = 1;
        lua_pushboolean(L, 1);
        return 1;
    }

    i2c->hi2c = hi2c;

    uint32_t address = luaL_optinteger(L, 2, 0x50);
    uint32_t speed = luaL_optinteger(L, 3, I2C_100KHZ);

    RCC_PeriphCLKInitTypeDef PeriphClkInitStruct = {0};
    PeriphClkInitStruct.PeriphClockSelection = RCC_PERIPHCLK_I2C4;
    PeriphClkInitStruct.I2c4ClockSelection = RCC_I2C4CLKSOURCE_D3PCLK1;
    if (HAL_RCCEx_PeriphCLKConfig(&PeriphClkInitStruct) != HAL_OK)
    {
        lua_pushboolean(L, 0);
        return 1;
    }

    enableI2CClock(i2c->instance);
    enableGPIOClock(i2c->port);

    GPIO_InitTypeDef GPIO_InitStruct = {0};
    GPIO_InitStruct.Pin = i2c->scl | i2c->sda;
    GPIO_InitStruct.Mode = GPIO_MODE_AF_OD;
    GPIO_InitStruct.Pull = GPIO_PULLUP;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_HIGH;
    GPIO_InitStruct.Alternate = i2c->AF;
    HAL_GPIO_Init(i2c->port, &GPIO_InitStruct);

    // Настройка I2C
    hi2c->Init.Timing = speed;
    hi2c->Init.OwnAddress1 = address;
    hi2c->Init.AddressingMode = I2C_ADDRESSINGMODE_7BIT;
    hi2c->Init.DualAddressMode = I2C_DUALADDRESS_DISABLE;
    hi2c->Init.OwnAddress2 = 0;
    hi2c->Init.OwnAddress2Masks = I2C_OA2_NOMASK;
    hi2c->Init.GeneralCallMode = I2C_GENERALCALL_DISABLE;
    hi2c->Init.NoStretchMode = I2C_NOSTRETCH_DISABLE;
    if (HAL_I2C_Init(hi2c) != HAL_OK)
    {
        lua_pushboolean(L, 0);
        return 1;
    }

    if (HAL_I2CEx_ConfigAnalogFilter(hi2c, I2C_ANALOGFILTER_ENABLE) != HAL_OK)
    {
        lua_pushboolean(L, 0);
        return 1;
    }

    if (HAL_I2CEx_ConfigDigitalFilter(hi2c, 0) != HAL_OK)
    {
        lua_pushboolean(L, 0);
        return 1;
    }

    i2c->mutex = xSemaphoreCreateMutex();
    i2c->sem = xSemaphoreCreateBinary();
    if (i2c->mutex == NULL || i2c->sem == NULL)
    {
        lua_pushboolean(L, 0);
        return 1;
    }

    HAL_NVIC_SetPriority(i2c->irqn, 5, 0);
    HAL_NVIC_EnableIRQ(i2c->irqn);

    i2c->initialized = 1;
    lua_pushboolean(L, 1);
    return 1;
}

// Завершение работы I2C
static int li2c_end(lua_State *L)
{
    I2C_HandleTypeDef *hi2c = (I2C_HandleTypeDef *)luaL_checkudata(L, 1, "I2C");
    I2C_Map *i2c = getI2C(hi2c);
    if (!i2c || !i2c->initialized)
    {
        luaL_error(L, "Invalid i2c handle");
    }

    if (i2c->instance == I2C1)
    {
        i2c->initialized = 0;
        lua_pushboolean(L, 1);
        return 1;
    }

    HAL_I2C_DeInit(hi2c);
    HAL_NVIC_DisableIRQ(i2c->irqn);
    HAL_NVIC_ClearPendingIRQ(i2c->irqn);

    if (i2c->sem)
    {
        vSemaphoreDelete(i2c->sem);
        i2c->sem = NULL;
    }
    if (i2c->mutex)
    {
        vSemaphoreDelete(i2c->mutex);
        i2c->mutex = NULL;
    }
    i2c->initialized = 0;
    lua_pushboolean(L, 1);
    return 1;
}

// Отправка данных по I2C
static int li2c_write(lua_State *L)
{
    I2C_HandleTypeDef *hi2c = (I2C_HandleTypeDef *)luaL_checkudata(L, 1, "I2C");
    I2C_Map *i2c = getI2C(hi2c);
    if (!i2c || !i2c->initialized)
    {
        luaL_error(L, "Invalid i2c handle");
    }
    uint16_t address = luaL_checkinteger(L, 2);
    size_t len;
    uint8_t *data = NULL;
    if (lua_type(L, 3) == LUA_TSTRING)
    {
        const char *str = lua_tolstring(L, 3, &len);
        data = (uint8_t *)sdramMalloc(len);
        memcpy(data, str, len);
    }
    else if (lua_type(L, 3) == LUA_TTABLE)
    {
        len = lua_rawlen(L, 3);
        data = (uint8_t *)sdramMalloc(len);
        for (uint32_t i = 0; i < len; i++)
        {
            lua_rawgeti(L, 3, i + 1);
            data[i] = (uint8_t)lua_tointeger(L, -1);
            lua_pop(L, 1);
        }
    }
    else
    {
        lua_pushboolean(L, 0);
        return 1;
    }
    // if (i2c->instance == I2C1)
    // {
    //     lua_pushboolean(L, i2c_write(&hi2c1, address, data, len));
    //     return 1;
    // }
    xSemaphoreTake(i2c->mutex, portMAX_DELAY);
    HAL_I2C_RegisterCallback(hi2c, HAL_I2C_MASTER_TX_COMPLETE_CB_ID, txrxCmplt);
    HAL_I2C_Master_Transmit_IT(hi2c, address, data, len);
    if (xSemaphoreTake(i2c->sem, 1000) != pdTRUE)
        lua_pushboolean(L, 0);
    else
        lua_pushboolean(L, 1);
    if (data)
        sdramFree(data);

    xSemaphoreGive(i2c->mutex);
    return 1;
}

// Получение данных по I2C
static int li2c_read(lua_State *L)
{
    I2C_HandleTypeDef *hi2c = (I2C_HandleTypeDef *)luaL_checkudata(L, 1, "I2C");
    I2C_Map *i2c = getI2C(hi2c);
    if (!i2c || !i2c->initialized)
    {
        luaL_error(L, "Invalid i2c handle");
    }

    uint16_t address = luaL_checkinteger(L, 2);
    size_t len = luaL_checkinteger(L, 3);
    uint8_t *data = (uint8_t *)sdramMalloc(len);
    // if (i2c->instance == I2C1)
    // {
    //     if (i2c_read(&hi2c1, address, data, len))
    //     {
    //         lua_newtable(L);
    //         for (uint32_t i = 0; i < len; i++)
    //         {
    //             lua_pushinteger(L, i + 1);
    //             lua_pushinteger(L, data[i]);
    //             lua_settable(L, -3);
    //         }
    //     }
    //     else 
    //         lua_pushnil(L);

    //     sdramFree(data);
    //     return 1;
    // }

    xSemaphoreTake(i2c->mutex, portMAX_DELAY);
    HAL_I2C_RegisterCallback(hi2c, HAL_I2C_MASTER_RX_COMPLETE_CB_ID, txrxCmplt);
    HAL_I2C_Master_Receive_IT(hi2c, address, data, len);
    if (xSemaphoreTake(i2c->sem, 1000) != pdTRUE)
    {
        lua_pushnil(L);
        sdramFree(data);
    }
    else
    {
        lua_newtable(L);
        for (uint32_t i = 0; i < len; i++)
        {
            lua_pushinteger(L, i + 1);
            lua_pushinteger(L, data[i]);
            lua_settable(L, -3);
        }
        sdramFree(data);
    }
    xSemaphoreGive(i2c->mutex);
    return 1;
}

// Регистрация методов
static const struct luaL_Reg i2c_methods[] = {
    {"begin", li2c_begin},
    {"write", li2c_write},
    {"read", li2c_read},
    {"end", li2c_end},
    {NULL, NULL}};

// Инициализация модуля
int luaopen_i2c(lua_State *L)
{
    luaL_newmetatable(L, "I2C");
    lua_pushvalue(L, -1);
    lua_setfield(L, -2, "__index");
    luaL_setfuncs(L, i2c_methods, 0);
    I2C_HandleTypeDef *hi2c = (I2C_HandleTypeDef *)lua_newuserdata(L, sizeof(I2C_HandleTypeDef));
    if (hi2c != NULL)
    {
        memset(hi2c, 0, sizeof(I2C_HandleTypeDef));
        hi2c->Instance = I2C4;
        luaL_getmetatable(L, "I2C");
        lua_setmetatable(L, -2);
        lua_setglobal(L, "Wire");
    }
    for (uint32_t i = 0; i < COUNT_I2C_PORTS; i++)
    {
        if (i2cMap[i].instance != NULL)
        {
            I2C_HandleTypeDef *hi2c = (I2C_HandleTypeDef *)lua_newuserdata(L, sizeof(I2C_HandleTypeDef));
            if (hi2c != NULL)
            {
                memset(hi2c, 0, sizeof(I2C_HandleTypeDef));
                hi2c->Instance = i2cMap[i].instance;
                luaL_getmetatable(L, "I2C");
                lua_setmetatable(L, -2);
                lua_setglobal(L, i2cNames[i]);
            }
        }
    }

    lua_pushinteger(L, I2C_100KHZ);
    lua_setglobal(L, "I2C100KHZ");
    lua_pushinteger(L, I2C_400KHZ);
    lua_setglobal(L, "I2C400KZ");
    lua_pushinteger(L, I2C_1MHZ);
    lua_setglobal(L, "I2C1MHZ");
    

    return 1;
}

/**

Wire:begin()
--Wire:begin(0x55, I2C400KZ)

local reg = {0x01, 0x00} -- Данные для отправки
Wire:write(0x70, data)
local recv = Wire:read(0xA0, 4)
if recv then
    print("Received:", table.unpack(recv))
end


**/
