#include "dhcpserver.h"
#include "app.h"
#include "list.h"

#define DHCP_SERVER_PORT 67
#define DHCP_CLIENT_PORT 68
#define DHCP_PACKET_SIZE 576
#define SERVER_COUNT     3

// Константы для DHCP опций
#define DHCP_OPT_MSG_TYPE    53
#define DHCP_OPT_SUBNET_MASK 1
#define DHCP_OPT_ROUTER      3
#define DHCP_OPT_END         255

// Константы типов сообщений
#define DHCP_DISCOVER  1
#define DHCP_OFFER     2
#define DHCP_REQUEST   3
#define DHCP_ACK       5

extern NetworkInterface_t apNetInterface;

DHCPServer *servers[SERVER_COUNT] = {NULL};
static uint8_t status = 0;

bool dhcpAddServer(DHCPServer *server)
{
    for (int i = 0; i < SERVER_COUNT; i++)
    {
        if (servers[i] == NULL)
        {
            server->lastIP.ulIP_IPv4 = server->ipAddr.ulIP_IPv4;
            memcpy(server->lastIP.xIP_IPv6.ucBytes, server->ipAddr.xIP_IPv6.ucBytes, sizeof(server->ipAddr.xIP_IPv6.ucBytes));
            servers[i] = server;
            return true;
        }
    }
    return false;
}

void dhcpRemoveServer(DHCPServer *server)
{
    for (int i = 0; i < SERVER_COUNT; i++)
    {
        if (servers[i] == server)
        {
            servers[i] = NULL;
            return;
        }
    }
}

static void addCommonOptions(uint8_t **ptr, DHCPServer *server) {
    // Subnet Mask
    *(*ptr)++ = DHCP_OPT_SUBNET_MASK;
    *(*ptr)++ = 4;
    *(uint32_t *)*ptr = server->subnetMask.ulIP_IPv4;
    *ptr += 4;

    // Router
    *(*ptr)++ = DHCP_OPT_ROUTER;
    *(*ptr)++ = 4;
    *(uint32_t *)*ptr = server->gateway.ulIP_IPv4;
    *ptr += 4;

    // End option
    *(*ptr)++ = DHCP_OPT_END;
}

static int findClientByMac(DHCPServer *server, const uint8_t *mac) {
    for(int i = 0; i < MAX_CLIENTS; i++) {
        if(memcmp(server->clients[i].mac, mac, 6) == 0) {
            return i;
        }
    }
    return -1;
}

static void initDhcpResponse(dhcpMsg *msg, DHCPServer *server, uint32_t client_ip) {
    msg->op = 2; // BOOTREPLY
    msg->yiaddr = client_ip;
    msg->siaddr = server->ipAddr.ulIP_IPv4;
}

void dhcpServerTask(void *arg)
{
    UNUSED(arg);
    int sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (sock <= 0)
    {
        ERR("DHCP Server: Failed to create socket\n");
        goto error;
    }

    struct sockaddr server_addr;
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(DHCP_SERVER_PORT);
    server_addr.sin_addr.s_addr  = INADDR_ANY;

    if (bind(sock, (const struct sockaddr *)&server_addr, sizeof(server_addr)) < 0)
    {
        close(sock);
        goto error;
    }
    for (;;)
    {
        if (status == 2)
        {
            close(sock);
            goto error;
        }

        struct sockaddr_in client_addr;
        socklen_t addr_len = sizeof(client_addr);
        char buffer[DHCP_PACKET_SIZE];

        int bytes_received = recvfrom(sock, buffer, DHCP_PACKET_SIZE, 0, (struct sockaddr *)&client_addr, &addr_len);
        if (bytes_received < (ssize_t)sizeof(dhcpMsg))
        {
            continue;
        }
        DHCPServer *server = NULL;
        for (int i = 0; i < SERVER_COUNT; i++)
        {
            if (servers[i] != NULL)
            {
                if (servers[i]->netInterface == ((Socket_t)sock)->pxEndPoint->pxNetworkInterface)
                {
                    server = servers[i];
                    break;
                }
            }
        }
        if (server == NULL)
        {
            continue;
        }

        dhcpMsg *msg = (dhcpMsg *)buffer;
        // Проверяем, что это DHCP запрос
        if (msg->op != 1)
        { // BOOTREQUEST
            continue;
        }

        // Ищем опцию DHCP Message Type
        uint8_t *options = (uint8_t *)msg->options;
        uint8_t *end = (uint8_t *)buffer + bytes_received;
        uint8_t msgType = 0;

        while (options < end)
        {
            dhcpOption *opt = (dhcpOption *)options;
            if (opt->tag == 53)
            { // DHCP Message Type
                msgType = opt->data[0];
                break;
            }
            if (opt->tag == 255)
                break; // End option
            options += 2 + opt->len;
        }

        // Основная обработка сообщений
        if (msgType == DHCP_DISCOVER) {
            int client_idx = findClientByMac(server, msg->chaddr);
            uint32_t client_ip = 0;

            if(client_idx >= 0) {
                client_ip = server->clients[client_idx].ip;
            } else {
                client_ip = server->lastIP.ulIP_IPv4 + htonl(1);
                // Сохраняем новый IP
                for(int i = 0; i < MAX_CLIENTS; i++) {
                    if(server->clients[i].ip == 0) {
                        memcpy(server->clients[i].mac, msg->chaddr, 6);
                        server->clients[i].ip = client_ip;
                        break;
                    }
                }
            }

            initDhcpResponse(msg, server, client_ip);
            uint8_t *ptr = (uint8_t *)msg->options;

            // Message Type = DHCP Offer
            *ptr++ = DHCP_OPT_MSG_TYPE;
            *ptr++ = 1;
            *ptr++ = DHCP_OFFER;

            addCommonOptions(&ptr, server);

            // Отправка ответа
            client_addr.sin_addr.s_addr = 0xFFFFFFFF; // Broadcast
            sendto(sock, msg, sizeof(dhcpMsg) + (ptr - (uint8_t *)msg->options), 0, 
                  (struct sockaddr *)&client_addr, addr_len);
        }
        else if (msgType == DHCP_REQUEST) {
            int client_idx = findClientByMac(server, msg->chaddr);
            uint32_t client_ip = (client_idx >= 0) ? 
                server->clients[client_idx].ip : 
                server->lastIP.ulIP_IPv4 + htonl(1);

            // Обновление данных
            server->lastIP.ulIP_IPv4 = client_ip;
            if(client_idx < 0) {
                for(int i = 0; i < MAX_CLIENTS; i++) {
                    if(server->clients[i].ip == 0) {
                        memcpy(server->clients[i].mac, msg->chaddr, 6);
                        server->clients[i].ip = client_ip;
                        break;
                    }
                }
            }

            INF("DHCP offer IP: %u.%u.%u.%u\n", 
                client_ip & 0xFF, (client_ip >> 8) & 0xFF, 
                (client_ip >> 16) & 0xFF, client_ip >> 24);

            initDhcpResponse(msg, server, client_ip);
            uint8_t *ptr = (uint8_t *)msg->options;

            // Message Type = DHCP ACK
            *ptr++ = DHCP_OPT_MSG_TYPE;
            *ptr++ = 1;
            *ptr++ = DHCP_ACK;

            addCommonOptions(&ptr, server);

            // Отправка ответа
            client_addr.sin_addr.s_addr = 0xFFFFFFFF; // Broadcast
            sendto(sock, msg, sizeof(dhcpMsg) + (ptr - (uint8_t *)msg->options), 0,
                  (struct sockaddr *)&client_addr, addr_len);
        }
    }

error:
    status = 0;
    vTaskDelete(NULL);
}

#define DHCP_TASK_STACK_SIZE (1024)
#define DHCP_TASK_PRIORITY (ipconfigIP_TASK_PRIORITY - 1)
static StackType_t taskStack[DHCP_TASK_STACK_SIZE] __ITCMRAM; 
static StaticTask_t taskBuffer __ITCMRAM;

bool dhcpServerStart()
{
    status = 1;
    return xTaskCreateStatic(dhcpServerTask, "DHCP Server", DHCP_TASK_STACK_SIZE, NULL, DHCP_TASK_PRIORITY, taskStack, &taskBuffer) != NULL;
}

void dhcpServerStop()
{
    status = 2;
}