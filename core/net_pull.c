#include <assert.h>

#include "event.h"

#include "utlib.h"
#include "net_pull.h"
#include "net_utils.h"
#include "mem.h"
#include "log.h"

#define EPOLL_EVENT_MAX 128

struct net_handler_t {
    int fd;
    net_pull_handler_t h_read, h_write, h_err;
    void *d_read, *d_write, *d_err;
    UT_hash_handle hh;
};

static struct net_handler_t *handlers = NULL;

static int epollfd = -1, fd_cnt = 0;

int net_pull_init() {
    epollfd = epoll_create(1);
    if (epollfd < 0) {
        PLOGUE("epoll_create");
        return -1;
    }
    return 0;
}

int net_pull_work() {
    if (epollfd < 0)
        return -1;

    if (fd_cnt == 0) {
        PLOGI("No fd added to epoll. Exiting.");
        return -1;
    }

    PLOGD("Calling epoll_wait(). fd_cnt: %d", fd_cnt);
    struct epoll_event events[EPOLL_EVENT_MAX];
    int nfds = epoll_wait(epollfd, events, EPOLL_EVENT_MAX, -1);
    if (nfds == -1) {
        PLOGUE("epoll_wait");
        return -1;
    }

    int i;
    for (i = 0; i < nfds; ++i) {
        int fd = events[i].data.fd;
        PLOGD("Handling event: %d fd: %d", events[i].events, fd);
        struct net_handler_t* h, nh;
        HASH_FIND_INT(handlers, &fd, h);
        memcpy(&nh, h, sizeof(struct net_handler_t));
        h->h_read = h->h_write = h->d_read = h->d_write = NULL;
        
        if (proxy_epoll_err(events[i]) && nh.h_err)
            nh.h_err(events[i], nh.d_err);
        
        if (nh.h_read) {
            PLOGD("READ handler for fd %d", fd);
            nh.h_read(events[i], nh.d_read);
        }

        if (nh.h_write) {
            PLOGD("WRITE handler for fd %d", fd);
            nh.h_write(events[i], nh.d_write);
        }
    }

    return 0;
}

int net_pull_add(int fd, net_pull_handler_t h_err, void* d_err) {
    PLOGD("Adding fd: %d", fd);

    struct epoll_event ev;
    memset(&ev, 0, sizeof(struct epoll_event));
    ev.data.fd = fd;
    ev.events = EPOLLIN | EPOLLOUT | EPOLLRDHUP | EPOLLET;
    if (epoll_ctl(epollfd, EPOLL_CTL_ADD, fd, &ev) < 0) {
        PLOGUE("epoll_ctl: add fd");
        return -1;
    }

    struct net_handler_t* h;
    HASH_FIND_INT(handlers, &fd, h);
    if (h == NULL) {
        h = mem_alloc(sizeof(struct net_handler_t));
        h->fd = fd;
        h->h_read = h->h_write = NULL;
        h->h_err = h_err;
        h->d_err = d_err;
        HASH_ADD_INT(handlers, fd, h);
    }
    ++fd_cnt;
    return 0;
}

int net_pull_set_handler(int fd, int flag, net_pull_handler_t handler, void* data) {
    PLOGD("Setting handler with fd: %d", fd);
    struct net_handler_t* h;
    HASH_FIND_INT(handlers, &fd, h);

    if (flag & EPOLLIN) {
        PLOGD("set READ handler for fd: %d", fd);
        h->h_read = handler;
        h->d_read = data;
    }
    if (flag & EPOLLOUT) {
        PLOGD("set WRITE handler for fd: %d", fd);
        h->h_write = handler;
        h->d_write = data;
    }
    return 0;
}

int net_pull_del(int fd) {
    PLOGD("Deleting fd: %d", fd);
    struct net_handler_t* h;
    HASH_FIND_INT(handlers, &fd, h);
    if (h == NULL) {
        PLOGD("Delete an non-existing fd to event listener");
        return -1;
    }

    if (epoll_ctl(epollfd, EPOLL_CTL_DEL, fd, NULL) < 0) {
        PLOGUE("epoll_ctl: del fd");
        return -1;
    }
    
    HASH_DEL(handlers, h);
    mem_free(h);
    --fd_cnt;
    return 0;
}

int net_pull_done() {
    struct net_handler_t *h, *tmp, **v, **p, **i;
    PLOGD("Closing all the connections");
    // Copy all the handler pointer out
    int cnt = HASH_COUNT(handlers);
    if (cnt == 0)
        return 0;
    v = (struct net_handler_t**) mem_alloc(cnt*sizeof(struct net_handler_t**));
    p = v;
    HASH_ITER(hh, handlers, h, tmp)
        *p++ = h;
    HASH_CLEAR(hh, handlers);

    struct epoll_event close_ev;
    close_ev.events = EPOLLERR;
    for (i = v; i != p; ++i) {
        struct net_handler_t nh;
        memcpy(&nh, *i, sizeof(struct net_handler_t));
        
        if (nh.h_err)
            nh.h_err(close_ev, nh.d_err);

        if (nh.h_read)
            nh.h_read(close_ev, nh.d_read);

        if (nh.h_write)
            nh.h_write(close_ev, nh.d_write);

        mem_free(*i);
    }
    
    mem_free(v);
    return 0;
}
