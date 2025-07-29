#ifndef __UNIX_SOCKET_WRAPPER_H
#define __UNIX_SOCKET_WRAPPER_H

#include "FreeRTOS_Sockets.h"
#include "FreeRTOS_DNS.h"
#include <sys/select.h>
#include <time.h>
#include <netinet/tcp.h>

#include <stdlib.h>
#include <netinet/in.h>
#include <sys/poll.h>

#define WAITFD_R        POLLIN
#define WAITFD_W        POLLOUT
#define WAITFD_C        (POLLIN|POLLOUT)

#define TCP_NODELAY 0x0001

#define SOL_SOCKET 0xffff /* options for socket level */

#define SO_SNDBUF 0x1001     /* send buffer size */
#define SO_RCVBUF 0x1002     /* receive buffer size */
#define SO_SNDLOWAT 0x1003   /* send low-water mark */
#define SO_RCVLOWAT 0x1004   /* receive low-water mark */
#define SO_SNDTIMEO 0x1005   /* send timeout */
#define SO_RCVTIMEO 0x1006   /* receive timeout */
#define SO_ERROR 0x1007      /* get error status and clear */
#define SO_TYPE 0x1008       /* get socket type */
#define SO_OVERFLOWED 0x1009 /* datagrams: return packets dropped */

#define SO_DEBUG 0x0001       /* turn on debugging info recording */
#define SO_ACCEPTCONN 0x0002  /* socket has had listen() */
#define SO_REUSEADDR 0x0004   /* allow local address reuse */
#define SO_KEEPALIVE 0x0008   /* keep connections alive */
#define SO_DONTROUTE 0x0010   /* just use interface addresses */
#define SO_BROADCAST 0x0020   /* permit sending of broadcast msgs */
#define SO_USELOOPBACK 0x0040 /* bypass hardware when possible */
#define SO_LINGER 0x0080      /* linger on close if data present */
#define SO_OOBINLINE 0x0100   /* leave received OOB data in line */
#define SO_REUSEPORT 0x0200   /* allow local address & port reuse */
#define SO_TIMESTAMP 0x0400   /* timestamp received dgram traffic */
// #define SO_BINDTODEVICE 0x0800		/* restrict traffic to an interface */

#define IPV6_OPTIONS 1     /* buf/ip6_opts; set/get IP6 options */
#define IPV6_RECVOPTS 5    /* bool; receive all IP6 opts w/dgram */
#define IPV6_RECVRETOPTS 6 /* bool; receive IP6 opts for response */
#define IPV6_RECVDSTADDR 7 /* bool; receive IP6 dst addr w/dgram */
#define IPV6_RETOPTS 8     /* ip6_opts; set/get IP6 options */

#define IPV6_SOCKOPT_RESERVED1 3 /* reserved for future use */
#define IPV6_UNICAST_HOPS 4      /* int; IP6 hops */
#define IPV6_MULTICAST_IF 9      /* u_int; set/get IP6 multicast i/f  */
#define IPV6_MULTICAST_HOPS 10   /* int; set/get IP6 multicast hops */
#define IPV6_MULTICAST_LOOP 11   /* u_int; set/get IP6 multicast loopback */
#define IPV6_JOIN_GROUP 12       /* ip6_mreq; join a group membership */
#define IPV6_LEAVE_GROUP 13      /* ip6_mreq; leave a group membership */
#define IPV6_PORTRANGE 14        /* int; range to choose for unspec port */

#define SOCKET_ERROR FREERTOS_SOCKET_ERROR
#define INVALID_SOCKET FREERTOS_INVALID_SOCKET

#define SOCK_STREAM FREERTOS_SOCK_STREAM
#define SOCK_DGRAM FREERTOS_SOCK_DGRAM
#define IPPROTO_TCP FREERTOS_IPPROTO_TCP
#define IPPROTO_UDP FREERTOS_IPPROTO_UDP
#define AF_UNSPEC 0
#define PF_UNSPEC AF_UNSPEC
#define AF_INET FREERTOS_AF_INET
#define AF_INET6 FREERTOS_AF_INET6
#define INET_ADDRSTRLEN 22
#define INET6_ADDRSTRLEN 65

#define INADDR_ANY FREERTOS_INADDR_ANY

#include <netdb.h>

#define IPPROTO_IP 0      /* dummy for IP */
#define IPPROTO_HOPOPTS 0 /* IP6 hop-by-hop options */
#define IPPROTO_ICMP 1    /* control message protocol */
#define IPPROTO_IGMP 2    /* group mgmt protocol */
#define IPPROTO_GGP 3     /* gateway^2 (deprecated) */
#define IPPROTO_IPV4 4    /* IP header */
#define IPPROTO_IPIP 4    /* IP inside IP */

#define IPPROTO_EGP 8  /* exterior gateway protocol */
#define IPPROTO_PUP 12 /* pup */

#define IPPROTO_IDP 22       /* xns idp */
#define IPPROTO_TP 29        /* tp-4 w/ class negotiation */
#define IPPROTO_IPV6 41      /* IP6 header */
#define IPPROTO_ROUTING 43   /* IP6 routing header */
#define IPPROTO_FRAGMENT 44  /* IP6 fragmentation header */
#define IPPROTO_RSVP 46      /* resource reservation */
#define IPPROTO_GRE 47       /* GRE encaps RFC 1701 */
#define IPPROTO_ESP 50       /* encap. security payload */
#define IPPROTO_AH 51        /* authentication header */
#define IPPROTO_MOBILE 55    /* IP Mobility RFC 2004 */
#define IPPROTO_IPV6_ICMP 58 /* IPv6 ICMP */
#define IPPROTO_ICMPV6 58    /* ICMP6 */
#define IPPROTO_NONE 59      /* IP6 no next header */
#define IPPROTO_DSTOPTS 60   /* IP6 destination option */
#define IPPROTO_EON 80       /* ISO cnlp */
#define IPPROTO_ETHERIP 97   /* Ethernet-in-IP */
#define IPPROTO_ENCAP 98     /* encapsulation header */
#define IPPROTO_PIM 103      /* Protocol indep. multicast */
#define IPPROTO_QNET 106     /* qnet protocol */
#define IPPROTO_IPCOMP 108   /* IP Payload Comp. Protocol */
#define IPPROTO_VRRP 112     /* VRRP RFC 2338 */
#define IPPROTO_CARP 112     /* Common Address Resolution Protocol */
#define IPPROTO_SCTP 132     /* SCTP RFC 2960 */
#define IPPROTO_RAW 255      /* raw IP packet */
#define IPPROTO_MAX 256

/* last return value of *_input(), meaning "all job for this pkt is done".  */
#define IPPROTO_DONE 257

/* sysctl placeholder for (FAST_)IPSEC */
#define CTL_IPPROTO_IPSEC 258

#define IP_OPTIONS 1          /* buf/ip_opts; set/get IP options */
#define IP_HDRINCL 2          /* int; header is included with data */
#define IP_TOS 3              /* int; IP type of service and preced. */
#define IP_TTL 4              /* int; IP time to live */
#define IP_RECVOPTS 5         /* bool; receive all IP opts w/dgram */
#define IP_RECVRETOPTS 6      /* bool; receive IP opts for response */
#define IP_RECVDSTADDR 7      /* bool; receive IP dst addr w/dgram */
#define IP_RETOPTS 8          /* ip_opts; set/get IP options */
#define IP_MULTICAST_IF 9     /* in_addr; set/get IP multicast i/f  */
#define IP_MULTICAST_TTL 10   /* u_char; set/get IP multicast ttl */
#define IP_MULTICAST_LOOP 11  /* u_char; set/get IP multicast loopback */
#define IP_ADD_MEMBERSHIP 12  /* ip_mreq; add an IP group membership */
#define IP_DROP_MEMBERSHIP 13 /* ip_mreq; drop an IP group membership */
#define IP_PORTALGO 18        /* int; port selection algo (rfc6056) */
#define IP_PORTRANGE 19       /* int; range to use for ephemeral port */
#define IP_RECVIF 20          /* bool; receive reception if w/dgram */
#define IP_ERRORMTU 21        /* int; get MTU of last xmit = EMSGSIZE */

#define IP_IPSEC_POLICY_COMPAT 22 /* struct; get/set security policy */
#define IP_IPSEC_POLICY (22 | GETSOCKOPT_EXTRA)

#define MSG_PEEK FREERTOS_MSG_PEEK

#define IPV6_V6ONLY 27
#define IN6ADDR_ANY_INIT \
    {                    \
        {                \
            {            \
                0        \
            }            \
        }                \
    }

typedef struct in_addr
{
    uint32_t ulIP_IPv4;
} in_addr;

#define sockaddr freertos_sockaddr
#define sockaddr_storage sockaddr
#define sockaddr_in freertos_sockaddr
#define sin_addr sin_address //.ulIP_IPv4
#define ss_family sin_family
#define s_addr ulIP_IPv4
#define s6_addr u.Byte

#ifndef htonl
#define htonl FreeRTOS_htonl
#endif
#ifndef ntohl
#define ntohl FreeRTOS_ntohl
#endif
#ifndef htons
#define htons FreeRTOS_htons
#endif
#ifndef ntohs
#define ntohs FreeRTOS_ntohs
#endif
#ifndef inet_ntop
#define inet_ntop FreeRTOS_inet_ntop
#endif

#define sa_family sin_family

typedef struct in6_addr
{
    union
    {
        uint8_t Byte[16];
        uint16_t Word[8];
    } u;
} in6_addr;

typedef struct sockaddr_in6
{
    uint16_t sin6_family;      /* AF_INET6 */
    uint16_t sin6_port;        /* Transport layer port number in network byte order */
    uint16_t sin6_flowinfo;    /* IPv6 flow information */
    struct in6_addr sin6_addr; /* IPv6 address */

} sockaddr_in6;

struct linger
{
    int l_onoff;  /* option on/off */
    int l_linger; /* linger time in seconds */
};

typedef struct ip_mreq
{
    struct in_addr imr_multiaddr; /* IP multicast address of group */
    struct in_addr imr_interface; /* local IP address of interface */
} ip_mreq;

typedef struct ipv6_mreq
{
    struct in6_addr ipv6mr_multiaddr; /*  IPv6 multicast addr */
    unsigned int ipv6mr_interface;    /*  interface index, or 0 */
} ipv6_mreq;

typedef unsigned char u_char;
typedef unsigned short u_short;
typedef unsigned int u_int;
typedef unsigned long u_long;

int socket(int domain, int type, int protocol);
int bind(int sockfd, const struct sockaddr *addr, socklen_t addrlen);
int listen(int sockfd, int backlog);
int accept(int sockfd, struct sockaddr *addr, socklen_t *addrlen);
int connect(int sockfd, const struct sockaddr *addr, socklen_t addrlen);
int send(int sockfd, const void *buf, size_t len, int flags);
int sendto(int sockfd, const void *buf, size_t len, int flags, const struct sockaddr *dest_addr, socklen_t addrlen);
int recv(int sockfd, void *buf, size_t len, int flags);
int recvfrom(int sockfd, void *buf, size_t len, int flags, struct sockaddr *src_addr, socklen_t *addrlen);
int close(int sockfd);
int shutdown(int sockfd, int how);
const char *inet_ntoa(struct in_addr in);
int inet_aton(const char *cp, struct in_addr *inp);
int setsockopt(int sockfd, int level, int optname, const void *optval, socklen_t optlen);
int getsockopt(int sockfd, int level, int optname, void *optval, socklen_t *optlen);
uint32_t inet_addr(const char *cp);
int getaddrinfo(const char *node, const char *service, const struct addrinfo *hints, struct addrinfo **res);
void freeaddrinfo(struct addrinfo *res);
int gethostname(char *name, size_t len);
int getpeername(int sockfd, struct sockaddr *addr, socklen_t *addrlen);

int getnameinfo(const struct sockaddr *sa, size_t salen, char *host,
                              size_t hostlen, char *serv, size_t servlen, int flags);
int getsockname(int s, struct sockaddr *addr, socklen_t *len);
int inet_pton(int af, const char *src, void *dst);
char *
gai_strerror(int err);

char *
hstrerror(int herr);

#endif