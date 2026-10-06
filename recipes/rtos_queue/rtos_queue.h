#ifndef RTOS_QUEUE_H
#define RTOS_QUEUE_H

#include <stdint.h>

// Одно измерение: его кладёт задача-датчик и забирает задача-обработчик
typedef struct
{
    uint32_t seq;    // номер измерения
    uint32_t tick;   // время измерения, мс от старта
    int32_t tempC;   // температура кристалла, °C (датчик DTS)
} rtosQueueSample;

// Создаёт очередь и две задачи: датчик (раз в RTOS_QUEUE_PERIOD_MS) и обработчик
void rtos_queue_start(void);

#endif
