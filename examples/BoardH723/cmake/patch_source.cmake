# patch_source(<input> <output> FIND <text> REPLACE <text> [FIND ... REPLACE ...])
#
# Копирует файл сторонней библиотеки в каталог сборки и делает в копии
# точечные замены. Оригинал в third_party/ не меняется, поэтому submodule
# остаётся чистым. Если строка FIND не найдена (библиотека обновилась),
# конфигурация останавливается с ошибкой — правку нужно пересмотреть.
# Переводы строк в FIND/REPLACE записываются как \n.
function(patch_source input output)
    file(READ "${input}" content)
    # submodule может быть выписан с CRLF (core.autocrlf) — сравниваем с LF
    string(REPLACE "\r\n" "\n" content "${content}")
    # Аргументы читаются через ARGV<n>: в коде на C есть ';', а ARGN
    # разбил бы такие строки на части.
    set(i 2)
    while(i LESS ARGC)
        math(EXPR i_find "${i} + 1")
        math(EXPR i_kw_replace "${i} + 2")
        math(EXPR i_replace "${i} + 3")
        if(NOT i_replace LESS ARGC
           OR NOT "${ARGV${i}}" STREQUAL "FIND"
           OR NOT "${ARGV${i_kw_replace}}" STREQUAL "REPLACE")
            message(FATAL_ERROR "patch_source: ожидается FIND <text> REPLACE <text>")
        endif()
        set(find "${ARGV${i_find}}")
        set(replace "${ARGV${i_replace}}")
        string(FIND "${content}" "${find}" pos)
        if(pos EQUAL -1)
            message(FATAL_ERROR "patch_source: в ${input} не найдено:\n${find}")
        endif()
        string(REPLACE "${find}" "${replace}" content "${content}")
        math(EXPR i "${i} + 4")
    endwhile()
    file(WRITE "${output}.tmp" "${content}")
    file(COPY_FILE "${output}.tmp" "${output}" ONLY_IF_DIFFERENT)
    file(REMOVE "${output}.tmp")
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${input}")
endfunction()
