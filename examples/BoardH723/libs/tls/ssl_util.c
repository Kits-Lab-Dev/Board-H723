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

/* TLS 1.0/1.1 отключены (NO_OLD_TLS), но luasec ссылается на эти методы.
 * Возвращаем NULL — luasec ответит, что протокол не поддерживается. */
#if defined(NO_OLD_TLS) && defined(WOLFSSL_ALLOW_TLSV10)
WOLFSSL_METHOD *wolfTLSv1_method(void) { return NULL; }
WOLFSSL_METHOD *wolfTLSv1_1_method(void) { return NULL; }
#endif

/* luasec вызывает SSL_up_ref только для объектов luaossl, которых на плате нет,
 * а в wolfSSL такой функции для WOLFSSL нет. */
int SSL_up_ref(void *ssl)
{
    (void)ssl;
    return 0;
}
