#ifndef _PROXY_CONN_H_
#define _PROXY_CONN_H_

#include <netinet/in.h>
#include <sys/epoll.h>

#include "utlib.h"
#include "async.h"

enum conn_stat {
    CONN_USED, CONN_FREE, CONN_CLOSED
};

struct conn_endpoint {
    int family, port;
    union {
        struct in_addr v4;
        struct in6_addr v6;
    } addr;
};

struct addr_pool_st;

// NOTICE: autoptr
struct conn {
    int fd; 
    // Read buf. No write buf
    char* buf;
    int buf_cap, buf_s, buf_e;
    enum conn_stat stat;
    struct conn_endpoint ep;
    struct addr_pool_st* apool;
};


#define CONN_NOTICE_READ EPOLLIN
#define CONN_NOTICE_WRITE EPOLLOUT

// for async_yield
struct conn_notice {
    int fd, flag;
};

char* ep_tostring(struct conn_endpoint* ep);

void conn_done(struct conn* conn);

typedef int (*conn_handler_t)(struct conn* conn);

int conn_module_init();

void conn_module_done();

void conn_set_accept_handler(conn_handler_t handler);

// Not actually free it, just return it back to connection pool
void conn_free(struct conn* conn);

// ASYNC func
struct conn* conn_get_by_endpoint(struct conn_endpoint* ep);

void conn_close(struct conn* conn);


#endif
