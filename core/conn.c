#include <sys/types.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <stdio.h>
#include <netdb.h>

#include "mem.h"
#include "arg.h"
#include "log.h"
#include "utlib.h"
#include "net_utils.h"
#include "net_pull.h"
#include "constants.h"
#include "event.h"
#include "main.h"

#include "conn.h"

#define CONN_IOBUF_SIZE 64*1024 //64k

struct conn_pool_ent {
    struct conn* conn;
    struct conn_pool_ent *prev, *next;
};

// conn_pool_t is a hash table keeps a list of conn_pool_st
// the first empty node is conn_pool_t.head
struct conn_pool {
    struct conn_endpoint ep;
    struct conn_pool_ent head;
    UT_hash_handle hh;
};

static struct conn_pool *free_pool = NULL;

static conn_req_handler req_handler = NULL;
static conn_rsp_handler rsp_handler = NULL;

static int sfds[SERVER_MAX_FD], sfd_cnt;

static void 
conn_init(struct conn* conn, int fd, struct conn_endpoint* ep) {
    conn->fd = fd;
    conn->buf = mem_alloc(CONN_IOBUF_SIZE);
    conn->buf_cap = CONN_IOBUF_SIZE;
    conn->buf_s = conn->buf_e = 0;
    conn->stat = CONN_FREE;
    if (ep)
        memcpy(&conn->ep, ep, sizeof(struct conn_endpoint));
    conn->apool = NULL;
}

void 
conn_done(void* p) {
    struct conn* conn = (struct conn*) p;
    if (conn->stat != CONN_CLOSED)
        conn_close(conn);
    else {
        net_pull_del(conn->fd);
        close(conn->fd);
    }
    mem_free(conn->buf);
}

int
conn_err_handler(struct epoll_event ev, void* data) {
    PLOGD("CONN ERR");
    struct conn* conn = (struct conn*) data;
    conn->stat = CONN_CLOSED;
    return 0;
}

static void
conn_addrpool_add(struct conn* conn) {
    struct conn_pool* h;
    HASH_FIND(hh, free_pool, &conn->ep, sizeof(struct conn_endpoint), h);
    struct conn_pool_ent* elem = mem_alloc(sizeof(struct conn_pool_ent));
    mem_incref(conn);
    conn->stat = CONN_FREE;
    elem->conn = conn;
    conn->apool = elem;
    if (h == NULL) {
        h = mem_alloc(sizeof(struct conn_pool));
        memcpy(&h->ep, &conn->ep, sizeof(struct conn_endpoint));
        elem->prev = &h->head;
        elem->next = NULL;
        h->head.prev = NULL;
        h->head.next = elem;
        
        HASH_ADD(hh, free_pool, ep, sizeof(struct conn_endpoint), h);
        return;
    }
    elem->next = h->head.next;
    elem->prev = &h->head;
    if (h->head.next)
        h->head.next->prev = elem;
    h->head.next = elem;
}

static void
conn_addrpool_del(struct conn* conn) {
    if (conn->apool == NULL) {
        PLOGF("Deleting a non existing connection from pool!!");
        return;
    }
    struct conn_pool_ent* elem = conn->apool;
    elem->prev->next = elem->next;
    if (elem->next)
        elem->next->prev = elem->prev;
    mem_free(elem);
    mem_decref(conn, conn_done);
}

static struct conn* 
conn_addrpool_try_get_and_del(struct conn_endpoint* ep) {
    struct conn_pool* h;
    HASH_FIND(hh, free_pool, ep, sizeof(struct conn_endpoint), h);
    if (h == NULL || h->head.next == NULL)
        return NULL;
    
    // Delete the first item 
    struct conn_pool_ent* elem = h->head.next;
    h->head.next = elem->next;
    if (elem->next)
        elem->next->prev = &h->head;
    struct conn* conn = elem->conn;
    conn->apool = NULL;
    mem_free(elem);
    return conn;
}

static int
conn_epoll_handler(struct epoll_event ev, void* data);

static int
conn_event_handler(event_id eid, void* data0, void* data1);

static int
conn_async_final(struct async_cxt* cxt) {
    if (cxt->stat != ASYNC_PAUSE) {
        mem_decref(cxt, async_done);
        return -1;
    }
    
    conn_notice* notice = (conn_notice*) cxt->yield_data;
    switch (cxt->yield_type) {
        case CONN_IO_WILL_BLOCK:
            net_pull_set_handler(notice->io_block.fd,
                                 notice->io_block.flag, 
                                 conn_epoll_handler, cxt);
            break;
            
        case CONN_WAIT_FOR_EVENT:
            event_set_handler(notice->wait_event.eid,
                              conn_event_handler, cxt);
            break;
            
        case YIELD_NONE:
        default:
            PLOGD("UNKNOWN YIELD TYPE");
    }
    return 0;
}  

static int
conn_epoll_handler(struct epoll_event ev, void* data) {
    struct async_cxt* cxt = (struct async_cxt*) data;
    int yield_ret = proxy_epoll_err(ev) ? -1 : 0;
    
    async_resume(cxt, yield_ret);
    conn_async_final(cxt);
    
    return 0;
}

static int
conn_event_handler(event_id eid, void* data0, void* data1) {
    struct async_cxt* cxt = (struct async_cxt*) data0;
    int yield_ret = (data1 == NULL);
    
    async_resume(cxt, yield_ret);
    conn_async_final(cxt);
    
    return 0;
}

static int 
conn_accept_handler(struct epoll_event ev, void* data) {
    int sfd = *(int*)data;
    PLOGD("Trying to handle connection in. fd: %d", sfd);
    if (proxy_epoll_err(ev)) {
        PLOGE("Accept handler gets an error epoll_event");
        net_pull_del(sfd);
        return -1;
    }
    
    // Keep this accept handler
    net_pull_set_handler(sfd, EPOLLIN, conn_accept_handler, data);
    
    while (1) {
        // Accept client connection
        struct sockaddr_storage addr;
        socklen_t addrlen = sizeof(addr);
        memset(&addr, 0, addrlen);
        int fd = accept(sfd, (struct sockaddr*)&addr, &addrlen);
        if (fd < 0) {
            if (errno == EAGAIN)
                return 0;
            PLOGUE("accept");
            return -1;
        }
        if (net_setnonblocking(fd)) {
            close(fd);
            return -1;
        }
        
        // Construct endpoint
        struct conn_endpoint ep;
        memset(&ep, 0, sizeof(ep));
        if (addr.ss_family == AF_INET) {
            struct sockaddr_in* addr4 = (struct sockaddr_in*)&addr;
            ep.family = AF_INET;
            ep.port = addr4->sin_port;
            memcpy(&ep.addr.v4, &(addr4->sin_addr), sizeof(struct in_addr));
        } else if (addr.ss_family == AF_INET6) {
            struct sockaddr_in6* addr6 = (struct sockaddr_in6*)&addr;
            ep.family = AF_INET6;
            ep.port = addr6->sin6_port;
            memcpy(&ep.addr.v6, &(addr6->sin6_addr), sizeof(struct in6_addr));
        } else {
            PLOGE("Unknown client connection type");
            close(fd);
            return -1;
        }
        
        // Construct connection
        struct conn* conn = mem_alloc_auto(sizeof(struct conn));
        conn_init(conn, fd, &ep);
        conn->stat = CONN_USED;
        
        net_pull_add(fd, conn_err_handler, conn);
        
        // Call req handler first
        struct async_cxt* cxt_req = mem_alloc_auto(sizeof(struct async_cxt));
        async_init(cxt_req);
        void* data_p = NULL;
        
        // async_call(cxt_req, req_handler, 2, conn, &data_p);
        cxt_req->stat = ASYNC_PAUSE;
        makecontext(&cxt_req->uc, req_handler, 2, conn, &data_p);
        async_resume(cxt_req, 0);
        
        int ret = conn_async_final(cxt_req);
        
        if (ret) {
            // req handler failed
            mem_decref(conn, conn_done);
            return 0;
        }
        
        // Call rsp handler
        struct async_cxt* cxt_rsp = mem_alloc_auto(sizeof(struct async_cxt));
        async_init(cxt_rsp);
        
        cxt_rsp->stat = ASYNC_PAUSE;
        makecontext(&cxt_rsp->uc, rsp_handler, 1, data_p);
        async_resume(cxt_rsp, 0);
        
        //async_call(cxt_rsp, rsp_handler, 1, data_p);
        
        mem_decref(conn, conn_done);
        ret = conn_async_final(cxt_rsp);
    }
    
    return 0;
}

int 
conn_module_init() {
    memset(sfds, -1, sizeof(sfds));
    sfd_cnt = 0;
    
    PLOGD("Resolving server address %s", arg.addr); 
    struct addrinfo *result, *rp;
    int ret = net_dns_lookup(arg.addr, NULL, &result);
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
        
        int ret;
        // Set the port
        if (rp->ai_family == AF_INET) {
            struct sockaddr_in* addr = (struct sockaddr_in*)(rp->ai_addr);
            addr->sin_port = htons(arg.port);
            PLOGI("Trying to bind to %s :%d", 
                  net_tostring(addr->sin_family, &(addr->sin_addr)), arg.port);
            ret = bind(fd, rp->ai_addr, sizeof(struct sockaddr_in));
        } else if (rp->ai_family == AF_INET6) {
            struct sockaddr_in6* addr = (struct sockaddr_in6*)(rp->ai_addr);
            addr->sin6_port = htons(arg.port);
            PLOGI("Trying to bind to %s :%d", 
                  net_tostring(addr->sin6_family, &(addr->sin6_addr)), 
                  arg.port);
            ret = bind(fd, rp->ai_addr, sizeof(struct sockaddr_in6));
        } else {
            PLOGF("Unknown network type");
            goto bind_failed;
        }
        
        if (ret < 0) {
            PLOGUE("bind");
            goto bind_failed;
        }
        
        if (net_setnonblocking(fd)) {
            close(fd);
            return -1;
        }
        
        if (listen(fd, 16) < 0) {
            PLOGUE("listen");
            goto bind_failed;
        }
        
        net_pull_add(fd, NULL, NULL);
        net_pull_set_handler(fd, EPOLLIN, conn_accept_handler, sfds + sfd_cnt);
        
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
    
    struct conn_pool *i, *tmp;
    HASH_ITER(hh, free_pool, i, tmp) {
        struct conn_pool_ent *p = i->head.next, *q;
        while (p) {
            q = p->next;
            mem_decref(p->conn, conn_done);
            mem_free(p);
            p = q;
        }
        HASH_DEL(free_pool, i);
        mem_free(i);
    }
    free_pool = NULL;
}

void conn_set_req_handler(conn_req_handler handler) {
    req_handler = handler;
}

void conn_set_rsp_handler(conn_rsp_handler handler) {
    rsp_handler = handler;
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
    // Try to get connection from pool
    struct conn* conn = conn_addrpool_try_get_and_del(ep);
    if (conn) {
        PLOGD("Got conn from pool");
        conn->stat = CONN_USED;
        net_pull_set_handler(conn->fd, EPOLLIN | EPOLLOUT, NULL, NULL);
        return conn;
    }
    
    PLOGD("Try to get new connection");
    int fd = socket(ep->family, SOCK_STREAM, 0);
    if (fd < 0) {
        PLOGUE("connect");
        return NULL;
    }
    if (net_setnonblocking(fd) < 0)
        goto conn_error;
    
    // Connect
    int ret;
    if (ep->family == AF_INET) {
        struct sockaddr_in addr;
        memset(&addr, 0, sizeof(addr));
        addr.sin_family = AF_INET;
        addr.sin_port = ep->port;
        memcpy(&addr.sin_addr, &ep->addr.v4, sizeof(struct in_addr));
        ret = connect(fd, &addr, sizeof(struct sockaddr_in));
    } else {
        struct sockaddr_in6 addr;
        memset(&addr, 0, sizeof(addr));
        addr.sin6_family = AF_INET6;
        addr.sin6_port = ep->port;
        memcpy(&addr.sin6_addr, &ep->addr.v6, sizeof(struct in6_addr));
        ret = connect(fd, &addr, sizeof(struct sockaddr_in6));
    }
    
    if (ret < 0 && errno != EINPROGRESS)
        goto conn_error;
    
    conn = mem_alloc_auto(sizeof(struct conn));
    conn->stat = CONN_CLOSED;
    net_pull_add(fd, conn_err_handler, conn);
    if (ret < 0 && errno == EINPROGRESS) {
        // will block
        conn_notice notice;
        notice.io_block.fd = fd;
        notice.io_block.flag = EPOLLIN;
        int ret = async_yield(CONN_IO_WILL_BLOCK, &notice);
        if (ret == -1) {
            net_pull_del(fd);
            mem_decref(conn, NULL);
            return NULL;
        }
    }
    
    PLOGD("Connected");
    
    conn_init(conn, fd, ep);
    return conn;
    
    conn_error:
    PLOGD("Connection failed. Closing fd");
    // TODO: getsockopt() to get error type
    close(fd);
    return NULL;
}

static int
conn_notice_free_connection_closed(struct epoll_event ev, void* data) {
    struct conn* conn = (struct conn*) data;
    if (proxy_epoll_err(ev)) {
        PLOGI("Idle connection to %s closed", 
              ep_tostring(&conn->ep));
    } else {
        PLOGE("Someting strange happend on an idle connection to %s . Closing it",
              ep_tostring(&conn->ep));
        net_pull_set_handler(conn->fd, EPOLLIN,
                             conn_notice_free_connection_closed, conn);
        return 0;
    }
    conn_close(conn);
    conn_addrpool_del(conn);
    return 0;
}

void conn_free(struct conn* conn) {
    if (conn->stat == CONN_CLOSED || main_stat == EXITING)
        return;
    PLOGD("Adding conn to free connection pool");
    conn->stat = CONN_FREE;
    conn_addrpool_add(conn);
    net_pull_set_handler(conn->fd, EPOLLIN,
                         conn_notice_free_connection_closed, conn);
}

char* ep_tostring(struct conn_endpoint* ep) {
    static char buf[256];
    const char* ret = inet_ntop(ep->family, &ep->addr, buf+1, 255);
    if (ret == NULL)
        return strerror(errno);
    int end = strlen(buf+1)+1;
    if (ep->family == AF_INET6) {
        buf[0] = '[';
        sprintf(buf+end, "]:%d", ntohs(ep->port));
        return buf;
    }
    sprintf(buf+end, ":%d", ntohs(ep->port));
    return buf+1;
}
