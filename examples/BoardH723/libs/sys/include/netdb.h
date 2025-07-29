#ifndef _NETDB_H
#define _NETDB_H

#include "FreeRTOS.h"
#include "FreeRTOS_IP.h"
#include "FreeRTOS_DNS.h"
#include "sys/socket.h"

#define AI_PASSIVE      0x01
#define AI_CANONNAME    0x02
#define AI_NUMERICHOST  0x04
#define AI_NUMERICSERV  0x08
#define AI_V4MAPPED     0x10
#define AI_ALL          0x20
#define AI_ADDRCONFIG   0x40

#define NI_NOFQDN       0x01  /* Only return nodename portion for local hosts */
#define NI_NUMERICHOST  0x02  /* Return numeric form of the host's address */
#define NI_NAMEREQD     0x04  /* Error if the host's name not in DNS */

#define NI_MAXHOST      1025  /* Max size of a fully-qualified domain name */
#define NI_MAXSERV      32    /* Max size of a service name */

#define	NETDB_INTERNAL	-1	/*%< see errno */
#define	NETDB_SUCCESS	0	/*%< no problem */

#define	NO_ADDRESS	NO_DATA		/* no address, look for MX record */
#define	HOST_NOT_FOUND	1 /*%< Authoritative Answer Host not found */
#define	TRY_AGAIN	2 /*%< Non-Authoritive Host not found, or SERVERFAIL */
#define	NO_RECOVERY	3 /*%< Non recoverable errors, FORMERR, REFUSED, NOTIMP */
#define	NO_DATA		4 /*%< Valid name, no data record of requested type */
#define	NO_ADDRESS	NO_DATA		/*%< no address, look for MX record */

#define AI_PASSIVE      0x01
#define AI_CANONNAME    0x02
#define AI_NUMERICHOST  0x04
#define AI_NUMERICSERV  0x08
#define AI_V4MAPPED     0x10
#define AI_ALL          0x20
#define AI_ADDRCONFIG   0x40

#define	EAI_ADDRFAMILY	 1	/*%< address family for hostname not supported */
#define	EAI_AGAIN	 2	/*%< temporary failure in name resolution */
#define	EAI_BADFLAGS	 3	/*%< invalid value for ai_flags */
#define	EAI_FAIL	 4	/*%< non-recoverable failure in name resolution */
#define	EAI_FAMILY	 5	/*%< ai_family not supported */
#define	EAI_MEMORY	 6	/*%< memory allocation failure */
#define	EAI_NODATA	 7	/*%< no address associated with hostname */
#define	EAI_NONAME	 8	/*%< hostname nor servname provided, or not known */
#define	EAI_SERVICE	 9	/*%< servname not supported for ai_socktype */
#define	EAI_SOCKTYPE	10	/*%< ai_socktype not supported */
#define	EAI_SYSTEM	11	/*%< system error returned in errno */
#define	EAI_BADHINTS	12	/* invalid value for hints */
#define	EAI_PROTOCOL	13	/* resolved protocol is unknown */
#define	EAI_OVERFLOW	14	/* argument buffer overflow */
#define	EAI_MAX		15

extern int h_errno;

struct hostent {
    char  *h_name;
    char **h_aliases;
    int    h_addrtype;
    int    h_length;
    char **h_addr_list;
    #define h_addr h_addr_list[0] /* for backward compatibility */
};

#define addrinfo freertos_addrinfo

// struct addrinfo {
//     int               ai_flags;      /* Input flags. */
//     int               ai_family;     /* Address family of socket. */
//     int               ai_socktype;   /* Socket type. */
//     int               ai_protocol;   /* Protocol of socket. */
//     socklen_t         ai_addrlen;    /* Length of socket address. */
//     struct sockaddr  *ai_addr;       /* Socket address of socket. */
//     char             *ai_canonname;  /* Canonical name of service location. */
//     struct addrinfo  *ai_next;       /* Pointer to next in list. */
// };

static inline struct hostent *gethostbyname(const char *name) {
    // Это пример реализации, который может быть заменен на реальную реализацию в зависимости от системы
    static struct hostent h;
    static char *aliases[] = { NULL };
    static char *addr_list[2]; // Массив для адресов

    uint32_t ip_addr = FreeRTOS_gethostbyname(name);
    if (ip_addr == 0) {
        return NULL; // Если адрес не найден, возвращаем NULL
    }

    static uint32_t s_addr; // Статическая переменная для хранения IP-адреса
    s_addr = ip_addr; // Сохраняем IP-адрес

    addr_list[0] = (char *)&s_addr;
    addr_list[1] = NULL;

    h.h_name = (char *)name;
    h.h_aliases = aliases;
    h.h_addrtype = AF_INET;
    h.h_length = sizeof(uint32_t);
    h.h_addr_list = addr_list;

    return &h;
}

static inline struct hostent *gethostbyaddr(const void *addr, size_t len, int type) 
{
    static struct hostent h;
    static char *aliases[] = { NULL };
    static char *addr_list[2]; // Массив для адресов

    if (len != sizeof(uint32_t) || type != AF_INET) {
        return NULL; 
    }

    addr_list[0] = (char *)addr;
    addr_list[1] = NULL;

    h.h_name = "unknown"; 
    h.h_aliases = aliases;
    h.h_addrtype = type;
    h.h_length = len;
    h.h_addr_list = addr_list;

    return &h;
}




#endif