#include "mem.h"
#include "arg.h"
#include "log.h"
#include "net_utils.h"

#include "conn.h"

#define CONN_IOBUF_SIZE 64*1024 //64k

struct addr_pool_st {
    struct conn* conn;
    struct addr_pool_st *next;
};

struct addr_pool_t {
    struct sockaddr addr;
    struct addr_pool_st *head, *tail;
    UT_hash_handle hh;
} *addr_pool = NULL;

static conn_handler_t accept_handler = NULL;

static int serverfd = -1;

static void 
conn_init(struct conn* conn, int fd, struct sockaddr* addr) {
    conn->fd = fd;
    conn->buf = mem_alloc(CONN_IOBUF_SIZE);
    conn->buf_cap = CONN_IOBUF_SIZE;
    conn->buf_s = conn->buf_e = 0;
    conn->stat = CONN_FREE;
    if (addr)
        memcpy(&conn->addr, addr, sizeof(struct sockaddr));
}

void 
conn_fina(struct conn* conn) {
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
conn_addrpool_try_get_and_del(struct sockaddr* addr) {
    struct addr_pool_t* h;
    HASH_FIND(hh, addr_pool, addr, sizeof(struct sockaddr), h);
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
        mem_decref(conn, conn_fina);
    }
    HASH_DEL(addr_pool, h);
    mem_free(h);
    return NULL;
}

static void 
conn_async_final(struct async_cxt* cxt);

static int
conn_resume_handler(struct epoll_event ev, void* data) {
    struct async_cxt* cxt = (struct async_cxt*) data;
    int yield_ret = proxy_epoll_err(ev);

    PLOGD("Trying to resume context. yield_ret = %d", yield_ret);
    async_resume(cxt, yield_ret);

    conn_async_final(cxt);

    int retval = cxt->stat == ASYNC_PAUSE ? 0 : 1;
    mem_decref(cxt, async_fina);
    return retval;
}

static int 
conn_wrap_accept_handler(struct epoll_event ev, void* data) {
    PLOGD("Trying to handle connection in");
    if (proxy_epoll_err(ev)) {
        PLOGE("Accept handler gets an error epoll_event");
        return -1;
    }
    struct sockaddr addr;
    socklen_t addrlen = sizeof(struct sockaddr);
    int fd = accept(serverfd, &addr, &addrlen);
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
    conn_init(conn, fd, NULL);
    struct async_cxt* cxt = mem_alloc_auto(sizeof(struct async_cxt));
    async_init(cxt);

    async_call(cxt, accept_handler, conn);

    conn_async_final(cxt);

    mem_decref(conn, conn_fina);
    mem_decref(cxt, async_fina);
    // Always keep the accept handler
    return 0;
}

static void 
conn_async_final(struct async_cxt* cxt) {
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
    serverfd = proxy_net_bind(arg.addr, arg.port);
    if (serverfd < 0)
        return -1;
    return 0;
}

void 
conn_module_fina() {
    PLOGD("Closing server socket fd");
    close(serverfd);
}

void 
conn_set_accept_handler(conn_handler_t handler) {
    accept_handler = handler;
    net_pull_add(serverfd, EPOLLIN);
    net_pull_set_handler(serverfd, conn_wrap_accept_handler, NULL);
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
conn_get_by_addr(struct sockaddr* addr) {
    struct conn* conn = conn_addrpool_try_get_and_del(addr);
    if (conn) {
        PLOGD("Got conn from pool");
        return conn;
    }
    PLOGD("Try to get new connection");
    int fd = socket(addr->sa_family, SOCK_STREAM, 0);
    if (fd < 0) {
        PLOGUE("connect");
        return NULL;
    }

    if (proxy_net_setnonblocking(fd) < 0)
        goto conn_error;

    int ret = connect(fd, addr, sizeof(struct sockaddr));
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
    conn_init(conn, fd, addr);
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

