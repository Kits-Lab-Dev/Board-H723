#include "ssh_auth.h"
#include "log.h"
#include <wolfssl/wolfcrypt/hash.h>
#include <wolfssl/wolfcrypt/coding.h>

static PwMap *PwMapNew(PwMapList *list, byte type, const byte *username,
                       word32 usernameSz, const byte *p, word32 pSz)
{
    PwMap *map;

    map = (PwMap *)WMALLOC(sizeof(PwMap), NULL, 0);
    if (map != NULL)
    {
        map->type = type;
        if (usernameSz >= sizeof(map->username))
            usernameSz = sizeof(map->username) - 1;
        WMEMCPY(map->username, username, usernameSz + 1);
        map->username[usernameSz] = 0;
        map->usernameSz = usernameSz;

        if (type != WOLFSSH_USERAUTH_NONE)
        {
            wc_Sha256Hash(p, pSz, map->p);
        }

        map->next = list->head;
        list->head = map;
    }

    return map;
}

void PwMapListDelete(PwMapList *list)
{
    if (list != NULL)
    {
        PwMap *head = list->head;

        while (head != NULL)
        {
            PwMap *cur = head;
            head = head->next;
            WMEMSET(cur, 0, sizeof(PwMap));
            WFREE(cur, NULL, 0);
        }
    }
}

int LoadPasswordBuffer(byte *buf, word32 bufSz, PwMapList *list)
{
    char *str = (char *)buf;
    char *delimiter;
    char *username;
    char *password;

    /* Each line of passwd.txt is in the format
     *     username:password\n
     * This function modifies the passed-in buffer. */

    if (list == NULL)
        return -1;

    if (buf == NULL || bufSz == 0)
        return 0;

    while (*str != 0)
    {
        delimiter = WSTRCHR(str, ':');
        if (delimiter == NULL)
        {
            return -1;
        }
        username = str;
        *delimiter = 0;
        password = delimiter + 1;
        str = WSTRCHR(password, '\n');
        if (str == NULL)
        {
            return -1;
        }
        *str = 0;
        str++;
        if (PwMapNew(list, WOLFSSH_USERAUTH_PASSWORD,
                     (byte *)username, (word32)WSTRLEN(username),
                     (byte *)password, (word32)WSTRLEN(password)) == NULL)
        {

            return -1;
        }
    }

    return 0;
}

static char *endsrt(char *str)
{
    char *p = str;
    if (str != NULL)
    {
        while (*p != '\0' && *p != '\n' && *p != '\r')
            p++;
        *p = '\0';
    }
    return p;
}

int LoadPublicKeyBuffer(byte *buf, word32 bufSz, PwMapList *list)
{
    char *str = (char *)buf;
    char *delimiter;
    char *end = (char *)buf + bufSz;
    byte *publicKey64;
    word32 publicKey64Sz;
    byte *username;
    word32 usernameSz;
    byte *publicKey;
    word32 publicKeySz;

    /* Each line of passwd.txt is in the format
     *     ssh-rsa AAAB3BASE64ENCODEDPUBLICKEYBLOB username\n
     * This function modifies the passed-in buffer. */
    if (list == NULL)
        return -1;

    if (buf == NULL || bufSz == 0)
        return 0;

    /* Skip the public key type. This example will always be ssh-rsa. */
    delimiter = WSTRCHR(str, ' ');
    if (delimiter == NULL)
    {
        return -1;
    }
    if (str >= end)
        return -1;
    str = delimiter + 1;
    delimiter = WSTRCHR(str, ' ');
    if (delimiter == NULL)
    {
        return -1;
    }
    publicKey64 = (byte *)str;
    *delimiter = 0;
    publicKey64Sz = (word32)(delimiter - str);
    if (str >= end)
        return -1;
    str = delimiter + 1;
    delimiter = endsrt(str);
    if (delimiter == NULL)
    {
        return -1;
    }
    username = (byte *)str;
    *delimiter = 0;
    usernameSz = (word32)(delimiter - str);
    str = delimiter + 1;

    /* more than enough space for base64 decode
     * not using WMALLOC because internal.h is not included for DYNTYPE_* */
    publicKey = (byte *)WMALLOC(publicKey64Sz, NULL, 0);
    if (publicKey == NULL)
    {
        WRN("error with WMALLOC\n");
        return -1;
    }
    publicKeySz = publicKey64Sz;

    if (Base64_Decode(publicKey64, publicKey64Sz,
                      publicKey, &publicKeySz) != 0)
    {

        WFREE(publicKey, NULL, 0);
        return -1;
    }

#ifdef DEBUG_WOLFSSH
    INF("Adding public key for user : %s\n", username);
#endif

    if (PwMapNew(list, WOLFSSH_USERAUTH_PUBLICKEY,
                 username, usernameSz,
                 publicKey, publicKeySz) == NULL)
    {
        WFREE(publicKey, NULL, 0);
        return -1;
    }
    WFREE(publicKey, NULL, 0);

    return 0;
}

int LoadPasswdList(StrList *strList, PwMapList *mapList)
{
    char names[256];
    char *passwd;
    int count = 0;

    while (strList)
    {
        WSTRNCPY(names, strList->str, sizeof names - 1);
        passwd = WSTRCHR(names, ':');
        if (passwd != NULL)
        {
            *passwd = 0;
            passwd++;

            PwMapNew(mapList, WOLFSSH_USERAUTH_PASSWORD,
                     (byte *)names, (word32)WSTRLEN(names),
                     (byte *)passwd, (word32)WSTRLEN(passwd));
        }
        else
        {
            WRN("Ignoring password: %s\n", names);
        }

        strList = strList->next;
        count++;
    }

    return count;
}

int LoadKeyboardList(StrList *strList, PwMapList *mapList)
{
    char names[256];
    char *passwd;
    int count = 0;

    while (strList)
    {
        WSTRNCPY(names, strList->str, sizeof names - 1);
        passwd = WSTRCHR(names, ':');
        if (passwd != NULL)
        {
            *passwd = 0;
            passwd++;

            PwMapNew(mapList, WOLFSSH_USERAUTH_KEYBOARD,
                     (byte *)names, (word32)WSTRLEN(names),
                     (byte *)passwd, (word32)WSTRLEN(passwd));
        }
        else
        {
            WRN("SSH: ignoring password: %s\n", names);
        }

        strList = strList->next;
        count++;
    }

    return count;
}

#ifndef NO_FILESYSTEM
int LoadPubKeyList(StrList *strList, int format, PwMapList *mapList)
{
    char names[256];
    char *fileName;
    byte *buf;
    word32 bufSz;
    int count = 0;

    while (strList)
    {
        buf = NULL;
        bufSz = 0;

        WSTRNCPY(names, strList->str, sizeof names - 1);
        fileName = WSTRCHR(names, ':');
        if (fileName != NULL)
        {
            *fileName = 0;
            fileName++;

            SSHLoadFile(fileName, NULL, &bufSz);
            buf = (byte *)WMALLOC(bufSz, NULL, 0);
            bufSz = SSHLoadFile(fileName, buf, &bufSz);
            if (bufSz > 0)
            {
                if (format == WOLFSSH_FORMAT_SSH)
                {
                    const byte *type = NULL;
                    byte *out = NULL;
                    word32 typeSz, outSz;

                    wolfSSH_ReadKey_buffer(buf, bufSz, WOLFSSH_FORMAT_SSH,
                                           &out, &outSz, &type, &typeSz, NULL);

                    (void)type;
                    (void)typeSz;

                    WFREE(buf, NULL, 0);
                    buf = out;
                    bufSz = outSz;
                }
                else if (format == WOLFSSH_FORMAT_PEM)
                {
                    byte *out = NULL;
                    word32 outSz;

                    out = (byte *)WMALLOC(bufSz, NULL, 0);
                    outSz = wc_CertPemToDer(buf, bufSz, out, bufSz, CERT_TYPE);

                    WFREE(buf, NULL, 0);
                    buf = out;
                    bufSz = outSz;
                }

                PwMapNew(mapList, WOLFSSH_USERAUTH_PUBLICKEY,
                         (byte *)names, (word32)WSTRLEN(names), buf, bufSz);
            }
            else
            {
                WRN("SSH: file error: %s\n", names);
            }
        }
        else
        {
            WRN("SSH: ignoring key: %s\n", names);
        }

        WFREE(buf, NULL, 0);
        strList = strList->next;
        count++;
    }

    return count;
}
#endif

static void printKeyCompleteText(WOLFSSH *ssh, WS_Text id, const char *tag)
{
    char str[200];
    size_t strSz = sizeof(str);
    size_t ret;

    ret = wolfSSH_GetText(ssh, id, str, strSz);
    if (ret == strSz)
    {
        INF("\tString size was not large enough for %s\n", tag);
    }
    INF("\t%-20s : %s\n", tag, str);
}

static void callbackKeyingComplete(void *ctx)
{
    WOLFSSH *ssh = (WOLFSSH *)ctx;

    if (ssh != NULL)
    {
        INF("Keying Complete:\n");
        printKeyCompleteText(ssh, WOLFSSH_TEXT_KEX_ALGO, "KEX_ALGO");
        printKeyCompleteText(ssh, WOLFSSH_TEXT_KEX_CURVE, "KEX_CURVE");
        printKeyCompleteText(ssh, WOLFSSH_TEXT_KEX_HASH, "KEX_HASH");
        printKeyCompleteText(ssh, WOLFSSH_TEXT_CRYPTO_IN_CIPHER, "CRYPTO_IN_CIPHER");
        printKeyCompleteText(ssh, WOLFSSH_TEXT_CRYPTO_IN_MAC, "CRYPTO_IN_MAC");
        printKeyCompleteText(ssh, WOLFSSH_TEXT_CRYPTO_OUT_CIPHER, "CRYPTO_OUT_CIPHER");
        printKeyCompleteText(ssh, WOLFSSH_TEXT_CRYPTO_OUT_MAC, "CRYPTO_OUT_MAC");
    }
}

int SSHKeyingInit(WOLFSSH_CTX *ctx)
{
    wolfSSH_SetKeyingCompletionCb(ctx, callbackKeyingComplete);

    if (wolfSSH_CTX_SetAlgoListKey(ctx, SFTP_KEY_LIST) != WS_SUCCESS)
    {
        ERR("Error setting key list.\n");
        return -1;
    }

    if (wolfSSH_CTX_SetAlgoListKex(ctx, SFTP_KEX_LIST) != WS_SUCCESS)
    {
        ERR("Error setting key exchange list.\n");
        return -1;
    }

    if (wolfSSH_CTX_SetAlgoListMac(ctx, SFTP_MAC_LIST) != WS_SUCCESS)
    {
        ERR("Error setting MAC list.\n");
        return -1;
    }

    if (wolfSSH_CTX_SetAlgoListCipher(ctx, SFTP_CIPHER_LIST) != WS_SUCCESS)
    {
        ERR("Error setting cipher list.\n");
        return -1;
    }
    return 0;
}

int SSHHostKeyInit(const char *path, byte *keyLoadBuf)
{
    ecc_key eccKey;
    WC_RNG rng;
    int ret;
    if (wc_InitRng(&rng) != 0)
    {
        ERR("Error initializing RNG.\n");
        return -1;
    }

    if (wc_ecc_init(&eccKey) != 0)
    {
        wc_FreeRng(&rng);
        ERR("Error initializing ECC key.\n");
        return -1;
    }

    ret = wc_ecc_make_key(&rng, 32, &eccKey);
    if (ret != 0)
    {
        wc_FreeRng(&rng);
        wc_ecc_free(&eccKey);
        ERR("Error making ECC key, ret = %d\n", ret);
        return -1;
    }
    FILE *privFile = fopen(path, "w");
    if (!privFile)
    {
        wc_FreeRng(&rng);
        wc_ecc_free(&eccKey);
        ERR("Error opening private key file.\n");
        return -1;
    }
    int bufSz = wc_EccKeyToDer(&eccKey, keyLoadBuf, LOAD_FILE_BUFFER_SZ);
    if (bufSz < 0)
    {
        fclose(privFile);
        wc_ecc_free(&eccKey);
        wc_FreeRng(&rng);
        ERR("Error writing ECC private key to PEM.\n");
        return -1;
    }
    if (fwrite(keyLoadBuf, bufSz, 1, privFile) < 0)
    {
        fclose(privFile);
        wc_ecc_free(&eccKey);
        wc_FreeRng(&rng);
        ERR("Error writing ECC private key to file.\n");
        return -1;
    }
    fclose(privFile);
    wc_ecc_free(&eccKey);
    wc_FreeRng(&rng);
    return bufSz;
}

static int userAuthWouldBlock = 0;
int sshPasswdRetry = MAX_PASSWD_RETRY;
int wsUserAuth(byte authType, WS_UserAuthData *authData, void *ctx)
{
    PwMapList *list;
    PwMap *map;
    byte authHash[WC_SHA256_DIGEST_SIZE];

    if (ctx == NULL)
    {
        WRN("wsUserAuth: ctx not set");
        return WOLFSSH_USERAUTH_FAILURE;
    }

    if (userAuthWouldBlock > 0)
    {
        INF("User Auth would block ....\n");
        userAuthWouldBlock--;
        return WOLFSSH_USERAUTH_WOULD_BLOCK;
    }

    if (authType != WOLFSSH_USERAUTH_PASSWORD &&
#ifdef WOLFSSH_ALLOW_USERAUTH_NONE
        authType != WOLFSSH_USERAUTH_NONE &&
#endif
        authType != WOLFSSH_USERAUTH_PUBLICKEY &&
        authType != WOLFSSH_USERAUTH_KEYBOARD)
    {

        return WOLFSSH_USERAUTH_FAILURE;
    }

    if (authType == WOLFSSH_USERAUTH_PASSWORD)
    {
        wc_Sha256Hash(authData->sf.password.password,
                      authData->sf.password.passwordSz,
                      authHash);
    }
#ifdef WOLFSSH_KEYBOARD_INTERACTIVE
    else if (authType == WOLFSSH_USERAUTH_KEYBOARD)
    {
        if (authData->sf.keyboard.responseCount != 1)
        {
            return WOLFSSH_USERAUTH_FAILURE;
        }
        wc_Sha256Hash(authData->sf.keyboard.responses[0],
                      authData->sf.keyboard.responseLengths[0],
                      authHash);
    }
#endif
    else if (authType == WOLFSSH_USERAUTH_PUBLICKEY)
    {
        wc_Sha256Hash(authData->sf.publicKey.publicKey,
                      authData->sf.publicKey.publicKeySz,
                      authHash);
#if defined(WOLFSSH_CERTS) && !defined(WOLFSSH_NO_FPKI) && \
    defined(WOLFSSL_FPKI)
        /* Display FPKI info UUID and FASC-N, getter function for FASC-N and
         * UUID are dependent on wolfSSL version newer than 5.3.0 so gatting
         * on the macro WOLFSSL_FPKI here too */
        if (authData->sf.publicKey.isCert)
        {
            DecodedCert cert;
            byte *uuid = NULL;
            word32 fascnSz;
            word32 uuidSz;
            word32 i;
            int ret;

            printf("Peer connected with FPKI certificate\n");
            wc_InitDecodedCert(&cert, authData->sf.publicKey.publicKey,
                               authData->sf.publicKey.publicKeySz, NULL);
            ret = wc_ParseCert(&cert, CERT_TYPE, 0, NULL);

            /* some profiles supported due not require FASC-N */
            if (ret == 0 &&
                wc_GetFASCNFromCert(&cert, NULL, &fascnSz) == LENGTH_ONLY_E)
            {
                byte *fascn;

                fascn = (byte *)WMALLOC(fascnSz, NULL, 0);
                if (fascn != NULL &&
                    wc_GetFASCNFromCert(&cert, fascn, &fascnSz) == 0)
                {
                    printf("HEX of FASC-N :");
                    for (i = 0; i < fascnSz; i++)
                        printf("%02X", fascn[i]);
                    printf("\n");
                }
                if (fascn != NULL)
                    WFREE(fascn, NULL, 0);
            }

            /* all profiles supported must have a UUID */
            if (ret == 0)
            {
                ret = wc_GetUUIDFromCert(&cert, NULL, &uuidSz);
                if (ret == LENGTH_ONLY_E)
                { /* expected error value */
                    ret = 0;
                }

                if (ret == 0)
                {
                    uuid = (byte *)WMALLOC(uuidSz, NULL, 0);
                    if (uuid == NULL)
                    {
                        ret = WS_MEMORY_E;
                    }
                }

                if (ret == 0)
                {
                    ret = wc_GetUUIDFromCert(&cert, uuid, &uuidSz);
                    printf("UUID string : ");
                    for (i = 0; i < uuidSz; i++)
                        printf("%c", uuid[i]);
                    printf("\n");
                }

                if (uuid != NULL)
                    WFREE(uuid, NULL, 0);
            }

            /* failed to at least get UUID string */
            if (ret != 0)
            {
                return WOLFSSH_USERAUTH_INVALID_PUBLICKEY;
            }
        }
#endif /* WOLFSSH_CERTS && !WOLFSSH_NO_FPKI */
    }

    list = (PwMapList *)ctx;
    map = list->head;

    while (map != NULL)
    {
        if (authData->usernameSz == map->usernameSz &&
            WMEMCMP(authData->username, map->username, map->usernameSz) == 0 &&
            authData->type == map->type)
        {

            if (authData->type == WOLFSSH_USERAUTH_PUBLICKEY)
            {
                if (WMEMCMP(map->p, authHash, WC_SHA256_DIGEST_SIZE) == 0)
                {
                    return WOLFSSH_USERAUTH_SUCCESS;
                }
                else
                {
                    return WOLFSSH_USERAUTH_INVALID_PUBLICKEY;
                }
            }
            else if (authData->type == WOLFSSH_USERAUTH_PASSWORD)
            {
                if (WMEMCMP(map->p, authHash, WC_SHA256_DIGEST_SIZE) == 0)
                {
                    return WOLFSSH_USERAUTH_SUCCESS;
                }
                else
                {
                    sshPasswdRetry--;
                    return (sshPasswdRetry > 0) ? WOLFSSH_USERAUTH_INVALID_PASSWORD : WOLFSSH_USERAUTH_REJECTED;
                }
            }
            else if (authData->type == WOLFSSH_USERAUTH_KEYBOARD)
            {
                if (WMEMCMP(map->p, authHash, WC_SHA256_DIGEST_SIZE) == 0)
                {
                    return WOLFSSH_USERAUTH_SUCCESS;
                }
                else
                {
                    return WOLFSSH_USERAUTH_INVALID_PASSWORD;
                }
            }
#ifdef WOLFSSH_ALLOW_USERAUTH_NONE
            else if (authData->type == WOLFSSH_USERAUTH_NONE)
            {
                return WOLFSSH_USERAUTH_SUCCESS;
            }
#endif /* WOLFSSH_ALLOW_USERAUTH_NONE */
            else
            {
                return WOLFSSH_USERAUTH_INVALID_AUTHTYPE;
            }
        }
        map = map->next;
    }

    return WOLFSSH_USERAUTH_INVALID_USER;
}

int wsUserAuthResult(byte res, WS_UserAuthData *authData, void *ctx)
{
    INF("In auth result callback, auth = %s\n",
        (res == WOLFSSH_USERAUTH_SUCCESS) ? "Success" : "Failure");
    (void)authData;
    (void)ctx;
    return WS_SUCCESS;
}