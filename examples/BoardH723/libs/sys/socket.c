#include "sys/socket.h"
#include "FreeRTOS_Sockets.h"
#include "netdb.h"
#include "sys/errno.h"
#include "sys/poll.h"
#include <reent.h>

int h_errno;

int socket(int domain, int type, int protocol)
{
    return (int)FreeRTOS_socket(domain, type, protocol);
}

int bind(int sockfd, const struct sockaddr *addr, socklen_t addrlen)
{
    return FreeRTOS_bind((Socket_t)sockfd, addr, addrlen);
}

int listen(int sockfd, int backlog)
{
    return FreeRTOS_listen((Socket_t)sockfd, backlog);
}

int accept(int sockfd, struct sockaddr *addr, socklen_t *addrlen)
{
    return (int)FreeRTOS_accept((Socket_t)sockfd, addr, addrlen);
}

int connect(int sockfd, const struct sockaddr *addr, socklen_t addrlen)
{
    return FreeRTOS_connect((Socket_t)sockfd, addr, addrlen);
}

int send(int sockfd, const void *buf, size_t len, int flags)
{
    return (int)FreeRTOS_send((Socket_t)sockfd, buf, len, flags);
}

int sendto(int sockfd, const void *buf, size_t len, int flags, const struct sockaddr *dest_addr, socklen_t addrlen)
{
    return FreeRTOS_sendto((Socket_t)sockfd, buf, len, flags, dest_addr, addrlen);
}

int recv(int sockfd, void *buf, size_t len, int flags)
{
 Socket_t socket = (Socket_t)sockfd;

    // Check if the socket is a UDP socket
    if (socket->ucProtocol == IPPROTO_UDP) {
        // For UDP, use recvfrom with NULL for src_addr and addrlen
        int ret = FreeRTOS_recvfrom(socket, buf, len, flags, NULL, NULL);
        if (ret == 0) {
            _REENT->_errno = 0;
            return 0;
        } else if (ret == -pdFREERTOS_ERRNO_ENOTCONN) {
            _REENT->_errno = -2;
            return -2;
        } else if (ret == -pdFREERTOS_ERRNO_ETIMEDOUT) {
            _REENT->_errno = -1;
            return -1;
        }
        return ret;
    } else {
        // For TCP, use FreeRTOS_recv
        int ret = FreeRTOS_recv(socket, buf, len, flags);
        if (ret == 0) {
            _REENT->_errno = 0;
            return 0;
        } else if (ret == -pdFREERTOS_ERRNO_ENOTCONN) {
            _REENT->_errno = -2;
            return -2;
        } else if (ret == -pdFREERTOS_ERRNO_ETIMEDOUT) {
            _REENT->_errno = -1;
            return -1;
        }
        return ret;
    }
}

int recvfrom(int sockfd, void *buf, size_t len, int flags, struct sockaddr *src_addr, socklen_t *addrlen)
{
   int ret = FreeRTOS_recvfrom((Socket_t)sockfd, buf, len, flags, src_addr, addrlen);
        if(ret == 0){
        _REENT->_errno = 0;
    return 0;
    }
    else if(ret == -pdFREERTOS_ERRNO_ENOTCONN){
        _REENT->_errno = -2;
        return -2;
    }
    else if(ret == -pdFREERTOS_ERRNO_ETIMEDOUT){
        _REENT->_errno = -1;
        return -1;
    }
    return ret;
}

int shutdown(int sockfd, int how)
{
    return FreeRTOS_shutdown((Socket_t)sockfd, how);
}

const char *inet_ntoa(struct in_addr in)
{
    static char buffer[INET_ADDRSTRLEN];
    return FreeRTOS_inet_ntoa(in.s_addr, buffer);
}

int inet_aton(const char *cp, struct in_addr *inp)
{
    uint32_t val;
    char c;
    int parts[4];
    int i, num;

    if (!cp || !inp)
        return 0;

    for (i = 0; i < 4; i++)
    {
        num = 0;
        while ((c = *cp++) != '\0' && c != '.')
        {
            if (c < '0' || c > '9')
                return 0;
            num = num * 10 + (c - '0');
            if (num > 255)
                return 0;
        }
        parts[i] = num;
        if (c == '\0' && i != 3)
            return 0;
    }
    val = (parts[0] << 24) | (parts[1] << 16) | (parts[2] << 8) | parts[3];
    inp->s_addr = FreeRTOS_htonl(val);
    return 1;
}

int setsockopt(int sockfd, int level, int optname, const void *optval, socklen_t optlen)
{
    return (int)FreeRTOS_setsockopt((Socket_t)sockfd, level, optname, optval, optlen);
}

int getsockopt(int sockfd, int level, int optname, void *optval, socklen_t *optlen)
{
    if (level == SOL_SOCKET)
    {
        FreeRTOS_Socket_t *pxSocket = (FreeRTOS_Socket_t *)sockfd;
        
        if ((optval == NULL) || (optlen == NULL) || (pxSocket == NULL)) {
            return -pdFREERTOS_ERRNO_EINVAL;
        }

        switch (optname)
        {
        case SO_ERROR:
            if (*optlen >= sizeof(int32_t)) {
                // *(int32_t *)optval = pxSocket->xSocketError;
                // pxSocket->xSocketError = 0; // Сбрасываем ошибку после чтения
                *optlen = sizeof(int32_t);
                return 0;
            }
            break;

        case SO_TYPE:
            if (*optlen >= sizeof(int32_t)) {
                *(int32_t *)optval = pxSocket->ucProtocol == IPPROTO_TCP ? SOCK_STREAM : SOCK_DGRAM;
                *optlen = sizeof(int32_t);
                return 0;
            }
            break;

        // Добавляем обработку дополнительных опций
        case SO_SNDBUF:
        case SO_RCVBUF:
            if (*optlen >= sizeof(int32_t)) {
                *(int32_t *)optval = 8192; // Фиксированный размер буфера
                *optlen = sizeof(int32_t);
                return 0;
            }
            break;

        case SO_SNDLOWAT:
        case SO_RCVLOWAT:
            if (*optlen >= sizeof(int32_t)) {
                *(int32_t *)optval = 1; // Минимальный порог = 1 байт
                *optlen = sizeof(int32_t);
                return 0;
            }
            break;

        case SO_SNDTIMEO:
        case SO_RCVTIMEO:
            if (*optlen >= sizeof(struct timeval)) {
                struct timeval *tv = optval;
                tv->tv_sec = pxSocket->xReceiveBlockTime / 1000;
                tv->tv_usec = (pxSocket->xReceiveBlockTime % 1000) * 1000;
                *optlen = sizeof(struct timeval);
                return 0;
            }
            break;

        case SO_ACCEPTCONN:
            if (*optlen >= sizeof(int32_t)) {
                *(int32_t *)optval = (pxSocket->ucProtocol == FREERTOS_SOCK_STREAM && 
                                     pxSocket->u.xTCP.eTCPState == eTCP_LISTEN) ? 1 : 0;
                *optlen = sizeof(int32_t);
                return 0;
            }
            break;

        // Заглушки для нереализованных опций
        case SO_DEBUG:
        case SO_DONTROUTE:
        case SO_BROADCAST:
        case SO_OOBINLINE:
            if (*optlen >= sizeof(int32_t)) {
                *(int32_t *)optval = 0;
                *optlen = sizeof(int32_t);
                return 0;
            }
            break;
        case SOL_SOCKET:
            if (*optlen >= sizeof(int32_t)) {
                *(int32_t *)optval = pxSocket->ucProtocol;
                *optlen = sizeof(int32_t);
                return 0;
            }
            break;

        default:
            break;
        }
    }
    return -pdFREERTOS_ERRNO_EINVAL;
}

uint32_t inet_addr(const char *cp)
{

    return FreeRTOS_inet_addr(cp);
}

int getaddrinfo(const char *node, const char *service, const struct addrinfo *hints, struct addrinfo **res)
{
    struct addrinfo tmp_hints;

    // Обрабатываем специальный случай 0.0.0.0
    if (node && strcmp(node, "0.0.0.0") == 0) {
        node = NULL; // Интерпретируем как INADDR_ANY
    }

    if (hints && hints->ai_family == AF_UNSPEC) {
        memcpy(&tmp_hints, hints, sizeof(tmp_hints));
        tmp_hints.ai_family = AF_INET;
        hints = &tmp_hints;
    }

    if (node == NULL) {
        *res = pvPortMalloc(sizeof(struct addrinfo));
        if (*res == NULL) {
            return -pdFREERTOS_ERRNO_ENOMEM;
        }

        // Инициализируем структуру по умолчанию
        memset(*res, 0, sizeof(struct addrinfo));
        if (hints) {
            memcpy(*res, hints, sizeof(struct addrinfo));
        }

        // Устанавливаем тип сокета и протокол по умолчанию, если не заданы
        if ((*res)->ai_socktype == 0) {
            (*res)->ai_socktype = SOCK_STREAM; // по умолчанию TCP
        }

        if ((*res)->ai_protocol == 0) {
            (*res)->ai_protocol = IPPROTO_TCP; // по умолчанию TCP
        }

        // Если пользователь указал UDP, меняем тип
        if (hints && hints->ai_socktype == SOCK_DGRAM) {
            (*res)->ai_socktype = SOCK_DGRAM;
            (*res)->ai_protocol = IPPROTO_UDP;
        }

        (*res)->ai_addr = pvPortMalloc(sizeof(struct freertos_sockaddr));
        memset((*res)->ai_addr, 0, sizeof(struct freertos_sockaddr));
        if (service) {
            (*res)->ai_addr->sin_port = FreeRTOS_htons(atoi(service));
        }
        (*res)->ai_addrlen = sizeof(struct freertos_sockaddr);
        (*res)->ai_addr->sin_family = hints ? hints->ai_family : AF_INET;
        (*res)->ai_canonname = NULL;

        return 0;
    }

    int ret = (int)FreeRTOS_getaddrinfo(node, service, hints, res);
    if (ret == 0) {
        (*res)->ai_addr->sin_family = AF_INET;
        (*res)->ai_addr->sin_port = service ? FreeRTOS_htons(atoi(service)) : 0;
        (*res)->ai_addr->sin_flowinfo = hints ? hints->ai_addr->sin_flowinfo : 0;

        if ((*res)->ai_protocol == 0 && hints && hints->ai_protocol == 0) {
            (*res)->ai_protocol = IPPROTO_TCP;
        } else if (hints) {
            (*res)->ai_protocol = hints->ai_protocol;
        }

        if ((*res)->ai_socktype == 0 && hints && hints->ai_socktype == 0) {
            (*res)->ai_socktype = SOCK_STREAM;
        } else if (hints) {
            (*res)->ai_socktype = hints->ai_socktype;
        }
    }

    return ret;
}

void freeaddrinfo(struct addrinfo *res)
{
    FreeRTOS_freeaddrinfo(res);
}

int getpeername(int sockfd, struct sockaddr *addr, socklen_t *addrlen)
{
    return 0;
}

int getnameinfo(const struct sockaddr *sa, size_t salen, char *host,
                              size_t hostlen, char *serv, size_t servlen, int flags)
{
    struct sockaddr_in *sin = (struct sockaddr_in *)sa;
    struct hostent *hp;
    char tmpserv[16];

    if (sa->sa_family != AF_UNSPEC && sa->sa_family != AF_INET)
        return (EAI_FAMILY);
    if (serv != NULL)
    {
        snprintf(tmpserv, sizeof(tmpserv), "%lu", ntohl(sin->sin_port));
        if (strlcpy(serv, tmpserv, servlen) >= servlen)
            return (EAI_MEMORY);
    }

    if (host != NULL)
    {
        if (flags & NI_NUMERICHOST)
        {
            struct in_addr addr = {.s_addr = sin->sin_addr.s_addr};

            if (strlcpy(host, inet_ntoa(addr),
                        hostlen) >= hostlen)
                return (EAI_MEMORY);
            else
                return (0);
        }
        else
        {
            hp = gethostbyaddr((char *)&sin->sin_addr,
                               sizeof(struct in_addr), AF_INET);
            if (hp == NULL)
                return (EAI_NODATA);

            if (strlcpy(host, hp->h_name, hostlen) >= hostlen)
                return (EAI_MEMORY);
            else
                return (0);
        }
    }
    return (0);
}

int getsockname(int s, struct sockaddr *addr, socklen_t *len)
{
    if(!addr || !len || s <= 0)
        return -1;
    memset(addr, 0, sizeof(struct sockaddr));
    Socket_t sock = (Socket_t)s;
    addr->sin_len = sizeof(struct sockaddr);
    uint8_t l = sizeof(addr->sin_address.xIP_IPv6.ucBytes);
    for(int i = 0; i < l; i++)
        addr->sin_address.xIP_IPv6.ucBytes[i] = sock->pxEndPoint->ipv6_settings.xIPAddress.ucBytes[i];

    addr->sin_address.ulIP_IPv4 = sock->pxEndPoint->ipv4_settings.ulIPAddress;
    return 1;
}

int inet_pton(int af, const char *src, void *dst)
{
    if (af == AF_INET)
    {
        struct in_addr *addr = (struct in_addr *)dst;
        return inet_aton(src, addr);
    }
    else if (af == AF_INET6)
    {
        // struct in6_addr *addr = (struct in6_addr *)dst;
        // int status = inet_aton(AF_INET6, src, addr->Byte);
        // return status;
    }
    return 0;
}

int select(int n, fd_set *readfds, fd_set *writefds, fd_set *exceptfds, struct timeval *timeout)
{
    TickType_t ticks;
    if(timeout == NULL)
        ticks = 0;
    else
        ticks = pdMS_TO_TICKS(timeout->tv_sec * 1000 + timeout->tv_usec / 1000);

    return FreeRTOS_select((SocketSet_t)readfds, ticks);
}

char *
gai_strerror(int err)
{
	switch (err) {
	case EAI_NODATA:
		return ("no address associated with name");
	case EAI_MEMORY:
		return ("memory allocation failure");
	case EAI_NONAME:
		return ("nodename nor servname provided, or not known");
	case EAI_FAMILY:
		return ("ai_family not supported");
	case EAI_AGAIN:
		return ("temparary failure");
	case EAI_FAIL:
		return ("unrecoverable failure");
	default:
		return ("unknown/invalid error");
	}
}    

int gethostname(char *name, size_t len) {
	int l = strlen(pcApplicationHostnameHook()) > len ? len : strlen(pcApplicationHostnameHook()); 
	l++;
	memcpy(name, pcApplicationHostnameHook(), l-1);
	name[l - 1] = '\0';
    return 0;
}
  

  static const char *const h_errlist[] = {
    "Resolver Error 0 (no error)",
    "Unknown host",		/* 1 HOST_NOT_FOUND */
    "Host name lookup failure",	/* 2 TRY_AGAIN */
    "Unknown server error",	/* 3 NO_RECOVERY */
    "No address associated with name", /* 4 NO_ADDRESS */
};

char *
hstrerror(int herr)
{
    if (0 <= herr && herr < (sizeof h_errlist / sizeof h_errlist[0]))
	return (char *) h_errlist[herr];
    else if(herr == -17)
	return "unknown error";
    else
	return "Error number out of range (hstrerror)";
}

int poll(struct pollfd *fds, nfds_t nfds, int timeout)
{
    struct timeval tv = {
        // timeout is in milliseconds
        .tv_sec = timeout / 1000,
        .tv_usec = (timeout % 1000) * 1000,
    };
    int max_fd = -1;
    fd_set readfds;
    fd_set writefds;
    fd_set errorfds;
    struct _reent* r = __getreent();
    int ret = 0;

    if (fds == NULL) {
        __errno_r(r) = ENOENT;
        return -1;
    }

    FD_ZERO(&readfds);
    FD_ZERO(&writefds);
    FD_ZERO(&errorfds);

    for (unsigned int i = 0; i < nfds; ++i) {
        fds[i].revents = 0;

        if (fds[i].fd < 0) {
            // revents should remain 0 and events ignored (according to the documentation of poll()).
            continue;
        }

        if (fds[i].fd >= FD_SETSIZE) {
            fds[i].revents |= POLLNVAL;
            ++ret;
            continue;
        }

        if (fds[i].events & (POLLIN | POLLRDNORM | POLLRDBAND | POLLPRI)) {
            FD_SET(fds[i].fd, &readfds);
            FD_SET(fds[i].fd, &errorfds);
            max_fd = MAX(max_fd, fds[i].fd);
        }

        if (fds[i].events & (POLLOUT | POLLWRNORM | POLLWRBAND)) {
            FD_SET(fds[i].fd, &writefds);
            FD_SET(fds[i].fd, &errorfds);
            max_fd = MAX(max_fd, fds[i].fd);
        }
    }

    const int select_ret = select(max_fd + 1, &readfds, &writefds, &errorfds, timeout < 0 ? NULL : &tv);

    if (select_ret > 0) {
        ret += select_ret;

        for (unsigned int i = 0; i < nfds; ++i) {
            if (FD_ISSET(fds[i].fd, &readfds)) {
                fds[i].revents |= POLLIN;
            }

            if (FD_ISSET(fds[i].fd, &writefds)) {
                fds[i].revents |= POLLOUT;
            }

            if (FD_ISSET(fds[i].fd, &errorfds)) {
                fds[i].revents |= POLLERR;
            }
        }
    } else {
        ret = select_ret;
        // keeping the errno from select()
    }

    return ret;
}