#ifndef SSHSERVER_H
#define SSHSERVER_H

#include "FreeRTOS.h"
#include "sys/socket.h"
#include "task.h"
#include <string.h>

#ifdef SSH_SERVER_STACK_SDRAM
#define SERVER_STACK_MALLOC sdramMalloc
#define SERVER_STACK_FREE sdramFree
#else
#define SERVER_STACK_MALLOC pvPortMalloc
#define SERVER_STACK_FREE vPortFree
#endif

void SSHServerStart();

#endif /* SSHSERVER_H */