#include "rtos_queue.h"
#include "app.h"
#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"
#include "dts.h"

#define RTOS_QUEUE_PERIOD_MS 1000 // период измерений
#define RTOS_QUEUE_BATCH 5        // обработчик печатает среднее по стольким измерениям
#define RTOS_QUEUE_LENGTH 8       // глубина очереди, измерений

#define SENSOR_STACK_SIZE 256    // слов (×4 байта)
#define CONSUMER_STACK_SIZE 512  // printf внутри INF — стек побольше

// Всё статически: память видна в map-файле, xTaskCreate не упадёт от нехватки кучи
static StaticQueue_t queueBuffer;
static uint8_t queueStorage[RTOS_QUEUE_LENGTH * sizeof(rtosQueueSample)];
static QueueHandle_t queue;

static StackType_t sensorStack[SENSOR_STACK_SIZE];
static StaticTask_t sensorTcb;
static StackType_t consumerStack[CONSUMER_STACK_SIZE];
static StaticTask_t consumerTcb;

// Производитель: меряет по расписанию и кладёт в очередь, сам ничего не печатает
static void sensorTask(void *arg)
{
    (void)arg;
    rtosQueueSample s = {0};
    TickType_t last = xTaskGetTickCount();
    for (;;)
    {
        // vTaskDelayUntil держит ровный период независимо от времени работы цикла
        vTaskDelayUntil(&last, pdMS_TO_TICKS(RTOS_QUEUE_PERIOD_MS));
        s.seq++;
        s.tick = xTaskGetTickCount();
        HAL_DTS_GetTemperature(&hdts, &s.tempC);
        // Не ждём: если обработчик отстал и очередь полна, измерение теряется
        if (xQueueSend(queue, &s, 0) != pdPASS)
            WRN("rtos_queue: queue full, sample %lu lost\n", s.seq);
    }
}

// Потребитель: спит в xQueueReceive, пока нет данных, — процессор свободен
static void consumerTask(void *arg)
{
    (void)arg;
    rtosQueueSample s;
    int32_t sum = 0;
    int n = 0;
    for (;;)
    {
        // Таймаут вдвое больше периода: если датчик замолчал — узнаем об этом
        if (xQueueReceive(queue, &s, pdMS_TO_TICKS(2 * RTOS_QUEUE_PERIOD_MS)) != pdPASS)
        {
            WRN("rtos_queue: no data for %d ms\n", 2 * RTOS_QUEUE_PERIOD_MS);
            continue;
        }
        sum += s.tempC;
        if (++n == RTOS_QUEUE_BATCH)
        {
            INF("rtos_queue: #%lu t=%lu ms, die %ld C (avg of %d), queued %lu\n",
                s.seq, s.tick, sum / n, n, uxQueueMessagesWaiting(queue));
            sum = 0;
            n = 0;
        }
    }
}

void rtos_queue_start(void)
{
    queue = xQueueCreateStatic(RTOS_QUEUE_LENGTH, sizeof(rtosQueueSample), queueStorage, &queueBuffer);
    // Потребитель на приоритет выше: получает данные сразу, как они появились
    xTaskCreateStatic(sensorTask, "rqSensor", SENSOR_STACK_SIZE, NULL, tskIDLE_PRIORITY + 2,
                      sensorStack, &sensorTcb);
    xTaskCreateStatic(consumerTask, "rqConsumer", CONSUMER_STACK_SIZE, NULL, tskIDLE_PRIORITY + 3,
                      consumerStack, &consumerTcb);
    INF("rtos_queue: started, period %d ms\n", RTOS_QUEUE_PERIOD_MS);
}
