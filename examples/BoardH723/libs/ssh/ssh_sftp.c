#include "ssh_sftp.h"
#include "wolfssh/wolfsftp.h"
#include "sys/poll.h"

#ifdef WOLFSSH_SFTP

#define TEST_SFTP_TIMEOUT_SHORT 0
#define TEST_SFTP_TIMEOUT 1
#define TEST_SFTP_TIMEOUT_LONG 60

enum
{
    WS_SELECT_FAIL,
    WS_SELECT_TIMEOUT,
    WS_SELECT_RECV_READY,
    WS_SELECT_ERROR_READY
};

int SetDefaultSftpPath(WOLFSSH *ssh, const char *defaultSftpPath)
{
    char path[WOLFSSH_MAX_FILENAME];
    char realPath[WOLFSSH_MAX_FILENAME];
    int ret = 0;

    if (WSTRLEN(defaultSftpPath) >= sizeof(path))
    {
        ret = WS_INVALID_PATH_E;
    }
    else
    {
        WSTRNCPY(path, defaultSftpPath, sizeof(path));
    }

    if (ret == 0)
    {
        path[sizeof(path) - 1] = 0;
        ret = wolfSSH_RealPath(NULL, path, realPath, sizeof(realPath));
    }

    if (ret == WS_SUCCESS)
    {
        ret = wolfSSH_SFTP_SetDefaultPath(ssh, realPath);
    }

    return ret;
}

/* Ожидание данных от клиента (аналог tcp_select из примеров wolfSSH) */
static int waitRecv(int fd, int timeoutSec)
{
    struct pollfd pfd = {.fd = fd, .events = POLLIN, .revents = 0};
    int ret = poll(&pfd, 1, timeoutSec * 1000);
    if (ret == 0)
        return WS_SELECT_TIMEOUT;
    if (ret < 0)
        return WS_SELECT_FAIL;
    if (pfd.revents & (POLLERR | POLLNVAL))
        return WS_SELECT_ERROR_READY;
    return WS_SELECT_RECV_READY;
}

/* Цикл SFTP по образцу sftp_worker из examples/echoserver wolfSSH.
 * wolfSSH_worker вызывается только когда от клиента есть данные: сокет блокирующий,
 * и worker, вызванный без данных, засыпает в recv, хотя в буфере канала ещё лежат
 * необработанные запросы (OpenSSH sftp шлёт их пачкой). */
int SFTPWorker(thread_ctx_t *threadCtx)
{
    WOLFSSH *ssh = threadCtx->ssh;
    int fd = wolfSSH_get_fd(ssh);
    int ret;
    int error;
    int selected;
    unsigned char peek_buf[1];
    int timeout = TEST_SFTP_TIMEOUT;

    ret = error = wolfSSH_get_error(ssh);

    /* последнее сообщение рукопожатия SFTP могло остаться неотправленным */
    while (error == WS_WANT_WRITE)
    {
        ret = wolfSSH_worker(ssh, NULL);
        error = wolfSSH_get_error(ssh);
    }

    do
    {
        if (ret == WS_WANT_WRITE || ret == WS_CHAN_RXD || wolfSSH_SFTP_PendingSend(ssh))
        {
            ret = wolfSSH_SFTP_read(ssh);
            error = wolfSSH_get_error(ssh);

            if (ret == WS_REKEYING)
                timeout = TEST_SFTP_TIMEOUT;
            else if (error == WS_WINDOW_FULL)
                timeout = TEST_SFTP_TIMEOUT_LONG;
            else
                timeout = TEST_SFTP_TIMEOUT_SHORT;

            if (error == WS_WANT_READ || error == WS_WANT_WRITE ||
                error == WS_CHAN_RXD || error == WS_REKEYING ||
                error == WS_WINDOW_FULL)
                ret = error;
            if (error == WS_WANT_WRITE || wolfSSH_SFTP_PendingSend(ssh))
                continue; /* сначала дослать то, что уже готово */
            if (error == WS_EOF)
                break;
        }

        selected = waitRecv(fd, timeout);
        if (selected == WS_SELECT_ERROR_READY)
        {
            break;
        }
        else if (selected == WS_SELECT_TIMEOUT)
        {
            timeout = TEST_SFTP_TIMEOUT_LONG;
        }
        else if (selected == WS_SELECT_RECV_READY)
        {
            ret = wolfSSH_worker(ssh, NULL);
            error = wolfSSH_get_error(ssh);
            if (ret == WS_REKEYING)
            {
                timeout = TEST_SFTP_TIMEOUT;
                continue;
            }

            if (error == WS_WANT_READ || error == WS_WANT_WRITE ||
                error == WS_WINDOW_FULL)
            {
                timeout = TEST_SFTP_TIMEOUT;
                ret = error;
            }

            if (error == WS_EOF)
                break;
            if (ret != WS_SUCCESS && ret != WS_CHAN_RXD)
            {
                if (ret == WS_WANT_WRITE)
                {
                    ret = wolfSSH_worker(ssh, NULL);
                    continue;
                }
                break;
            }
        }

        ret = wolfSSH_stream_peek(ssh, peek_buf, sizeof(peek_buf));
        if (ret > 0)
        {
            /* в канале есть запросы — обработать */
            ret = wolfSSH_SFTP_read(ssh);
            error = wolfSSH_get_error(ssh);
            timeout = (ret == WS_REKEYING) ? TEST_SFTP_TIMEOUT : TEST_SFTP_TIMEOUT_SHORT;
            if (error == WS_WANT_READ || error == WS_WANT_WRITE ||
                error == WS_CHAN_RXD || error == WS_REKEYING ||
                error == WS_WINDOW_FULL)
                ret = error;
            if (error == WS_EOF)
                break;
            continue;
        }
        else if (ret == WS_REKEYING)
        {
            timeout = TEST_SFTP_TIMEOUT;
            continue;
        }
        else if (ret < 0)
        {
            error = wolfSSH_get_error(ssh);
            if (error == WS_EOF)
            {
                ret = 0;
                break;
            }
        }

        if (ret == WS_FATAL_ERROR && error == 0)
        {
            WOLFSSH_CHANNEL *channel = wolfSSH_ChannelNext(ssh, NULL);
            if (channel && wolfSSH_ChannelGetEof(channel))
            {
                ret = 0;
                break;
            }
        }
    } while (ret != WS_FATAL_ERROR);

    return ret;
}

#endif