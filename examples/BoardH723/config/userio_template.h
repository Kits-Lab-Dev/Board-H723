/* userio_template.h — подключается wolfssh/test.h при WOLFSSH_USER_IO.
 * Сокеты дают POSIX-обёртки над FreeRTOS+TCP из libs/sys. */
#ifndef USERIO_TEMPLATE_H
#define USERIO_TEMPLATE_H

#ifdef WOLFSSH_USER_IO
#include "sys/socket.h"
#endif

#endif /* USERIO_TEMPLATE_H */
