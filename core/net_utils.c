#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/fcntl.h>
#include <netdb.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#include "log.h"

#include "net_utils.h"

int proxy_net_connect(struct sockaddr* addr, int port) {
    return -1;
}

int proxy_net_setnonblocking(int fd) {
    int opts = fcntl(fd, F_GETFL);
    if(opts < 0) {
        PLOGUE("fcntl(sock, GETFL)");
        return -1;
    }

    opts = opts | O_NONBLOCK;
    if (fcntl(fd, F_SETFL, opts) < 0) {
        PLOGUE("fcntl(sock, SETFL, opts|O_NONBLOCK)");
        return -1;
    }  

    return 0;
}

int proxy_epoll_err(struct epoll_event ev) {
    return ev.events & EPOLLERR || ev.events & EPOLLHUP;
}

const char* proxy_net_tostring(int family, char* addr) {
    static char buf[256];
/*
    int ret = getnameinfo(addr, sizeof(struct sockaddr), 
            buf, sizeof(buf), NULL, 0, NI_NAMEREQD);
    if (ret)
        return gai_strerror(ret);
    return buf;
    */
    PLOGD("%d", family);
    inet_ntop(family, addr, buf, 256);
    return buf;
}

int proxy_net_dns_lookup(char* host, char* service, struct addrinfo** result) {
    struct addrinfo hints;
    memset(&hints, 0, sizeof(struct addrinfo));
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    
    int ret = getaddrinfo(host, service, &hints, result);
    return ret;
}
