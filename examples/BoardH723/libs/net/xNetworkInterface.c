#include "control.h"
#include "driver.h"
#include "xNetworkInterface.h"
#include "app.h"
#include "usbd_ncm_if.h"

// #define NCM_MAC_ADDR0  0x00
// #define NCM_MAC_ADDR1  0x80
// #define NCM_MAC_ADDR2  0xE1
// #define NCM_MAC_ADDR3  0x00
// #define NCM_MAC_ADDR4  0x00
// #define NCM_MAC_ADDR5  0x17

// #define NCM_IF (17) 
// uint8_t NCM_Transmit_HS(uint8_t* Buf, uint16_t Len){

// }

// void ncmInit(){

// }
// #define USBD_OK 0

static uint8_t staUcIPAddress[4];
static uint8_t staUcNetMask[4];
static uint8_t staUcGatewayAddress[4];
static uint8_t staUcDNSServerAddress[4];
static uint8_t staUcMACAddress[6];

static uint8_t apUcIPAddress[4] = {192, 168, 11, 1};
static uint8_t apUcNetMask[4] = {255, 255, 255, 0};
static uint8_t apUcGatewayAddress[4] = {0, 0, 0, 0};
static uint8_t apUcDNSServerAddress[4] = {0, 0, 0, 0};
static uint8_t apUcMACAddress[6];

static uint8_t ncmUcIPAddress[4] = {192, 168, 4, 1};
static uint8_t ncmUcNetMask[4] = {255, 255, 255, 0};
static uint8_t ncmUcGatewayAddress[4] = {0, 0, 0, 0};
static uint8_t ncmUcDNSServerAddress[4] = {0, 0, 0, 0};
static uint8_t ncmUcMACAddress[6] = {NCM_MAC_ADDR0, NCM_MAC_ADDR1, NCM_MAC_ADDR2, NCM_MAC_ADDR3, NCM_MAC_ADDR4, NCM_MAC_ADDR5+1};

NetworkInterface_t staNetInterface;
NetworkInterface_t apNetInterface;
NetworkInterface_t ncmNetInterface;

NetworkEndPoint_t staNetEndPoint;
NetworkEndPoint_t apNetEndPoint;
NetworkEndPoint_t ncmNetEndPoint;

bool volatile staInterfaceUp = false;
bool volatile apInterfaceUp = false;
bool volatile ncmInterfaceUp = false;

#define MAX_MAC_STR_LEN (16)

#define ETH_RX_BUF_SIZE 1536

const char *staName = "sta";
const char *apName = "ap";
const char *ncmName = "ncm";

uint8_t ucNetworkPackets[ipconfigNUM_NETWORK_BUFFER_DESCRIPTORS * ETH_RX_BUF_SIZE]  __attribute__((section(".ip_PoolSection")));

void xFillInterfaceDescriptor(NetworkInterface_t *pxInterface);
static BaseType_t xNetworkInterfaceInitialise(NetworkInterface_t *pxInterface);
static BaseType_t xNetworkInterfaceOutput(NetworkInterface_t *pxInterface,
										  NetworkBufferDescriptor_t *const pxDescriptor,
										  BaseType_t xReleaseAfterSend);

static BaseType_t ncmNetworkInterfaceOutput(NetworkInterface_t *pxInterface,
											  NetworkBufferDescriptor_t *const pxDescriptor,
											  BaseType_t xReleaseAfterSend);

bool networkInterfaceUpdateMAC(void)
{
	bool ret;
	ret = wifiGetMacAddress(ESP_STA, staUcMACAddress);
	if (!ret)
		return false;
	return wifiGetMacAddress(ESP_AP, apUcMACAddress);
}

bool networkInterfaceInitialise(void)
{
	xFillInterfaceDescriptor(&staNetInterface);
	staNetInterface.pcName = staName;
	FreeRTOS_AddNetworkInterface(&staNetInterface);

	xFillInterfaceDescriptor(&apNetInterface);
	apNetInterface.pcName = apName;
	FreeRTOS_AddNetworkInterface(&apNetInterface);

	memset(&ncmNetInterface, 0, sizeof(NetworkInterface_t));
	ncmNetInterface.pfInitialise = xNetworkInterfaceInitialise;
	ncmNetInterface.pfOutput = ncmNetworkInterfaceOutput;
	ncmNetInterface.pfGetPhyLinkStatus = xGetPhyLinkStatus;
	ncmNetInterface.pcName = ncmName;
	FreeRTOS_AddNetworkInterface(&ncmNetInterface);
	
	configASSERT(FreeRTOS_IPInit_Multi());
	ncmInit();
	vNetworkNotifyIFDown(ESP_STA);
	vNetworkNotifyIFDown(ESP_AP);
	vNetworkNotifyIFDown(NCM_IF);

	return true;
}

size_t uxNetworkInterfaceAllocateRAMToBuffers(NetworkBufferDescriptor_t pxNetworkBuffers[ipconfigNUM_NETWORK_BUFFER_DESCRIPTORS])
{
	uint8_t *ucRAMBuffer = ucNetworkPackets;
	uint32_t ul;

	for (ul = 0; ul < ipconfigNUM_NETWORK_BUFFER_DESCRIPTORS; ul++)
	{
		pxNetworkBuffers[ul].pucEthernetBuffer = ucRAMBuffer + ipBUFFER_PADDING;
		*((unsigned *)ucRAMBuffer) = (unsigned)(&(pxNetworkBuffers[ul]));
		ucRAMBuffer += ETH_RX_BUF_SIZE;
	}
	return ETH_RX_BUF_SIZE - ipBUFFER_PADDING;
}

static BaseType_t xNetworkInterfaceInitialise(NetworkInterface_t *pxInterface)
{
	if (pxInterface == &staNetInterface)
	{
		FreeRTOS_FillEndPoint(pxInterface, &staNetEndPoint, staUcIPAddress, staUcNetMask, staUcGatewayAddress, staUcDNSServerAddress, staUcMACAddress);
		staNetEndPoint.bits.bWantDHCP = pdTRUE;
	}
	else if (pxInterface == &apNetInterface)
	{
		FreeRTOS_FillEndPoint(pxInterface, &apNetEndPoint, apUcIPAddress, apUcNetMask, apUcGatewayAddress, apUcDNSServerAddress, apUcMACAddress);
	}
	else if (pxInterface == &ncmNetInterface)
	{
		FreeRTOS_FillEndPoint(pxInterface, &ncmNetEndPoint, ncmUcIPAddress, ncmUcNetMask, ncmUcGatewayAddress, ncmUcDNSServerAddress, ncmUcMACAddress);
	}
	else
		return pdFALSE;

	return pdTRUE;
}

BaseType_t xGetPhyLinkStatus(NetworkInterface_t *pxInterface)
{
	if (pxInterface == &staNetInterface)
	{
		return staInterfaceUp ? pdTRUE : pdFALSE;
	}
	else if (pxInterface == &apNetInterface)
	{
		return apInterfaceUp ? pdTRUE : pdFALSE;
	}
	else if (pxInterface == &ncmNetInterface)
	{
		return ncmInterfaceUp ? pdTRUE : pdFALSE;
	}
	else
		return pdFALSE;
}

static BaseType_t xNetworkInterfaceOutput(NetworkInterface_t *pxInterface,
										  NetworkBufferDescriptor_t *const pxDescriptor,
										  BaseType_t xReleaseAfterSend)
{
	BaseType_t ret = pdFAIL;
	esp_interface interface;

	if (xGetPhyLinkStatus(pxInterface) == pdFALSE)
	{
		if (xReleaseAfterSend == pdTRUE)
			vReleaseNetworkBufferAndDescriptor(pxDescriptor);
		return pdFAIL;
	}

	if (pxInterface == &staNetInterface)
	{
		interface = ESP_STA;
	}
	else if (pxInterface == &apNetInterface)
	{
		interface = ESP_AP;
	}
	else
		return pdFALSE;

	p_spi_buf buf;
	buf.type = interface;
	// buf.data = (uint8_t*)pxDescriptor;
	// buf.len = sizeof(NetworkBufferDescriptor_t);
	// buf.is_released = xReleaseAfterSend == pdTRUE;
	// buf.free_data_fn = (void (*)(void *))vReleaseNetworkBufferAndDescriptor;
	buf.data = (uint8_t *)pvPortMalloc(pxDescriptor->xDataLength);
	buf.len = pxDescriptor->xDataLength;
	buf.is_released = true;
	buf.free_data_fn = vPortFree;
	memcpy(buf.data, pxDescriptor->pucEthernetBuffer, pxDescriptor->xDataLength);
	if (xReleaseAfterSend == pdTRUE)
		vReleaseNetworkBufferAndDescriptor(pxDescriptor);
	ret = spiDriverWrite(&buf);

	return ret;
}


void vNetworkNotifyIFUp(int interface)
{
	if (interface == ESP_STA)
	{
		staInterfaceUp = true;
	}
	else if (interface == ESP_AP)
	{
		apInterfaceUp = true;
	}
	else if (interface == NCM_IF)
	{
		ncmInterfaceUp = true;
	}
}

void vNetworkNotifyIFDown(int interface)
{
	if (interface == ESP_STA)
	{
		staInterfaceUp = false;
		FreeRTOS_NetworkDown(&staNetInterface);
	}
	else if (interface == ESP_AP)
	{
		apInterfaceUp = false;
		FreeRTOS_NetworkDown(&apNetInterface);
	}
	else if (interface == NCM_IF)
	{
		ncmInterfaceUp = false;
		FreeRTOS_NetworkDown(&ncmNetInterface);
	}
}

void xFillInterfaceDescriptor(NetworkInterface_t *pxInterface)
{
	memset(pxInterface, 0, sizeof(NetworkInterface_t));
	pxInterface->pfInitialise = xNetworkInterfaceInitialise;
	pxInterface->pfOutput = xNetworkInterfaceOutput;
	pxInterface->pfGetPhyLinkStatus = xGetPhyLinkStatus;
}

BaseType_t xNetworkInterfaceInput(NetworkInterface_t *pxInterface, spi_buf *buf)
{
	if (xGetPhyLinkStatus(pxInterface) == pdFALSE)
		return pdFAIL;

	const TickType_t xDescriptorWaitTime = pdMS_TO_TICKS(250);

	NetworkBufferDescriptor_t *rxReceivedBuffer = pxGetNetworkBufferWithDescriptor(buf->len, xDescriptorWaitTime);
	if (rxReceivedBuffer == NULL)
	{
		/* The event was lost because a network buffer was not available.
		 * Call the standard trace macro to log the occurrence. */
		iptraceETHERNET_RX_EVENT_LOST();
	}
	else
	{
		BaseType_t xDoRelease = pdFALSE;
		rxReceivedBuffer->pxInterface = pxInterface;
		rxReceivedBuffer->pxEndPoint = pxInterface->pxEndPoint;

		memcpy(rxReceivedBuffer->pucEthernetBuffer, buf->data, buf->len);
		rxReceivedBuffer->xDataLength = buf->len;
		if (eConsiderFrameForProcessing(rxReceivedBuffer->pucEthernetBuffer) != eProcessBuffer)
		{
			/* The Ethernet frame can be dropped, but the Ethernet buffer must be released. */
			xDoRelease = pdTRUE;
		}
		else
		{
			/* The event about to be sent to the TCP/IP is an Rx event.
			 * pvData is used to point to the network buffer descriptor that
			 * now references the received data. */

			IPStackEvent_t xRxEvent =
				{
					.eEventType = eNetworkRxEvent,
					.pvData = (void *)rxReceivedBuffer};

			/* Send the data to the TCP/IP stack. */
			if (xSendEventStructToIPTask(&xRxEvent, xDescriptorWaitTime) != pdFALSE)
			{
				/* The message was successfully sent to the TCP/IP stack.
				 * Call the standard trace macro to log the occurrence. */
				iptraceNETWORK_INTERFACE_RECEIVE();
			}
			else
			{
				xDoRelease = pdTRUE;

				/* The buffer could not be sent to the IP task so the buffer
				 * must be released. */

				/* Make a call to the standard trace macro to log the
				 * occurrence. */
				iptraceETHERNET_RX_EVENT_LOST();
			}
		}

		if (xDoRelease != pdFALSE)
		{
			vReleaseNetworkBufferAndDescriptor(rxReceivedBuffer);
		}
	}
	return pdTRUE;
}

static BaseType_t ncmNetworkInterfaceOutput(NetworkInterface_t *pxInterface,
											  NetworkBufferDescriptor_t *const pxDescriptor,
											  BaseType_t xReleaseAfterSend)
{
	BaseType_t ret = pdTRUE;
	if (xGetPhyLinkStatus(pxInterface) == pdFALSE)
	{
		if (xReleaseAfterSend == pdTRUE)
			vReleaseNetworkBufferAndDescriptor(pxDescriptor);
		return pdFAIL;
	}

	int r = 0;
	while (NCM_Transmit(pxDescriptor->pucEthernetBuffer, pxDescriptor->xDataLength) != USBD_OK)
	{
		vTaskDelay(1);
		r++;
		if (r > 40)
		{
			ret = pdFAIL;
			break;
		}
	}

	if (xReleaseAfterSend == pdTRUE)
		vReleaseNetworkBufferAndDescriptor(pxDescriptor);

	return ret;
}

BaseType_t ncmNetworkInterfaceInput(uint8_t *buf, uint32_t size)
{
	if (xGetPhyLinkStatus(&ncmNetInterface) == pdFALSE)
		return pdFAIL;

	const TickType_t xDescriptorWaitTime = pdMS_TO_TICKS(250);

	NetworkBufferDescriptor_t *rxReceivedBuffer = pxGetNetworkBufferWithDescriptor(size, xDescriptorWaitTime);
	if (rxReceivedBuffer == NULL)
	{
		/* The event was lost because a network buffer was not available.
		 * Call the standard trace macro to log the occurrence. */
		iptraceETHERNET_RX_EVENT_LOST();
	}
	else
	{
		BaseType_t xDoRelease = pdFALSE;
		rxReceivedBuffer->pxInterface = &ncmNetInterface;
		rxReceivedBuffer->pxEndPoint = ncmNetInterface.pxEndPoint;

		memcpy(rxReceivedBuffer->pucEthernetBuffer, buf, size);
		rxReceivedBuffer->xDataLength = size;
		if (eConsiderFrameForProcessing(rxReceivedBuffer->pucEthernetBuffer) != eProcessBuffer)
		{
			/* The Ethernet frame can be dropped, but the Ethernet buffer must be released. */
			xDoRelease = pdTRUE;
		}
		else
		{
			/* The event about to be sent to the TCP/IP is an Rx event.
			 * pvData is used to point to the network buffer descriptor that
			 * now references the received data. */

			IPStackEvent_t xRxEvent =
				{
					.eEventType = eNetworkRxEvent,
					.pvData = (void *)rxReceivedBuffer};

			/* Send the data to the TCP/IP stack. */
			if (xSendEventStructToIPTask(&xRxEvent, xDescriptorWaitTime) != pdFALSE)
			{
				/* The message was successfully sent to the TCP/IP stack.
				 * Call the standard trace macro to log the occurrence. */
				iptraceNETWORK_INTERFACE_RECEIVE();
			}
			else
			{
				xDoRelease = pdTRUE;

				/* The buffer could not be sent to the IP task so the buffer
				 * must be released. */

				/* Make a call to the standard trace macro to log the
				 * occurrence. */
				iptraceETHERNET_RX_EVENT_LOST();
			}
		}

		if (xDoRelease != pdFALSE)
		{
			vReleaseNetworkBufferAndDescriptor(rxReceivedBuffer);
		}
	}
	return pdTRUE;
}

BaseType_t FreeRTOS_close(Socket_t xSocket)
{
	if (!xSocket)
		return 0;
	FreeRTOS_shutdown(xSocket, 0);
	uint32_t tick = HAL_GetTick();
	while (FreeRTOS_tx_size(xSocket) > 0 && HAL_GetTick() - tick < 5000)
	{
		vTaskDelay(1);
	}
	return FreeRTOS_closesocket(xSocket);
}