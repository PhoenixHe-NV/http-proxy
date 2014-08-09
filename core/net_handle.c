#include <ctype.h>

#include "conn.h"
#include "log.h"
#include "mem.h"
#include "string.h"
#include "conn_io.h"
#include "constants.h"

#include "net_handle.h"

struct net_handlers {
    char* method;
    net_handler_t handler;
} handlers[CONN_MAX_HANDLER_NUM];

void net_handle_register(char* method, net_handler_t handler) {
    for (int i = 0; i < CONN_MAX_HANDLER_NUM; ++i)
        if (handlers[i].method == NULL) {
            handlers[i].method = method;
            handlers[i].handler = handler;
            return;
    }
    PLOGF("Handlers reach max num limit! Handler for method %s is not registered", method);
}

static int method_strcmp(char* m0, char* m1) {
    while (*m0 && *m1 && *m1 != ' ') {
        if (*m0 != *m1)
            return -1;
        ++m0; ++m1;
    }
    if ((*m0 == '\0') ^ (*m1 == '\0'|| *m1 == ' '))
        return -1;
    return 0;
}

static net_handler_t net_get_handler(char* method) {
    for (int i = 0; i < CONN_MAX_HANDLER_NUM; ++i) {
        if (handlers[i].method == NULL)
            break;
        if (method_strcmp(handlers[i].method, method) == 0)
            return handlers[i].handler;
    }
    PLOGE("Could not find handler. Bad request.");
    return NULL;
}

int net_fetch_headers(struct conn* conn, struct net_data* data) {
    int ret;
    while (1) {
        int p0 = data->buf.len;
        ret = conn_gets(conn, HTTP_HEADER_MAXLEN, &data->buf);
        if (ret <= 0)
            return ret;
        strbuf_append(&data->buf, '\0');
        ret = net_parse_header(data, p0);
        if (ret)
            return -1;
    }
    return 0;
}

/*
static int net_conn_handler(struct conn* conn) {
    PLOGD("Entering NET HANDLER!!");
    PLOGD("fd:%d", conn->fd);
    mem_incref(conn);

    int ret = 0;
    struct net_data data;
    net_data_init(&data);
    while (1) {
        ret = conn_gets(conn, HTTP_HEADER_MAXLEN, &data.buf);
        if (ret <= 0) {
            PLOGD("Cannot parse startline");
            net_bad_request(conn);
            ret = -1;
            break;
        }

        net_handler_t handler = net_get_handler(data.buf.p);
        if (handler == NULL) {
            net_bad_request(conn);
            ret = -1;
            break;
        }
        strbuf_append(&data.buf, '\0');

        ret = net_fetch_headers(conn, &data);
        if (ret) {
            net_bad_request(conn);
            break;
        }

        struct net_req req;
        req.data = &data;
        ret = net_parse_req(&req);
        if (ret) {
            PLOGD("Cannot parse headers");
            net_bad_request(conn);
            break;
        }

        if (handler(conn, &req))
            break;

        net_data_done(&data);
        net_data_init(&data);
    }
    net_data_done(&data);

    mem_decref(conn, conn_done);
    return ret;
}
*/

static int net_req_handler(struct conn* conn) {
}

static int net_rsp_handler(struct conn* conn, void* data) {
}

void net_handle_module_init() {
    memset(handlers, 0, sizeof(handlers));
    conn_set_accept_handler(net_conn_handler);
}

void net_bad_request(struct conn* conn) {
    if (conn->stat == CONN_CLOSED)
        return;
    // TODO: Return a standard html page
    char msg[] = "400 Bad Request\r\nServer: http-proxy\r\n\r\n";
    conn_write(conn, msg, strlen(msg));
    conn_close(conn);
}

void net_bad_gateway(struct conn* conn) {
    if (conn->stat == CONN_CLOSED)
        return;
    char msg[] = "502 Bad Gateway\r\nServer: http-proxy\r\n\r\n";
    conn_write(conn, msg, strlen(msg));
}
