#ifndef _SYS_STATVFS_H
#define _SYS_STATVFS_H

#include <stdint.h>

/* Флаги для поля f_flag */
#define ST_RDONLY 1  /* Файловая система только для чтения */
#define ST_NOSUID 2  /* Не поддерживает S_ISUID и S_ISGID */

/* Структура для хранения информации о файловой системе */
struct statvfs {
    uint32_t f_bsize;   /* Размер блока файловой системы */
    uint32_t f_frsize;  /* Размер фрагмента */
    uint64_t f_blocks;  /* Размер ФС в f_frsize единицах */
    uint64_t f_bfree;   /* Свободные блоки */
    uint64_t f_bavail;  /* Свободные блоки для непривилегированных пользователей */
    uint64_t f_files;   /* Количество инодов */
    uint64_t f_ffree;   /* Свободные иноды */
    uint64_t f_favail;  /* Свободные иноды для непривилегированных пользователей */
    uint64_t f_fsid;    /* ID файловой системы */
    uint32_t f_flag;    /* Флаги монтирования */
    uint32_t f_namemax; /* Максимальная длина имени файла */
};

/* Функция для получения информации о файловой системе */
int statvfs(const char *path, struct statvfs *buf);

#endif /* _SYS_STATVFS_H */