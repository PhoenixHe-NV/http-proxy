#include <sys/signalfd.h>
#include <errno.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/fcntl.h>
#include <netdb.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <signal.h>

#include "log.h"
#include "constants.h"
#include "conn.h"
#include "net_pull.h"
#include "utlib.h"

#include "net_utils.h"

int net_connect(struct sockaddr* addr, int port) {
    return -1;
}

int net_setnonblocking(int fd) {
    int opts = fcntl(fd, F_GETFL);
    if(opts < 0) {
        PLOGUE("fcntl(sock, GETFL)");
        return -1;
    }

    opts = opts | O_NONBLOCK;
    if (fcntl(fd, F_SETFL, opts) < 0) {
        PLOGUE("fcntl(sock, SETFL, opts|O_NONBLOCK)");
        return -1;
    }  

    return 0;
}

int proxy_epoll_err(struct epoll_event ev) {
    return ev.events & EPOLLERR || 
           ev.events & EPOLLHUP || 
           ev.events & EPOLLRDHUP;
}

const char* net_tostring(int family, void* addr) {
    static char buf[256];
    inet_ntop(family, addr, buf, 256);
    return buf;
}

static int signal_fd;

static int dns_notify_handler(struct epoll_event ev, void* data) {
    net_pull_set_handler(signal_fd, EPOLLIN, dns_notify_handler, NULL);
    struct signalfd_siginfo info;
    while (1) {
        int ret = read(signal_fd, &info, sizeof(info));
        if (ret == EAGAIN)
            break;
        if (!(ret == sizeof(struct signalfd_siginfo)))
            break;
        event_id eid = *(event_id*)info.ssi_ptr;
        PLOGD("Post dns event: %d", eid);
        event_post(eid, NULL);
    }
    return 0;
}

void net_utils_init() {
    sigset_t mask;
    sigemptyset(&mask);
    sigaddset(&mask, SIG_DNS_NOTIFY);
    sigprocmask(SIG_BLOCK, &mask, NULL);
    signal_fd = signalfd(-1, &mask, SFD_NONBLOCK);
    net_pull_add(signal_fd, NULL, NULL);
    net_pull_set_handler(signal_fd, EPOLLIN, dns_notify_handler, NULL);
}

int net_dns_lookup(char* host, char* service, struct addrinfo** result) {
    struct addrinfo hints;
    memset(&hints, 0, sizeof(struct addrinfo));
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    
    int ret = getaddrinfo(host, service, &hints, result);
    return ret;
}

struct dns_cache_table {
    char host[DOMAIN_MAXLEN];
    struct addrinfo* result;
    UT_hash_handle hh;
};

static struct dns_cache_table* dns_cache = NULL;

int net_dns_lookup_a(char* host, char* service, struct addrinfo** result) {
    struct dns_cache_table* h = NULL;
    HASH_FIND_STR(dns_cache, host, h);
    
    if (h) {
        *result = h->result;
        return 0;
    }
    
    struct addrinfo hints;
    memset(&hints, 0, sizeof(struct addrinfo));
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    
    //int ret = getaddrinfo(host, service, &hints, result);
    struct gaicb req, *p = &req;
    req.ar_name = host;
    req.ar_service = service;
    req.ar_request = &hints;
    req.ar_result = NULL;
    
    event_id eid = event_get_id();
    struct sigevent ev;
    ev.sigev_notify = SIGEV_SIGNAL;
    ev.sigev_signo = SIG_DNS_NOTIFY;
    ev.sigev_value.sival_ptr = &eid;
    
    int ret = getaddrinfo_a(GAI_NOWAIT, &p, 1, &ev);
    if (ret) {
        PLOGE("getaddrinfo_a: %s", gai_error(&req));
        goto dns_final;
    }
    conn_notice notice;
    notice.wait_event.eid = eid;
    PLOGD("dns block on event %d", eid);
    async_yield(CONN_WAIT_FOR_EVENT, &notice);
    
dns_final:
    event_free_id(eid);
    *result = req.ar_result;
    
    if (req.ar_result) {
        h = mem_alloc(sizeof(struct dns_cache_table));
        h->result = req.ar_result;
        strncpy(&h->host, host, DOMAIN_MAXLEN);
        HASH_ADD_STR(dns_cache, host, h);
    }
    
    return ret;
}
