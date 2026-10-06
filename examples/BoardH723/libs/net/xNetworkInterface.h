#ifndef __ESP_HOSTED_H
#define __ESP_HOSTED_H

#include "FreeRTOS_IP.h"
#include "FreeRTOS_Sockets.h"
#include "FreeRTOS_IP_Private.h"
#include "FreeRTOS_DNS.h"
#include "NetworkBufferManagement.h"
#include "NetworkInterface.h"
#include "stdbool.h"
#include "driver.h"

#ifdef __cplusplus
extern "C" {
#endif

bool networkInterfaceInitialise(void);
bool networkInterfaceUpdateMAC(void);
BaseType_t xNetworkInterfaceInput(NetworkInterface_t *pxInterface, spi_buf *buf);
BaseType_t ncmNetworkInterfaceInput(uint8_t *buf, uint32_t size);

void vNetworkNotifyIFUp(int interface);
void vNetworkNotifyIFDown(int interface);

extern NetworkInterface_t staNetInterface;
extern NetworkInterface_t apNetInterface;
extern NetworkInterface_t ncmNetInterface;
extern NetworkEndPoint_t ncmNetEndPoint;
extern NetworkEndPoint_t staNetEndPoint;
extern NetworkEndPoint_t apNetEndPoint;


#ifdef __cplusplus
}
#endif

#endif
