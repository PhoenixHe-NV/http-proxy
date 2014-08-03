#ifndef _PROXY_CONN_H_
#define _PROXY_CONN_H_

#include <netinet/in.h>
#include <sys/epoll.h>

#include "utlib.h"
#include "async.h"
#include "iobuf.h"

enum conn_stat {
    CONN_USED, CONN_FREE, CONN_CLOSED
};

// NOTICE: autoptr
struct conn {
    int fd; 
    // Read buf. No write buf
    char* buf;
    int buf_cap, buf_s, buf_e;
    enum conn_stat stat;
    struct sockaddr addr;
};


#define CONN_NOTICE_READ EPOLLIN
#define CONN_NOTICE_WRITE EPOLLOUT

// for async_yield
struct conn_notice {
    int fd, flag;
};

void conn_fina(struct conn* conn);

typedef int (*conn_handler_t)(struct conn* conn);

int conn_module_init();

void conn_module_fina();

void conn_set_accept_handler(conn_handler_t handler);

// Not actually free it, just return it back to connection pool
void conn_free(struct conn* conn);

// ASYNC funcs
struct conn* conn_get_by_addr(struct sockaddr* addr);

void conn_close(struct conn* conn);


#endif
