#include "lua.h"
#include "lauxlib.h"
#include "lualib.h"
#include "stm32h7xx_hal.h"
#include "stm32h7xx_hal_fdcan.h"
#include "FreeRTOS.h"
#include "semphr.h"

#include "stdbool.h"
#include "sdram.h"

#define CAN_ID_STD 0
#define CAN_RTR_DATA 0
#define RX_QUEUE_SIZE 10

typedef struct
{
    FDCAN_GlobalTypeDef *instance;
    FDCAN_HandleTypeDef *handle;
    GPIO_TypeDef *port;
    uint16_t rx;
    uint16_t tx;
    uint8_t AF;
    IRQn_Type irqn;
    uint8_t initialized;
    SemaphoreHandle_t sem;
    SemaphoreHandle_t mutex;
    QueueHandle_t rxQueue;
    int callback;
    lua_State *L;
    TaskHandle_t rxTask;
} CAN_Map;

typedef struct __packed
{
    CAN_Map *can;
    uint32_t id;
    uint8_t size;
    bool rtr;
    bool extended;
    uint8_t data[8];
} CanMsg;

CAN_Map canMap[] = {
    {FDCAN2, NULL, GPIOB, GPIO_PIN_12, GPIO_PIN_13, GPIO_AF9_FDCAN2, FDCAN2_IT0_IRQn, 0, NULL, NULL, NULL, 0, NULL, NULL},
    {FDCAN3, NULL, GPIOA, GPIO_PIN_11, GPIO_PIN_12, GPIO_AF5_FDCAN3, FDCAN3_IT0_IRQn, 0, NULL, NULL, NULL, 0, NULL, NULL}};

static const char *canNames[] = {
    "CAN",
    "CAN1",
};

#define nCAN2 0
#define nCAN3 1

#define COUNT_CAN_PORTS (sizeof(canMap) / sizeof(CAN_Map))
#define CAN_RX_TASK_STACK_SIZE 1024

uint32_t inits = 0;

static inline CAN_Map *getCAN(FDCAN_HandleTypeDef *handle)
{
    for (uint8_t i = 0; i < COUNT_CAN_PORTS; i++)
    {
        if (canMap[i].handle == handle)
            return &canMap[i];
    }
    return NULL;
}

static inline CAN_Map *setCAN(FDCAN_HandleTypeDef *hcan)
{
    for (uint8_t i = 0; i < COUNT_CAN_PORTS; i++)
    {
        if (canMap[i].instance == hcan->Instance)
        {
            canMap[i].handle = hcan;
            return &canMap[i];
        }
    }
    return NULL;
}

void FDCAN2_IT0_IRQHandler(void)
{
    if (canMap[nCAN2].handle != NULL)
        HAL_FDCAN_IRQHandler(canMap[nCAN2].handle);
}

void FDCAN3_IT0_IRQHandler(void)
{
    if (canMap[nCAN3].handle != NULL)
        HAL_FDCAN_IRQHandler(canMap[nCAN3].handle);
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
}

static void rxCmplt(FDCAN_HandleTypeDef *handle)
{
    CAN_Map *can = getCAN(handle);
    if (can)
    {
        FDCAN_RxHeaderTypeDef header;
        CanMsg msg;
        if (HAL_FDCAN_GetRxMessage(handle, FDCAN_RX_FIFO0, &header, msg.data) == HAL_OK)
        {
            msg.can = can;
            msg.id = header.Identifier;
            msg.size = header.DataLength;
            xQueueSendFromISR(can->rxQueue, &msg, NULL);
        }
    }
}

static void txCmplt(FDCAN_HandleTypeDef *handle)
{
    CAN_Map *can = getCAN(handle);
    if (can)
    {
        xSemaphoreGiveFromISR(can->sem, NULL);
    }
}

// Инициализация CAN
static int lcan_begin(lua_State *L)
{
    FDCAN_HandleTypeDef *hcan = (FDCAN_HandleTypeDef *)luaL_checkudata(L, 1, "CANBUS");
    if (!hcan)
        return luaL_error(L, "Invalid handle");

    uint32_t speed = luaL_optinteger(L, 2, 500000);

    CAN_Map *can = setCAN(hcan);
    if (!can)
    {
        lua_pushnil(L);
        lua_pushstring(L, "CAN_NOT_INITIALIZED");
        return 2;
    }

    GPIO_InitTypeDef GPIO_InitStruct = {0};
    RCC_PeriphCLKInitTypeDef PeriphClkInitStruct = {0};
    PeriphClkInitStruct.PeriphClockSelection = RCC_PERIPHCLK_FDCAN;
    PeriphClkInitStruct.FdcanClockSelection = RCC_FDCANCLKSOURCE_PLL;
    if (HAL_RCCEx_PeriphCLKConfig(&PeriphClkInitStruct) != HAL_OK)
    {
        return luaL_error(L, "Failed to configure FDCAN clock");
    }
    __HAL_RCC_FDCAN_CLK_ENABLE();
    enableGPIOClock(can->port);
    GPIO_InitStruct.Pin = can->rx | can->tx;
    GPIO_InitStruct.Mode = GPIO_MODE_AF_PP;
    GPIO_InitStruct.Pull = GPIO_NOPULL;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_HIGH;
    GPIO_InitStruct.Alternate = can->AF;
    HAL_GPIO_Init(can->port, &GPIO_InitStruct);

    // Настройка CAN
    hcan->Init.FrameFormat = FDCAN_FRAME_CLASSIC;
    hcan->Init.Mode = FDCAN_MODE_NORMAL;
    hcan->Init.AutoRetransmission = DISABLE;
    hcan->Init.TransmitPause = DISABLE;
    hcan->Init.ProtocolException = DISABLE;
    hcan->Init.NominalPrescaler = 11;
    hcan->Init.NominalSyncJumpWidth = 1;
    hcan->Init.NominalTimeSeg1 = 14;
    hcan->Init.NominalTimeSeg2 = 5;
    hcan->Init.DataPrescaler = 11;
    hcan->Init.DataSyncJumpWidth = 1;
    hcan->Init.DataTimeSeg1 = 14;
    hcan->Init.DataTimeSeg2 = 5;
    hcan->Init.MessageRAMOffset = 0;
    hcan->Init.StdFiltersNbr = 1;
    hcan->Init.ExtFiltersNbr = 0;
    hcan->Init.RxFifo0ElmtsNbr = 1;
    hcan->Init.RxFifo0ElmtSize = FDCAN_DATA_BYTES_8;
    hcan->Init.RxFifo1ElmtsNbr = 0;
    hcan->Init.RxFifo1ElmtSize = FDCAN_DATA_BYTES_8;
    hcan->Init.RxBuffersNbr = 0;
    hcan->Init.RxBufferSize = FDCAN_DATA_BYTES_8;
    hcan->Init.TxEventsNbr = 0;
    hcan->Init.TxBuffersNbr = 0;
    hcan->Init.TxFifoQueueElmtsNbr = 1;
    hcan->Init.TxFifoQueueMode = FDCAN_TX_FIFO_OPERATION;
    hcan->Init.TxElmtSize = FDCAN_DATA_BYTES_8;

    if (HAL_FDCAN_Init(hcan) != HAL_OK)
        return luaL_error(L, "Failed to initialize CAN");
    if (!can->sem)
        can->sem = xSemaphoreCreateBinary();

    if (!can->mutex)
        can->mutex = xSemaphoreCreateMutex();

    if (!can->rxQueue)
        can->rxQueue = xQueueCreate(RX_QUEUE_SIZE, sizeof(CanMsg));

    HAL_NVIC_SetPriority(can->irqn, 5, 0);
    HAL_NVIC_EnableIRQ(can->irqn);

    HAL_StatusTypeDef res = HAL_FDCAN_Start(hcan);
    HAL_FDCAN_RegisterCallback(hcan, HAL_FDCAN_RX_BUFFER_NEW_MSG_CB_ID, rxCmplt);
    HAL_FDCAN_ActivateNotification(hcan, FDCAN_IT_TX_FIFO_EMPTY | FDCAN_IT_RX_FIFO0_NEW_MESSAGE | FDCAN_IT_RX_FIFO0_FULL, 0);
    if (res == HAL_OK)
    {
        can->initialized = 1;
    }
    lua_pushboolean(L, res == HAL_OK);
    return 1;
}

static int lcan_end(lua_State *L)
{
    FDCAN_HandleTypeDef *hcan = (FDCAN_HandleTypeDef *)luaL_checkudata(L, 1, "CANBUS");
    if (!hcan)
        return luaL_error(L, "Invalid handle");
    CAN_Map *can = getCAN(hcan);
    if (!can || !can->initialized)
        return luaL_error(L, "CAN not initialized");
    HAL_StatusTypeDef res = HAL_FDCAN_Stop(hcan);
    can->initialized = 0;

    if (can->rxQueue)
        vQueueDelete(can->rxQueue);
    can->rxQueue = NULL;
    if (can->sem)
        vSemaphoreDelete(can->sem);
    can->sem = NULL;
    if (can->mutex)
        vSemaphoreDelete(can->mutex);
    can->mutex = NULL;

    lua_pushboolean(L, res == HAL_OK);
    return 1;
}

static int lcan_transmit(lua_State *L)
{
    FDCAN_HandleTypeDef *hcan = (FDCAN_HandleTypeDef *)luaL_checkudata(L, 1, "CANBUS");
    if (!hcan)
        return luaL_error(L, "Invalid handle");
    CAN_Map *can = getCAN(hcan);
    if (!can || !can->initialized)
    {
        lua_pushnil(L);
        lua_pushstring(L, "CAN_NOT_INITIALIZED");
        return 2;
    }
    // Проверяем наличие таблицы в параметрах
    luaL_checktype(L, 2, LUA_TTABLE);

    bool rtr = false;
    bool extended = false;

    lua_getfield(L, 2, "rtr");
    if (lua_isboolean(L, -1))
        rtr = lua_toboolean(L, -1);
    lua_pop(L, 1);

    lua_getfield(L, 2, "extended");
    if (lua_isboolean(L, -1))
        extended = lua_toboolean(L, -1);
    lua_pop(L, 1);

    lua_getfield(L, 2, "id");
    uint32_t id = luaL_checkinteger(L, -1);
    lua_pop(L, 1);

    // Получаем данные
    lua_getfield(L, 2, "data");
    luaL_checktype(L, -1, LUA_TTABLE);
    size_t len = lua_rawlen(L, -1);
    uint8_t *data = (uint8_t *)sdramMalloc(len);
    if (!data)
    {
        lua_pushnil(L);
        lua_pushstring(L, "SDRAM_ALLOC_FAILED");
        return 2;
    }

    for (size_t i = 0; i < len; i++)
    {
        lua_rawgeti(L, -1, i + 1);
        data[i] = (uint8_t)lua_tointeger(L, -1);
        lua_pop(L, 1);
    }

    // Настройка заголовка передачи
    FDCAN_TxHeaderTypeDef TxHeader;
    TxHeader.Identifier = id;
    TxHeader.IdType = extended ? FDCAN_EXTENDED_ID : FDCAN_STANDARD_ID;
    TxHeader.TxFrameType = rtr ? FDCAN_REMOTE_FRAME : FDCAN_DATA_FRAME;
    TxHeader.DataLength = len;
    TxHeader.ErrorStateIndicator = FDCAN_ESI_ACTIVE;
    TxHeader.BitRateSwitch = FDCAN_BRS_OFF;
    TxHeader.FDFormat = FDCAN_CLASSIC_CAN;
    TxHeader.TxEventFifoControl = FDCAN_STORE_TX_EVENTS;
    TxHeader.Identifier = (uint32_t)lua_tointeger(L, 1);
    TxHeader.MessageMarker = 0;

    // Добавляем сообщение в очередь на передачу
    if (xSemaphoreTake(can->mutex, 1000) != pdTRUE)
    {
        sdramFree(data);
        lua_pushnil(L);
        lua_pushstring(L, "MUTEX_TIMEOUT");
        return 2;
    }
    HAL_FDCAN_RegisterCallback(hcan, HAL_FDCAN_TX_FIFO_EMPTY_CB_ID, txCmplt);
    HAL_StatusTypeDef status = HAL_FDCAN_AddMessageToTxFifoQ(hcan, &TxHeader, data);
    if (status != HAL_OK)
    {
        sdramFree(data);
        lua_pushnil(L);
        switch (hcan->ErrorCode)
        {
        case HAL_FDCAN_ERROR_FIFO_FULL:
            lua_pushstring(L, "FIFO_FULL");
            break;
        case HAL_FDCAN_ERROR_PARAM:
            lua_pushstring(L, "PARAM_ERROR");
            break;
        case HAL_FDCAN_ERROR_NOT_STARTED:
            lua_pushstring(L, "NOT_STARTED_ERROR");
            break;
        default:
            lua_pushstring(L, "UNKNOWN_ERROR");
            break;
        }
        return 2;
    }
    if (xSemaphoreTake(can->sem, 1000) != pdTRUE)
    {
        xSemaphoreGive(can->mutex);
        lua_pushnil(L);
        lua_pushstring(L, "TRANSMITE_FAILED");
        return 2;
    }
    sdramFree(data);
    xSemaphoreGive(can->mutex);
    lua_pushboolean(L, 1);
    return 1;
}

// Чтение данных из CAN
static int lcan_read(lua_State *L)
{
    FDCAN_HandleTypeDef *hcan = (FDCAN_HandleTypeDef *)luaL_checkudata(L, 1, "CANBUS");
    if (!hcan)
        return luaL_error(L, "Invalid handle");

    CAN_Map *can = getCAN(hcan);
    if (!can || !can->initialized)
    {
        lua_pushnil(L);
        lua_pushstring(L, "CAN_NOT_INITIALIZED");
        return 2;
    }
    CanMsg msg;
    if (xQueuePeek(can->rxQueue, &msg, 0) == pdTRUE)
    {
        if (xQueueReceive(can->rxQueue, &msg, 0) == pdTRUE)
        {
            lua_newtable(L);
            lua_pushinteger(L, msg.id);
            lua_setfield(L, -2, "id");
            lua_pushboolean(L, msg.rtr);
            lua_setfield(L, -2, "rtr");
            lua_pushboolean(L, msg.extended);
            lua_setfield(L, -2, "extended");
            lua_pushstring(L, "data");
            lua_newtable(L);
            for (int i = 0; i < msg.size; i++)
            {
                lua_pushinteger(L, i + 1);
                lua_pushinteger(L, msg.data[i]);
                lua_settable(L, -3);
            }
            lua_settable(L, -3);
            return 1;
        }
    }
    lua_pushnil(L);
    lua_pushstring(L, "NO_MESSAGE_AVAILABLE");
    return 2;
}

// Получение количества доступных пакетов в буфере
static int lcan_available(lua_State *L)
{
    FDCAN_HandleTypeDef *hcan = (FDCAN_HandleTypeDef *)luaL_checkudata(L, 1, "CANBUS");
    if (!hcan)
        return luaL_error(L, "Invalid handle");

    CAN_Map *can = getCAN(hcan);
    if (!can || !can->initialized)
    {
        lua_pushnil(L);
        lua_pushstring(L, "CAN_NOT_INITIALIZED");
        return 2;
    }

    lua_pushinteger(L, uxQueueMessagesWaiting(can->rxQueue));
    return 1;
}

// Установка фильтра для стандартных пакетов
static int lcan_filter(lua_State *L)
{
    FDCAN_HandleTypeDef *hcan = (FDCAN_HandleTypeDef *)luaL_checkudata(L, 1, "CAN");
    if (!hcan)
        return luaL_error(L, "Invalid handle");
    CAN_Map *can = getCAN(hcan);
    if (!can || !can->initialized)
    {
        lua_pushnil(L);
        lua_pushstring(L, "CAN_NOT_INITIALIZED");
        return 2;
    }
    uint32_t id = luaL_checkinteger(L, 2);
    uint32_t mask = lua_toboolean(L, 3);

    FDCAN_FilterTypeDef sFilterConfig;
    sFilterConfig.FilterIndex = 0;
    sFilterConfig.FilterType = FDCAN_STANDARD_ID;
    sFilterConfig.FilterConfig = FDCAN_FILTER_REJECT;
    sFilterConfig.IsCalibrationMsg = 0;
    sFilterConfig.FilterID1 = id;
    sFilterConfig.FilterID2 = mask;
    HAL_StatusTypeDef status = HAL_FDCAN_ConfigFilter(hcan, &sFilterConfig);
    if (status != HAL_OK)
    {
        lua_pushnil(L);
        lua_pushstring(L, "CONFIG_FILTER_ERROR");
        return 2;
    }

    lua_pushboolean(L, 1);
    return 1;
}

// Установка фильтра для расширенных пакетов
static int lcan_filterExtended(lua_State *L)
{
    FDCAN_HandleTypeDef *hcan = (FDCAN_HandleTypeDef *)luaL_checkudata(L, 1, "CAN");
    if (!hcan)
        return luaL_error(L, "Invalid handle");
    CAN_Map *can = getCAN(hcan);
    if (!can || !can->initialized)
    {
        lua_pushnil(L);
        lua_pushstring(L, "CAN_NOT_INITIALIZED");
        return 2;
    }
    uint32_t id = luaL_checkinteger(L, 2);
    uint32_t mask = lua_toboolean(L, 3);

    FDCAN_FilterTypeDef sFilterConfig;
    sFilterConfig.FilterIndex = 0;
    sFilterConfig.FilterType = FDCAN_EXTENDED_ID;
    sFilterConfig.FilterConfig = FDCAN_FILTER_REJECT;
    sFilterConfig.IsCalibrationMsg = 0;
    sFilterConfig.FilterID1 = id;
    sFilterConfig.FilterID2 = mask;
    HAL_StatusTypeDef status = HAL_FDCAN_ConfigFilter(hcan, &sFilterConfig);
    if (status != HAL_OK)
    {
        lua_pushnil(L);
        lua_pushstring(L, "CONFIG_FILTER_ERROR");
        return 2;
    }

    lua_pushboolean(L, 1);
    return 1;
}

void rxTask(void *pcan)
{
    CAN_Map *can = (CAN_Map *)pcan;
    lua_State *L = can->L;
    CanMsg msg;
    if (xQueueReceive(can->rxQueue, &msg, portMAX_DELAY) == pdTRUE)
    {
        if (can->callback != LUA_NOREF)
        {
            lua_State *threadL = lua_newthread(L);
            lua_rawgeti(L, LUA_REGISTRYINDEX, can->callback);

            lua_newtable(L);
            lua_pushinteger(L, msg.id);
            lua_setfield(L, -2, "id");
            lua_pushboolean(L, msg.rtr);
            lua_setfield(L, -2, "rtr");
            lua_pushboolean(L, msg.extended);
            lua_setfield(L, -2, "extended");
            lua_pushstring(L, "data");
            lua_newtable(L);
            for (int i = 0; i < msg.size; i++)
            {
                lua_pushinteger(L, i + 1);
                lua_pushinteger(L, msg.data[i]);
                lua_settable(L, -3);
            }
            lua_settable(L, -3);

            lua_xmove(L, threadL, 1);
            if (lua_isfunction(threadL, -1))
            {
                if (lua_pcall(threadL, 1, 0, 0) != LUA_OK)
                {
                    ERR("%s\n", lua_tostring(threadL, -1));
                    lua_pop(threadL, 1);
                }
            }
            lua_closethread(threadL, L);
        }
    }
}

static int lcan_onreceive(lua_State *L)
{
    FDCAN_HandleTypeDef *hcan = (FDCAN_HandleTypeDef *)luaL_checkudata(L, 1, "CANBUS");
    if (!hcan)
        return luaL_error(L, "Invalid handle");

    CAN_Map *can = getCAN(hcan);
    if (!can || !can->initialized)
    {
        lua_pushnil(L);
        lua_pushstring(L, "CAN_NOT_INITIALIZED");
        return 2;
    }
    int callback = 0;
    // Сохраняем callback функцию в registry
    if (lua_isfunction(L, 2))
    {
        callback = luaL_ref(L, LUA_REGISTRYINDEX);
        // Сохраняем состояние Lua и ссылку на callback
        can->L = L;
        can->callback = callback;
        xTaskCreate(rxTask, "canRxTask", CAN_RX_TASK_STACK_SIZE, (void *)can, tskIDLE_PRIORITY + 1, &can->rxTask);
    }
    else
    {
        can->L = NULL;
        can->callback = 0;
        if(can->rxTask)
            vTaskDelete(can->rxTask);
    }
    lua_pushboolean(L, true);
    return 1;
}

// Регистрация методов
static const struct luaL_Reg can_methods[] = {
    {"begin", lcan_begin},
    {"end", lcan_end},
    {"transmit", lcan_transmit},
    {"read", lcan_read},
    {"available", lcan_available},
    {"filter", lcan_filter},
    {"filterExtended", lcan_filterExtended},
    {"onReceive", lcan_onreceive},
    {NULL, NULL}};

int luaopen_can(lua_State *L)
{
    luaL_newmetatable(L, "CANBUS");
    lua_pushvalue(L, -1);
    lua_setfield(L, -2, "__index");
    luaL_setfuncs(L, can_methods, 0);

    for (uint32_t i = 0; i < COUNT_CAN_PORTS; i++)
    {
        if (canMap[i].instance != NULL)
        {
            FDCAN_HandleTypeDef *hcan = (FDCAN_HandleTypeDef *)lua_newuserdata(L, sizeof(FDCAN_HandleTypeDef));
            if (hcan)
            {
                memset(hcan, 0, sizeof(FDCAN_HandleTypeDef));
                hcan->Instance = canMap[i].instance;
                luaL_getmetatable(L, "CANBUS");
                lua_setmetatable(L, -2);
                lua_setglobal(L, canNames[i]);
            }
        }
    }

    return 1;
}

/*

{
    id = 1,
    rtr = true,
    extended = true,
    data = {1, 2, 3}
}

*/