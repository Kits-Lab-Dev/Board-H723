#ifndef SSH_UTILS_H
#define SSH_UTILS_H

#include <wolfssh/ssh.h>
#include <wolfssl/ssl.h>
#include <sys/un.h>
#include "sdram.h"

#define LOAD_FILE_BUFFER_SZ 4096

#ifndef SSH_BUFFER_SIZE
#define SSH_BUFFER_SIZE 4096
#endif

#define SSH_WORKER_TASK_STACK_SIZE (1024 * 4)
#define SSH_WORKER_TASK_PRIORITY (tskIDLE_PRIORITY)

#ifdef WOLFSSH_AGENT
typedef struct WS_AgentCbActionCtx
{
    struct sockaddr_un name;
    WS_SOCKET_T listenFd;
    WS_SOCKET_T fd;
    pid_t pid;
    int state;
} WS_AgentCbActionCtx;
#endif

typedef struct
{
    WOLFSSH *ssh;
    WS_SOCKET_T fd;
    word32 id;
#ifdef WOLFSSH_AGENT
    WS_AgentCbActionCtx agentCbCtx;
    byte agentBuffer[SSH_BUFFER_SIZE];
#endif
#ifdef WOLFSSH_SHELL
    byte channelBuffer[SSH_BUFFER_SIZE];
#endif
    StackType_t workerStack[SSH_WORKER_TASK_STACK_SIZE];
    StaticTask_t workerTask;
} thread_ctx_t;

typedef struct StrList
{
    const char *str;
    struct StrList *next;
} StrList;

StrList *StrListAdd(StrList *list, const char *str);
void StrListFree(StrList *list);

int SSHLoadFile(const char *fileName, byte *buf, word32 *bufSz);
int SSHLoadKey(const char *fame, byte *buf, word32 bufSz);

void SSHDebuggingEnable(void);
void SSLDebuggingEnable(void);
void SSHSetIO(WOLFSSH_CTX *ctx);

#endif /* SSH_UTILS_H */