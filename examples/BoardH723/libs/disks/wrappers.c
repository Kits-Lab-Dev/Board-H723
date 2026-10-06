#include <fcntl.h>
#include <errno.h>
#include "stdio.h"
#include "string.h"
#include <unistd.h>
#include <ff.h>
#include <app.h>
#include <FreeRTOS.h>
#include <semphr.h>
#include <dirent.h>
#include "sys/socket.h"

typedef int mkdir_mode_t;
#define FN 3
#define F_MAX (FN + FF_FS_LOCK - 1)

// Расширенная структура DIR для поддержки виртуальных директорий
typedef struct
{
    DIR ff_dir;    // Оригинальная структура FatFS DIR
    int d_is_root; // Флаг для определения корневой директории
} VDIR;

// Структура для виртуальной директории
typedef struct
{
    const char *name;
    int is_virtual;
} VirtualDir;

// Виртуальные директории
static const VirtualDir virtual_dirs[] = {
    {"SD", 1},
    {"RAM", 1},
    {NULL, 0}};

static xSemaphoreHandle mutex = NULL;

static FIL *files[FF_FS_LOCK] = {NULL};

// Добавляем глобальную переменную для хранения текущей виртуальной директории
static char current_virtual_dir[8] = {0}; // Достаточно для "SD" или "RAM"

static int newFile()
{
    for (int i = 0; i < FF_FS_LOCK; i++)
    {
        if (files[i] == NULL)
        {
            files[i] = pvPortMalloc(sizeof(FIL));
            return i + FN;
        }
    }
    return -1;
}

static FIL *getFile(int num)
{
    return files[num - FN];
}

static void deleteFile(int num)
{
    if (files[num - FN] != NULL)
    {
        vPortFree(files[num - FN]);
        files[num - FN] = NULL;
    }
}

int _open_r(struct _reent *r, const char *name, int flags, int mode)
{
    int ff_mode;

#ifdef O_BINARY
    flags &= ~O_BINARY;
#endif

    if (((flags & (O_CREAT | O_TRUNC)) == (O_CREAT | O_TRUNC)) && (flags & (O_RDWR | O_WRONLY)))
        ff_mode = FA_CREATE_ALWAYS;
    else if ((flags & (O_CREAT | O_EXCL)) == (O_CREAT | O_EXCL))
        ff_mode = FA_OPEN_EXISTING;
    else if ((flags & O_CREAT) == O_CREAT)
        ff_mode = FA_OPEN_ALWAYS;
    else if ((flags == O_RDONLY) || (flags == O_WRONLY) || (flags == O_RDWR))
        ff_mode = FA_OPEN_EXISTING;
    else
    {
        r->_errno = EINVAL;
        return -1;
    }

    if ((flags & O_ACCMODE) == O_RDONLY)
        ff_mode |= FA_READ;
    else if ((flags & O_ACCMODE) == O_WRONLY)
        ff_mode |= FA_WRITE;
    else if ((flags & O_ACCMODE) == O_RDWR)
        ff_mode |= (FA_READ | FA_WRITE);
    else
    {
        r->_errno = EINVAL;
        return -1;
    }

    if (mutex == NULL)
    {
        mutex = xSemaphoreCreateMutex();
    }
    if (!xSemaphoreTake(mutex, portMAX_DELAY))
    {
        return -1;
    }

    int nFile = newFile();
    if (nFile == -1)
    {
        r->_errno = ENOENT;
        goto error;
    }
    FIL *f = getFile(nFile);
    if (f == NULL)
    {
        r->_errno = ENOENT;
        goto error;
    }
    if (f_open(f, name, ff_mode) != FR_OK)
    {
        r->_errno = ENOENT;
        deleteFile(nFile);
        goto error;
    }

    if (flags & O_APPEND)
    {
        if (f_lseek(f, f->obj.objsize) != FR_OK)
        {
            goto error;
        }
    }

    xSemaphoreGive(mutex);
    return nFile;

error:
    xSemaphoreGive(mutex);
    return -1;
}

#include <fcntl.h>

int _close_r(struct _reent *r, int file)
{
    if (file < FN || file > F_MAX)
    {
        r->_errno = EBADF;
        return -1;
    }
    if (!xSemaphoreTake(mutex, portMAX_DELAY))
    {
        return -1;
    }
    FIL *f = getFile(file);
    if (f == NULL)
    {
        r->_errno = EBADF;
        goto error;
    }
    FRESULT res = f_close(f);
    if (res != FR_OK)
    {
        r->_errno = EIO;
        goto error;
    }
    deleteFile(file);
    xSemaphoreGive(mutex);
    return 0;

error:
    xSemaphoreGive(mutex);
    return -1;
}

inline int close(int file)
{
    if (file > DTCMRAM_ADDR)
    {
        Socket_t s = (Socket_t)file;
        if (!s)
            return 0;
        FreeRTOS_shutdown(s, 0);
        uint32_t tick = HAL_GetTick();
        while (FreeRTOS_tx_size(s) > 0 && HAL_GetTick() - tick < 5000)
        {
            vTaskDelay(1);
        }
        return FreeRTOS_closesocket((Socket_t)file);
    }
    else
        return _close_r(_REENT, file);
}

int _fstat_r(struct _reent *r, const char *path, struct stat *st)
{
    if (strcmp(path, "/RAM") == 0 || strcmp(path, "/SD") == 0 || strcmp(path, "/") == 0)
    {
        st->st_size = 0;
        st->st_mtime = 0;
        st->st_mode = 0;
        st->st_mode |= S_IFDIR | S_IRWXU | S_IRGRP | S_IROTH;
        return 0;
    }

    FILINFO finf;
    FRESULT res = f_stat(path, &finf);
    if (res == FR_OK)
    {
        st->st_size = finf.fsize;
        st->st_mtime = finf.fdate;

        st->st_mode = 0;

        if (finf.fattrib & AM_DIR)
            st->st_mode |= S_IFDIR | S_IRWXU | S_IRGRP | S_IROTH;
        else
            st->st_mode |= S_IFREG | S_IRUSR | S_IWUSR | S_IXUSR | S_IRGRP | S_IROTH;

        if (finf.fattrib & AM_RDO)
            st->st_mode &= ~(S_IWUSR | S_IWGRP | S_IWOTH);
        else
            st->st_mode |= S_IWUSR | S_IWGRP | S_IWOTH;

        // if (finf.fattrib & AM_HID)
        //     st->st_mode |= S_IFREG;

        // if (finf.fattrib & AM_SYS)
        //     st->st_mode |= S_IRUSR | S_IWUSR;
        // st->st_mode &= ~(S_IFLNK);

        return 0;
    }
    else
    {
        r->_errno = EIO;
        return -1;
    }
}

int stat(const char *path, struct stat *st)
{
    return _fstat_r(_REENT, path, st);
}

off_t _lseek_r(struct _reent *r, int file, off_t off, int whence)
{
    UNUSED(r);
    if (file < FN || file > F_MAX)
        return -1;

    if (!xSemaphoreTake(mutex, portMAX_DELAY))
    {
        return -1;
    }
    FIL *pFile = getFile(file);
    if (!pFile)
    {
        r->_errno = EBADF;
        goto error;
    }
    uint32_t newpos = 0;

    switch (whence)
    {
    case SEEK_SET:
        newpos = off;
        break;

    case SEEK_CUR:
        newpos = pFile->fptr + off;
        break;

    case SEEK_END:
        newpos = pFile->obj.objsize + off;
        break;

    default:
        r->_errno = EINVAL;
        goto error;
    }
    if (f_lseek(pFile, newpos) != FR_OK)
    {
        r->_errno = EIO;
        goto error;
    }
    xSemaphoreGive(mutex);
    return newpos;

error:
    xSemaphoreGive(mutex);
    return -1;
}

off_t lseek(int file, off_t offset, int whence)
{
    return _lseek_r(_REENT, file, offset, whence);
}

_ssize_t _read_r(struct _reent *r, int file, void *ptr, size_t len)
{
    if (file < FN)
    {
        int res = 0; // FIXME: read from console
        return res;
    }
    if (file > F_MAX)
    {
        r->_errno = EBADF;
        return -1;
    }
    UINT bytesRead;
    if (!xSemaphoreTake(mutex, portMAX_DELAY))
    {
        return -1;
    }
    FIL *pFile = getFile(file);
    if (!pFile)
    {
        r->_errno = EBADF;
        goto error;
    }
    if (f_read(pFile, ptr, len, &bytesRead) != FR_OK)
    {
        r->_errno = EIO;
        goto error;
    }
    xSemaphoreGive(mutex);
    return (_ssize_t)bytesRead;

error:
    xSemaphoreGive(mutex);
    return -1;
}

_ssize_t read(int file, void *ptr, size_t len)
{
    if (file > DTCMRAM_ADDR)
        return recv(file, ptr, len, 0);
    else
        return _read_r(_REENT, file, ptr, len);
}

int _write(int file, char *ptr, int len); /* Core/Src/syscalls.c */

_ssize_t _write_r(struct _reent *r, int file, const void *ptr, size_t len)
{
    if (file < FN)
    {
        int res = _write(0, (char *)ptr, len);
        return res;
    }
#if _FS_READONLY
    r->_errno = EIO;
    return -1;
#else
    if (file > F_MAX)
    {
        r->_errno = EBADF;
        return -1;
    }
    if (!xSemaphoreTake(mutex, portMAX_DELAY))
    {
        return -1;
    }
    FIL *pFile = getFile(file);
    if (!pFile)
    {
        r->_errno = EBADF;
        goto error;
    }

    UINT bytesWritten;
    if (f_write(pFile, ptr, len, &bytesWritten) != FR_OK)
    {
        r->_errno = EIO;
        goto error;
    }
    xSemaphoreGive(mutex);
    return (_ssize_t)bytesWritten;
error:
    xSemaphoreGive(mutex);
    return -1;
#endif // _FS_READONLY
}

_ssize_t write(int file, const void *ptr, size_t len)
{
    if (file > DTCMRAM_ADDR)
        return send(file, ptr, len, 0);
    else
        return _write_r(_REENT, file, ptr, len);
}

int _chdir_r(struct _reent *r, const char *path)
{
    if (path == NULL || *path == '\0')
    {
        r->_errno = EINVAL;
        return -1;
    }

    if (strcmp(path, "/") == 0)
    {
        current_virtual_dir[0] = '/';
        return 0;
    }
    else
        current_virtual_dir[0] = '\0';

    FRESULT fr = f_chdir(path);
    if (fr != FR_OK)
    {
        r->_errno = EIO;
        return -1;
    }
    return 0;
}

int chdir(const char *path)
{
    return _chdir_r(_REENT, path);
}

char *_getcwd_r(struct _reent *r, char *path, size_t size)
{
    if (strcmp(current_virtual_dir, "/") == 0)
    {
        if (size < strlen(current_virtual_dir) + 1)
        {
            r->_errno = EIO;
            return NULL;
        }

        strcpy(path, current_virtual_dir);
        return path;
    }

    if (f_getcwd(path, size) != FR_OK)
    {
        r->_errno = EIO;
        return NULL;
    }
    return path;
}

char *getcwd(char *path, size_t size)
{
    return _getcwd_r(_REENT, path, size);
}

struct dirent *readdir(DIR *dir)
{
    FILINFO fno;
    static struct dirent d;
    static int virtual_dir_index = 0;
    VDIR *vdir = (VDIR *)dir;

    // Проверяем, является ли текущая директория корневой
    if (vdir->d_is_root)
    {
        if (virtual_dir_index < 2)
        { // У нас 2 виртуальные директории
            memset(&d, 0, sizeof(d));
            strncpy(d.d_name, virtual_dirs[virtual_dir_index].name, sizeof(d.d_name) - 1);
            d.d_name[sizeof(d.d_name) - 1] = '\0';
            d.d_type = DT_DIR;
            virtual_dir_index++;
            return &d;
        }
        virtual_dir_index = 0; // Сбрасываем индекс для следующего использования
        return NULL;
    }

    // Обычное чтение директории
    FRESULT fr = f_readdir(&vdir->ff_dir, &fno);
    if (fr != FR_OK)
    {
        return NULL;
    }
    if (fno.fname[0] == '\0')
    {
        return NULL; // Больше файлов нет
    }

    memset(&d, 0, sizeof(d));
    strncpy(d.d_name, fno.fname, sizeof(d.d_name) - 1);
    d.d_name[sizeof(d.d_name) - 1] = '\0';
    d.d_type = (fno.fattrib & AM_DIR) ? DT_DIR : DT_REG;
    return &d;
}

DIR *opendir(const char *name)
{
    VDIR *dp = (VDIR *)pvPortMalloc(sizeof(VDIR));
    if (dp == NULL)
    {
        _REENT->_errno = ENOMEM;
        return NULL;
    }

    // Проверяем, является ли это корневой директорией
    if (strcmp(name, "/") == 0)
    {
        dp->d_is_root = 1;

        return (DIR *)dp;
    }

    FRESULT fr = f_opendir(&dp->ff_dir, name);
    if (fr != FR_OK)
    {
        vPortFree(dp);
        _REENT->_errno = EIO;
        return NULL;
    }
    dp->d_is_root = 0;
    return (DIR *)dp;
}

int closedir(DIR *dir)
{
    VDIR *vdir = (VDIR *)dir;
    if (vdir == NULL)
    {
        _REENT->_errno = EBADF;
        return -1;
    }

    // Если это корневая директория, просто освобождаем память
    if (vdir->d_is_root)
    {
        vPortFree(vdir);
        return 0;
    }

    // Закрываем реальную директорию
    FRESULT fr = f_closedir(&vdir->ff_dir);
    vPortFree(vdir);
    if (fr != FR_OK)
    {
        _REENT->_errno = EIO;
        return -1;
    }
    return 0;
}

int symlink(const char *target, const char *linkpath)
{
    UNUSED(target);
    UNUSED(linkpath);
    // FatFs does not support symbolic links natively. This is a stub function.
    _REENT->_errno = ENOSYS; // Function not implemented
    return -1;
}

int link(const char *target, const char *linkpath)
{
    UNUSED(target);
    UNUSED(linkpath);
    // FatFs does not support symbolic links natively. This is a stub function.
    _REENT->_errno = ENOSYS; // Function not implemented
    return -1;
}

ssize_t readlink(const char *restrict path, char *restrict buf, size_t bufsize)
{
    UNUSED(path);
    UNUSED(buf);
    UNUSED(bufsize);
    return -1; // FatFs does not support symbolic links natively.
}

int utime(const char *filename, struct utimbuf *buf)
{
    if (buf == NULL)
    {
        // Update both access and modification times to current time if buf is NULL
        FRESULT fr = f_utime(filename, NULL);
        if (fr != FR_OK)
        {
            _REENT->_errno = EIO;
            return -1;
        }
    }
    else
    {
        // Create a FILINFO structure to update times
        FILINFO fno;
        fno.fdate = ((buf->modtime >> 9) & 127) | (((buf->modtime >> 5) & 15) << 5) | ((buf->modtime & 31) << 11);
        fno.ftime = ((buf->modtime >> 11) & 31) | (((buf->modtime >> 5) & 63) << 5);
        FRESULT fr = f_utime(filename, &fno);
        if (fr != FR_OK)
        {
            _REENT->_errno = EIO;
            return -1;
        }
    }
    return 0;
}

int _mkdir_r(struct _reent *r, const char *path, mkdir_mode_t mode)
{
    UNUSED(mode);
    if (path == NULL || *path == '\0')
    {
        r->_errno = EINVAL;
        return -1;
    }
    FRESULT fr = f_mkdir(path);
    if (fr != FR_OK)
    {
        r->_errno = EIO;
        return -1;
    }
    return 0;
}

int mkdir(const char *path, mode_t mode)
{
    return _mkdir_r(_REENT, path, mode);
}

int _unlink_r(struct _reent *r, const char *fname)
{
    if (fname == NULL || *fname == '\0')
    {
        r->_errno = EINVAL;
        return -1;
    }
    FRESULT fr = f_unlink(fname);
    if (fr != FR_OK)
    {
        r->_errno = EIO;
        return -1;
    }
    return 0;
}

int unlink(const char *path)
{
    return _unlink_r(_REENT, path);
}

int rmdir(const char *path)
{
    return _unlink_r(_REENT, path);
}

int _rename_r(struct _reent *r, const char *oldname, const char *newname)
{
    if (oldname == NULL || newname == NULL || *oldname == '\0' || *newname == '\0')
    {
        r->_errno = EINVAL;
        return -1;
    }
    FRESULT fr = f_rename(oldname, newname);
    if (fr != FR_OK)
    {
        r->_errno = EIO;
        return -1;
    }
    return 0;
}

int rename(const char *oldname, const char *newname)
{
    return _rename_r(_REENT, oldname, newname);
}

int fcopy(const char *src, const char *dest)
{
    FILE *srcFile = fopen(src, "rb");
    if (!srcFile)
    {
        return -1;
    }
    FILE *destFile = fopen(dest, "wb");
    if (!destFile)
    {
        fclose(srcFile);
        return -1;
    }
    char buffer[256];
    size_t bytesRead;
    int ret = 0;
    while ((bytesRead = fread(buffer, 1, sizeof(buffer), srcFile)) > 0)
    {
        if (fwrite(buffer, 1, bytesRead, destFile) != bytesRead)
        {
            fclose(srcFile);
            fclose(destFile);
            return -1;
        }
        ret += bytesRead;
    }
    fclose(srcFile);
    fclose(destFile);
    return ret;
}

void *pvPortRealloc(void *pv, size_t xWantedSize)
{
    void *pvNew;

    if (pv == NULL)
        return pvPortMalloc(xWantedSize);

    pvNew = pvPortMalloc(xWantedSize);
    if (pvNew != NULL)
    {
        memcpy(pvNew, pv, xWantedSize);
        vPortFree(pv);
    }

    return pvNew;
}
