#include <assert.h>

#include "event.h"

#include "utlib.h"
#include "net_pull.h"
#include "mem.h"
#include "log.h"

#define EPOLL_EVENT_MAX 128

struct net_handler_t {
    int fd;
    net_pull_handler_t handler;
    void *data;
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
    PLOGD("epoll_wait() returned");

    int i;
    for (i = 0; i < nfds; ++i) {
        int fd = events[i].data.fd;
        PLOGD("Handling event: %d fd: %d", i, fd);
        struct net_handler_t* h;
        HASH_FIND_INT(handlers, &fd, h);
        if (!h || !(h->handler)) {
            PLOGF("NULL handler!!");
            continue;
        }
        PLOGD("Dispatch handler");
        net_pull_handler_t handler = h->handler;
        void* data = h->data;
        h->handler = NULL;
        h->data = NULL;
        if (handler(events[i], data) == 0) {
            // Keep the handler
            h->handler = handler;
            h->data = data;
        }
    }

    return 0;
}

int net_pull_add(int fd, int event) {
    PLOGD("Adding fd: %d event: %d", fd, event);

    struct epoll_event ev;
    ev.data.fd = fd;
    ev.events = event | EPOLLET;
    if (epoll_ctl(epollfd, EPOLL_CTL_ADD, fd, &ev) < 0) {
        PLOGUE("epoll_ctl: add fd");
        return -1;
    }

    struct net_handler_t* h;
    HASH_FIND_INT(handlers, &fd, h);
    if (h == NULL) {
        h = mem_alloc(sizeof(struct net_handler_t));
        h->fd = fd;
        h->handler = h->data = NULL;
        HASH_ADD_INT(handlers, fd, h);
    }
    ++fd_cnt;
    return 0;
}

int net_pull_set_handler(int fd, net_pull_handler_t handler, void* data) {
    PLOGD("Setting handler with fd: %d", fd);
    struct net_handler_t* h;
    HASH_FIND_INT(handlers, &fd, h);
    h->handler = handler;
    h->data = data;
    return 0;
}

int net_pull_del(int fd) {
    PLOGD("Deleting fd: %d", fd);
    struct net_handler_t* h;
    HASH_FIND_INT(handlers, &fd, h);
    assert(h && "Delete an non-existing fd to event listener");

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
    struct net_handler_t *h, *tmp;
    PLOGD("Notice all the handler to close connections");
    struct epoll_event close_ev;
    close_ev.events = EPOLLERR;
    HASH_ITER(hh, handlers, h, tmp) {
        if (h->handler)
            h->handler(close_ev, h->data);
    }
    return 0;
}
