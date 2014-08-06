#include "conn.h"
#include "log.h"
#include "strbuf.h"
#include "net_utils.h"
#include "net_handle.h"
#include "net_data.h"

#include "net_http.h"

static int net_http_handler(struct conn* conn, struct net_req* req) {
    PLOGD("Entering HTTP handler!");
    struct strbuf* buf = &req->data->buf;
    PLOGD("HTTP/%d.%d", req->ver_major, req->ver_minor);
    PLOGD("Method: %s", buf->p);
    PLOGD("Host: %s", buf->p + req->host);
    PLOGD("Port: %d", req->port);
    PLOGD("Protocol: %s", buf->p + req->protocol);
    PLOGD("Path: %s", buf->p + req->path);

    PLOGI("%s", proxy_net_tostring(&conn->addr));

    return 0;
}

void net_http_module_init() {
    net_handle_register("GET", net_http_handler);
    net_handle_register("POST", net_http_handler);
    net_handle_register("HEAD", net_http_handler);
    net_handle_register("PUT", net_http_handler);
    net_handle_register("DELETE", net_http_handler);
}
