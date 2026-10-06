#include "ssh_utils.h"
#include "sys/socket.h"

#ifdef XMALLOC_USER
#include "sdram.h"
#include "app.h"
inline void *XMALLOC(size_t n, void *heap, int type){
    UNUSED(heap);
    UNUSED(type);
    void *p = sdramMalloc(n);
    return p;
}

inline void *XREALLOC(void *p, size_t n, void *heap, int type)
{
    UNUSED(heap);
    UNUSED(type);
    void *p2 = sdramRealloc(p, n);
    return p2;
}

inline void XFREE(void *p, void *heap, int type)
{
    UNUSED(heap);
    UNUSED(type);
    sdramFree(p);
}

#endif

static int iorecv(WOLFSSH *ctx, void *buffer, word32 length, void *user)
{
    (void)user; // Unused parameter

    if (ctx == NULL || buffer == NULL)
    {
        return -1;
    }

    int sock = wolfSSH_get_fd(ctx);
    if (sock == (int)INVALID_SOCKET)
    {
        return -1;
    }

    int ret = recv(sock, buffer, length, 0);
    if (ret < 0)
    {
        return -1;
    }

    return ret;
}

static int iosend(WOLFSSH *ctx, void *buffer, word32 length, void *user)
{
    (void)user; // Unused parameter

    if (ctx == NULL || buffer == NULL)
    {
        return -1;
    }

    int sock = wolfSSH_get_fd(ctx);
    if (sock == (int)INVALID_SOCKET)
    {
        return -1;
    }

    int ret = send(sock, buffer, length, 0);
    if (ret < 0)
    {
        return -1;
    }

    return ret;
}

static void ssh_LoggingCb(enum wolfSSH_LogLevel level, const char *msg)
{
    switch (level)
    {
    case WS_LOG_DEBUG:
        DBG("ssh: %s\r\n", msg);
        break;
    case WS_LOG_ERROR:
        ERR("ssh: %s\r\n", msg);
        break;
    case WS_LOG_WARN:
        WRN("ssh: %s\r\n", msg);
        break;
    case WS_LOG_INFO:
        INF("ssh: %s\r\n", msg);
        break;
    default:
        WRN("ssh: %s\r\n", msg);
        break;
    }
}

/* уровни wolfSSL: ERROR_LOG = 0, остальное — трассировка */
static void ssl_LoggingCb(const int level, const char *msg)
{
    if (level == 0)
        ERR("ssl: %s\r\n", msg);
    else
        DBG("ssl: %s\r\n", msg);
}

StrList *StrListAdd(StrList *list, const char *str)
{
    if (str != NULL)
    {
        StrList *newStr = (StrList *)WMALLOC(sizeof *newStr, NULL, 0);

        if (newStr != NULL)
        {
            newStr->str = str;
            newStr->next = list;
            list = newStr;
        }
    }

    return list;
}

void StrListFree(StrList *list)
{
    StrList *curStr;

    while (list != NULL)
    {
        curStr = list;
        list = list->next;
        WFREE(curStr, NULL, 0);
    }
}

int SSHLoadFile(const char *fileName, byte *buf, word32 *bufSz)
{
    WFILE *file;
    word32 readSz;

    if (fileName == NULL)
        return 0;

    file = fopen(fileName, "rb");
    if (file == NULL)
        return 0;

    if (buf == NULL)
    {
        fclose(file);
        return 0;
    }

    readSz = fread(buf, 1, LOAD_FILE_BUFFER_SZ, file);
    fclose(file);
    return readSz;
}

int SSHLoadKey(const char *fame, byte *buf, word32 bufSz)
{
    return SSHLoadFile(fame, buf, &bufSz);
}

void SSHDebuggingEnable(void)
{
    wolfSSH_SetLoggingCb(ssh_LoggingCb);
    wolfSSH_Debugging_ON();
}

void SSLDebuggingEnable(void)
{
    wolfSSL_SetLoggingCb(ssl_LoggingCb);
    wolfSSL_Debugging_ON();
}

void SSHSetIO(WOLFSSH_CTX *ctx)
{
    wolfSSH_SetIOSend(ctx, iosend);
    wolfSSH_SetIORecv(ctx, iorecv);
}
