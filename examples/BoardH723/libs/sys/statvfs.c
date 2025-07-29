#include "sys/statvfs.h"
#include "ff.h"

int statvfs(const char *path, struct statvfs *buf) {
    FATFS *fs;
    DWORD free_clusters;
    
    // Получаем информацию о файловой системе через FatFS
    if (f_getfree(path, &free_clusters, &fs) != FR_OK) {
        return -1;
    }
    
    // Заполняем структуру statvfs
    buf->f_bsize = 512; // Обычный размер сектора
    buf->f_frsize = fs->csize * 512; // Размер кластера
    buf->f_blocks = (uint64_t)(fs->n_fatent - 2); // Общее количество кластеров
    buf->f_bfree = (uint64_t)free_clusters; // Свободные кластеры
    buf->f_bavail = (uint64_t)free_clusters; // То же самое для непривилегированных
    buf->f_files = 0; // FatFS не отслеживает количество файлов
    buf->f_ffree = 0;
    buf->f_favail = 0;
    buf->f_fsid = 0;
    // buf->f_flag = (fs->flag & FA_READ) ? ST_RDONLY : 0; // Закомментировано из-за отсутствия поля
    buf->f_namemax = 255; // Максимальная длина имени в FatFS
    
    return 0;
}