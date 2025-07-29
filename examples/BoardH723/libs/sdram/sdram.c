#include "sdram.h"
#include "fmc.h"
#include "FreeRTOS.h"
#include "semphr.h"

static void *sdram = (void *)SDRAM_BANK1_ADDR;

lwmem_region_t regions[2];

lwmem_t lwmem;

static SemaphoreHandle_t mutex;

bool sdramInit()
{
    mutex = xSemaphoreCreateMutex();
    regions[0].start_addr = sdram;
    regions[0].size = SDRAM_BANK1_SIZE;
    regions[1].start_addr = NULL;
    regions[1].size = 0;

    SDRAM_HandleTypeDef *hsdram = &hsdram1;
    FMC_SDRAM_CommandTypeDef command = {0};
    __IO uint32_t tmpmrd = 0;
    /* Step 3:  Configure a clock configuration enable command */
    command.CommandTarget = FMC_SDRAM_CMD_TARGET_BANK1;
    command.CommandMode = FMC_SDRAM_CMD_CLK_ENABLE;
    command.AutoRefreshNumber = 2;
    command.ModeRegisterDefinition = 0;
    if (HAL_SDRAM_SendCommand(hsdram, &command, SDRAM_TIMEOUT) != HAL_OK)
        return false;

    /* Step 4: Insert 100 us minimum delay */
    /* Inserted delay is equal to 1 ms due to systick time base unit (ms) */
    HAL_Delay(1);

    /* Step 5: Configure a PALL (precharge all) command */
    command.CommandMode = FMC_SDRAM_CMD_PALL;
    command.AutoRefreshNumber = 1;
    command.ModeRegisterDefinition = 0;
    if (HAL_SDRAM_SendCommand(hsdram, &command, SDRAM_TIMEOUT) != HAL_OK)
        return false;

    /* Step 6 : Configure a Auto-Refresh command */
    command.CommandMode = FMC_SDRAM_CMD_AUTOREFRESH_MODE;
    command.AutoRefreshNumber = 8;
    command.ModeRegisterDefinition = 0;
    if (HAL_SDRAM_SendCommand(hsdram, &command, SDRAM_TIMEOUT) != HAL_OK)
        return false;

    /* Step 7: Program the external memory mode register */
    tmpmrd = (uint32_t)SDRAM_MODEREG_BURST_LENGTH_1 |
             SDRAM_MODEREG_BURST_TYPE_SEQUENTIAL |
             SDRAM_MODEREG_CAS_LATENCY_3 |
             SDRAM_MODEREG_OPERATING_MODE_STANDARD |
             SDRAM_MODEREG_WRITEBURST_MODE_SINGLE;
    command.CommandMode = FMC_SDRAM_CMD_LOAD_MODE;
    command.AutoRefreshNumber = 1;
    command.ModeRegisterDefinition = tmpmrd;
    if (HAL_SDRAM_SendCommand(hsdram, &command, SDRAM_TIMEOUT) != HAL_OK)
        return false;

    /* Step 8: Set the refresh rate counter */
    /* (15.62 us x Freq) - 20 */
    /* Set the device refresh counter */
    if (HAL_SDRAM_ProgramRefreshRate(hsdram, 2000) != HAL_OK)
        return false;

    // инициализация менеджера памяти
    if (!lwmem_assignmem_ex(&lwmem, regions))
        return false;
    return true;
}

 void *sdramMalloc(size_t size)
{
    if (xMutexTake(mutex, portMAX_DELAY) == pdFALSE)
        return 0;
    void *p = lwmem_malloc_ex(&lwmem, regions, size);
    xMutexGive(mutex);
    return p;
}

 void *sdramRealloc(void *p, size_t size)
{
    if (xMutexTake(mutex, portMAX_DELAY) == pdFALSE)
        return 0;
    void *ptr = lwmem_realloc_ex(&lwmem, regions, p, size);
    xMutexGive(mutex);
    return ptr;
}

void *sdramCalloc(size_t nmemb, size_t size)
{
    if (xSemaphoreTake(mutex, portMAX_DELAY) == pdFALSE)
        return NULL;
    void *p = lwmem_calloc_ex(&lwmem, regions, nmemb, size);
    xSemaphoreGive(mutex);
    return p;
}

 void sdramFree(void *p)
{
    if (xMutexTake(mutex, portMAX_DELAY) == pdFALSE)
        return;
    lwmem_free_ex(&lwmem, p);
    xMutexGive(mutex);
}

uint32_t sdramGetUseMem()
{
    if (xMutexTake(mutex, portMAX_DELAY) == pdFALSE)
        return 0;
    uint32_t um = SDRAM_BANK1_SIZE - lwmem.mem_available_bytes;
    xSemaphoreGive(mutex);
    return um;
}

uint32_t sdramGetFreeMem()
{
    if (xMutexTake(mutex, portMAX_DELAY) == pdFALSE)
        return 0;
    uint32_t fm = lwmem.mem_available_bytes;
    xMutexGive(mutex);
    return fm;
}