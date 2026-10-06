#ifndef SD_FILE_H
#define SD_FILE_H

#include <stdbool.h>
#include <stdint.h>

// Ждёт SD-карту, дописывает строку в /SD/recipes/log.txt раз в SD_FILE_PERIOD_S секунд,
// при старте один раз меряет скорость записи и чтения
void sd_file_start(void);

// Дописать строку в файл (stdio). Каталог должен существовать.
bool sdFileAppend(const char *path, const char *line);

// Записать и прочитать файл размером sizeKB блоками по 32 КБ (FatFs), скорость в КБ/с
bool sdFileBenchmark(const char *path, uint32_t sizeKB, uint32_t *writeKBps, uint32_t *readKBps);

#endif
