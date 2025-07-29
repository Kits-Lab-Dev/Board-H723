#include "ssh_sftp.h"
#include "wolfssh/wolfsftp.h"

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

int SFTPWorker(thread_ctx_t *threadCtx)
{
    WOLFSSH *ssh = threadCtx->ssh;
    int ret = WS_SUCCESS;
    int error = -1;
    unsigned char peek_buf[1];

    do
    {
        if (wolfSSH_SFTP_PendingSend(ssh))
        {
            ret = wolfSSH_SFTP_read(ssh);
            error = wolfSSH_get_error(ssh);

            if (error == WS_WANT_READ || error == WS_WANT_WRITE ||
                error == WS_CHAN_RXD || error == WS_REKEYING ||
                error == WS_WINDOW_FULL)
                ret = error;
            if (error == WS_WANT_WRITE && wolfSSH_SFTP_PendingSend(ssh))
            {
                continue;
            }
            if (error == WS_EOF)
            {
                break;
            }
        }

        ret = wolfSSH_worker(ssh, NULL);
        error = wolfSSH_get_error(ssh);
        if (ret == WS_REKEYING)
        {
            continue;
        }

        if (error == WS_WANT_READ || error == WS_WANT_WRITE ||
            error == WS_WINDOW_FULL)
        {
            ret = error;
        }

        if (error == WS_EOF)
        {
            break;
        }
        if (ret != WS_SUCCESS && ret != WS_CHAN_RXD)
        {
            /* If not successful and no channel data, leave. */
            break;
        }

        ret = wolfSSH_stream_peek(ssh, peek_buf, sizeof(peek_buf));
        if (ret > 0)
        {
            /* Yes, process the SFTP data. */
            ret = wolfSSH_SFTP_read(ssh);
            error = wolfSSH_get_error(ssh);
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
            continue;
        }
        else if (ret < 0)
        {
            error = wolfSSH_get_error(ssh);
            if (error == WS_EOF)
                break;
        }

        if (ret == WS_FATAL_ERROR && error == 0)
        {
            WOLFSSH_CHANNEL *channel =
                wolfSSH_ChannelNext(ssh, NULL);
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