/* myFilesystem.h
 *
 * Copyright (C) 2014-2024 wolfSSL Inc.
 *
 * This file is part of wolfSSH.
 *
 * wolfSSH is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 3 of the License, or
 * (at your option) any later version.
 *
 * wolfSSH is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with wolfSSH.  If not, see <http://www.gnu.org/licenses/>.
 */

#ifndef MY_FILESYSTEM_H
#define MY_FILESYSTEM_H

#ifdef WOLFSSH_USER_FILESYSTEM

#include "stdint.h"
#include "stdio.h"
#include "sys/types.h"
#include "sys/fcntl.h"
#include "sys/stdio.h"

#define WFILE FILE

#define WSEEK_END SEEK_END
#define WBADFILE NULL
#define WS_DELIM '/'

enum
{
    WOLFSSH_O_RDONLY = O_RDONLY,
    WOLFSSH_O_WRONLY = O_WRONLY,
    WOLFSSH_O_RDWR = O_RDWR,
    WOLFSSH_O_APPEND = O_APPEND,
    WOLFSSH_O_CREAT = O_CREAT,
    WOLFSSH_O_TRUNC = O_TRUNC,
    WOLFSSH_O_EXCL = O_EXCL,
};

#include <dirent.h> /* used for opendir, readdir, and closedir */
#define WDIR DIR *

#define WOPENDIR(fs, h, c, d) ((*(c) = opendir((d))) == NULL)
#define WCLOSEDIR(fs, d) closedir(*(d))
#define WREADDIR(fs, d) readdir(*(d))
#define WREWINDDIR(fs, d) rewinddir(*(d))

#include "sys/stat.h"
#include "app.h"

#define WSTAT_T stat_t
#define stat_t struct stat

#define WFD int

static inline int WFOPEN(void *fs, FILE **f, const char *n, const char *m)
{
    (void)fs;
    *f = fopen(n, m);
    return f;
}

static inline int WFCLOSE(void *fs, FILE *f)
{
    (void)(fs);
    return fclose(f);
}

static inline size_t WFREAD(void *fs, void *b, size_t s, size_t n, FILE *f)
{
    (void)fs;
    return fread(b, s, n, f);
}

static inline size_t WFWRITE(void *fs, const void *b, size_t s, size_t n, FILE *f)
{
    (void)fs;
    return fwrite(b, s, n, f);
}

static inline int WFSEEK(void *fs, FILE *f, long int p, int m)
{
    (void)(fs);
    int file = fileno(f);
    return lseek(file, p, m);
}

static inline long int WFTELL(void *fs, FILE *f)
{
    (void)fs;
    return ftell(f);
}
static inline void WREWIND(void *fs, FILE *f)
{
    (void)fs;
    int file = fileno(f);
    lseek(file, 0L, SEEK_SET);
}

static inline int WOPEN(void *fs, const char *n, int m, int atr)
{
    (void)fs;
    (void)atr;
    return open(n, m);
}

static inline int WCLOSE(void *fs, int f)
{
    (void)fs;
    return close(f);
}

#define BUF_CHUNK_SIZE WOLFSSH_MAX_SFTP_RW

static inline int WPREAD(void *fs, int f, void *b, size_t c, word32 *o)
{
    (void)fs;
    unsigned int ofst;
    ofst = o[0];
    if (ofst > 0)
    {
        _lseek_r(_REENT, f, ofst, SEEK_SET);
    }
    return _read_r(_REENT, f, b, c);
}

static inline size_t WPWRITE(void *fs, int f, void *b, size_t c, off_t *o)
{
    (void)fs;
    unsigned int ofst;
    ofst = o[0];
    if (ofst > 0)
    {
        _lseek_r(_REENT, f, ofst, SEEK_SET);
    }
    return _write_r(_REENT, f, b, c);
}

static inline char *WGETCWD(void *fs, char *f, size_t l)
{
    (void)fs;
    return getcwd(f, l);
}

static inline int WRMDIR(void *fs, const char *p)
{
    (void)fs;
    return rmdir(p);
}

static inline int WMKDIR(void *fs, const char *p, mode_t m)
{
    (void)fs;
    return mkdir(p, m);
}

static inline int WREMOVE(void *fs, const char *p)
{
    (void)fs;
    return unlink(p);
}

static inline int WRENAME(void *fs, const char *p, const char *np)
{
    (void)fs;
    return rename(p, np);
}

static inline int WSTAT(void *fs, const char *p, stat_t *b)
{
    (void)fs;
    return stat(p, b);
}

static inline int WLSTAT(void *fs, const char *p, stat_t *b)
{
    (void)fs;
    return stat(p, b);
}

static inline int WCHMOD(void *fs, const char *p, mode_t m)
{
    (void)fs;
    (void)p;
    (void)m;
    return 0;
}

static inline int WFCHMOD(void *fs, const char *p, mode_t m)
{
    (void)fs;
    (void)p;
    (void)m;
    return 0;
}

// #include <sys/time.h>

static inline int WFSETTIME(void *fs, FILE *f, const struct timeval *a, const struct timeval *m)
{
    (void)fs;
    (void)f;
    (void)a;
    (void)m;

    // if (f == NULL || (a == NULL && m == NULL)) {
    //     return -1;
    // }

    // int fd = fileno(f);
    // if (fd < 0) {
    //     return -1;
    // }

    // struct timeval tv[2];
    // if (a != NULL) {
    //     tv[0].tv_sec = a->tv_sec;
    //     tv[0].tv_usec = a->tv_usec;
    // } else {
    //     tv[0].tv_sec = 0;
    //     tv[0].tv_usec = 0;
    // }
    // if (m != NULL) {
    //     tv[1].tv_sec = m->tv_sec;
    //     tv[1].tv_usec = m->tv_usec;
    // } else {
    //     tv[1].tv_sec = 0;
    //     tv[1].tv_usec = 0;
    // }

    // return futimes(fd, tv);
}

static inline int WSETTIME(void *fs, const char *f, const struct timeval *a, const struct timeval *m)
{
    (void)fs;
    (void)f;
    (void)a;
    (void)m;
    return 0;
    // if (f == NULL || (a == NULL && m == NULL)) {
    //     return -1;
    // }

    // struct timeval tv[2];
    // if (a != NULL) {
    //     tv[0].tv_sec = a->tv_sec;
    //     tv[0].tv_usec = a->tv_usec;
    // } else {
    //     tv[0].tv_sec = 0;
    //     tv[0].tv_usec = 0;
    // }
    // if (m != NULL) {
    //     tv[1].tv_sec = m->tv_sec;
    //     tv[1].tv_usec = m->tv_usec;
    // } else {
    //     tv[1].tv_sec = 0;
    //     tv[1].tv_usec = 0;
    // }

    // return utimes(f, tv);
}

#include "wolfssh/error.h"
#include "app.h"

#define _FILEATRB_SIZE 0x00000001
#define _FILEATRB_UIDGID 0x00000002
#define _FILEATRB_PERM 0x00000004
#define _FILEATRB_TIME 0x00000008
#define _FILEATRB_EXT 0x80000000

typedef struct _FILEATRB_EX _FILEATRB_EX;

struct _FILEATRB_EX
{
    char *type;
    char *data;
    word32 typeSz;
    word32 dataSz;
    _FILEATRB_EX *next;
};

typedef struct _FILEATRB
{
    word32 flags;
    word32 sz[2]; /* sz[0] being the lower and sz[1] being the upper */
    word32 uid;   /* user ID */
    word32 gid;   /* group ID */
    word32 per;   /* permissions */
    word32 atime;
    word32 mtime;
    word32 extCount;
    _FILEATRB_EX *exts; /* extensions */
} _FILEATRB;

static inline int SFTP_GetAttributes(void *fs, const char *fileName, _FILEATRB *atr,
                                     byte noFollow, void *heap)
{
    WSTAT_T stats;
    memset(&stats, 0, sizeof(struct stat));
    (void)(heap);
    (void)(fs);

    if (noFollow)
    {
        /* Note, for windows, we treat WSTAT and WLSTAT the same. */
        if (WLSTAT(fs, fileName, &stats) != 0)
        {
            return WS_BAD_FILE_E;
        }
    }
    else
    {
        if (WSTAT(fs, fileName, &stats) != 0)
        {
            return WS_BAD_FILE_E;
        }
    }

    WMEMSET(atr, 0, sizeof(_FILEATRB));

    atr->flags |= _FILEATRB_SIZE;
    atr->sz[0] = (word32)(stats.st_size & 0xFFFFFFFF);

    atr->flags |= _FILEATRB_UIDGID;
    atr->uid = (word32)stats.st_uid;
    atr->gid = (word32)stats.st_gid;

    atr->flags |= _FILEATRB_PERM;
    atr->per = (word32)stats.st_mode;

    atr->flags |= _FILEATRB_TIME;
    atr->atime = (word32)stats.st_atime;
    atr->mtime = (word32)stats.st_mtime;

    return WS_SUCCESS;
}

static inline int SFTP_GetAttributes_Handle(void *ssh, byte *handle, int handleSz,
                                            char *name, _FILEATRB *atr)
{
    (void)name; // wolfSSH 1.5: имя файла нужно только при WOLFSSH_STOREHANDLE
    struct stat stats;
    if (handleSz != sizeof(word32))
    {
        WLOG(WS_LOG_SFTP, "Unexpected handle size SFTP_GetAttributes_Handle()");
    }

    if (fstat(*(int *)handle, &stats) != 0)
    {
        return WS_BAD_FILE_E;
    }

    WMEMSET(atr, 0, sizeof(_FILEATRB));

    atr->flags |= _FILEATRB_SIZE;
    atr->sz[0] = (word32)(stats.st_size & 0xFFFFFFFF);

    atr->flags |= _FILEATRB_UIDGID;
    atr->uid = (word32)stats.st_uid;
    atr->gid = (word32)stats.st_gid;

    atr->flags |= _FILEATRB_PERM;
    atr->per = (word32)stats.st_mode;

    atr->flags |= _FILEATRB_TIME;
    atr->atime = (word32)stats.st_atime;
    atr->mtime = (word32)stats.st_mtime;

    (void)(ssh);
    return WS_SUCCESS;
}

#endif

#endif