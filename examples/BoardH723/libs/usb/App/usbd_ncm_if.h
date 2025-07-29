#ifndef __USBD_NCM_IF_H__
#define __USBD_NCM_IF_H__

#ifdef __cplusplus
 extern "C" {
#endif

#include "usbd_ncm.h"

#define APP_RX_DATA_SIZE  2048
#define APP_TX_DATA_SIZE  2048

#define NCM_MAC_ADDR0  0x00
#define NCM_MAC_ADDR1  0x80
#define NCM_MAC_ADDR2  0xE1
#define NCM_MAC_ADDR3  0x00
#define NCM_MAC_ADDR4  0x00
#define NCM_MAC_ADDR5  0x17

#define NCM_IF (17) 

extern USBD_NCM_ItfTypeDef USBD_Interface_fops_HS;
extern uint8_t ncmMACAddress[6];

uint8_t NCM_Transmit(uint8_t* Buf, uint16_t Len);

void ncmInit();

#ifdef __cplusplus
}
#endif

#endif /* __USBD_NCM_IF_H__ */

