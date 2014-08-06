#ifndef _PROXY_NET_UTILS_H_
#define _PROXY_NET_UTILS_H_

#include <sys/epoll.h>
#include <sys/socket.h>

int proxy_net_connect(struct sockaddr* addr, int port);

int proxy_net_setnonblocking(int fd);

int proxy_epoll_err(struct epoll_event ev);

const char* proxy_net_tostring(int family, char* addr);

int proxy_net_dns_lookup(char* host, char* service, struct addrinfo** result);

#endif
