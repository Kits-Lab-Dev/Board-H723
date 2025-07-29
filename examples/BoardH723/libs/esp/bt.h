#ifndef __BT_LE_DRIVER__H
#define __BT_LE_DRIVER__H

#include "driver.h"
#include "nimble/ble.h"

bool bleStackInit(void);

bool bt_rx_process(uint8_t *data, uint16_t len);
void ble_app_start(void);
void ble_store_config_init(void);
#endif
