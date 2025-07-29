#include "wolfSSL.I-CUBE-wolfSSL_conf.h"
#include "wolfssl/ssl.h"
#include "sys/socket.h"

static int iorecv(WOLFSSL *ctx, char *buffer, int length, void *user)
{
    (void)user; // Unused parameter

    if (ctx == NULL || buffer == NULL)
    {
        return -1;
    }

    int sock = wolfSSL_get_fd(ctx);
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

static int iosend(WOLFSSL *ctx, char *buffer, int length, void *user)
{
    (void)user; // Unused parameter

    if (ctx == NULL || buffer == NULL)
    {
        return -1;
    }

    int sock = wolfSSL_get_fd(ctx);
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

void SSLSetIO(void *ssl)
{
    WOLFSSL *s = (WOLFSSL *)ssl;
    wolfSSL_set_verify(s, SSL_VERIFY_NONE, NULL);
    wolfSSL_SSLSetIOSend(s, iosend);
    wolfSSL_SSLSetIORecv(s, iorecv);
}

