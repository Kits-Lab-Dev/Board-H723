#include "lua.h"
#include "lauxlib.h"
#include "lualib.h"
#include "stm32h7xx_hal.h"
#include "FreeRTOS.h"
#include "semphr.h"
#include "string.h"
#include "stdbool.h"
#include "sdram.h"

// Структура для хранения состояния SPI
typedef struct
{
    SPI_TypeDef *instance;
    SPI_HandleTypeDef *hspi;
    GPIO_TypeDef *port;
    GPIO_TypeDef *nssport;
    uint16_t mosi;
    uint16_t miso;
    uint16_t sck;
    uint16_t nss;
    uint16_t cs2;
    uint16_t cs3;
    uint16_t cs4;
    uint8_t chipselCount;
    uint8_t AF;
    IRQn_Type irqn;
    uint8_t initialized;
    SemaphoreHandle_t sem;
    SemaphoreHandle_t mutex;
} SPI_Map;

static SPI_Map spiMap[] = {
    {SPI1, NULL, GPIOA, GPIOA, GPIO_PIN_7, GPIO_PIN_6, GPIO_PIN_5, GPIO_PIN_4, GPIO_PIN_3, GPIO_PIN_2, GPIO_PIN_1, 1, GPIO_AF5_SPI1, SPI1_IRQn, 0, NULL, NULL},
    {SPI3, NULL, GPIOC, GPIOA, GPIO_PIN_12, GPIO_PIN_11, GPIO_PIN_10, GPIO_PIN_15, GPIO_PIN_10, GPIO_PIN_9, GPIO_PIN_8, 1, GPIO_AF6_SPI3, SPI3_IRQn, 0, NULL, NULL}};

static const char *spiNames[] = {
    "SPI",  // SPI1
    "SPI3", // SPI3
};

#define COUNT_SPI_PORTS (sizeof(spiMap) / sizeof(SPI_Map))

static inline SPI_Map *getSPI(SPI_HandleTypeDef *hspi)
{
    for (uint8_t i = 0; i < COUNT_SPI_PORTS; i++)
    {
        if (spiMap[i].instance == hspi->Instance)
            return &spiMap[i];
    }
    return NULL;
}

static inline SPI_Map *setSPI(SPI_HandleTypeDef *hspi)
{
    for (uint8_t i = 0; i < COUNT_SPI_PORTS; i++)
    {
        if (spiMap[i].instance == hspi->Instance)
        {
            spiMap[i].hspi = hspi;
            return &spiMap[i];
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
}

static void enableSPIClock(SPI_TypeDef *spi)
{
    if (spi == SPI1)
        __HAL_RCC_SPI1_CLK_ENABLE();
    else if (spi == SPI2)
        __HAL_RCC_SPI2_CLK_ENABLE();
    else if (spi == SPI3)
        __HAL_RCC_SPI3_CLK_ENABLE();
    else if (spi == SPI4)
        __HAL_RCC_SPI4_CLK_ENABLE();
    else if (spi == SPI5)
        __HAL_RCC_SPI5_CLK_ENABLE();
    else if (spi == SPI6)
        __HAL_RCC_SPI6_CLK_ENABLE();
}

#define SPI_62MHZ SPI_BAUDRATEPRESCALER_2
#define SPI_31MHZ SPI_BAUDRATEPRESCALER_4
#define SPI_15_5MHZ SPI_BAUDRATEPRESCALER_8
#define SPI_7_5MHZ SPI_BAUDRATEPRESCALER_16
#define SPI_3_75MHZ SPI_BAUDRATEPRESCALER_32
#define SPI_1_875MHZ SPI_BAUDRATEPRESCALER_64
#define SPI_937KHZ SPI_BAUDRATEPRESCALER_128
#define SPI_468KHZ SPI_BAUDRATEPRESCALER_256

#define SPI_MODE0 (0)
#define SPI_MODE1 (1)
#define SPI_MODE2 (2)
#define SPI_MODE3 (3)

#define SPIx1 0
#define SPIx3 1


void SPI1_IRQHandler(void)
{
    SPI_HandleTypeDef *hspi = spiMap[SPIx1].hspi;
    if (hspi)
        HAL_SPI_IRQHandler(hspi);
}

void SPI3_IRQHandler(void)
{
    SPI_HandleTypeDef *hspi = spiMap[SPIx3].hspi;
    if (hspi)
        HAL_SPI_IRQHandler(hspi);
}

static void txrxCmplt(SPI_HandleTypeDef *hspi)
{
    SPI_Map *spi = getSPI(hspi);
    if (spi)
    {
        if(spi->sem)
            xSemaphoreGiveFromISR(spi->sem, NULL);
    }
}

// Инициализация SPI
static int lspi_begin(lua_State *L)
{
    SPI_HandleTypeDef *hspi = (SPI_HandleTypeDef *)luaL_checkudata(L, 1, "SPI");
    SPI_Map *spi = getSPI(hspi);
    if (spi == NULL)
    {
        return luaL_error(L, "SPI device not found");
    }

    uint8_t csCount = luaL_optinteger(L, 2, 1);
    uint32_t mode = luaL_optinteger(L, 3, SPI_MODE0);
    uint32_t speed = luaL_optinteger(L, 4, SPI_1_875MHZ);
    
    if (csCount < 1 || csCount > 4)
    {
        return luaL_error(L, "Invalid CS count");
    }
    spi->chipselCount = csCount;
    RCC_PeriphCLKInitTypeDef PeriphClkInitStruct = {0};
    PeriphClkInitStruct.PeriphClockSelection = RCC_PERIPHCLK_SPI3 | RCC_PERIPHCLK_SPI1;
    PeriphClkInitStruct.Spi123ClockSelection = RCC_SPI123CLKSOURCE_PLL2;
    if (HAL_RCCEx_PeriphCLKConfig(&PeriphClkInitStruct) != HAL_OK)
    {
        lua_pushboolean(L, 0);
        return 1;
    }
    enableSPIClock(spi->instance);
    enableGPIOClock(spi->port);
    enableGPIOClock(spi->nssport);

    GPIO_InitTypeDef GPIO_InitStruct = {0};
    GPIO_InitStruct.Pin = spi->mosi | spi->miso | spi->sck;
    GPIO_InitStruct.Mode = GPIO_MODE_AF_PP;
    GPIO_InitStruct.Pull = GPIO_NOPULL;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_VERY_HIGH;
    GPIO_InitStruct.Alternate = spi->AF;
    HAL_GPIO_Init(spi->port, &GPIO_InitStruct);

    if (csCount > 1)
    {
        GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
        GPIO_InitStruct.Pin = spi->nss | spi->cs2;
        if (csCount > 2)
            GPIO_InitStruct.Pin |= spi->cs3;
        if (csCount > 3)
            GPIO_InitStruct.Pin |= spi->cs4;
    }
    else
        GPIO_InitStruct.Pin = spi->nss;

    HAL_GPIO_Init(spi->nssport, &GPIO_InitStruct);

    if (csCount > 1)
    {
        HAL_GPIO_WritePin(spi->nssport, spi->nss, GPIO_PIN_SET); // Chip select active high
        HAL_GPIO_WritePin(spi->nssport, spi->cs2, GPIO_PIN_SET); // Chip select active high
        if (csCount > 2)
            HAL_GPIO_WritePin(spi->nssport, spi->cs3, GPIO_PIN_SET); // Chip select active high
        if (csCount > 3)
            HAL_GPIO_WritePin(spi->nssport, spi->cs4, GPIO_PIN_SET); // Chip select active high
    }

    hspi->Init.Mode = SPI_MODE_MASTER;
    hspi->Init.Direction = SPI_DIRECTION_2LINES;
    hspi->Init.DataSize = SPI_DATASIZE_8BIT;
    hspi->Init.CLKPolarity = mode & 0x02 ? SPI_POLARITY_HIGH : SPI_POLARITY_LOW;
    hspi->Init.CLKPhase = mode & 0x01 ? SPI_PHASE_2EDGE : SPI_PHASE_1EDGE;
    hspi->Init.NSS = csCount == 1 ? SPI_NSS_HARD_OUTPUT : SPI_NSS_SOFT;
    hspi->Init.BaudRatePrescaler = speed;
    hspi->Init.FirstBit = SPI_FIRSTBIT_MSB;
    hspi->Init.TIMode = SPI_TIMODE_DISABLE;
    hspi->Init.CRCCalculation = SPI_CRCCALCULATION_DISABLE;
    hspi->Init.CRCPolynomial = 0x0;
    hspi->Init.NSSPMode = SPI_NSS_PULSE_DISABLE;
    hspi->Init.NSSPolarity = SPI_NSS_POLARITY_LOW;
    hspi->Init.FifoThreshold = SPI_FIFO_THRESHOLD_01DATA;
    hspi->Init.TxCRCInitializationPattern = SPI_CRC_INITIALIZATION_ALL_ZERO_PATTERN;
    hspi->Init.RxCRCInitializationPattern = SPI_CRC_INITIALIZATION_ALL_ZERO_PATTERN;
    hspi->Init.MasterSSIdleness = SPI_MASTER_SS_IDLENESS_01CYCLE;
    hspi->Init.MasterInterDataIdleness = SPI_MASTER_INTERDATA_IDLENESS_01CYCLE;
    hspi->Init.MasterReceiverAutoSusp = SPI_MASTER_RX_AUTOSUSP_DISABLE;
    hspi->Init.MasterKeepIOState = SPI_MASTER_KEEP_IO_STATE_DISABLE;
    hspi->Init.IOSwap = SPI_IO_SWAP_DISABLE;
    if (HAL_SPI_Init(hspi) != HAL_OK)
    {
        lua_pushboolean(L, 0);
        return 1;
    }

    spi->mutex = xSemaphoreCreateMutex();
    spi->sem = xSemaphoreCreateBinary();
    if (spi->mutex == NULL || spi->sem == NULL)
    {
        lua_pushboolean(L, 0);
        return 1;
    }

    spi->initialized = 1;
    lua_pushboolean(L, 1);
    return 1;
}

// Завершение работы SPI
static int lspi_end(lua_State *L)
{
    SPI_HandleTypeDef *hspi = (SPI_HandleTypeDef *)luaL_checkudata(L, 1, "SPI");
    SPI_Map *spi = getSPI(hspi);
    if (!spi || !spi->initialized)
    {
        luaL_error(L, "Invalid spi handle");
    }

    HAL_SPI_DeInit(hspi);

    if (spi->sem)
    {
        vSemaphoreDelete(spi->sem);
        spi->sem = NULL;
    }
    if (spi->mutex)
    {
        vSemaphoreDelete(spi->mutex);
        spi->mutex = NULL;
    }
    spi->initialized = 0;
    lua_pushboolean(L, 1);
    return 1;
}

// Отправка данных по SPI
static int lspi_write(lua_State *L)
{
    SPI_HandleTypeDef *hspi = (SPI_HandleTypeDef *)luaL_checkudata(L, 1, "SPI");
    SPI_Map *spi = getSPI(hspi);
    if (!spi || !spi->initialized)
    {
        luaL_error(L, "Invalid spi handle");
    }
    size_t len;
    uint8_t *data = NULL;
    if (lua_type(L, 2) == LUA_TSTRING)
    {
        const char *str = lua_tolstring(L, 2, &len);
        data = (uint8_t *)sdramMalloc(len);
        memcpy(data, str, len);
    }
    else if (lua_type(L, 2) == LUA_TTABLE)
    {
        len = lua_rawlen(L, 2);
        data = (uint8_t *)sdramMalloc(len);
        for (uint32_t i = 0; i < len; i++)
        {
            lua_rawgeti(L, 2, i + 1);
            data[i] = (uint8_t)lua_tointeger(L, -1);
            lua_pop(L, 1);
        }
    }
    else
    {
        lua_pushboolean(L, 0);
        return 1;
    }
    uint16_t cs = luaL_optinteger(L, 3, 1); // Chip select pin
    uint16_t pin = spi->nss;
    xSemaphoreTake(spi->mutex, portMAX_DELAY);
    if (spi->chipselCount > 1)
    {
        if (cs == 2)
            pin = spi->cs2;
        else if (cs == 3)
            pin = spi->cs3;
        else if (cs == 4)
            pin = spi->cs4;

        HAL_GPIO_WritePin(spi->nssport, pin, GPIO_PIN_RESET); // Chip select active low
    }
    HAL_SPI_RegisterCallback(hspi, HAL_SPI_TX_COMPLETE_CB_ID, txrxCmplt);
    HAL_SPI_Transmit_IT(hspi, data, len);
    if (xSemaphoreTake(spi->sem, 1000) != pdTRUE)
        lua_pushboolean(L, 0);
    else
        lua_pushboolean(L, 1);
    if (data)
        sdramFree(data);

    if (spi->chipselCount > 1)
    {
        HAL_GPIO_WritePin(spi->nssport, pin, GPIO_PIN_SET); // Chip select active high
    }

    xSemaphoreGive(spi->mutex);
    return 1;
}

// Получение данных по SPI
static int lspi_read(lua_State *L)
{
    SPI_HandleTypeDef *hspi = (SPI_HandleTypeDef *)luaL_checkudata(L, 1, "SPI");
    SPI_Map *spi = getSPI(hspi);
    if (!spi || !spi->initialized)
    {
        luaL_error(L, "Invalid spi handle");
    }

    size_t len = luaL_checkinteger(L, 2);
    uint8_t *data = (uint8_t *)sdramMalloc(len);
    
    uint16_t cs = luaL_optinteger(L, 3, 1); // Chip select pin
    uint16_t pin = spi->nss;
    xSemaphoreTake(spi->mutex, portMAX_DELAY);
    if (spi->chipselCount > 1)
    {
        if (cs == 2)
            pin = spi->cs2;
        else if (cs == 3)
            pin = spi->cs3;
        else if (cs == 4)
            pin = spi->cs4;

        HAL_GPIO_WritePin(spi->nssport, pin, GPIO_PIN_RESET); // Chip select active low
    }
    HAL_SPI_RegisterCallback(hspi, HAL_SPI_RX_COMPLETE_CB_ID, txrxCmplt);
    HAL_SPI_Receive_IT(hspi, data, len);
    bool res = xSemaphoreTake(spi->sem, 1000) != pdTRUE;
    if (spi->chipselCount > 1)
    {
        HAL_GPIO_WritePin(spi->nssport, pin, GPIO_PIN_SET); // Chip select active high
    }

    if (res)
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
    xSemaphoreGive(spi->mutex);
    return 1;
}

// Передача-прием данных через SPI
static int lspi_transfer(lua_State *L)
{
    SPI_HandleTypeDef *hspi = (SPI_HandleTypeDef *)luaL_checkudata(L, 1, "SPI");
    SPI_Map *spi = getSPI(hspi);
    if (spi == NULL)
    {
        return luaL_error(L, "SPI device not found");
    }

    size_t len;
    uint8_t *txData = NULL;
    if (lua_type(L, 2) == LUA_TSTRING)
    {
        const char *str = lua_tolstring(L, 2, &len);
        txData = (uint8_t *)sdramMalloc(len);
        memcpy(txData, str, len);
    }
    else if (lua_type(L, 2) == LUA_TTABLE)
    {
        len = lua_rawlen(L, 2);
        txData = (uint8_t *)sdramMalloc(len);
        for (uint32_t i = 0; i < len; i++)
        {
            lua_rawgeti(L, 2, i + 1);
            txData[i] = (uint8_t)lua_tointeger(L, -1);
            lua_pop(L, 1);
        }
    }
    else
    {
        lua_pushboolean(L, 0);
        return 1;
    }
    uint8_t *rxData = (uint8_t *)sdramMalloc(len);
    uint16_t cs = luaL_optinteger(L, 3, 1); // Chip select pin
    uint16_t pin = spi->nss;
    xSemaphoreTake(spi->mutex, portMAX_DELAY);
    if (spi->chipselCount > 1)
    {
        if (cs == 2)
            pin = spi->cs2;
        else if (cs == 3)
            pin = spi->cs3;
        else if (cs == 4)
            pin = spi->cs4;

        HAL_GPIO_WritePin(spi->nssport, pin, GPIO_PIN_RESET); // Chip select active low
    }
    HAL_SPI_RegisterCallback(hspi, HAL_SPI_TX_COMPLETE_CB_ID, txrxCmplt);
    HAL_SPI_TransmitReceive_IT(hspi, txData, rxData, len);
    bool res = xSemaphoreTake(spi->sem, 1000) != pdTRUE;
    if (spi->chipselCount > 1)
    {
        HAL_GPIO_WritePin(spi->nssport, pin, GPIO_PIN_SET); // Chip select active high
    }

    if (res)
    {
        lua_pushnil(L);
        sdramFree(txData);
        sdramFree(rxData);
    }
    else
    {
        lua_newtable(L);
        for (uint32_t i = 0; i < len; i++)
        {
            lua_pushinteger(L, i + 1);
            lua_pushinteger(L, rxData[i]);
            lua_settable(L, -3);
        }
        sdramFree(txData);
        sdramFree(rxData);
    }
    xSemaphoreGive(spi->mutex);
    return 1;
}

// Регистрация методов
static const struct luaL_Reg spi_methods[] = {
    {"begin", lspi_begin},
    {"write", lspi_write},
    {"read", lspi_read},
    {"transfer", lspi_transfer},
    {"end", lspi_end},
    {NULL, NULL}};

// Инициализация модуля
int luaopen_spi(lua_State *L)
{
    luaL_newmetatable(L, "SPI");
    lua_pushvalue(L, -1);
    lua_setfield(L, -2, "__index");
    luaL_setfuncs(L, spi_methods, 0);

    SPI_HandleTypeDef *hspi = (SPI_HandleTypeDef *)lua_newuserdata(L, sizeof(SPI_HandleTypeDef));
    if (hspi != NULL)
    {
        memset(hspi, 0, sizeof(SPI_HandleTypeDef));
        hspi->Instance = SPI1;
        luaL_getmetatable(L, "SPI");
        lua_setmetatable(L, -2);
        lua_setglobal(L, "SPI");
    }

    for (uint32_t i = 0; i < COUNT_SPI_PORTS; i++)
    {
        if (spiMap[i].instance != NULL)
        {
            SPI_HandleTypeDef *hspi = (SPI_HandleTypeDef *)lua_newuserdata(L, sizeof(SPI_HandleTypeDef));
            if (hspi != NULL)
            {
                memset(hspi, 0, sizeof(SPI_HandleTypeDef));
                hspi->Instance = spiMap[i].instance;
                luaL_getmetatable(L, "SPI");
                lua_setmetatable(L, -2);
                lua_setglobal(L, spiNames[i]);
            }
        }
    }

    lua_pushinteger(L, SPI_62MHZ);
    lua_setglobal(L, "SPI62MHZ");
    lua_pushinteger(L, SPI_31MHZ);
    lua_setglobal(L, "SPI31MHZ");
    lua_pushinteger(L, SPI_15_5MHZ);
    lua_setglobal(L, "SPI16MHZ");
    lua_pushinteger(L, SPI_7_5MHZ);
    lua_setglobal(L, "SPI8MHZ");
    lua_pushinteger(L, SPI_3_75MHZ);
    lua_setglobal(L, "SPI4MHZ");
    lua_pushinteger(L, SPI_1_875MHZ);
    lua_setglobal(L, "SPI2MHZ");
    lua_pushinteger(L, SPI_937KHZ);
    lua_setglobal(L, "SPI1MHZ");
    lua_pushinteger(L, SPI_468KHZ);
    lua_setglobal(L, "SPI500KHz");

    lua_pushinteger(L, SPI_MODE0); // SPI mode 0
    lua_setglobal(L, "SPI_MODE0");
    lua_pushinteger(L, SPI_MODE1); // SPI mode 1
    lua_setglobal(L, "SPI_MODE1");
    lua_pushinteger(L, SPI_MODE2); // SPI mode 2
    lua_setglobal(L, "SPI_MODE2");
    lua_pushinteger(L, SPI_MODE3); // SPI mode 3
    lua_setglobal(L, "SPI_MODE3");

    return 1;
}

/**
SPI:begin(2, SPI_MODE0, SPI_468KHZ)
local out_data = {0x51, 0x52, 0x53}
local in_data = SPI:transfer(data)

 */