#ifndef __SPI_DRIVER__H
#define __SPI_DRIVER__H

#include "spi.h"
#include "stdbool.h"
#include <FreeRTOS.h>
#include <queue.h>
#include <task.h>
#include <semphr.h>

#define min(X, Y)               (((X) < (Y)) ? (X) : (Y))

#define SPI_BUF_DATA_LEN (1600 - sizeof(uint16_t) - sizeof(esp_interface))
#define SPI_BUF_LEN (sizeof(spi_buf))

typedef int esp_err_t;

#define ESP_OK          0       
#define ESP_FAIL        -1      

typedef enum __packed
{
    ESP_STA,
    ESP_AP,
    ESP_BT,
    ESP_CONTROL,
    ESP_NONE = 0xFFFF
}esp_interface;

typedef struct __packed
{
    esp_interface type;
    uint16_t len;
    uint8_t data[SPI_BUF_DATA_LEN];
}spi_buf;

typedef struct __packed{
    uint8_t *data;
    uint16_t len;
    esp_interface type;
	bool is_released;
	void(*free_data_fn)(void* p);
}p_spi_buf;

bool spiDriverInit(void);
bool spiDriverWrite(p_spi_buf *buf);

#endif