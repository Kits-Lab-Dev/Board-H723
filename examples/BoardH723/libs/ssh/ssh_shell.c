#include "ssh_shell.h"
#include "term.h"
#include "app.h"
#include "wolfssh/ssh.h"
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <dirent.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/socket.h>
#include "disks.h"
#include "sddisk.h"
#include "xNetworkInterface.h"

#ifdef WITH_LUA
#include "luaengine.h"
#endif

static const char *keyworld[] = {
#ifdef WITH_LUA
    "lua",
#endif
    "ls",
    "reboot",
    "exit",
    "help",
    "cd",
    "pwd",
    "mkdir",
    "rm",
    "cp",
    "cat",
    "echo",
    "uname",
    "touch",
    "top",
    "initsddisk",
    "ip"};
#define _NUM_OF_CMD sizeof(keyworld) / sizeof(keyworld[0])

static char *compl_world[_NUM_OF_CMD + 1];
static int ChildRunning = 0;

#ifdef WITH_LUA

#endif

static int term_write(term_t *trm, const char *str)
{
    int len = strlen(str);
    if (len == 0 || len > TERM_CFG_CMDLINE_LEN)
        return -1;

    WOLFSSH *ssh = ((thread_ctx_t *)trm->userdata_ptr)->ssh;
    word32 cn;

    if (wolfSSH_GetLastRxId(ssh, &cn) != WS_SUCCESS)
    {
        WLOG(WS_LOG_ERROR, "Failed to get last received channel ID");
        return -1;
    }

    if (wolfSSH_ChannelIdSend(ssh, cn, (byte *)str, len) <= 0)
    {
        WLOG(WS_LOG_ERROR, "Failed to send data");
        return -1;
    }
    return 0;
}

static int term_read(term_t *trm, char *c, size_t n)
{
    WOLFSSH *ssh = ((thread_ctx_t *)trm->userdata_ptr)->ssh;
    word32 cn;
    if (n == 0 && c == NULL)
    {
        int sock = wolfSSH_get_fd(ssh);
        if (sock == (int)INVALID_SOCKET)
        {
            return -1;
        }
        char b[4];
        return recv(sock, b, 4, FREERTOS_MSG_DONTWAIT | FREERTOS_MSG_PEEK);
    }

    if (wolfSSH_GetLastRxId(ssh, &cn) != WS_SUCCESS)
    {
        WLOG(WS_LOG_ERROR, "Failed to get last received channel ID");
        return -1;
    }
    int cnt_r = wolfSSH_worker(ssh, &cn);
    if (cnt_r < 0)
    {
        int rc = wolfSSH_get_error(ssh);
        if (rc == WS_CHAN_RXD)
        {
            return wolfSSH_ChannelIdRead(ssh, cn, (byte *)c, (word32)n);
        }
        else if (rc == WS_CHANNEL_CLOSED)
        {
            return -1;
        }
        else if (rc != WS_WANT_READ)
        {
            return -1;
        }
    }
}

static int newline(term_t *trm, const char *line)
{
    UNUSED(trm);

    return 0;
}

static void draw_progress_bar(term_t *trm, float percent, int width)
{
    char bar[width + 3];
    int filled = (int)((percent / 100.0f) * width);
    bar[0] = '[';
    for (int i = 0; i < width; i++)
    {
        if (i < filled)
        {
            bar[i + 1] = '#'; // '█'
        }
        else
        {
            bar[i + 1] = '.'; // '░'
        }
    }
    bar[width - 1] = ']';
    bar[width] = '\0';
    trm->output(trm, bar);
}

static int execute(term_t *trm, int argc, const char *const *argv)
{
    if (argc == 0)
        return 0;
#ifdef WITH_LUA
    if (strcmp(argv[0], "lua") == 0)
    {
        if (argc < 2)
        {
            trm->output(trm, "Usage: lua <script> or lua -f <script.lua>\r\n");
            return -1;
        }
        if (strcmp(argv[1], "-f") == 0)
        {
            if (argc < 3)
            {
                trm->output(trm, "Usage: lua -f <script.lua>\r\n");
                return -1;
            }
            lua_State *L = luaInit();
            LuaContextData *ctx = luaGetUD(L);
            ctx->trm = trm;
            int ret = luaRunFile(L, argv[2]) ? 0 : -1;
            luaDeinit(L);
            return ret;
        }
        else
        {
            lua_State *L = luaInit();
            LuaContextData *ctx = luaGetUD(L);
            ctx->trm = trm;
            int ret = luaCall(L, argv[1]) ? 0 : -1;
            luaDeinit(L);
            return ret;
        }
        return -1;
    }
#endif
    // Команда ls - вывод содержимого директории
    if (strcmp(argv[0], "ls") == 0)
    {
        DIR *dir;
        struct dirent *entry;
        const char *path = trm->cwd;
        int longFormat = 0;
        for (int i = 1; i < argc; i++)
        {
            if (strcmp(argv[i], "-l") == 0)
                longFormat = 1;
            else
                path = argv[i];
        }
        /* Относительный путь — от текущего каталога; "/SD" и "/SD/" — одно и то же */
        char dirpath[256];
        if (path[0] != '/')
        {
            size_t clen = strlen(trm->cwd);
            snprintf(dirpath, sizeof(dirpath), "%s%s%s", trm->cwd,
                     clen && trm->cwd[clen - 1] == '/' ? "" : "/", path);
            path = dirpath;
        }
        size_t plen = strlen(path);
        if (plen > 1 && path[plen - 1] != '/' && plen + 1 < sizeof(dirpath))
        {
            if (path != dirpath)
                snprintf(dirpath, sizeof(dirpath), "%s/", path);
            else
                strcat(dirpath, "/");
            path = dirpath;
        }

        dir = opendir(path);
        if (dir)
        {
            while ((entry = readdir(dir)) != NULL)
            {
                struct stat st;
                char fullpath[512];
                snprintf(fullpath, sizeof(fullpath), "%s%s%s", path,
                         path[strlen(path) - 1] == '/' ? "" : "/", entry->d_name);

                if (stat(fullpath, &st) == 0)
                {
                    if (longFormat)
                    {
                        char line[48];
                        snprintf(line, sizeof(line), "%c %10lu  ", S_ISDIR(st.st_mode) ? 'd' : '-',
                                 (unsigned long)st.st_size);
                        trm->output(trm, line);
                        trm->output(trm, entry->d_name);
                        trm->output(trm, S_ISDIR(st.st_mode) ? "/\r\n" : "\r\n");
                    }
                    else if (S_ISDIR(st.st_mode))
                    {
                        trm->output(trm, "\033[1;34m"); // Синий для директорий
                        trm->output(trm, entry->d_name);
                        trm->output(trm, "/\033[0m  ");
                    }
                    else if (st.st_mode & S_IXUSR)
                    {
                        trm->output(trm, "\033[1;32m"); // Зеленый для исполняемых
                        trm->output(trm, entry->d_name);
                        trm->output(trm, "*\033[0m  ");
                    }
                    else
                    {
                        trm->output(trm, entry->d_name);
                        trm->output(trm, "  ");
                    }
                }
            }
            closedir(dir);
            trm->output(trm, "\r\n");
        }
        else
        {
            trm->output(trm, "Error opening directory: ");
            trm->output(trm, strerror(errno));
            trm->output(trm, "\r\n");
        }
        return 0;
    }

    // Команда cd - смена текущей директории
    if (strcmp(argv[0], "cd") == 0)
    {
        if (argc < 2)
        {
            // Без аргументов переходим в домашнюю директорию
            term_set_cwd(trm, "/");
        }
        const char *d = argv[1];
        if (strcmp(argv[1], "..") == 0)
        {
            int l = strlen(trm->cwd);
            for (int i = l; i >= 0; i--)
            {
                if (trm->cwd[i] == '/')
                {
                    trm->cwd[i] = '\0';
                    DIR *dir = opendir(trm->cwd);
                    if (dir)
                    {
                        closedir(dir);
                        return 0;
                    }
                }
            }
            trm->output(trm, "Failed to change directory\r\n");
            return -1;
        }
        if (d[0] == '/')
        {
            DIR *dir = opendir(d);
            if (dir)
            {
                term_set_cwd(trm, argv[1]);
                closedir(dir);
                return 0;
            }
        }
        else
        {
            int l = strlen(trm->cwd);
            trm->cwd[l] = '/';
            strcpy(&trm->cwd[l + 1], d);
            DIR *dir = opendir(trm->cwd);
            if (dir)
            {
                closedir(dir);
                return 0;
            }
            else
                trm->cwd[l] = '\0';
        }

        trm->output(trm, "Failed to change directory\r\n");
        return -1;
    }

    // Команда mkdir - создание директории
    if (strcmp(argv[0], "mkdir") == 0)
    {
        if (argc < 2)
        {
            trm->output(trm, "Usage: mkdir <directory>\r\n");
            return 1;
        }
        const char *fullpath = term_get_full_path(trm, argv[1]);
        if (mkdir(fullpath, 0755) != 0)
        {
            trm->output(trm, "Failed to create directory: ");
            trm->output(trm, strerror(errno));
            trm->output(trm, "\r\n");
            return 1;
        }
        return 0;
    }

    // Команда rm - удаление файла или директории
    if (strcmp(argv[0], "rm") == 0)
    {
        if (argc < 2)
        {
            trm->output(trm, "Usage: rm <file/directory>\r\n");
            return 1;
        }
        const char *fullpath = term_get_full_path(trm, argv[1]);
        if (unlink(fullpath) != 0)
        {
            trm->output(trm, "Failed to remove: ");
            trm->output(trm, strerror(errno));
            trm->output(trm, "\r\n");
            return 1;
        }

        return 0;
    }

    // Команда cat - вывод содержимого файла
    if (strcmp(argv[0], "cat") == 0)
    {
        if (argc < 2)
        {
            trm->output(trm, "Usage: cat <file>\r\n");
            return 1;
        }

        const char *fullpath = term_get_full_path(trm, argv[1]);
        FILE *fp = fopen(fullpath, "r");
        if (!fp)
        {
            trm->output(trm, "Failed to open file: ");
            trm->output(trm, strerror(errno));
            trm->output(trm, "\r\n");
            return 1;
        }

        char buf[1024];
        while (fgets(buf, sizeof(buf), fp) != NULL)
        {
            trm->output(trm, buf);
        }
        fclose(fp);
        return 0;
    }

    // Команда touch - создание/обновление файла
    if (strcmp(argv[0], "touch") == 0)
    {
        if (argc < 2)
        {
            trm->output(trm, "Usage: touch <file>\r\n");
            return 1;
        }
        const char *fullpath = term_get_full_path(trm, argv[1]);
        int fd = open(fullpath, O_CREAT | O_WRONLY, 0644);
        if (fd < 0)
        {
            trm->output(trm, "Failed to touch file: ");
            trm->output(trm, strerror(errno));
            trm->output(trm, "\r\n");
            return 1;
        }
        close(fd);
        return 0;
    }

    // Команда pwd - вывод текущей директории
    if (strcmp(argv[0], "pwd") == 0)
    {
        trm->output(trm, trm->cwd);
        trm->output(trm, "\r\n");
        return 0;
    }

    // Команда cp - копирование файла
    if (strcmp(argv[0], "cp") == 0)
    {
        if (argc < 3)
        {
            trm->output(trm, "Usage: cp <source> <destination>\r\n");
            return 1;
        }
        const char *fullpath = term_get_full_path(trm, argv[1]);
        FILE *src = fopen(fullpath, "rb");
        if (!src)
        {
            trm->output(trm, "Failed to open source file: ");
            trm->output(trm, strerror(errno));
            trm->output(trm, "\r\n");
            return 1;
        }
        fullpath = term_get_full_path(trm, argv[2]);
        FILE *dst = fopen(fullpath, "wb");
        if (!dst)
        {
            fclose(src);
            trm->output(trm, "Failed to create destination file: ");
            trm->output(trm, strerror(errno));
            trm->output(trm, "\r\n");
            return 1;
        }

        char buf[256];
        size_t n;
        while ((n = fread(buf, 1, sizeof(buf), src)) > 0)
        {
            if (fwrite(buf, 1, n, dst) != n)
            {
                trm->output(trm, "Error writing to destination file\r\n");
                break;
            }
        }

        fclose(src);
        fclose(dst);
        return 0;
    }

    // Команда reboot - перезагрузка системы
    if (strcmp(argv[0], "reboot") == 0)
    {
        if (argc > 1 && strcmp(argv[1], "dfu") == 0)
        {
            trm->output(trm, "Rebooting to USB DFU bootloader\r\n");
            vTaskDelay(100);
            rebootToDFU();
        }
        if (argc > 1 && strcmp(argv[1], "esploader") == 0)
        {
            trm->output(trm, "Starting ESP-Loader from /SD/utils/ESPLoader.bin\r\n");
            appStartEspLoader();
            return 0;
        }
        NVIC_SystemReset();
        return 0;
    }

    // Команда help - вывод справки
    if (strcmp(argv[0], "help") == 0)
    {
        trm->output(trm, "Available commands:\r\n");
#ifdef WITH_LUA
        trm->output(trm, "  lua <script> or\r\n      -f <script.lua>   - Run Lua script\r\n");
#endif
        trm->output(trm, "  ls [-l] [dir]         - List directory contents\r\n");
        trm->output(trm, "  cd <dir>              - Change current directory\r\n");
        trm->output(trm, "  pwd                   - Print working directory\r\n");
        trm->output(trm, "  mkdir <dir>           - Create directory\r\n");
        trm->output(trm, "  rm [-r] <file>        - Remove file or directory\r\n");
        trm->output(trm, "  cat <file>            - Display file contents\r\n");
        trm->output(trm, "  touch <file>          - Create empty file\r\n");
        trm->output(trm, "  cp <src> <dst>        - Copy file\r\n");
        trm->output(trm, "  echo [text]           - Display text\r\n");
        trm->output(trm, "  uname [-a]            - Print system information\r\n");
        trm->output(trm, "  top                   - Display system resources usage\r\n");
        trm->output(trm, "  initsddisk            - Initialize SD card\r\n");
        trm->output(trm, "  ip                    - Display IP address\r\n");
        trm->output(trm, "  reboot [dfu|esploader] - Restart system [into USB DFU / ESP-Loader]\r\n");
        trm->output(trm, "  help                  - Show this help\r\n");
        return 0;
    }

    // Команда echo - вывод текста
    if (strcmp(argv[0], "echo") == 0)
    {
        for (int i = 1; i < argc; i++)
        {
            trm->output(trm, argv[i]);
            if (i < argc - 1)
            {
                trm->output(trm, " ");
            }
        }
        trm->output(trm, "\r\n");
        return 0;
    }

    // Команда uname - вывод информации о системе
    if (strcmp(argv[0], "uname") == 0)
    {
        if (argc > 1 && strcmp(argv[1], "-a") == 0)
        {
            trm->output(trm, "MCU SSH Shell on STM32 ARM\r\n");
        }
        else
        {
            trm->output(trm, "MCU\r\n");
        }
        return 0;
    }

    if (strcmp(argv[0], "exit") == 0)
    {
        WOLFSSH *ssh = ((thread_ctx_t *)trm->userdata_ptr)->ssh;
        wolfSSH_stream_exit(ssh, 0);
        return 0;
    }

    if (strcmp(argv[0], "top") == 0)
    {
        char buf[128];
        trm->output(trm, "\033[?25l"); // Скрываем курсор
        do
        {
            float cpu_usage = 100.0f - CPU_IDLE;
            uint32_t free_mem = sdramGetFreeMem();
            uint32_t total_mem = SDRAM_BANK1_SIZE;
            uint32_t used_mem = total_mem - free_mem;
            float mem_usage_percent = (float)used_mem * 100.0f / total_mem;

            HeapStats_t heap;
            vPortGetHeapStats(&heap);
            uint32_t free_rtos = heap.xAvailableHeapSpaceInBytes;
            float rtos_usage_percent = 100.0f - ((float)free_rtos * 100.0f / configTOTAL_HEAP_SIZE);

            trm->output(trm, "CPU: \t");
            draw_progress_bar(trm, cpu_usage, 20);
            snprintf(buf, sizeof(buf), " %.1f%%      \r\n", cpu_usage);
            trm->output(trm, buf);

            trm->output(trm, "SDRAM: \t");
            draw_progress_bar(trm, mem_usage_percent, 20);
            snprintf(buf, sizeof(buf), " %.1f%% (%.1f/%.1f) MB     \r\n", mem_usage_percent, (float)used_mem / (1024.0f * 1024.0f), (float)total_mem / (1024.0f * 1024.0f));
            trm->output(trm, buf);

            trm->output(trm, "RTOS: \t");
            draw_progress_bar(trm, rtos_usage_percent, 20);
            snprintf(buf, sizeof(buf), " %.1f%% (%.1f/%.1f) KB      \r\n", rtos_usage_percent, (float)(configTOTAL_HEAP_SIZE - free_rtos) / 1024.0f, (float)(configTOTAL_HEAP_SIZE) / 1024.0f);
            trm->output(trm, buf);

            vTaskDelay(500);

            char c;
            if (trm->read(trm, 0, 0) > 0)
            {
                trm->read(trm, &c, 1);
                if (c == 3) // Ctrl+C
                    break;
            }
            trm->output(trm, "\r\033[3A"); // Перемещаем курсор на 3 строки вверх и в начало строки
        } while (1);

        trm->output(trm, "\033[?25h"); // Показываем курсор обратно
        return 0;
    }

    if (strcmp(argv[0], "initsddisk") == 0)
    {
        trm->output(trm, "Initializing SD card...\r\n(y/n): formatting will erase all data on the SD\r\n");
        do
        {
            char c;
            if (trm->read(trm, 0, 0) > 0)
            {
                trm->read(trm, &c, 1);
                if (c == 'y')
                {
                    trm->output(trm, "\ry\r\n");
                    sdDiskUnmount();
                    MKFS_PARM param;
                    param.fmt = FM_EXFAT;
                    param.align = 0;
                    param.n_fat = 0;
                    param.n_root = 0;
                    param.au_size = 0;
                    trm->output(trm, "Formatting SD card...\r\n");
                    char *work = (char *)pvPortMalloc(1024);
                    FRESULT res = f_mkfs("/SD", &param, work, 1024);
                    vPortFree(work);
                    trm->output(trm, res == FR_OK ? "Formatting complete.\r\n" : "Formatting failed.\r\n");
                    vTaskDelay(500);
                    if (res == FR_OK)
                    {
                        if (sdDiskMount())
                        {
                            trm->output(trm, "SD card mounted successfully.\r\n");
                            if (mkdir("/SD/.ssh", 0755) == 0)
                            {
                                trm->output(trm, "[.ssh] directory created successfully.\r\n");
                            }
                            if (fcopy("/RAM/.ssh/hostkey", "/SD/.ssh/hostkey") > 0)
                            {
                                trm->output(trm, "Host key copied successfully.\r\n");
                            }
                        }
                    }
                    break;
                }
                else
                    break;
            }
            vTaskDelay(500);

        } while (1);
        return 0;
    }

    if (strcmp(argv[0], "ip") == 0)
    {
        uint32_t ulIPAddress, ulNetMask, ulGatewayAddress, ulDNSServerAddress;
        char cBuffer[16];
        char ipStr[16];
        struct xNetworkEndPoint *endPoint = NULL;
        for (int i = 0; i < 3; i++)
        {
            switch (i)
            {
            case 0:
                endPoint = ncmNetInterface.pxEndPoint;
                trm->output(trm, "USB-NCM:\r\n");
                break;
            case 1:
                endPoint = staNetInterface.pxEndPoint;
                trm->output(trm, "WiFi-STA:\r\n");
                break;
            case 2:
                endPoint = apNetInterface.pxEndPoint;
                trm->output(trm, "WiFi-AP:\r\n");
                break;

            default:
                break;
            }
            FreeRTOS_GetEndPointConfiguration(&ulIPAddress, &ulNetMask, &ulGatewayAddress, &ulDNSServerAddress, endPoint);

            FreeRTOS_inet_ntoa(ulIPAddress, ipStr);
            sprintf(cBuffer, "\tIP Address: \t%s\r\n", ipStr);
            trm->output(trm, cBuffer);

            FreeRTOS_inet_ntoa(ulNetMask, ipStr);
            sprintf(cBuffer, "\tSubnet Mask: \t%s\r\n", ipStr);
            trm->output(trm, cBuffer);

            FreeRTOS_inet_ntoa(ulGatewayAddress, ipStr);
            sprintf(cBuffer, "\tGateway: \t%s\r\n", ipStr);
            trm->output(trm, cBuffer);

            FreeRTOS_inet_ntoa(ulDNSServerAddress, ipStr);
            sprintf(cBuffer, "\tDNS Server: \t%s\r\n\r\n", ipStr);
            trm->output(trm, cBuffer);
        }
        return 0;
    }

    // Если команда не найдена
    trm->output(trm, "Unknown command: ");
    trm->output(trm, argv[0]);
    trm->output(trm, "\r\nType 'help' for available commands\r\n");
    return 1;
}

static char **complete(term_t *trm, int argc, const char *const *argv)
{
    UNUSED(trm);
    uint32_t j = 0;
    compl_world[0] = NULL;
    if (argc == 1)
    {
        char *bit = (char *)argv[argc - 1];
        for (uint32_t i = 0; i < _NUM_OF_CMD; i++)
        {
            if (strstr(keyworld[i], bit) == keyworld[i])
            {
                compl_world[j++] = (char *)keyworld[i];
            }
        }
    }
    else
    {
        for (; j < _NUM_OF_CMD; j++)
        {
            compl_world[j] = (char *)keyworld[j];
        }
    }
    compl_world[j] = NULL;
    return compl_world;
}

void sigint(term_t *trm)
{
}

int ShellWorker(thread_ctx_t *threadCtx)
{
    WOLFSSH *ssh;
    int rc = 0;

    const char *userName;

    if (threadCtx == NULL)
        return 1;

    ssh = threadCtx->ssh;
    if (ssh == NULL)
        return WS_FATAL_ERROR;

    userName = wolfSSH_GetUsername(ssh);
    ChildRunning = 1;

    term_t *term = NULL;

    while (ChildRunning)
    {
        int cnt_r;
        word32 lastChannel = 0;

        if (!term)
        {
            term = sdramMalloc(sizeof(term_t));
            if (!term)
            {
                rc = WS_FATAL_ERROR;
                goto exit;
            }
            term_init(term, term_write, term_read, execute);
            term_set_complete_callback(term, complete);
            term_set_sigint_callback(term, sigint);
            term->newline = newline;
            term->userdata_ptr = threadCtx;
            term_set_prompt(term, userName, "mcu");
            term_set_cwd(term, "/");
            term_print_prompt(term);
        }

        cnt_r = wolfSSH_worker(ssh, &lastChannel);
        if (cnt_r < 0)
        {
            rc = wolfSSH_get_error(ssh);
            if (rc == WS_CHAN_RXD)
            {
                cnt_r = wolfSSH_ChannelIdRead(ssh, lastChannel, threadCtx->channelBuffer, sizeof threadCtx->channelBuffer);
                if (cnt_r <= 0)
                    break;

                termr_t ret = term_processing_input(term, threadCtx->channelBuffer, cnt_r);
                if (ret != termOK)
                    break;
            }
            else if (rc == WS_CHANNEL_CLOSED)
            {
                break;
            }
            else if (rc != WS_WANT_READ)
            {
                break;
            }
        }
    }

exit:
#ifdef WITH_LUA

#endif
    if (term)
    {
        sdramFree(term);
    }

    return 0;
}