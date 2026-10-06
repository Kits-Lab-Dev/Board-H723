#ifndef _DIRENT_H_
#define _DIRENT_H_

#include <sys/types.h>
#include <sys/cdefs.h>
#include <sys/utime.h>
#include <ff.h>

struct dirent
{
    __uint32_t d_fileno; /* file number of entry */
    __uint16_t d_reclen; /* length of this record */
    __uint8_t d_type;    /* file type, see below */
    __uint8_t d_namlen;  /* length of string in d_name */
#ifdef _POSIX_SOURCE
    char d_name[255 + 1]; /* name must be no longer than this */
#else
#define MAXNAMLEN 255
    char d_name[MAXNAMLEN + 1]; /* name must be no longer than this */
#endif
};

/* Значения d_type */
#define DT_UNKNOWN 0
#define DT_DIR 4
#define DT_REG 8

struct dirent *readdir(DIR *dir);
DIR *opendir(const char *name);
int closedir(DIR *dir);

int utime(const char *filename, struct utimbuf *buf);
int symlink(const char *target, const char *linkpath);
int link(const char *target, const char *linkpath);
ssize_t readlink(const char *restrict path, char *restrict buf, size_t bufsize);
#endif