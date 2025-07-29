#include "lua.h"
#include "lauxlib.h"
#include "lualib.h"
#include "stm32h7xx_hal.h"
#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"
#include "semphr.h"
#include "string.h"
#include "stdbool.h"
#include "sdram.h"

#include "stm32h7xx_hal_uart.h"
#include "stm32h7xx_hal_gpio.h"

// Структура для хранения состояния UART
typedef struct
{
    UART_HandleTypeDef *huart;
    USART_TypeDef *instance;
    uint8_t initialized;
    uint8_t data;
    GPIO_TypeDef *port;
    uint16_t txPin;
    uint16_t rxPin;
    uint8_t AF;
    IRQn_Type irqn;
    SemaphoreHandle_t txSemaphore;
    SemaphoreHandle_t mutex;
    QueueHandle_t rxQueue;
    void *queueStorage;
} Serial_Map;

static Serial_Map serials[] = {
    {NULL, USART2, 0, 0, GPIOA, GPIO_PIN_2, GPIO_PIN_3, GPIO_AF7_USART2, USART2_IRQn, NULL, NULL, NULL, NULL},
    {NULL, USART3, 0, 0, GPIOB, GPIO_PIN_10, GPIO_PIN_11, GPIO_AF7_USART3, USART3_IRQn, NULL, NULL, NULL, NULL},
    {NULL, UART5, 0, 0, GPIOB, GPIO_PIN_12, GPIO_PIN_13, GPIO_AF14_UART5, UART5_IRQn, NULL, NULL, NULL, NULL},
    // {NULL, USART1, 0, 0, GPIOB, GPIO_PIN_14, GPIO_PIN_15, GPIO_AF7_USART1, USART1_IRQn, NULL, NULL, NULL}
};

#define RX_QUEUE_SIZE 4096
#define COUNT_SERIAL_PORTS (sizeof(serials) / sizeof(Serial_Map))
#define U2 0
#define U3 1
#define U5 2
// #define U1 3

static inline Serial_Map *getSerial(UART_HandleTypeDef *huart)
{
    for (uint8_t i = 0; i < COUNT_SERIAL_PORTS; i++)
    {
        if (serials[i].instance == huart->Instance)
            return &serials[i];
    }
    return NULL;
}

static inline Serial_Map *setSerial(UART_HandleTypeDef *huart)
{
    for (uint8_t i = 0; i < COUNT_SERIAL_PORTS; i++)
    {
        if (serials[i].instance == huart->Instance)
        {
            serials[i].huart = huart;
            return &serials[i];
        }
    }
    return NULL;
}

// void USART1_IRQHandler(void)
// {
//     UART_HandleTypeDef *huart = serials[U1].huart;
//     if (huart != NULL)
//     {
//         HAL_UART_IRQHandler(huart);
//     }
// }

void USART2_IRQHandler(void)
{
    UART_HandleTypeDef *huart = serials[U2].huart;
    if (huart != NULL)
    {
        HAL_UART_IRQHandler(huart);
    }
}

void USART3_IRQHandler(void)
{
    UART_HandleTypeDef *huart = serials[U3].huart;
    if (huart != NULL)
    {
        HAL_UART_IRQHandler(huart);
    }
}

void UART5_IRQHandler(void)
{
    UART_HandleTypeDef *huart = serials[U5].huart;
    if (huart != NULL)
    {
        HAL_UART_IRQHandler(huart);
    }
}

// Обработчик прерывания UART
void RxCpltCallback(UART_HandleTypeDef *huart)
{
    Serial_Map *serial = getSerial(huart);
    xQueueSendFromISR(serial->rxQueue, &serial->data, NULL);
    HAL_UART_RegisterCallback(huart, HAL_UART_RX_COMPLETE_CB_ID, RxCpltCallback);
    HAL_UART_Receive_IT(huart, &serial->data, 1);
}

void TxCpltCallback(UART_HandleTypeDef *huart)
{
    Serial_Map *serial = getSerial(huart);
    if (serial->txSemaphore != NULL)
    {
        xSemaphoreGiveFromISR(serial->txSemaphore, NULL);
    }
}

static int lread(Serial_Map *serial, uint32_t timeout)
{
    uint8_t b;
    if (xQueueReceive(serial->rxQueue, &b, timeout) == pdTRUE)
    {
        return b;
    }
    return -1;
}

static int lwrite(Serial_Map *serial, uint8_t *data, uint32_t len)
{
    int bytes_written = 0;
    if (serial->txSemaphore != NULL && serial->mutex != NULL)
    {
        xSemaphoreTake(serial->mutex, portMAX_DELAY);
        HAL_UART_RegisterCallback(serial->huart, HAL_UART_TX_COMPLETE_CB_ID, TxCpltCallback);
        HAL_UART_Transmit_IT(serial->huart, data, len);
        xSemaphoreTake(serial->txSemaphore, 100);
        bytes_written = len - serial->huart->TxXferCount;
        xSemaphoreGive(serial->mutex);
    }
    return bytes_written;
}

// Изменяем структуру для хранения глобальных объектов Serial
static const char *serial_names[] = {
    "Serial",  // UART2
    "Serial1", // UART3
    "Serial2", // UART5
    "Serial3"  // USART1
};

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

static void enableUARTClock(USART_TypeDef *u)
{
    if (u == USART1)
        __HAL_RCC_USART1_CLK_ENABLE();
    else if (u == USART2)
        __HAL_RCC_USART2_CLK_ENABLE();
    else if (u == USART3)
        __HAL_RCC_USART3_CLK_ENABLE();
    else if (u == UART5)
        __HAL_RCC_UART5_CLK_ENABLE();
}

static int serial_begin(lua_State *L)
{
    UART_HandleTypeDef *huart = (UART_HandleTypeDef *)luaL_checkudata(L, 1, "UART");
    int baudrate = luaL_checkinteger(L, 2);

    Serial_Map *serial = getSerial(huart);
    setSerial(huart);
    RCC_PeriphCLKInitTypeDef PeriphClkInitStruct = {0};
    switch ((int)serial->instance)
    {
    case (int)USART1:
        PeriphClkInitStruct.PeriphClockSelection = RCC_PERIPHCLK_USART1;
        break;
    case (int)USART2:
        PeriphClkInitStruct.PeriphClockSelection = RCC_PERIPHCLK_USART2;
        break;
    case (int)USART3:
        PeriphClkInitStruct.PeriphClockSelection = RCC_PERIPHCLK_USART3;
        break;
    case (int)UART5:
        PeriphClkInitStruct.PeriphClockSelection = RCC_PERIPHCLK_UART5;
        break;
    default:
        lua_pushboolean(L, 0);
        return 1;
    }
    PeriphClkInitStruct.Usart16ClockSelection = RCC_USART16910CLKSOURCE_D2PCLK2;
    PeriphClkInitStruct.Usart234578ClockSelection = RCC_USART234578CLKSOURCE_D2PCLK1;
    if (HAL_RCCEx_PeriphCLKConfig(&PeriphClkInitStruct) != HAL_OK)
    {
        lua_pushboolean(L, 0);
        return 1;
    }
    enableUARTClock(serial->instance);

    // Настройка UART
    huart->Init.BaudRate = baudrate;
    huart->Init.WordLength = UART_WORDLENGTH_8B;
    huart->Init.StopBits = UART_STOPBITS_1;
    huart->Init.Parity = UART_PARITY_NONE;
    huart->Init.Mode = UART_MODE_TX_RX;
    huart->Init.HwFlowCtl = UART_HWCONTROL_NONE;
    huart->Init.OverSampling = UART_OVERSAMPLING_16;
    huart->Init.OneBitSampling = UART_ONE_BIT_SAMPLE_DISABLE;
    huart->Init.ClockPrescaler = UART_PRESCALER_DIV1;
    huart->AdvancedInit.AdvFeatureInit = UART_ADVFEATURE_RXOVERRUNDISABLE_INIT | UART_ADVFEATURE_DMADISABLEONERROR_INIT;
    huart->AdvancedInit.OverrunDisable = UART_ADVFEATURE_OVERRUN_DISABLE;
    huart->AdvancedInit.DMADisableonRxError = UART_ADVFEATURE_DMA_DISABLEONRXERROR;
    if (HAL_UART_Init(huart) != HAL_OK)
    {
        lua_pushboolean(L, 0);
        return 1;
    }
    if (HAL_UARTEx_SetTxFifoThreshold(huart, UART_TXFIFO_THRESHOLD_1_8) != HAL_OK)
    {
        lua_pushboolean(L, 0);
        return 1;
    }
    if (HAL_UARTEx_SetRxFifoThreshold(huart, UART_RXFIFO_THRESHOLD_1_8) != HAL_OK)
    {
        lua_pushboolean(L, 0);
        return 1;
    }
    if (HAL_UARTEx_DisableFifoMode(huart) != HAL_OK)
    {
        lua_pushboolean(L, 0);
        return 1;
    }

    if (!serial->rxQueue)
    {
        // serial->rxQueue = xQueueCreate(RX_QUEUE_SIZE, 1);
        uint8_t *queueStorage = sdramMalloc(RX_QUEUE_SIZE + sizeof(StaticQueue_t));
        StaticQueue_t *staticQueue = (StaticQueue_t *)(queueStorage + sizeof(StaticQueue_t));
        serial->rxQueue = xQueueCreateStatic(RX_QUEUE_SIZE, 1, queueStorage, staticQueue);
        serial->queueStorage = queueStorage;
    }

    enableGPIOClock(serial->port);

    GPIO_InitTypeDef GPIO_InitStruct = {0};
    GPIO_InitStruct.Pin = serial->txPin | serial->rxPin;
    GPIO_InitStruct.Mode = GPIO_MODE_AF_PP;
    GPIO_InitStruct.Pull = GPIO_NOPULL;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_HIGH;
    GPIO_InitStruct.Alternate = serial->AF;
    HAL_GPIO_Init(serial->port, &GPIO_InitStruct);

    if (!serial->txSemaphore)
        serial->txSemaphore = xSemaphoreCreateBinary();
    if (!serial->mutex)
        serial->mutex = xSemaphoreCreateMutex();

    HAL_NVIC_SetPriority(serial->irqn, 5, 0);
    HAL_NVIC_EnableIRQ(serial->irqn);
    HAL_UART_RegisterCallback(huart, HAL_UART_RX_COMPLETE_CB_ID, RxCpltCallback);
    HAL_UART_Receive_IT(huart, (uint8_t *)&serial->data, 1);

    serial->initialized = 1;
    lua_pushboolean(L, 1);
    return 1;
}

static int serial_end(lua_State *L)
{
    UART_HandleTypeDef *huart = (UART_HandleTypeDef *)luaL_checkudata(L, 1, "UART");
    Serial_Map *serial = getSerial(huart);
    if (!serial->initialized)
    {
        return luaL_error(L, "Serial not initialized");
        return 1;
    }
    HAL_UART_DeInit(huart);
    if (serial->rxQueue)
    {
        vQueueDelete(serial->rxQueue);
        serial->rxQueue = NULL;
        sdramFree(serial->queueStorage);
        serial->queueStorage = NULL;
    }
    HAL_GPIO_DeInit(serial->port, serial->txPin);
    HAL_GPIO_DeInit(serial->port, serial->rxPin);

    HAL_NVIC_DisableIRQ(serial->irqn);

    vSemaphoreDelete(serial->txSemaphore);
    vSemaphoreDelete(serial->mutex);
    serial->txSemaphore = NULL;
    serial->mutex = NULL;

    serial->initialized = 0;
    lua_pushboolean(L, 1);
    return 1;
}

// Метод print() для отправки строки
static int serial_print(lua_State *L)
{
    UART_HandleTypeDef *huart = (UART_HandleTypeDef *)luaL_checkudata(L, 1, "UART");
    Serial_Map *serial = getSerial(huart);
    if (!serial->initialized)
    {
        return luaL_error(L, "Serial not initialized");
    }
    const char *out = luaL_checkstring(L, 2);
    int bw = lwrite(serial, (uint8_t *)out, strlen(out));
    return bw;
}

// Метод available() для проверки наличия данных
static int serial_available(lua_State *L)
{
    UART_HandleTypeDef *huart = (UART_HandleTypeDef *)luaL_checkudata(L, 1, "UART");
    Serial_Map *serial = getSerial(huart);
    if (!serial->initialized)
    {
        lua_pushinteger(L, 0);
        return 1;
    }
    int q = uxQueueMessagesWaiting(serial->rxQueue);
    lua_pushinteger(L, q);
    return 1;
}

// Метод println() для печати строки с переводом строки
static int serial_println(lua_State *L)
{
    UART_HandleTypeDef *huart = (UART_HandleTypeDef *)luaL_checkudata(L, 1, "UART");
    Serial_Map *serial = getSerial(huart);
    if (!serial->initialized)
    {
        return luaL_error(L, "Serial not initialized");
    }
    int bw = 0;
    if (lua_gettop(L) > 1)
    {
        const char *out = luaL_checkstring(L, 2);
        size_t bw = lwrite(serial, (uint8_t *)out, strlen(out));
        if (bw == strlen(out))
        {
            uint8_t rn[2] = {'\r', '\n'};
            lwrite(serial, rn, 2);
        }
    }
    return bw;
}

// Метод write() для записи байта или массива байтов
static int serial_write(lua_State *L)
{
    UART_HandleTypeDef *huart = (UART_HandleTypeDef *)luaL_checkudata(L, 1, "UART");
    size_t len;
    Serial_Map *serial = getSerial(huart);
    if (!serial->initialized)
    {
        return luaL_error(L, "Serial not initialized");
    }

    if (lua_type(L, 2) == LUA_TNUMBER)
    {
        uint8_t byte = (uint8_t)luaL_checkinteger(L, 2);
        bool res = lwrite(serial, &byte, 1);
        lua_pushinteger(L, res);
    }
    else if (lua_type(L, 2) == LUA_TSTRING)
    {
        const char *out = lua_tolstring(L, 2, &len);
        int bw = lwrite(serial, (uint8_t *)out, strlen(out));
        return bw;
    }
    else if (lua_type(L, 2) == LUA_TTABLE)
    {

        int len = lua_rawlen(L, 2);
        uint8_t *data = sdramMalloc(len);
        for (int i = 0; i < len; i++)
        {
            lua_rawgeti(L, 2, i + 1);
            uint8_t b = (char)lua_tointeger(L, -1);
            lua_pop(L, 1);
            data[i] = b;
        }
        int bw = lwrite(serial, data, len);
        sdramFree(data);
        return bw;
    }
    else
    {
        return luaL_error(L, "Invalid argument type");
    }
    return 1;
}

// Метод read() для чтения одного байта
static int serial_read(lua_State *L)
{
    UART_HandleTypeDef *huart = (UART_HandleTypeDef *)luaL_checkudata(L, 1, "UART");
    Serial_Map *serial = getSerial(huart);
    if (!serial->initialized)
    {
        return luaL_error(L, "Serial not initialized");
    }
    int b = lread(serial, 0);
    if (b != -1)
    {
        lua_pushinteger(L, b);
        return 1;
    }
    lua_pushnil(L);
    return 1;
}

// Метод readBytes() для чтения нескольких байтов
static int serial_readBytes(lua_State *L)
{
    UART_HandleTypeDef *huart = (UART_HandleTypeDef *)luaL_checkudata(L, 1, "UART");
    size_t length = luaL_checkinteger(L, 2);
    int timeout = luaL_optinteger(L, 3, 1000); // Опциональный таймаут в мс
    Serial_Map *serial = getSerial(huart);
    if (!serial->initialized)
    {
        return luaL_error(L, "Serial not initialized");
    }

    size_t recvBytes = 0;
    lua_createtable(L, 0, 0);
    for (;;)
    {
        uint32_t t = xTaskGetTickCount();
        int b = lread(serial, timeout);
        if (b == -1)
        {
            lua_pushnil(L);
            return 1;
        }
        timeout -= xTaskGetTickCount() - t;
        recvBytes++;
        lua_pushinteger(L, b);
        lua_rawseti(L, -2, recvBytes);
        if (timeout <= 0 || recvBytes >= length)
        {
            break;
        }
    }
    return 1;
}

// Метод readString() для чтения строки до таймаута
static int serial_readString(lua_State *L)
{
    UART_HandleTypeDef *huart = (UART_HandleTypeDef *)luaL_checkudata(L, 1, "UART");
    int timeout = luaL_optinteger(L, 2, 1000); // Опциональный таймаут в мс
    Serial_Map *serial = getSerial(huart);
    if (!serial->initialized)
    {
        return luaL_error(L, "Serial not initialized");
    }

    lua_pushstring(L, ""); // Создаем пустую строку

    for (;;)
    {
        uint32_t t = xTaskGetTickCount();
        int byte = lread(serial, timeout);
        if (byte == -1)
        {
            lua_pushnil(L);
            return 1;
        }
        timeout -= xTaskGetTickCount() - t;

        char c = (char)byte;
        lua_pushlstring(L, &c, 1); // Добавляем один символ
        lua_concat(L, 2);          // Объединяем со строкой на стеке

        if (c == '\n' || c == '\r' || c == '\0' || timeout <= 0)
        {
            break;
        }
    }
    return 1;
}

// Регистрация методов
static const struct luaL_Reg serial_methods[] = {
    {"begin", serial_begin},
    {"end", serial_end},
    {"print", serial_print},
    {"println", serial_println},
    {"write", serial_write},
    {"available", serial_available},
    {"read", serial_read},
    {"readBytes", serial_readBytes},
    {"readString", serial_readString},
    {NULL, NULL}};

int luaopen_serial(lua_State *L)
{
    luaL_newmetatable(L, "UART");
    lua_pushvalue(L, -1);
    lua_setfield(L, -2, "__index");
    luaL_setfuncs(L, serial_methods, 0);

    // Создаем глобальные объекты Serial, Serial1, Serial2, Serial3
    for (uint32_t i = 0; i < COUNT_SERIAL_PORTS; i++)
    {
        if (serials[i].instance != NULL)
        {
            UART_HandleTypeDef *huart = (UART_HandleTypeDef *)lua_newuserdata(L, sizeof(UART_HandleTypeDef));
            if (huart != NULL)
            {
                memset(huart, 0, sizeof(UART_HandleTypeDef));
                huart->Instance = serials[i].instance;

                luaL_getmetatable(L, "UART");
                lua_setmetatable(L, -2);

                lua_setglobal(L, serial_names[i]);
            }
        }
    }

    return 1;
}

/**

-- Инициализация UART2
Serial:begin(9600)   -- UART2
Serial1:begin(9600)  -- UART3
Serial2:begin(9600)  -- UART5
Serial3:begin(9600)  -- USART1

-- Пример использования
Serial:println("Hello from UART2")
Serial1:println("Hello from UART3")

if Serial:available() > 0 then
    local data = Serial:read()
    print("Received from UART2:", data)
end

 */
