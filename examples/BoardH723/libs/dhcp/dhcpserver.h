#ifndef DHCPSERVER_H
#define DHCPSERVER_H

#include "sys/socket.h"
#include "FreeRTOS_Sockets.h"
#include "FreeRTOS_DNS.h"
#include "stdbool.h"
#include "stdint.h"

#define MAX_CLIENTS 3

#define ip_addr_t uint32_t

typedef struct __attribute__((packed)){
    uint8_t tag;          /* Option tag (e.g., 53 = Message type) */
    uint8_t len;          /* Length of option data */
    uint8_t data[0];      /* Variable-length option data */
} dhcpOption;

typedef struct __attribute__((packed)){
    uint8_t op;          /* Operation: 1 = BOOTREQUEST, 2 = BOOTREPLY */
    uint8_t htype;       /* Hardware type (e.g., Ethernet = 1) */
    uint8_t hlen;        /* Hardware address length */
    uint8_t hops;        /* Client sets to 0, optionally used by relay agents */
    uint32_t xid;         /* Transaction ID */
    uint16_t secs;        /* Seconds elapsed since client started trying to boot */
    uint16_t flags;       /* Flags (e.g., Broadcast flag) */
    ip_addr_t ciaddr;  /* Client IP address (if already known, or zero otherwise) */
    ip_addr_t yiaddr;  /* Your IP address (assigned by server) */
    ip_addr_t siaddr;  /* Server IP address */
    ip_addr_t giaddr;  /* Gateway IP address */
    uint8_t chaddr[16];   /* Client hardware address */
    uint8_t sname[64];   /* Optional server host name or domain name */
    uint8_t file[128];   /* Boot filename string */
    uint32_t magicCookie; /* Magic cookie to identify DHCP message */

    dhcpOption options[0];  // Variable-length DHCP options
} dhcpMsg;

typedef enum {
    DHCPServerStopped,
    DHCPServerRunning,
    DHCPServerError
} DHCPServerStatus;

typedef struct {
    uint8_t mac[6];
    uint32_t ip;
} DHCPClient;

typedef struct {
    NetworkInterface_t* netInterface;
    Socket_t socket;
    IP_Address_t ipAddr;
    IP_Address_t subnetMask;
    IP_Address_t gateway;
    bool stopRequest;
    IP_Address_t lastIP;
    DHCPServerStatus status;
    DHCPClient clients[MAX_CLIENTS];
} DHCPServer;

bool dhcpServerStart();
void dhcpServerStop();
bool dhcpAddServer(DHCPServer *server);
void dhcpRemoveServer(DHCPServer *server);

#endif