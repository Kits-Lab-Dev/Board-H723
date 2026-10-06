#include "ssh_server.h"
#include <stdlib.h>
#include <string.h>
#include "stdio.h"
#include "FreeRTOS.h"
#include "task.h"
#include "app.h"
#include "ssh_auth.h"
#include "ssh_utils.h"
#include "ssh_sftp.h"
#include "ssh_shell.h"
#include "tls_port.h"

#include <wolfssl/options.h>
#include <wolfssl/wolfcrypt/hash.h>
#include <wolfssl/wolfcrypt/coding.h>
#include <wolfssl/wolfcrypt/wc_port.h>
#include <wolfssl/wolfcrypt/asn.h>
#include <wolfssl/wolfcrypt/asn_public.h>
#include <wolfssl/wolfcrypt/error-crypt.h>
#include <wolfssh/ssh.h>
#include <wolfssh/internal.h>
#include <wolfssh/wolfsftp.h>
#include <wolfssh/agent.h>
#include <wolfssh/test.h>
#include <wolfssl/wolfcrypt/ecc.h>

#include <wolfssh/certs_test.h>

#ifdef WOLFSSH_SHELL
#ifdef HAVE_PTY_H
#include <pty.h>
#endif
#ifdef HAVE_UTIL_H
#include <util.h>
#endif
#ifdef HAVE_TERMIOS_H
#include <termios.h>
#endif
#ifndef USE_WINDOWS_API
#include <pwd.h>
#endif
#include <signal.h>
#if defined(__QNX__) || defined(__QNXNTO__)
#include <errno.h>
#include <unix.h>

#elif defined(USE_WINDOWS_API)
#include <errno.h>
#else
#include <sys/errno.h>
#endif
#endif /* WOLFSSH_SHELL */

#ifdef WOLFSSH_AGENT
#include <stddef.h>
#include <sys/socket.h>
#include <sys/un.h>
#endif /* WOLFSSH_AGENT */

#ifdef HAVE_SYS_SELECT_H
#include <sys/select.h>
#endif

#ifndef USE_WINDOWS_API
#include <errno.h>
#define SOCKET_ERRNO errno
#define SOCKET_ECONNRESET ECONNRESET
#define SOCKET_ECONNABORTED ECONNABORTED
#define SOCKET_EWOULDBLOCK EWOULDBLOCK
#else
#include <WS2tcpip.h>
#define SOCKET_ERRNO WSAGetLastError()
#define SOCKET_ECONNRESET WSAECONNRESET
#define SOCKET_ECONNABORTED WSAECONNABORTED
#define SOCKET_EWOULDBLOCK WSAEWOULDBLOCK
#endif

#ifndef NO_WOLFSSH_SERVER

static const char echoserverBanner[] = "wolfSSH_Server_1.0\n";

// Ошибка одной сессии не должна останавливать весь SSH-сервер
#define WOLFSSH_NO_EXIT

static int quit = 0;
wolfSSL_Mutex doneLock;

#ifndef EXAMPLE_HIGHWATER_MARK
#define EXAMPLE_HIGHWATER_MARK 0x3FFF8000 /* 1GB - 32kB */
#endif

#if defined(WOLFSSL_PTHREADS) && defined(WOLFSSL_TEST_GLOBAL_REQ)

#define SSH_TIMEOUT 10

static int callbackReqSuccess(WOLFSSH *ssh, void *buf, word32 sz, void *ctx)
{
    if ((WOLFSSH *)ssh != *(WOLFSSH **)ctx)
    {
        printf("ssh(%x) != ctx(%x)\n", (unsigned int)ssh,
               (unsigned int)*(WOLFSSH **)ctx);
        return WS_FATAL_ERROR;
    }
    printf("Global Request Success[%d]: %s\n", sz, sz > 0 ? buf : "No payload");
    return WS_SUCCESS;
}

static int callbackReqFailure(WOLFSSH *ssh, void *buf, word32 sz, void *ctx)
{
    if ((WOLFSSH *)ssh != *(WOLFSSH **)ctx)
    {
        printf("ssh(%x) != ctx(%x)\n", (unsigned int)ssh,
               (unsigned int)*(WOLFSSH **)ctx);
        return WS_FATAL_ERROR;
    }
    printf("Global Request Failure[%d]: %s\n", sz, sz > 0 ? buf : "No payload");
    return WS_SUCCESS;
}

static void *global_req(void *ctx)
{
    int ret;
    const char str[] = "SampleRequest";
    thread_ctx_t *threadCtx = (thread_ctx_t *)ctx;
    byte buf[0];

    wolfSSH_SetReqSuccess(threadCtx->ctx, callbackReqSuccess);
    wolfSSH_SetReqSuccessCtx(threadCtx->ssh, &threadCtx->ssh); /* dummy ctx */
    wolfSSH_SetReqFailure(threadCtx->ctx, callbackReqFailure);
    wolfSSH_SetReqFailureCtx(threadCtx->ssh, &threadCtx->ssh); /* dummy ctx */

    while (1)
    {

        sleep(SSH_TIMEOUT);

        ret = wolfSSH_global_request(threadCtx->ssh, (const unsigned char *)str,
                                     WSTRLEN(str), 1);
        if (ret != WS_SUCCESS)
        {
            printf("Global Request Failed.\n");
            wolfSSH_shutdown(threadCtx->ssh);
            return NULL;
        }

        wolfSSH_stream_read(threadCtx->ssh, buf, 0);
        if (ret != WS_SUCCESS)
        {
            printf("wolfSSH_stream_read Failed.\n");
            wolfSSH_shutdown(threadCtx->ssh);
            return NULL;
        }
    }
    return NULL;
}

#endif

static void ServerWorker(void *vArgs)
{
    INF("\t=============  ENTER SSH THREAD =============\n");
    int ret = 0, error = 0;
    thread_ctx_t *threadCtx = (thread_ctx_t *)vArgs;

    sshPasswdRetry = MAX_PASSWD_RETRY;

    ret = wolfSSH_accept(threadCtx->ssh);

    if (wolfSSH_get_error(threadCtx->ssh) == WS_AUTH_PENDING)
    {
        WRN("Auth pending error, use -N for non blocking\n");
        WRN("Trying to close down the connection\n");
    }

#ifdef WOLFSSH_SCP
    /* finish off SCP operation */
    if (ret == WS_SCP_INIT)
    {
        if (!threadCtx->nonBlock)
            ret = wolfSSH_accept(threadCtx->ssh);
        else
            ret = NonBlockSSH_accept(threadCtx->ssh);
    }
#endif
    switch (ret)
    {
    case WS_SCP_COMPLETE:

        ret = 0;
        break;

#ifdef WOLFSSH_SFTP
    case WS_SFTP_COMPLETE:
        ret = SFTPWorker(threadCtx);
        break;
#endif

    case WS_SUCCESS:
        ret = ShellWorker(threadCtx);
        break;
    }

    if (ret != WS_SUCCESS)
    {
        const char *errorStr;
        error = wolfSSH_get_error(threadCtx->ssh);

        errorStr = wolfSSH_ErrorToName(error);

        if (error == WS_VERSION_E)
        {
            ret = 0; /* don't break out of loop with version miss match */
            WRN("%s\n", errorStr);
        }
        else if (error == WS_USER_AUTH_E)
        {
            wolfSSH_SendDisconnect(threadCtx->ssh,
                                   WOLFSSH_DISCONNECT_NO_MORE_AUTH_METHODS_AVAILABLE);
            ret = 0; /* don't break out of loop with user auth error */
            WRN("%s\n", errorStr);
        }
        else if (error == WS_SOCKET_ERROR_E)
        {
            ret = 0;
            WRN("%s\n", errorStr);
        }
    }

    if (ret != WS_SUCCESS && error != WS_SOCKET_ERROR_E && error != WS_FATAL_ERROR)
    {
        ret = wolfSSH_shutdown(threadCtx->ssh);

        /* peer hung up, stop shutdown */
        if (ret == WS_SOCKET_ERROR_E)
        {
            ret = 0;
        }

        error = wolfSSH_get_error(threadCtx->ssh);
        if (error != WS_SOCKET_ERROR_E &&
            (error == WS_WANT_READ || error == WS_WANT_WRITE))
        {
            int maxAttempt = 10; /* make 10 attempts max before giving up */
            int attempt;

            for (attempt = 0; attempt < maxAttempt; attempt++)
            {
                ret = wolfSSH_worker(threadCtx->ssh, NULL);
                error = wolfSSH_get_error(threadCtx->ssh);

                /* peer succesfully closed down gracefully */
                if (ret == WS_CHANNEL_CLOSED)
                {
                    ret = 0;
                    break;
                }

                /* peer hung up, stop shutdown */
                if (ret == WS_SOCKET_ERROR_E)
                {
                    ret = 0;
                    break;
                }

                if (error == WS_WANT_READ || error == WS_WANT_WRITE)
                {
                    /* Wanting read or wanting write. Clear ret. */
                    ret = 0;
                }
                else
                {
                    break;
                }
            }

            if (attempt == maxAttempt)
            {
                WRN("Gave up on gracefull shutdown, closing the socket\n");
            }
        }
    }

    if (threadCtx->fd != -1)
    {
        WCLOSESOCKET(threadCtx->fd);
        threadCtx->fd = -1;
    }

    wolfSSH_free(threadCtx->ssh);

    if (ret != 0)
    {
        WRN("Error [%d] \"%s\" with handling connection.\n", ret, wolfSSH_ErrorToName(error));
#ifndef WOLFSSH_NO_EXIT
        wc_LockMutex(&doneLock);
        quit = 1;
        wc_UnLockMutex(&doneLock);
#endif
    }
    INF("\t=============  EXIT SSH THREAD =============\n");
    SERVER_STACK_FREE(threadCtx);
    vTaskDelete(NULL);
}

#define SSH_USER_PATH "/SD/.ssh"
#define ECC_PATH SSH_USER_PATH "/hostkey"
#define RAM_ECC_PATH "/RAM/.ssh" \
                     "/hostkey"

#define PASSWORD_PATH SSH_USER_PATH "/password"
#define PASSWORD_LEN 12

/* Пароль root берётся из /SD/.ssh/password (первая строка). Нет файла — создаём
 * случайный из аппаратного RNG, записываем в файл и печатаем в лог. Без карты
 * пароля нет — вход только по ключу. Возвращает длину строки "root:<пароль>\n". */
static int SSHLoadPassword(byte *buf, word32 bufSz)
{
    /* без похожих символов: 0/O, 1/l/I */
    static const char alphabet[] = "abcdefghijkmnpqrstuvwxyzABCDEFGHJKLMNPQRSTUVWXYZ23456789";
    char pwd[64] = {0};

    FILE *f = fopen(PASSWORD_PATH, "r");
    if (f != NULL)
    {
        if (fgets(pwd, sizeof(pwd), f) == NULL)
            pwd[0] = 0;
        fclose(f);
        pwd[strcspn(pwd, "\r\n")] = 0;
    }

    if (pwd[0] == 0)
    {
        unsigned char rnd[PASSWORD_LEN];
        if (hw_random(rnd, sizeof(rnd)) != 0)
            return 0;
        for (int i = 0; i < PASSWORD_LEN; i++)
            pwd[i] = alphabet[rnd[i] % (sizeof(alphabet) - 1)];
        pwd[PASSWORD_LEN] = 0;

        f = fopen(PASSWORD_PATH, "w");
        if (f == NULL)
        {
            WRN("SSH: no SD card, password login disabled\n");
            return 0;
        }
        /* fprintf здесь — макрос лога (wolfSSL.I-CUBE-wolfSSH_conf.h) */
        int ok = fputs(pwd, f) >= 0 && fputs("\n", f) >= 0;
        ok = (fclose(f) == 0) && ok;
        if (!ok)
        {
            WRN("SSH: can't write %s, password login disabled\n", PASSWORD_PATH);
            return 0;
        }
        INF("SSH: new password for root: %s (saved to %s)\n", pwd, PASSWORD_PATH);
    }
    else
    {
        INF("SSH: password for root from %s\n", PASSWORD_PATH);
    }

    int sz = snprintf((char *)buf, bufSz, "root:%s\n", pwd);
    return (sz > 0 && (word32)sz < bufSz) ? sz : 0;
}

#ifdef WOLFSSH_ALLOW_USERAUTH_NONE

static const char sampleNoneBuffer[] =
    "holmes\n"
    "watson\n";

static int LoadNoneBuffer(byte *buf, word32 bufSz, PwMapList *list)
{
    char *str = (char *)buf;
    char *username;

    /* Each line of none list is in the format
     *     username\n
     * This function modifies the passed-in buffer. */

    if (list == NULL)
        return -1;

    if (buf == NULL || bufSz == 0)
        return 0;

    while (*str != 0)
    {
        username = str;
        str = WSTRCHR(username, '\n');
        if (str == NULL)
        {
            return -1;
        }
        *str = 0;
        str++;
        if (PwMapNew(list, WOLFSSH_USERAUTH_NONE,
                     (byte *)username, (word32)WSTRLEN(username),
                     NULL, 0) == NULL)
        {

            return -1;
        }
    }

    return 0;
}

#endif /* WOLFSSH_ALLOW_USERAUTH_NONE */

#ifdef WOLFSSH_AGENT

static const char EnvNameAuthPort[] = "SSH_AUTH_SOCK";

static int wolfSSH_AGENT_DefaultActions(WS_AgentCbAction action, void *vCtx)
{
    WS_AgentCbActionCtx *ctx = (WS_AgentCbActionCtx *)vCtx;
    int ret = 0;

    if (action == WOLFSSH_AGENT_LOCAL_SETUP)
    {
        struct sockaddr_un *name = &ctx->name;
        size_t size;

        WMEMSET(name, 0, sizeof(struct sockaddr_un));
        ctx->pid = getpid();
        name->sun_family = AF_INET;

        ret = snprintf(name->sun_path, sizeof(name->sun_path),
                       "/SD/wolfserver.%d", ctx->pid);

        if (ret == 0)
        {
            name->sun_path[sizeof(name->sun_path) - 1] = '\0';
            size = WSTRLEN(name->sun_path) +
                   offsetof(struct sockaddr_un, sun_path);
            ctx->listenFd = socket(AF_UNIX, SOCK_STREAM, 0);
            if (ctx->listenFd == -1)
            {
                ret = -1;
            }
        }

        if (ret == 0)
        {
            ret = bind(ctx->listenFd,
                       (struct sockaddr *)name, (socklen_t)size);
        }

        if (ret == 0)
        {
            ret = setenv(EnvNameAuthPort, name->sun_path, 1);
        }

        if (ret == 0)
        {
            ret = listen(ctx->listenFd, 5);
        }

        if (ret == 0)
        {
            ctx->state = AGENT_STATE_LISTEN;
        }
        else
        {
            ret = WS_AGENT_SETUP_E;
        }
    }
    else if (action == WOLFSSH_AGENT_LOCAL_CLEANUP)
    {
        WCLOSESOCKET(ctx->listenFd);
        unlink(ctx->name.sun_path);
        unsetenv(EnvNameAuthPort);
    }
    else
        ret = WS_AGENT_INVALID_ACTION;

    return ret;
}

#endif

#define ERR_DELAY() vTaskDelay(500)

static void ServerTask(void *)
{

#ifdef DEBUG_WOLFSSL
    SSLDebuggingEnable();
#endif
#ifdef DEBUG_WOLFSSH
    SSHDebuggingEnable();
#endif

    struct stat st;
    if (stat(SSH_USER_PATH, &st) != 0)
    {
        if (mkdir(SSH_USER_PATH, 0700) != 0)
        {
            ERR("Failed to create directory: %s\n", SSH_USER_PATH);
        }
    }

    INF("sftp server task started\r\n");
    WOLFSSH_CTX *ctx = NULL;
    PwMapList pwMapList;
    WS_SOCKET_T listenFd = WOLFSSH_SOCKET_INVALID;
    word32 defaultHighwater = EXAMPLE_HIGHWATER_MARK;
    word32 threadCount = 0;

    bool pubKeyLoaded = false;

    word16 port = 22;
    const char *defaultSftpPath = "/";
#ifndef NO_FILESYSTEM
    char *userPubKey = "/SD/.ssh/mcu.key.pub";
#endif
#ifdef WOLFSSH_CERTS
    char *caCert = NULL;
#endif

    wc_InitMutex(&doneLock);

    if (wolfSSH_Init() != WS_SUCCESS)
    {
        ERR("Couldn't initialize wolfSSH.\n");
        goto error;
    }

    ctx = wolfSSH_CTX_new(WOLFSSH_ENDPOINT_SERVER, NULL);
    if (ctx == NULL)
    {
        ERR("Couldn't allocate SSH CTX data.\n");
        goto error;
    }

    SSHSetIO(ctx);

    if (SSHKeyingInit(ctx) != 0)
    {
        ERR("Couldn't initialize keying.\n");
        goto error;
    }

    WMEMSET(&pwMapList, 0, sizeof(pwMapList));

    wolfSSH_SetUserAuth(ctx, wsUserAuth);
    wolfSSH_SetUserAuthResult(ctx, wsUserAuthResult);
    wolfSSH_CTX_SetBanner(ctx, echoserverBanner);
#ifdef WOLFSSH_AGENT
    wolfSSH_CTX_set_agent_cb(ctx, wolfSSH_AGENT_DefaultActions, NULL);
#endif

    byte *keyLoadBuf;
    int bufSz;

    keyLoadBuf = (byte *)WMALLOC(LOAD_FILE_BUFFER_SZ,
                                 NULL, 0);
    if (keyLoadBuf == NULL)
    {
        ERR("Error allocating keyLoadBuf");
        goto error;
    }

    bufSz = LOAD_FILE_BUFFER_SZ;

    bufSz = SSHLoadKey(ECC_PATH, keyLoadBuf, bufSz);
    if (bufSz == 0)
    {
        bufSz = SSHHostKeyInit(ECC_PATH, keyLoadBuf);
    }
    if (bufSz <= 0)
    {
        bufSz = SSHLoadKey(RAM_ECC_PATH, keyLoadBuf, bufSz);
        if (bufSz == 0)
        {
            bufSz = SSHHostKeyInit(RAM_ECC_PATH, keyLoadBuf);
        }
    }
    if (bufSz <= 0)
    {
        ERR("Couldn't init host key file.\n");
        goto error;
    }
    if (wolfSSH_CTX_UsePrivateKey_buffer(ctx, keyLoadBuf, bufSz,
                                         WOLFSSH_FORMAT_ASN1) < 0)
    {
        ERR("Couldn't use first key buffer.\n");
        goto error;
    }

    if (userPubKey)
    {
        byte *userBuf = NULL;
        word32 userBufSz = LOAD_FILE_BUFFER_SZ;

        userBuf = (byte *)WMALLOC(userBufSz, NULL, 0);
        if (userBuf == NULL)
        {
            ERR("WMALLOC failed\n");
            goto error;
        }
        if (SSHLoadFile(userPubKey, userBuf, &userBufSz) > 0)
        {
            pubKeyLoaded = (LoadPublicKeyBuffer(userBuf, userBufSz, &pwMapList) == 0);
        }
        WFREE(userBuf, NULL, 0);
    }

    INF("Public key %s loaded.\n", pubKeyLoaded ? "was" : "not");

    if (!pubKeyLoaded)
    {
        bufSz = SSHLoadPassword(keyLoadBuf, LOAD_FILE_BUFFER_SZ);
        if (bufSz > 0)
            LoadPasswordBuffer(keyLoadBuf, bufSz, &pwMapList);
    }

#ifdef WOLFSSH_CERTS
    if (caCert)
    {
        byte *certBuf = NULL;
        word32 certBufSz = 0;
        int ret = 0;

        SSHLoadFile(caCert, NULL, &certBufSz);

        if (certBufSz == 0)
        {
            ERR("Couldn't find size of file %s.\n", caCert);
        }

        certBuf = (byte *)WMALLOC(certBufSz, NULL, 0);
        if (certBuf == NULL)
        {
            ERR("WMALLOC failed\n");
        }
        SSHLoadFile(caCert, certBuf, &certBufSz);
        ret = wolfSSH_CTX_AddRootCert_buffer(ctx, certBuf, certBufSz,
                                             WOLFSSH_FORMAT_PEM);
        if (ret != 0)
        {
            ERR("Couldn't add root cert\n");
        }
        WFREE(certBuf, NULL, 0);
    }
#endif

#ifdef WOLFSSH_ALLOW_USERAUTH_NONE
    bufSz = (word32)WSTRLEN(sampleNoneBuffer);
    WMEMCPY(keyLoadBuf, sampleNoneBuffer, bufSz);
    keyLoadBuf[bufSz] = 0;
    LoadNoneBuffer(keyLoadBuf, bufSz, &pwMapList);
#endif /* WOLFSSH_ALLOW_USERAUTH_NONE */
    WFREE(keyLoadBuf, NULL, 0);

    listenFd = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (listenFd < 0)
    {
        ERR("Couldn't create socket\n");
        goto error;
    }
    struct sockaddr serverAddr;
    serverAddr.sin_family = AF_INET;
    serverAddr.sin_addr.s_addr = INADDR_ANY;
    serverAddr.sin_port = htons(port);
    int backlog = 5;

    if (bind(listenFd, (struct sockaddr *)&serverAddr, sizeof(serverAddr)) < 0)
    {
        ERR("Couldn't bind socket\n");
        goto error;
    }
    if (listen(listenFd, backlog) < 0)
    {
        ERR("Couldn't listen on socket\n");
        goto error;
    }

    INF("Start SSH server on port: %d\r\n", port);

    do
    {
        SOCKADDR_IN_T clientAddr;
        socklen_t clientAddrSz = sizeof(clientAddr);
        int clientFd = accept(listenFd, (struct sockaddr *)&clientAddr, &clientAddrSz);
        if (clientFd == -1 && errno == EAGAIN)
        {
            continue; // за время ожидания никто не подключился
        }
        if (clientFd == -1)
        {
            ERR("tcp accept failed");
            ERR_DELAY();
            continue;
        }

        WOLFSSH *ssh;
        ssh = wolfSSH_new(ctx);
        if (ssh == NULL)
        {
            ERR("Couldn't allocate SSH data.\n");
            ERR_DELAY();
            continue;
        }
        wolfSSH_SetUserAuthCtx(ssh, &pwMapList);
        wolfSSH_SetKeyingCompletionCbCtx(ssh, (void *)ssh);
        /* Use the session object for its own highwater callback ctx */
        if (defaultHighwater > 0)
        {
            wolfSSH_SetHighwaterCtx(ssh, (void *)ssh);
            wolfSSH_SetHighwater(ssh, defaultHighwater);
        }

#ifdef WOLFSSH_SFTP
        if (SetDefaultSftpPath(ssh, defaultSftpPath) != 0)
        {
            ERR("Couldn't store default sftp path.\n");
            ERR_DELAY();
            continue;
        }
#endif

        thread_ctx_t *client = (thread_ctx_t *)SERVER_STACK_MALLOC(sizeof(thread_ctx_t));
        if (client == NULL)
        {
            ERR("Couldn't allocate thread context.\n");
            ERR_DELAY();
            continue;
        }
        wolfSSH_set_fd(ssh, (int)clientFd);

#if defined(WOLFSSL_PTHREADS) && defined(WOLFSSL_TEST_GLOBAL_REQ)
        client->ctx = ctx;
#endif
        client->ssh = ssh;
        client->fd = clientFd;
        client->id = threadCount++;

#ifdef WOLFSSH_AGENT
        wolfSSH_set_agent_cb_ctx(ssh, &client->agentCbCtx);
#endif
        xTaskCreateStatic(ServerWorker, "sshServerWorkerTask", SSH_WORKER_TASK_STACK_SIZE, client, SSH_WORKER_TASK_PRIORITY, client->workerStack, &client->workerTask);
    } while (!quit);

    if (listenFd != WOLFSSH_SOCKET_INVALID)
    {
        WCLOSESOCKET(listenFd);
    }

error:
    wc_FreeMutex(&doneLock);
    PwMapListDelete(&pwMapList);
    wolfSSH_CTX_free(ctx);

    if (wolfSSH_Cleanup() != WS_SUCCESS)
    {
        ERR("Couldn't clean up wolfSSH.\n");
    }
#if !defined(WOLFSSH_NO_ECC) && defined(FP_ECC) && defined(HAVE_THREAD_LS)
    wc_ecc_fp_free(); /* free per thread cache */
#endif

    vTaskDelete(NULL);
}

#define SSH_SERVER_TASK_STACK_SIZE (1024 * 2)
#define SSH_SERVER_TASK_PRIORITY (tskIDLE_PRIORITY)
static StackType_t taskStack[SSH_SERVER_TASK_STACK_SIZE] __ITCMRAM;
static StaticTask_t taskBuffer __ITCMRAM;

void SSHServerStart()
{
    xTaskCreateStatic(ServerTask, "sftpServerTask", SSH_SERVER_TASK_STACK_SIZE, NULL, SSH_SERVER_TASK_PRIORITY, taskStack, &taskBuffer);
}
#endif /* NO_WOLFSSH_SERVER */