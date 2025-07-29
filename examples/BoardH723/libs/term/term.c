#include "term.h"
#include <string.h>
#include <stdio.h>

termr_t term_init(term_t *trm, term_output_fn out_fn, term_read_fn read_fn, term_exec_fn exec_fn)
{
    if (!trm || !out_fn)
        return termERRPAR;
    memset(trm, 0, sizeof(term_t));
    // Инициализация структуры терминала
    trm->output = out_fn;
    trm->read = read_fn;
    trm->exec = exec_fn;
    trm->get_compl = NULL;
    trm->sigint = NULL;
    trm->cmdlen = 0;
    trm->cursor = 0;
    trm->escape = 0;
    trm->esc_code = term_ESC_NULL;
    trm->echo = term_ECHO_ON;
    trm->echo_off_pos = -1;
    trm->last_endl = 0;
    trm->userdata_ptr = NULL;
    trm->hist_pos = 0;

    // Очистка буфера командной строки
    memset(trm->cmdline_str, 0, sizeof(trm->cmdline_str));

    // Инициализация кольцевого буфера истории
    trm->ring_hist.head = 0;
    trm->ring_hist.tail = 0;
    trm->ring_hist.count = 0;
    memset(trm->ring_hist.entries, 0, sizeof(trm->ring_hist.entries));

    return termOK;
}

termr_t term_set_execute_callback(term_t *trm, term_exec_fn exec_fn)
{
    if (!trm)
        return termERRPAR;
    trm->exec = exec_fn;
    return termOK;
}

termr_t term_set_complete_callback(term_t *trm, term_get_compl_fn get_completion_fn)
{
    if (!trm)
        return termERRPAR;
    trm->get_compl = get_completion_fn;
    return termOK;
}

termr_t term_set_sigint_callback(term_t *trm, term_sigint_fn sigint_fn)
{
    if (!trm)
        return termERRPAR;
    trm->sigint = sigint_fn;
    return termOK;
}

termr_t term_set_prompt(term_t *trm, const char *username, const char *hostname)
{
    if (!trm)
        return termERRPAR;
    int len = 0;
    memcpy(trm->prompt, TERM_COLOR_GREEN, strlen(TERM_COLOR_GREEN));
    len += strlen(TERM_COLOR_GREEN);
    memcpy(trm->prompt + len, username, strlen(username));
    len += strlen(username);
    memcpy(trm->prompt + len, "@", 1);
    len += 1;
    memcpy(trm->prompt + len, hostname, strlen(hostname));
    len += strlen(hostname);
    memcpy(trm->prompt + len, ":", 1);
    len += 1;
    memcpy(trm->prompt + len, TERM_COLOR_DEFAULT, strlen(TERM_COLOR_DEFAULT));
    len += strlen(TERM_COLOR_DEFAULT);
    trm->prompt[len] = '\0';
    return termOK;
}

termr_t term_set_echo(term_t *trm, term_echo_t echo)
{
    if (!trm)
        return termERRPAR;
    trm->echo = echo;
    return termOK;
}

void term_print_prompt(term_t *trm)
{
    if (trm->output)
    {
        trm->output(trm, trm->prompt);
        trm->output(trm, trm->cwd);
        trm->output(trm, "# ");
    }
}

static void term_clear_line(term_t *trm)
{
    if (trm->output)
    {
        trm->output(trm, "\r\033[K"); // Очистка текущей строки
    }
}

static void term_handle_backspace(term_t *trm)
{
    if (trm->cursor > 0 && trm->cursor <= trm->cmdlen) // Добавлена проверка курсора
    {
        // Определяем длину предыдущего символа (UTF-8)
        int utf8_len = 1;
        while (trm->cursor - utf8_len > 0 && (trm->cmdline_str[trm->cursor - utf8_len] & 0xC0) == 0x80)
        {
            utf8_len++;
        }

        // Удаляем символ
        memmove(&trm->cmdline_str[trm->cursor - utf8_len],
                &trm->cmdline_str[trm->cursor],
                trm->cmdlen - trm->cursor);
        trm->cursor -= utf8_len;
        trm->cmdlen -= utf8_len;
        trm->cmdline_str[trm->cmdlen] = '\0'; // Явное завершение строки

        if (trm->echo == term_ECHO_ON)
        {
            trm->output(trm, "\b");
            trm->output(trm, "\033[s\033[?25l"); // Сохраняем позицию курсора
            trm->output(trm, &trm->cmdline_str[trm->cursor]);
            trm->output(trm, " ");
            trm->output(trm, "\033[u\033[?25h"); // Восстанавливаем позицию курсора
        }
    }
}

static void term_handle_delete(term_t *trm)
{
    if (trm->cursor < trm->cmdlen && trm->cmdlen > 0) 
    {
        // Определяем длину следующего символа (UTF-8)
        int utf8_len = 1;
        while (trm->cursor + utf8_len < trm->cmdlen && (trm->cmdline_str[trm->cursor + utf8_len] & 0xC0) == 0x80)
        {
            utf8_len++;
        }

        memmove(&trm->cmdline_str[trm->cursor],
                &trm->cmdline_str[trm->cursor + utf8_len],
                trm->cmdlen - (trm->cursor + utf8_len));
        trm->cmdlen -= utf8_len;
        trm->cmdline_str[trm->cmdlen] = '\0'; // Явное завершение строки

        if (trm->echo == term_ECHO_ON)
        {
            trm->output(trm, "\033[s\033[?25l"); // Сохраняем позицию курсора
            trm->output(trm, &trm->cmdline_str[trm->cursor]);
            trm->output(trm, " ");
            trm->output(trm, "\033[u\033[?25h"); // Восстанавливаем позицию курсора
        }
    }
}

static void term_move_cursor_left(term_t *trm)
{
    if (trm->cursor > 0)
    {
        // Определяем длину предыдущего символа (UTF-8)
        int utf8_len = 1;
        while (trm->cursor - utf8_len > 0 && (trm->cmdline_str[trm->cursor - utf8_len] & 0xC0) == 0x80)
        {
            utf8_len++;
        }

        trm->cursor -= utf8_len; // Перемещаем курсор влево
        if (trm->echo == term_ECHO_ON)
        {
            trm->output(trm, "\033[D"); // Двигаем курсор влево на терминале
        }
    }
}

static void term_move_cursor_right(term_t *trm)
{
    if (trm->cursor < trm->cmdlen)
    {
        // Определяем длину следующего символа (UTF-8)
        int utf8_len = 1;
        while (trm->cursor + utf8_len < trm->cmdlen && (trm->cmdline_str[trm->cursor + utf8_len] & 0xC0) == 0x80)
        {
            utf8_len++;
        }

        trm->cursor += utf8_len; // Перемещаем курсор вправо
        if (trm->echo == term_ECHO_ON)
        {
            trm->output(trm, "\033[C"); // Двигаем курсор вправо на терминале
        }
    }
}

static void term_add_to_history(term_t *trm)
{
    if (trm->cmdlen == 0)
        return;

    // Перемещаем tail при переполнении
    if (trm->ring_hist.count >= TERM_CFG_RING_HISTORY_LEN)
    {
        trm->ring_hist.tail = (trm->ring_hist.tail + 1) % TERM_CFG_RING_HISTORY_LEN;
    }
    else
    {
        trm->ring_hist.count++;
        trm->hist_pos = trm->ring_hist.count;
    }

    // Сохраняем команду в кольцевом буфере
    memcpy(trm->ring_hist.entries[trm->ring_hist.head].cmdline, trm->cmdline_str, trm->cmdlen + 1);
    trm->ring_hist.entries[trm->ring_hist.head].len = trm->cmdlen;
    trm->ring_hist.head = (trm->ring_hist.head + 1) % TERM_CFG_RING_HISTORY_LEN;
}

static void term_restore_from_history(term_t *trm, int direction)
{
    if (trm->ring_hist.count == 0)
        return;

    trm->hist_pos += direction;
    if (trm->hist_pos < 0)
        trm->hist_pos = 0;
    if (trm->hist_pos >= trm->ring_hist.count)
    {
        trm->hist_pos = trm->ring_hist.count;
        trm->cmdline_str[0] = '\0';
        term_clear_line(trm);
        term_print_prompt(trm);
        return;
    }

    int index = (trm->ring_hist.tail + trm->hist_pos) % TERM_CFG_RING_HISTORY_LEN;
    const term_hist_entry_t *entry = &trm->ring_hist.entries[index];

    memcpy(trm->cmdline_str, entry->cmdline, entry->len + 1);
    trm->cmdlen = entry->len;
    trm->cursor = entry->len;

    if (trm->echo == term_ECHO_ON)
    {
        term_clear_line(trm);
        term_print_prompt(trm);
        trm->output(trm, trm->cmdline_str);
    }
}

static void term_handle_tab_completion(term_t *trm)
{
    if (!trm->get_compl)
        return;

    // Разбираем текущую строку на аргументы
    char *argv[TERM_CFG_CMDLINE_LEN / 2];
    int argc = 0;
    char *token = strtok(trm->cmdline_str, " ");

    while (token && argc < TERM_CFG_CMDLINE_LEN / 2)
    {
        argv[argc++] = token;
        token = strtok(NULL, " ");
    }

    // Получаем варианты автодополнения
    char **completions = trm->get_compl(trm, argc, (const char *const *)argv);
    if (completions)
    {
 // Обработка вариантов автодополнения
        int i = 0;
        while (completions[i] != NULL) i++; // Считаем количество вариантов

        if (i == 1) // Если только один вариант
        {
            // Дополняем строку командной строки
            int cur = trm->cursor;
            int len = strlen(completions[0]) - trm->cursor;
            if (trm->cmdlen + len < TERM_CFG_CMDLINE_LEN)
            {
                strcat(trm->cmdline_str, &completions[0][cur]);
                trm->cmdlen += len;
                trm->cursor += len;
                trm->cmdline_str[trm->cmdlen] = '\0'; // Явное завершение строки

                if (trm->echo == term_ECHO_ON)
                {
                    trm->output(trm, &completions[0][cur]); // Выводим дополнение
                }
            }
        }
        else if (i > 1) // Если несколько вариантов
        {
            trm->output(trm, "\r\n");
            // Выводим список вариантов
            for (int j = 0; j < i; j++)
            {
                trm->output(trm, "\t");
                trm->output(trm, completions[j]);
                trm->output(trm, "\r\n"); // Переход на новую строку
            }
            term_print_prompt(trm); // Печатаем подсказку
            trm->output(trm, trm->cmdline_str); // Выводим текущую строку
        }
    }
}

static void term_execute_command(term_t *trm)
{
    if (!trm->exec || trm->cmdlen == 0)
        return;

    char *argv[TERM_CFG_ARG_COUNT];
    int argc = 0;
    char *str = trm->cmdline_str;
    char quote = 0;
    char *token_start = NULL;
    char *write_pos = str;

    // Пропускаем начальные пробелы
    int32_t i = 0;
    while (i < trm->cmdlen && str[i] == ' ')
    {
        i++;
    }
    token_start = &str[i];

    // Обработка командной строки
    for (; i <= trm->cmdlen; i++)
    {
        char c = str[i];

        // Обработка экранирования
        if (c == '\\' && i + 1 <= trm->cmdlen)
        {
            char next = str[i + 1];
            if (next == '"' || next == '\'')
            {
                *write_pos++ = next;
                i++;
                continue;
            }
        }

        // Обработка кавычек
        if ((c == '"' || c == '\''))
        {
            if (quote == 0)
            {
                quote = c;
                continue;
            }
            else if (c == quote)
            {
                quote = 0;
                continue;
            }
        }

        // Обработка разделителей (пробелов)
        if ((c == ' ' || c == '\0') && quote == 0)
        {
            if (write_pos > token_start)
            {
                *write_pos = '\0';
                if (argc < TERM_CFG_ARG_COUNT)
                {
                    argv[argc++] = token_start;
                }
            }

            if (c == ' ')
            {
                write_pos++;
                // Пропускаем последующие пробелы
                while (i + 1 < trm->cmdlen && str[i + 1] == ' ')
                {
                    i++;
                }
                token_start = write_pos;
            }
        }
        else
        {
            *write_pos++ = c;
        }
    }

    // Проверка на незакрытые кавычки
    if (quote != 0)
    {
        // Если кавычки не закрыты, отменяем выполнение команды
        return;
    }

    // Вызываем обработчик команды
    if (argc > 0)
    {
        trm->exec(trm, argc, (const char *const *)argv);
    }
}

termr_t term_processing_input(term_t *trm, const void *data_ptr, size_t len)
{
    if (!trm || !data_ptr || !len)
        return termERRPAR;

    const uint8_t *data = (const uint8_t *)data_ptr;

    for (size_t i = 0; i < len; i++)
    {
        uint8_t c = data[i];

        // Обработка escape-последовательностей
        if (trm->escape)
        {
            if (trm->esc_code == term_ESC_BRACKET)
            {
                switch (c)
                {
                case 'A': // Стрелка вверх (история)
                    term_restore_from_history(trm, -1);
                    break;
                case 'B': // Стрелка вниз (история)
                    term_restore_from_history(trm, +1);
                    break;
                // Для стрелки влево:
                case 'D':
                    term_move_cursor_left(trm);
                    break;
                // Для стрелки вправо:
                case 'C':
                    term_move_cursor_right(trm);
                    break;
                case '3': // Delete key
                    trm->esc_code = term_ESC_DEL;
                    continue;
                case 'H': // Home
                    trm->cursor = 0;
                    if (trm->echo == term_ECHO_ON)
                        trm->output(trm, "\r");
                    break;
                case 'F': // End
                    trm->cursor = trm->cmdlen;
                    if (trm->echo == term_ECHO_ON)
                    {
                        term_clear_line(trm);
                        term_print_prompt(trm);
                        trm->output(trm, trm->cmdline_str);
                    }
                    break;
                }
                trm->escape = 0;
            }
            else if (trm->esc_code == term_ESC_NULL)
            {
                if (c == '[') // Начало escape-последовательности
                {
                    trm->escape = 1;
                    trm->esc_code = term_ESC_BRACKET;
                }
            }
            else if (trm->esc_code == term_ESC_DEL)
            {
                if (c == '~')
                {
                    term_handle_delete(trm);
                }
                trm->escape = 0;
            }
            continue;
        }

        // Начало escape-последовательности
        if (c == '\033')
        {
            trm->escape = 1;
            trm->esc_code = term_ESC_NULL;
            continue;
        }

        // Обработка обычных символов и управляющих клавиш
        switch (c)
        {
        case '\r':
        case '\n':
            if (c == trm->last_endl)
            {
                trm->last_endl = 0;
                // continue;
            }
            trm->last_endl = c;

            if (trm->echo == term_ECHO_ON)
                trm->output(trm, "\r\n");

            if (trm->cmdlen > 0)
            {
                term_add_to_history(trm);
                trm->cmdline_str[trm->cmdlen] = '\0';
                if (trm->newline)
                    trm->newline(trm, trm->cmdline_str);
                if (trm->exec)
                {
                    term_execute_command(trm);
                }
            }

            trm->cmdlen = 0;
            trm->cursor = 0;
            trm->cmdline_str[0] = '\0';
            term_print_prompt(trm);
            break;

        case '\b':
        case 127: // Backspace
            term_handle_backspace(trm);
            break;

        case '\t': // Tab (автодополнение)
            if (trm->get_compl)
            {
                term_handle_tab_completion(trm);
            }
            break;

        case 3: // Ctrl+C
            if (trm->sigint)
            {
                trm->sigint(trm);
            }
            trm->cmdlen = 0;
            trm->cursor = 0;
            trm->cmdline_str[0] = '\0';
            if (trm->echo == term_ECHO_ON)
                trm->output(trm, "^C\r\n");
            term_print_prompt(trm);
            break;

        default:
            // Изменяем проверку для поддержки UTF-8
            if ((c >= 32 && c < 127) ||  // ASCII печатаемые символы
                (c >= 0xC0 && c < 0xF5)) // Начало UTF-8 последовательности
            {
                if (trm->cmdlen < TERM_CFG_CMDLINE_LEN)
                {
                    if (c >= 0xC0) // UTF-8 символ
                    {
                        // Определяем длину UTF-8 последовательности
                        int utf8_len = 0;
                        if ((c & 0xE0) == 0xC0)
                            utf8_len = 2; // 2 байта
                        else if ((c & 0xF0) == 0xE0)
                            utf8_len = 3; // 3 байта
                        else if ((c & 0xF8) == 0xF0)
                            utf8_len = 4; // 4 байта

                        // Проверяем, достаточно ли байт в входном буфере
                        if (i + utf8_len <= len)
                        {
                            // Проверяем, что все последующие байты валидные (начинаются с 10)
                            int valid = 1;
                            for (int j = 1; j < utf8_len; j++)
                            {
                                if ((data[i + j] & 0xC0) != 0x80)
                                {
                                    valid = 0;
                                    break;
                                }
                            }

                            if (valid && trm->cmdlen + utf8_len <= TERM_CFG_CMDLINE_LEN)
                            {
                                // Сдвигаем существующий текст если курсор не в конце
                                if (trm->cursor > trm->cmdlen)
                                    trm->cursor = trm->cmdlen;

                                // Сдвигаем текст только если курсор внутри строки
                                if (trm->cursor < trm->cmdlen)
                                {
                                    memmove(&trm->cmdline_str[trm->cursor + utf8_len],
                                            &trm->cmdline_str[trm->cursor],
                                            trm->cmdlen - trm->cursor);
                                }

                                // Копируем все байты UTF-8 последовательности
                                for (int j = 0; j < utf8_len; j++)
                                {
                                    trm->cmdline_str[trm->cursor + j] = data[i + j];
                                }
                                trm->cursor += utf8_len;
                                trm->cmdlen += utf8_len;
                                trm->cmdline_str[trm->cmdlen] = '\0';

                                if (trm->echo == term_ECHO_ON)
                                {
                                    // Выводим UTF-8 символ
                                    char utf8_char[5] = {0};
                                    memcpy(utf8_char, &data[i], utf8_len);
                                    trm->output(trm, utf8_char);

                                    // Выводим оставшуюся часть строки
                                    if (trm->cursor < trm->cmdlen)
                                    {
                                        trm->output(trm, "\033[s\033[?25l"); // Сохраняем позицию курсора
                                        trm->output(trm, &trm->cmdline_str[trm->cursor]);
                                        trm->output(trm, "\033[u\033[?25h"); // Восстанавливаем позицию курсора
                                    }
                                }

                                // Пропускаем дополнительные байты UTF-8 последовательности
                                i += utf8_len - 1;
                            }
                        }
                    }
                    else // ASCII символ
                    {
                        if (trm->cursor < trm->cmdlen)
                        {
                            memmove(&trm->cmdline_str[trm->cursor + 1],
                                    &trm->cmdline_str[trm->cursor],
                                    trm->cmdlen - trm->cursor);
                        }
                        trm->cmdline_str[trm->cursor] = c;
                        trm->cursor++;
                        trm->cmdlen++;
                        trm->cmdline_str[trm->cmdlen] = '\0';

                        if (trm->echo == term_ECHO_ON)
                        {
                            char ch[2] = {c, '\0'};
                            trm->output(trm, ch);
                            if (trm->cursor < trm->cmdlen)
                            {
                                trm->output(trm, "\033[s\033[?25l"); // Сохраняем позицию курсора
                                trm->output(trm, &trm->cmdline_str[trm->cursor]);
                                trm->output(trm, "\033[u\033[?25h"); // Восстанавливаем позицию курсора
                            }
                        }
                    }
                }
            }
            break;
        }
    }

    return termOK;
}

uint32_t term_get_version(void)
{
    return 0x010000; // Версия 1.0.0
}

termr_t term_set_cwd(term_t *trm, const char *cwd)
{
    if (!trm)
        return termERRPAR;

    int len = strlen(cwd);
    memcpy(trm->cwd, cwd, len);
    trm->cwd[len++] = '\0';
    return termOK;
}

char* term_get_full_path(term_t *trm, const char *path)
{
    if (path[0] == '/') {
        strncpy(trm->fullPathBuffer, path, sizeof(trm->fullPathBuffer) - 1);
    } else {
        snprintf(trm->fullPathBuffer, sizeof(trm->fullPathBuffer), "%s/%s", trm->cwd, path);
    }
    return trm->fullPathBuffer;
}

