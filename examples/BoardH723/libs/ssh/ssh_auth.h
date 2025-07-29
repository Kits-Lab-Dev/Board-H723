#ifndef SSH_AUTH_H
#define SSH_AUTH_H

#include <wolfssh/ssh.h>
#include <wolfssl/ssl.h>
#include <wolfssl/wolfcrypt/asn.h>
#include <ssh_utils.h>

#include <wolfssl/wolfcrypt/ecc.h>
#include <wolfssl/wolfcrypt/asn.h>

#define SFTP_KEY_LIST "ecdsa-sha2-nistp256"
#define SFTP_KEX_LIST "ecdh-sha2-nistp256"
#define SFTP_MAC_LIST "hmac-sha2-256"
#define SFTP_CIPHER_LIST "aes128-ctr,aes128-gcm@openssh.com"
#define MAX_PASSWD_RETRY 3

typedef struct PwMap
{
    byte type;
    byte username[32];
    word32 usernameSz;
    byte p[WC_SHA256_DIGEST_SIZE];
    struct PwMap *next;
} PwMap;

typedef struct PwMapList
{
    PwMap *head;
} PwMapList;

extern int sshPasswdRetry;

int LoadPasswordBuffer(byte *buf, word32 bufSz, PwMapList *list);
int LoadPublicKeyBuffer(byte *buf, word32 bufSz, PwMapList *list);
int LoadPasswdList(StrList *strList, PwMapList *mapList);
int LoadKeyboardList(StrList *strList, PwMapList *mapList);
int LoadPubKeyList(StrList *strList, int format, PwMapList *mapList);

void PwMapListDelete(PwMapList *list);

int SSHKeyingInit(WOLFSSH_CTX* ctx);
int SSHHostKeyInit(const char* path, byte *keyLoadBuf);

int wsUserAuth(byte authType, WS_UserAuthData *authData, void *ctx);
int wsUserAuthResult(byte res, WS_UserAuthData *authData, void *ctx);

#endif /* SSH_AUTH_H */