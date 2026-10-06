#include "ntpclient.h"
#include "time.h"
#include "rtc.h"
#include "stdint.h"

#ifndef TIME_ZONE
#define TIME_ZONE (+3)
#endif

#define TIME_ZONE_SEC (TIME_ZONE * 3600)

enum EStatus
{
    EStatusLookup,
    EStatusAsking,
    EStatusPause,
    EStatusFailed,
};

static enum EStatus xStatus = EStatusLookup;

static const char *pcTimeServers[] =
    {
        "0.asia.pool.ntp.org",
        "0.europe.pool.ntp.org",
        "0.id.pool.ntp.org",
        "0.south-america.pool.ntp.org",
        "0.oceania.pool.ntp.org",
        "0.north-america.pool.ntp.org"
    };

static BaseType_t xHasIPAddress = pdFALSE;
static TickType_t uxSendTime;
static struct freertos_sockaddr xIPAddressFound;
static uint32_t ulIPAddressFound;
static Socket_t xNTP_UDPSocket = NULL;
static struct SNtpPacket xNTPPacket;
static char cRecvBuffer[sizeof(struct SNtpPacket) + 64];

static void NTPTask(void *);

time_t get_time(time_t *puxTime)
{
    RTC_TimeTypeDef sTime;
    RTC_DateTypeDef sDate;

    // Read current RTC time and date
    HAL_RTC_GetTime(&hrtc, &sTime, RTC_FORMAT_BIN);
    HAL_RTC_GetDate(&hrtc, &sDate, RTC_FORMAT_BIN);

    // Convert RTC data to time_t (Unix epoch)
    struct tm tm = {0};
    tm.tm_year = sDate.Year + 100; // 2000-2099 -> 100-199
    tm.tm_mon = sDate.Month - 1;
    tm.tm_mday = sDate.Date;
    tm.tm_hour = sTime.Hours;
    tm.tm_min = sTime.Minutes;
    tm.tm_sec = sTime.Seconds;
    tm.tm_isdst = 0;

    time_t t = mktime(&tm);
    if (puxTime != NULL)
        *puxTime = t;
    return t;
}

int set_time(const time_t *t)
{
    struct tm tm;
    gmtime_r(t, &tm); // Use UTC time

    // Неиспользуемые поля обнулены: мусор в WeekDay при записи сдвигается
    // в биты года регистра RTC_DR и портит год
    RTC_TimeTypeDef sTime = {0};
    RTC_DateTypeDef sDate = {0};

    // Convert time_t to RTC format
    sDate.WeekDay = tm.tm_wday == 0 ? RTC_WEEKDAY_SUNDAY : tm.tm_wday;
    sDate.Year = tm.tm_year - 100; // 100-199 -> 2000-2099
    sDate.Month = tm.tm_mon + 1;
    sDate.Date = tm.tm_mday;
    sTime.Hours = tm.tm_hour;
    sTime.Minutes = tm.tm_min;
    sTime.Seconds = tm.tm_sec;

    // Write to RTC
    HAL_RTC_SetTime(&hrtc, &sTime, RTC_FORMAT_BIN);
    HAL_RTC_SetDate(&hrtc, &sDate, RTC_FORMAT_BIN);
    return 0;
}

void startNTPTask()
{
    xNTP_UDPSocket = FreeRTOS_socket(FREERTOS_AF_INET, FREERTOS_SOCK_DGRAM, FREERTOS_IPPROTO_UDP);

    if (xNTP_UDPSocket != NULL)
    {
        struct freertos_sockaddr xAddress;
        BaseType_t xReceiveTimeOut = pdMS_TO_TICKS(5000);

        xAddress.sin_address.ulIP_IPv4 = 0ul;
        xAddress.sin_port = FreeRTOS_htons(NTP_PORT);
        xAddress.sin_family = FREERTOS_AF_INET;

        FreeRTOS_bind(xNTP_UDPSocket, &xAddress, sizeof(xAddress));
        FreeRTOS_setsockopt(xNTP_UDPSocket, 0, FREERTOS_SO_RCVTIMEO, &xReceiveTimeOut, sizeof(xReceiveTimeOut));
        xTaskCreate(NTPTask, (const char *)"NTP client", 1024, NULL, 0, NULL);
    }
    else
    {
        WRN(("Creating socket failed\n"));
    }
}

static void prvSwapFields(struct SNtpPacket *pxPacket)
{
    /* NTP messages are big-endian */
    pxPacket->rootDelay = FreeRTOS_htonl(pxPacket->rootDelay);
    pxPacket->rootDispersion = FreeRTOS_htonl(pxPacket->rootDispersion);

    pxPacket->referenceTimestamp.seconds = FreeRTOS_htonl(pxPacket->referenceTimestamp.seconds);
    pxPacket->referenceTimestamp.fraction = FreeRTOS_htonl(pxPacket->referenceTimestamp.fraction);

    pxPacket->originateTimestamp.seconds = FreeRTOS_htonl(pxPacket->originateTimestamp.seconds);
    pxPacket->originateTimestamp.fraction = FreeRTOS_htonl(pxPacket->originateTimestamp.fraction);

    pxPacket->receiveTimestamp.seconds = FreeRTOS_htonl(pxPacket->receiveTimestamp.seconds);
    pxPacket->receiveTimestamp.fraction = FreeRTOS_htonl(pxPacket->receiveTimestamp.fraction);

    pxPacket->transmitTimestamp.seconds = FreeRTOS_htonl(pxPacket->transmitTimestamp.seconds);
    pxPacket->transmitTimestamp.fraction = FreeRTOS_htonl(pxPacket->transmitTimestamp.fraction);
}

static void prvReadTime(struct SNtpPacket *pxPacket)
{
    time_t uxCurrentSeconds;
    time_t uxCurrentMS;
    TickType_t uxTravelTime;

    uxTravelTime = xTaskGetTickCount() - uxSendTime;

    /* Transform the contents of the fields from big to native endian. */
    prvSwapFields(pxPacket);

    uxCurrentSeconds = pxPacket->receiveTimestamp.seconds - TIME1970;
    uxCurrentMS = pxPacket->receiveTimestamp.fraction / 4294967;
    uxCurrentSeconds += uxCurrentMS / 1000;
    uxCurrentMS = uxCurrentMS % 1000;

    uxCurrentSeconds += TIME_ZONE_SEC; 

    set_time(&uxCurrentSeconds);

    // struct tm xTimeStruct;
    // gmtime_r(&uxCurrentSeconds, &xTimeStruct);
    // INF("NTP time: %u/%u/%02u %2u:%02u:%02u.%03u (%lu ms)\n",
    //     (unsigned)xTimeStruct.tm_mday,
    //     (unsigned)xTimeStruct.tm_mon + 1,
    //     (unsigned)xTimeStruct.tm_year + 1900,
    //     (unsigned)xTimeStruct.tm_hour,
    //     (unsigned)xTimeStruct.tm_min,
    //     (unsigned)xTimeStruct.tm_sec,
    //     (unsigned)uxCurrentMS,
    //     uxTravelTime);
}

static void prvNTPPacketInit()
{
    memset(&xNTPPacket, '\0', sizeof(xNTPPacket));

    xNTPPacket.flags = 0xDB;                /* value 0xDB : mode 3 (client), version 3, leap indicator unknown 3 */
    xNTPPacket.poll = 10;                   /* 10 means 1 << 10 = 1024 seconds */
    xNTPPacket.precision = 0xFA;            /* = 250 = 0.015625 seconds */
    xNTPPacket.rootDelay = 0x5D2E;          /* 0x5D2E = 23854 or (23854/65535)= 0.3640 sec */
    xNTPPacket.rootDispersion = 0x0008CAC8; /* 0x0008CAC8 = 8.7912  seconds */

    /* use the recorded NTP time */
    time_t uxSecs = get_time(NULL); /* apTime may be NULL, returns seconds */

    xNTPPacket.referenceTimestamp.seconds = uxSecs; /* Current time */
    xNTPPacket.transmitTimestamp.seconds = uxSecs + 3;

    /* Transform the contents of the fields from native to big endian. */
    prvSwapFields(&xNTPPacket);
}

static void NTPTask(void *arg)
{
    BaseType_t xServerIndex = 3;
    UNUSED(arg);

    struct freertos_sockaddr xAddress;

    xStatus = EStatusLookup;

    for (;;)
    {
        switch (xStatus)
        {
        case EStatusLookup:

            if (xHasIPAddress == 0)
            {
                char pcServerName[64];

                if (++xServerIndex == sizeof(pcTimeServers) / sizeof(pcTimeServers[0]))
                {
                    xServerIndex = 0;
                }

                snprintf(pcServerName, sizeof pcServerName, "%s", pcTimeServers[xServerIndex]);

                struct freertos_addrinfo xHints;
                struct freertos_addrinfo *pxResults = NULL;
                memset(&(xHints), 0, sizeof xHints);
                xHints.ai_family = FREERTOS_AF_INET4;

                FreeRTOS_getaddrinfo(pcServerName,  /* The name of the node or device */
                                     NULL,          /* Ignored for now. */
                                     &(xHints),     /* If not NULL: preferences. */
                                     &(pxResults)); /* An allocated struct, containing the results. */

                if (pxResults != NULL)
                {
                    ulIPAddressFound = pxResults->ai_addr->sin_address.ulIP_IPv4;
                    xHasIPAddress = pdTRUE;
                    xStatus = EStatusAsking;
                }
            }
            else
            {
                xStatus = EStatusAsking;
            }
            vTaskDelay(1000);
            break;

        case EStatusAsking:
            prvNTPPacketInit();
            uxSendTime = xTaskGetTickCount();

            xAddress.sin_address.ulIP_IPv4 = ulIPAddressFound;
            xAddress.sin_port = FreeRTOS_htons(NTP_PORT);
            xAddress.sin_family = FREERTOS_AF_INET;

            FreeRTOS_sendto(xNTP_UDPSocket,
                            (void *)&xNTPPacket,
                            sizeof(xNTPPacket),
                            0, &(xAddress),
                            sizeof(xAddress));

            uint32_t xAddressSize;
            BaseType_t xReturned;

            xAddressSize = sizeof(xAddress);
            xReturned = FreeRTOS_recvfrom(xNTP_UDPSocket, (void *)cRecvBuffer, sizeof(cRecvBuffer), 0, &xAddress, &xAddressSize);

            switch (xReturned)
            {
            case 0:
            case -pdFREERTOS_ERRNO_EAGAIN:
            case -pdFREERTOS_ERRNO_EINTR:
                break;

            default:

                if (xReturned < sizeof(xNTPPacket))
                {
                    INF("FreeRTOS_recvfrom: returns %ld\n", (long)xReturned);
                }
                else
                {
                    prvReadTime((struct SNtpPacket *)cRecvBuffer);

                    if (xStatus != EStatusPause)
                    {
                        xStatus = EStatusPause;
                    }
                }

                break;
            }

            break;

        case EStatusPause:
            vTaskDelay(2000);
            // xStatus = EStatusLookup;
            break;

        case EStatusFailed:
            break;
        }
    }
}