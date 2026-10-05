#include "app.h"
#include "stdio.h"
#include <stdarg.h>
#include "FreeRTOS.h"
#include "task.h"
#include "FreeRTOS_IP.h"
#include "sdram.h"
#include "ramdisk.h"
#include "dts.h"
#include "rtc.h"
#include "usb_device.h"
#include "fmc.h"
#include "usb_otg.h"
#include "control.h"
#include "xNetworkInterface.h"
#include "bt.h"
#include <sddisk.h>
#include "dhcpserver.h"
#ifdef WITH_SSH
#include "ssh_server.h"
#endif
#include "rng.h"
#include "ntpclient.h"

#ifdef WITH_LUA
#include "luaengine.h"
#endif

#define BOOTLOOADER_ADDRESS D1_AXISRAM1_BASE
#define BOOTLOADER_SIZE (320 * 1024)
#define BOOTLOADER_PATH "/SD/utils/ESPLoader.bin"

#define HOSTNAME "MCU"

uint8_t ucHeap[configTOTAL_HEAP_SIZE] __RAM_D1;

uint32_t ulNextRand;
UBaseType_t uxRand(void)
{
    const uint32_t ulMultiplier = 1664525UL;
    const uint32_t ulIncrement = 1013904223UL;
    ulNextRand = (ulMultiplier * ulNextRand + ulIncrement);
    return ulNextRand;
}

bool enterBootloaderMode = false;

#define APP_TASK_STACK_SIZE (1024)
#define APP_TASK_PRIORITY (tskIDLE_PRIORITY + 1)

 StackType_t appTaskStack[APP_TASK_STACK_SIZE] __ITCMRAM;
 StaticTask_t appTaskBuffer __ITCMRAM;

#ifdef WITH_LUA
extern const char *mainlua;

#define LUA_TASK_STACK_SIZE (1024 * 8)
#define LUA_TASK_PRIORITY (tskIDLE_PRIORITY + 1)
StackType_t luaTaskStack[LUA_TASK_STACK_SIZE] = {0};
StaticTask_t luaTaskBuffer = {0};

void luaTask(void *)
{
    lua_State *L;
    for (;;)
    {
        L = luaInit();
        luaRun(L, mainlua);
        luaDeinit(L);
    }
}
#endif

void bootloader()
{
    uint32_t address = HAL_RTCEx_BKUPRead(&hrtc, RTC_BKP_DR31);
    if (address == BOOTLOOADER_ADDRESS)
    {
        __disable_irq();
        HAL_PWR_EnableBkUpAccess();
        HAL_RTCEx_BKUPWrite(&hrtc, RTC_BKP_DR31, 0);
        HAL_PWR_DisableBkUpAccess();
        __set_PRIMASK(1);
        SCB->VTOR = address;
        // uint32_t msp_value = *(__IO uint32_t *)address;
        // __set_MSP(msp_value);
        __IO uint32_t a = *(__IO uint32_t *)(address + 4);
        void (*jump)(void) = (void (*)(void))a;
        __enable_irq();
        jump();
    }
}

static void checkBootMode()
{
    if (enterBootloaderMode)
    {
        WRN("bootloader mode...\n");
        uint8_t *loader = sdramMalloc(BOOTLOADER_SIZE);
        FILE *f = fopen(BOOTLOADER_PATH, "r+");
        if (!f)
        {
            ERR("Failed to open bootloader file\n");
            return;
        }
        int rb = fread(loader, 1, BOOTLOADER_SIZE, f);
        if (rb > 0)
        {
            vTaskDelay(100);
            vPortEnterCritical();
            __disable_irq();
            HAL_MPU_Disable();
            rb = (rb / 4) + 1;
            uint32_t *p = (uint32_t *)BOOTLOOADER_ADDRESS;
            for (int i = 0; i < rb; i++)
            {
                p[i] = ((uint32_t *)loader)[i];
            }
            SCB_InvalidateICache();
            SCB_CleanDCache();
            SCB_DisableDCache();
            SCB_DisableICache();
            HAL_PWR_EnableBkUpAccess();
            HAL_RTCEx_BKUPWrite(&hrtc, RTC_BKP_DR31, BOOTLOOADER_ADDRESS);
            HAL_PWR_DisableBkUpAccess();
            NVIC_SystemReset();
        }
    }
}

void appTask(void *)
{
    HAL_RNG_GenerateRandomNumber(&hrng, &ulNextRand);

    MX_USB_DEVICE_Init();

    HAL_DTS_Start(&hdts);
    sdramInit();
    logInit();

    ramDiskMount();

    espInit();
    networkInterfaceInitialise();
    dhcpServerStart();
    bleStackInit();
    espEnable(true);
    vTaskDelay(250);

    espFeatureEnable(ftBluetooth, true);
    ble_app_start();

    static DHCPServer WiFiAPdhcpServer;
    WiFiAPdhcpServer.stopRequest = false;
    WiFiAPdhcpServer.ipAddr.ulIP_IPv4 = FreeRTOS_inet_addr("192.168.11.1");
    WiFiAPdhcpServer.subnetMask.ulIP_IPv4 = FreeRTOS_inet_addr("255.255.255.0");
    WiFiAPdhcpServer.gateway.ulIP_IPv4 = FreeRTOS_inet_addr("192.168.11.1");
    WiFiAPdhcpServer.netInterface = &apNetInterface;
    dhcpAddServer(&WiFiAPdhcpServer);

    sdCardDetectLoop();

#ifdef WITH_LUA
    DIR d;
    if (f_opendir(&d, "/RAM/lib") != FR_OK)
    {
        f_mkdir("/RAM/lib");
    }
    if (f_opendir(&d, "/RAM/.ssh") != FR_OK)
    {
        f_mkdir("/RAM/.ssh");
    }
    xTaskCreateStatic(luaTask, "luaTask", LUA_TASK_STACK_SIZE, NULL, LUA_TASK_PRIORITY, luaTaskStack, &luaTaskBuffer);
#endif

#ifdef WITH_SSH
    SSHServerStart();
#endif

    startNTPTask();
    
    for (;;)
    {
        checkBootMode();
        sdCardDetectLoop();
        vTaskDelay(1000);
    }
}

void lowPriorityTickHook()
{
    static uint32_t hrLedTicks;
    if ((xTaskGetTickCount() - hrLedTicks) > 250)
    {
        hrLedTicks = xTaskGetTickCount();
        HAL_GPIO_TogglePin(LED_GPIO_Port, LED_Pin);
    }
    static uint32_t bootMode;
    if (HAL_GPIO_ReadPin(BOOT_BTN_GPIO_Port, BOOT_BTN_Pin) == GPIO_PIN_SET)
    {
        bootMode++;
        if (bootMode > 500) // system bootloader
        {
            NVIC_SystemReset();
        }
    }
    else
    {
        if (bootMode > 0)
        {
            enterBootloaderMode = true;
        }
    }
}

void app() // адрес 0 в ITCMRAM закрыт секцией .itcm_null_guard в BoardH723.ld
{
    xTaskCreateStatic(appTask, "appTask", APP_TASK_STACK_SIZE, NULL, APP_TASK_PRIORITY, appTaskStack, &appTaskBuffer);
    vTaskStartScheduler();
    for (;;)
    {
        HAL_Delay(100);
    }
}

volatile float CPU_IDLE = 0;
void vApplicationIdleHook(void)
{
    static portTickType LastTick;
    static uint32_t count;
    static uint32_t max_count;

    count++;

    if ((xTaskGetTickCount() - LastTick) > 1000)
    {
        LastTick = xTaskGetTickCount();
        if (count > max_count)
            max_count = count;
        CPU_IDLE = (100.0f * (float)count) / (float)max_count;
        count = 0;
    }

    static uint32_t lowPriTick;
    if ((xTaskGetTickCount() - lowPriTick) > 1)
    {
        lowPriTick = xTaskGetTickCount();
        lowPriorityTickHook();
    }
}

void vApplicationGetIdleTaskMemory(StaticTask_t **ppxIdleTaskTCBBuffer, StackType_t **ppxIdleTaskStackBuffer, uint32_t *pulIdleTaskStackSize)
{

    static StaticTask_t Idle_TCB;
    static StackType_t Idle_Stack[configMINIMAL_STACK_SIZE];

    *ppxIdleTaskTCBBuffer = &Idle_TCB;
    *ppxIdleTaskStackBuffer = &Idle_Stack[0];
    *pulIdleTaskStackSize = (uint32_t)configMINIMAL_STACK_SIZE;
}

void vApplicationGetTimerTaskMemory(StaticTask_t **ppxTimerTaskTCBBuffer, StackType_t **ppxTimerTaskStackBuffer, uint32_t *pulTimerTaskStackSize)
{
    /* Timer task control block and stack */
    static StaticTask_t Timer_TCB;
    static StackType_t Timer_Stack[configTIMER_TASK_STACK_DEPTH];

    *ppxTimerTaskTCBBuffer = &Timer_TCB;
    *ppxTimerTaskStackBuffer = &Timer_Stack[0];
    *pulTimerTaskStackSize = (uint32_t)configTIMER_TASK_STACK_DEPTH;
}

void vApplicationStackOverflowHook(TaskHandle_t xTask, char *pcTaskName)
{
    UNUSED(xTask);
    ERR("StackOverflow: %s", pcTaskName);
}

void vApplicationIPNetworkEventHook_Multi(eIPCallbackEvent_t eNetworkEvent,
                                          struct xNetworkEndPoint *pxEndPoint)

{
    uint32_t ulIPAddress, ulNetMask, ulGatewayAddress, ulDNSServerAddress;
    char cBuffer[16];

    /* Если сеть только что поднялась... */
    if (eNetworkEvent == eNetworkUp)
    {
        INF("Network is up!\n");
#if defined(ipconfigIPv4_BACKWARD_COMPATIBLE) && (ipconfigIPv4_BACKWARD_COMPATIBLE == 0)
        FreeRTOS_GetEndPointConfiguration(&ulIPAddress, &ulNetMask, &ulGatewayAddress, &ulDNSServerAddress, pxEndPoint);
#else
        FreeRTOS_GetAddressConfiguration(&ulIPAddress, &ulNetMask, &ulGatewayAddress, &ulDNSServerAddress);
#endif /* defined( ipconfigIPv4_BACKWARD_COMPATIBLE ) && ( ipconfigIPv4_BACKWARD_COMPATIBLE == 0 ) */
        FreeRTOS_inet_ntoa(ulIPAddress, cBuffer);
        INF("\r\nIP Address: %s\n", cBuffer);

        FreeRTOS_inet_ntoa(ulNetMask, cBuffer);
        INF("Subnet Mask: %s\n", cBuffer);

        FreeRTOS_inet_ntoa(ulGatewayAddress, cBuffer);
        INF("Gateway Address: %s\n", cBuffer);

        FreeRTOS_inet_ntoa(ulDNSServerAddress, cBuffer);
        INF("DNS Server Address: %s\n\n", cBuffer);
    }
    else
    {
        INF("Network is down!\n");
    }
}

BaseType_t xApplicationGetRandomNumber(uint32_t *pulNumber)
{
    *(pulNumber) = uxRand();
    return pdTRUE;
}

uint32_t ulApplicationGetNextSequenceNumber(uint32_t ulSourceAddress,
                                            uint16_t usSourcePort,
                                            uint32_t ulDestinationAddress,
                                            uint16_t usDestinationPort)
{
    (void)ulSourceAddress;
    (void)usSourcePort;
    (void)ulDestinationAddress;
    (void)usDestinationPort;

    return (uint32_t)uxRand();
}

const char *pcApplicationHostnameHook(void)
{
    return HOSTNAME;
}

BaseType_t xApplicationDNSQueryHook_Multi(struct xNetworkEndPoint *pxEndPoint,
                                          const char *pcName)
{
    UNUSED(pxEndPoint);
    BaseType_t xReturn;
    if (strcmp(pcName, HOSTNAME) == 0)
    {
        xReturn = pdPASS;
    }
    else
    {
        xReturn = pdFAIL;
    }

    return xReturn;
}

void vApplicationPingReplyHook(ePingReplyStatus_t eStatus, uint16_t usIdentifier)
{
    UNUSED(eStatus);
    UNUSED(usIdentifier);
}

uint32_t ulApplicationTimeHook(void)
{
    return 0;
}

eDHCPCallbackAnswer_t xApplicationDHCPHook_Multi(eDHCPCallbackPhase_t eDHCPPhase, struct xNetworkEndPoint *pxEndPoint, IP_Address_t *pxIPAddress)
{
    UNUSED(pxEndPoint);
    UNUSED(pxIPAddress);
    UNUSED(eDHCPPhase);
    return eDHCPContinue;
}