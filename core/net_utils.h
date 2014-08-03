#ifndef _PROXY_NET_UTILS_H_
#define _PROXY_NET_UTILS_H_

#include <sys/epoll.h>

int proxy_net_bind(char* addr, int port);

int proxy_net_connect(char* addr, int port);

int proxy_net_setnonblocking(int fd);

int proxy_epoll_err(struct epoll_event ev);

#endif
