#ifndef _PROXY_NET_UTILS_H_
#define _PROXY_NET_UTILS_H_

#include <sys/epoll.h>
#include <sys/socket.h>
#include <netdb.h>

int net_connect(struct sockaddr* addr, int port);

int net_setnonblocking(int fd);

int proxy_epoll_err(struct epoll_event ev);

const char* net_tostring(int family, void* addr);

int net_dns_lookup(char* host, char* service, struct addrinfo** result);

#endif
