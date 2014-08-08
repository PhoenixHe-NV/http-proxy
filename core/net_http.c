#include "conn.h"
#include "log.h"
#include "strbuf.h"
#include "conn.h"
#include "net_utils.h"
#include "net_handle.h"
#include "net_data.h"

#include "net_http.h"

static int net_http_handler(struct conn* client, struct net_req* req) {
    PLOGD("Entering HTTP handler!");
    struct strbuf* buf = &req->data->buf;
    PLOGD("HTTP/%d.%d", req->ver_major, req->ver_minor);
    PLOGD("Method: %s", buf->p);
    PLOGD("Host: %s", buf->p + req->host);
    PLOGD("Port: %d", req->port);
    if (req->protocol > 0)
        PLOGD("Protocol: %s", buf->p + req->protocol);
    PLOGD("Path: %s", buf->p + req->path);

    PLOGI("%s", ep_tostring(&client->ep));

    struct addrinfo *res, *rp;
    int ret = net_dns_lookup(buf->p + req->host, NULL, &res);
    if (ret) {
        PLOGUE("getaddrinfo");
        net_bad_gateway(client);
        return -1;
    }

    struct conn* server;
    for (rp = res; rp; rp = rp->ai_next) {
        struct conn_endpoint ep;
        memset(&ep, 0, sizeof(struct conn_endpoint));
        ep.family = rp->ai_family;
        ep.port = htons(req->port);

        if (ep.family == AF_INET)
            memcpy(&ep.addr, &((struct sockaddr_in*)rp->ai_addr)->sin_addr, sizeof(struct in_addr));
        else if (ep.family == AF_INET6)
            memcpy(&ep.addr, &((struct sockaddr_in6*)rp->ai_addr)->sin6_addr, sizeof(struct in6_addr));
        else {
            PLOGE("getaddrinfo returned unknown network family");
            continue;
        }

        server = conn_get_by_endpoint(&ep);
        if (server == NULL) {
            PLOGE("Cannot connect to %s", ep_tostring(&ep));
            continue;
        }

        PLOGD("Connected to %s", ep_tostring(&ep));
        break;
    }
    freeaddrinfo(res);

    conn_free(server);
    mem_decref(server, conn_close);
    return 0;
}

void net_http_module_init() {
    net_handle_register("GET", net_http_handler);
    net_handle_register("POST", net_http_handler);
    net_handle_register("HEAD", net_http_handler);
    net_handle_register("PUT", net_http_handler);
    net_handle_register("DELETE", net_http_handler);
}
