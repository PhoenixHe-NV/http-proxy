#ifndef _PROXY_CONN_H_
#define _PROXY_CONN_H_

#include <netinet/in.h>
#include <sys/epoll.h>

#include "utlib.h"
#include "async.h"
#include "event.h"

enum conn_stat {
    CONN_USED, CONN_FREE, CONN_CLOSED
};

struct conn_endpoint {
    int family;
    in_port_t port;
    union {
        struct in_addr v4;
        struct in6_addr v6;
    } addr;
};

struct conn_pool_ent;

// NOTICE: autoptr
struct conn {
    int fd; 
    // Read buf. No write buf
    char* buf;
    int buf_cap, buf_s, buf_e;
    enum conn_stat stat;
    struct conn_endpoint ep;
    struct conn_pool_ent* apool;
};


#define CONN_NOTICE_READ EPOLLIN
#define CONN_NOTICE_WRITE EPOLLOUT

// for async_yield
typedef union conn_notice_u {
    struct {
        int fd, flag;
    } io_block;
    struct {
        event_id eid;
    } wait_event;
} conn_notice;

typedef int (*conn_req_handler)(struct conn* conn, void** data_ptr);
typedef int (*conn_rsp_handler)(void* data);

char* ep_tostring(struct conn_endpoint* ep);

void conn_done(void* conn);

int conn_module_init();

void conn_module_done();

void conn_set_req_handler(conn_req_handler handler);

void conn_set_rsp_handler(conn_rsp_handler handler);

// Not actually free it, just return it back to connection pool
void conn_free(struct conn* conn);

// ASYNC func
struct conn* conn_get_by_endpoint(struct conn_endpoint* ep);

void conn_close(struct conn* conn);


#endif
