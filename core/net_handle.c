#include "conn.h"
#include "log.h"
#include "mem.h"
#include "string.h"
#include "conn_io.h"

#include "net_handle.h"

static void net_req_init(struct net_req* req) {
    memset(req, 0 sizeof(struct net_req));
    utstring_new(req->st);
}

void net_req_fina(struct net_req* req) {
    utstring_free(req->st);
}

static int
net_conn_handler(struct conn* conn) {
    PLOGD("Entering NET HANDLER!!");
    PLOGD("fd:%d", conn->fd);
    mem_incref(conn);

    int ret = 0;
    net_req req;
    net_req_init(&req);
    while (1) {
        ret = net_fetch_req(conn, &req);
        if (ret) break;
        net_handler_t handler = get_handler(utstring_body(req->st));
        if (handler == NULL) {
            ret = -1;
            break;
        }
        handler(conn, &req);
    }

    mem_decref(conn, conn_fina);
    return ret;
}

void net_handle_module_init() {
    conn_set_accept_handler(net_conn_handler);
}

