#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/fcntl.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#include "log.h"

#include "net_utils.h"

int proxy_net_bind(char* addr, int port) {
    PLOGI("Trying to bind to %s:%d", addr, port);
    struct sockaddr_in server_addr;
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(port);
    // TODO: use getaddrinfo()
    if (inet_aton(addr, &(server_addr.sin_addr)) == 0) {
        PLOGE("inet_aton failed for addr: %s", addr);
        return -1;
    }

    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) {
        PLOGUE("socket");
        return -1;
    }

    int flag = 0;
    if (setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &flag , sizeof(int)) < 0) {
        PLOGUE("setsockopt: SO_REUSEADDR");
        goto bind_failed;
    }

    if (bind(fd, (struct sockaddr*)&server_addr, sizeof(struct sockaddr)) < 0) {
        PLOGUE("bind");
        goto bind_failed;
    }

    if (listen(fd, 16) < 0) {
        PLOGUE("listen");
        goto bind_failed;
    }

    PLOGI("Bind success");
    return fd;

bind_failed:
    close(fd);
    return -1;
}

int proxy_net_connect(char* addr, int port) {
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
    PLOGD("event: %d", ev.events);
    PLOGD("EPOLLERR: %d, EPOLLHUP: %d", EPOLLERR, EPOLLHUP);
    return ev.events & EPOLLERR || ev.events & EPOLLHUP;
}
