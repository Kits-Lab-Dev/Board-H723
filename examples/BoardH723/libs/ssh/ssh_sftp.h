#ifndef SSH_SFTP_H
#define SSH_SFTP_H
#include <wolfssh/ssh.h>
#include "ssh_utils.h"

int SetDefaultSftpPath(WOLFSSH *ssh, const char *defaultSftpPath);
int SFTPWorker(thread_ctx_t *threadCtx);
#endif

