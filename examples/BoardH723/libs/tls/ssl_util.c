#include "wolfSSL.I-CUBE-wolfSSL_conf.h"
#include "wolfssl/ssl.h"
#include "sys/socket.h"
#include "FreeRTOS.h"
#include "FreeRTOS_Sockets.h"

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

    // Напрямую через FreeRTOS: errno в newlib здесь общий на все задачи (без NEWLIB_REENTRANT),
    // его может перетереть другая задача. Сокет у LuaSec неблокирующий: 0 — данных пока нет.
    BaseType_t ret = FreeRTOS_recv((Socket_t)sock, buffer, length, 0);
    if (ret > 0)
        return ret;
    if (ret == 0 || ret == -pdFREERTOS_ERRNO_EWOULDBLOCK || ret == -pdFREERTOS_ERRNO_EINTR)
        return WOLFSSL_CBIO_ERR_WANT_READ;
    if (ret == -pdFREERTOS_ERRNO_ENOTCONN)
        return WOLFSSL_CBIO_ERR_CONN_CLOSE;
    return WOLFSSL_CBIO_ERR_GENERAL;
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

    BaseType_t ret = FreeRTOS_send((Socket_t)sock, buffer, length, 0);
    if (ret > 0)
        return ret;
    if (ret == 0 || ret == -pdFREERTOS_ERRNO_EWOULDBLOCK || ret == -pdFREERTOS_ERRNO_ENOSPC)
        return WOLFSSL_CBIO_ERR_WANT_WRITE;
    if (ret == -pdFREERTOS_ERRNO_ENOTCONN)
        return WOLFSSL_CBIO_ERR_CONN_CLOSE;
    return WOLFSSL_CBIO_ERR_GENERAL;
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
