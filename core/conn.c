#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>

#include "mem.h"
#include "arg.h"
#include "log.h"
#include "utlib.h"
#include "net_utils.h"
#include "net_pull.h"
#include "constants.h"

#include "conn.h"

#define CONN_IOBUF_SIZE 64*1024 //64k

struct addr_pool_st {
    struct conn* conn;
    struct addr_pool_st *next;
};

struct addr_pool_t {
    struct endpoint ep;
    struct addr_pool_st *head, *tail;
    UT_hash_handle hh;
} *addr_pool = NULL;

static conn_handler_t accept_handler = NULL;

static int sfds[SERVER_MAX_FD], sfd_cnt;

static void 
conn_init(struct conn* conn, int fd, struct conn_endpoint* ep) {
    conn->fd = fd;
    conn->buf = mem_alloc(CONN_IOBUF_SIZE);
    conn->buf_cap = CONN_IOBUF_SIZE;
    conn->buf_s = conn->buf_e = 0;
    conn->stat = CONN_FREE;
    if (addr)
        memcpy(&conn->ep, ep, sizeof(struct conn_endpoint));
}

void 
conn_done(struct conn* conn) {
    PLOGD("CONN FREE fd: %d", conn->fd);
    if (conn->stat != CONN_CLOSED)
        conn_close(conn);
    mem_free(conn->buf);
}

static void
conn_addrpool_add(struct conn* conn) {
    struct addr_pool_t* h;
    HASH_FIND(hh, addr_pool, &conn->addr, sizeof(struct sockaddr), h);
    struct addr_pool_st* elem = mem_alloc(sizeof(struct addr_pool_st));
    mem_incref(conn);
    elem->conn = conn;
    elem->next = NULL;
    if (h == NULL) {
        // Create entity
        h = mem_alloc(sizeof(struct addr_pool_t));
        memcpy(&h->addr, &conn->addr, sizeof(struct sockaddr));
        h->head = h->tail = elem;
        HASH_ADD(hh, addr_pool, addr, sizeof(struct sockaddr), h);
        return;
    }
    h->tail->next = elem;
    h->tail = elem;
}

static struct conn* 
conn_addrpool_try_get_and_del(struct conn_endpoint* ep) {
    struct addr_pool_t* h;
    HASH_FIND(hh, addr_pool, ep, sizeof(struct conn_endpoint), h);
    if (h == NULL)
        return NULL;
    struct addr_pool_st *p = h->head, **pre = &h->head;
    struct conn* conn = NULL;
    while (p) {
        conn = p->conn;
        *pre = p->next;
        mem_free(p);
        p = *pre;
        if (conn->stat == CONN_FREE)
            // Transfer handle
            return conn;
        mem_decref(conn, conn_done);
    }
    HASH_DEL(addr_pool, h);
    mem_free(h);
    return NULL;
}

static void 
conn_async_done(struct async_cxt* cxt);

static int
conn_resume_handler(struct epoll_event ev, void* data) {
    struct async_cxt* cxt = (struct async_cxt*) data;
    int yield_ret = proxy_epoll_err(ev) ? -1 : 0;

    PLOGD("Trying to resume context. yield_ret = %d", yield_ret);
    async_resume(cxt, yield_ret);

    conn_async_done(cxt);

    int retval = cxt->stat == ASYNC_PAUSE ? 0 : 1;
    mem_decref(cxt, async_done);
    return retval;
}

static int 
conn_wrap_accept_handler(struct epoll_event ev, void* data) {
    int sfd = *(int*)data;
    PLOGD("Trying to handle connection in. fd: %d", sfd);
    if (proxy_epoll_err(ev)) {
        PLOGE("Accept handler gets an error epoll_event");
        return -1;
    }

    struct sockaddr addr;
    socklen_t addrlen = sizeof(struct sockaddr);
    memset(&addr, 0, addrlen);
    int fd = accept(sfd, &addr, &addrlen);
    if (fd < 0) {
        PLOGUE("accept");
        return -1;
    }

    if (proxy_net_setnonblocking(fd)) {
        close(fd);
        return -1;
    }

    net_pull_add(fd, EPOLLIN | EPOLLOUT);

    struct conn* conn = mem_alloc_auto(sizeof(struct conn));
    conn_init(conn, fd, &addr);
    struct async_cxt* cxt = mem_alloc_auto(sizeof(struct async_cxt));
    async_init(cxt);

    async_call(cxt, accept_handler, conn);

    conn_async_done(cxt);

    mem_decref(conn, conn_done);
    mem_decref(cxt, async_done);
    // Always keep the accept handler
    return 0;
}

static void 
conn_async_done(struct async_cxt* cxt) {
    if (cxt->stat != ASYNC_PAUSE)
        return;
    if (cxt->yield_type != CONN_IO_WILL_BLOCK)
        return;
    struct conn_notice* notice = (struct conn_notice*) cxt->yield_data;
    mem_incref(cxt);
    net_pull_set_handler(notice->fd, conn_resume_handler, cxt);
};

int 
conn_module_init() {
//    sfd = proxy_net_bind(arg.addr, arg.port);
    memset(sfds, -1, sizeof(sfds));
    sfd_cnt = 0;

    PLOGD("Resolving server address %s", arg.addr); 
    struct addrinfo *result, *rp;
    int ret = proxy_net_dns_lookup(arg.addr, NULL, &result);
    if (ret) {
        PLOGE("getaddrinfo: %s", gai_strerror(ret));
        result = NULL;
    }

    for (rp = result; rp; rp = rp->ai_next) {
        int fd = socket(rp->ai_family, rp->ai_socktype, rp->ai_protocol);
        if (fd < 0) {
            PLOGUE("socket");
            continue;
        }

        int flag = 0;
        if (setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &flag , sizeof(int)) < 0) {
            PLOGUE("setsockopt: SO_REUSEADDR");
            goto bind_failed;
        }

        struct sockaddr_in* addr = (struct sockaddr_in*)rp->ai_addr;
        PLOGI("Trying to bind to %s :%d", 
                proxy_net_tostring(addr->sin_family, &(addr->sin_addr)),
                arg.port);
        // Set the port
        if (rp->ai_family == AF_INET)
            ((struct sockaddr_in*)rp->ai_addr)->sin_port = arg.port;
        if (rp->ai_family == AF_INET6)
            ((struct sockaddr_in6*)rp->ai_addr)->sin6_port = arg.port;

        if (bind(fd, rp->ai_addr, sizeof(struct sockaddr)) < 0) {
            PLOGUE("bind");
            goto bind_failed;
        }

        if (listen(fd, 16) < 0) {
            PLOGUE("listen");
            goto bind_failed;
        }

        net_pull_add(fd, EPOLLIN);
        net_pull_set_handler(fd, conn_wrap_accept_handler, sfds + sfd_cnt);

        sfds[sfd_cnt++] = fd;
        PLOGI("Bind success");
        continue;

bind_failed:
        close(fd);
    }

    if (result)
        freeaddrinfo(result);

    if (sfd_cnt == 0) {
        PLOGF("Cannot bind to any address");
        return -1;
    }

    return 0;
}

void 
conn_module_done() {
    PLOGD("Closing server socket fd");
    for (int i = 0; sfds[i] >= 0; ++i)
        close(sfds[i]);
}

void 
conn_set_accept_handler(conn_handler_t handler) {
    accept_handler = handler;
}

void 
conn_close(struct conn* conn) {
    if (conn->stat == CONN_CLOSED)
        return;
    PLOGD("CONN CLOSE fd: %d", conn->fd);
    net_pull_del(conn->fd);
    close(conn->fd);
    conn->stat = CONN_CLOSED;
}

struct conn* 
conn_get_by_endpoint(struct conn_endpoint* ep) {
    struct conn* conn = conn_addrpool_try_get_and_del(ep);
    if (conn) {
        PLOGD("Got conn from pool");
        return conn;
    }
    PLOGD("Try to get new connection");
    int fd = socket(ep->family, SOCK_STREAM, 0);
    if (fd < 0) {
        PLOGUE("connect");
        return NULL;
    }

    if (proxy_net_setnonblocking(fd) < 0)
        goto conn_error;

    sockaddr_storage sa;
    memset(sa, 0, sizeof(sa));
    if (ep->family == AF_INET)
        // TODO

    int ret = connect(fd, &sa, sizeof(struct sockaddr));
    if (ret < 0 && errno != EINPROGRESS)
        goto conn_error;
    
    net_pull_add(fd, EPOLLIN | EPOLLOUT);
    if (ret < 0 && errno == EINPROGRESS) {
        // will block
        struct conn_notice notice;
        notice.fd = fd;
        notice.flag = EPOLLIN;
        int ret = async_yield(CONN_IO_WILL_BLOCK, &notice);
        if (ret == -1) {
            net_pull_del(fd);
            return NULL;
        }
    }

    PLOGD("Connected");

    conn = mem_alloc_auto(sizeof(struct conn));
    conn_init(conn, fd, ep);
    return conn;

conn_error:
    PLOGD("Connection failed. Closing fd");
    // TODO: getsockopt() to get error type
    close(fd);
    return NULL;
}

void conn_free(struct conn* conn) {
    PLOGD("Adding conn to free connection pool");
    conn_addrpool_add(conn);
}

