#include <ctype.h>
#include <assert.h>

#include "conn.h"
#include "log.h"
#include "mem.h"
#include "string.h"
#include "conn_io.h"
#include "net_utils.h"
#include "constants.h"
#include "main.h"

#include "net_handle.h"

struct net_handlers {
    char* method;
    net_req_handler_t req_handler;
    net_rsp_handler_t rsp_handler;
} handlers[CONN_MAX_HANDLER_NUM];

void net_handle_register(char* method, 
                         net_req_handler_t req_handler, 
                         net_rsp_handler_t rsp_handler) {
    for (int i = 0; i < CONN_MAX_HANDLER_NUM; ++i)
        if (handlers[i].method == NULL) {
            handlers[i].method = method;
            handlers[i].req_handler = req_handler;
            handlers[i].rsp_handler = rsp_handler;
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

static struct net_handlers* net_get_handler(char* method) {
    for (int i = 0; i < CONN_MAX_HANDLER_NUM; ++i) {
        if (handlers[i].method == NULL)
            break;
        if (method_strcmp(handlers[i].method, method) == 0)
            return handlers + i;
    }
    PLOGE("Could not find handler for %s . Bad request.", method);
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

int net_fetch_http(struct conn* conn, struct net_data* data) {
    // Fetch startline
    int ret = conn_gets(conn, HTTP_HEADER_MAXLEN, &data->buf);
    if (ret <= 0) {
        PLOGD("Cannot fetch startline");
        return ret;
    }
    strbuf_append(&data->buf, '\0');
    
    // Fetch headers
    ret = net_fetch_headers(conn, data);
    if (ret) {
        PLOGD("Cannot fetch headers");
        return ret;
    }
    
    return 0;
}

static int net_req_handler(struct conn* client, void** data_ptr) {
    mem_incref(client);
    
    // Init net_handle_cxt
    struct net_handle_cxt cxt;
    cxt.client = client;
    cxt.pool = NULL;
    cxt.dns_cache = NULL;
    cxt.head = cxt.tail = NULL;
    cxt.ev_notice_req = event_get_id();
    cxt.ev_notice_rsp = event_get_id();
    cxt.req_blocked = 0;
    cxt.rsp_blocked = 0;
    cxt.req_will_exit = 0;
    cxt.rsp_will_exited = 0;
    *data_ptr = &cxt;
    
    // Register to ev_notice_req, and resume immediately
    event_post(cxt.ev_notice_req, (void*)1);
    conn_notice notice;
    notice.wait_event.eid = cxt.ev_notice_req;
    cxt.req_blocked = 1;
    async_yield(CONN_WAIT_FOR_EVENT, &notice);
    cxt.req_blocked = 0;
    
    int ret = 0;
    // Fetch request
    while (1) {
        if (client->stat == CONN_CLOSED || 
                main_stat == EXITING)
            break;
        
        // NOTICE: data is held in net_req, will be freed in net_req_done
        struct net_data* data = mem_alloc(sizeof(struct net_data));
        net_data_init(data);
        
        ret = net_fetch_http(client, data);
        if (ret) {
            net_bad_request(client);
            net_data_done(data);
            mem_free(data);
            goto req_done;
        }

        struct net_handlers* h = net_get_handler(data->buf.p);
        if (h == NULL) {
            net_bad_request(client);
            net_data_done(data);
            mem_free(data);
            ret = -1;
            goto req_done;
        }

        struct net_req* req = mem_alloc_auto(sizeof(struct net_req));
        req->data = data;
        ret = net_parse_req(req);
        if (ret) {
            PLOGD("Cannot parse headers");
            net_bad_request(client);
            mem_decref(req, net_req_done);
            goto req_done;
        }
        
        ret = h->req_handler(&cxt, req);
        
        if (ret) {
            mem_decref(req, net_req_done);
            goto req_done;
        }
        
        mem_decref(req, net_req_done);
    }
    
req_done:
    
    cxt.req_will_exit = 1;

    if (!cxt.rsp_will_exited) {
        event_post(cxt.ev_notice_rsp, 0);
        cxt.req_blocked = 1;
        async_yield(CONN_WAIT_FOR_EVENT, &notice);
        cxt.req_blocked = 0;
    }
    
    // Free dns cache
    struct dns_table *hd, *td;
    HASH_ITER(hh, cxt.dns_cache, hd, td) {
        freeaddrinfo(hd->result);
        HASH_DEL(cxt.dns_cache, hd);
        mem_free(hd);
    }
    
    // Return all the free connection to conn_pool
    struct conn_table *h, *tmp;
    HASH_ITER(hh, cxt.pool, h, tmp) {
        // conn_free(h->server);
        mem_decref(h->server, conn_done);
        HASH_DEL(cxt.pool, h);
        mem_free(h);
    }
    
    // Close all the using connection
    struct net_handle_list *p = cxt.head, *q;
    while (p) {
        q = p->next;
        mem_decref(p->server, conn_done);
        mem_decref(p->req, net_req_done);
        mem_free(p);
        p = q;
    }
    
    // Free event id
    event_free_id(cxt.ev_notice_req);
    
    mem_decref(client, conn_done);
    PLOGD("REQ_HANDLER RETURNED");
    return ret;
}

static int net_rsp_handler(void* data) {
    struct net_handle_cxt* cxt = (struct net_handle_cxt*) data;
    struct conn* client = cxt->client;
    mem_incref(client);
    
    // Register to ev_notice_rsp, and resume immediately
    event_post(cxt->ev_notice_rsp, (void*)1);
    conn_notice notice;
    notice.wait_event.eid = cxt->ev_notice_rsp;
    cxt->rsp_blocked = 1;
    async_yield(CONN_WAIT_FOR_EVENT, &notice);
    cxt->rsp_blocked = 0;
    
    int ret = 0;
    while (1) {
        if (client->stat == CONN_CLOSED || 
                main_stat == EXITING)
            break;
        
        struct net_handle_list* h = cxt->head;
        if (h == NULL) {
            // Null request list. Waiting for req_handler
            cxt->rsp_blocked = 1;
            ret = async_yield(CONN_WAIT_FOR_EVENT, &notice);
            cxt->rsp_blocked = 0;
            if (ret)
                break;
            continue;
        }
        
        struct net_handlers* h0 = net_get_handler(h->req->data->buf.p);
        ret = h0->rsp_handler(cxt, h->server);
        
        if (h->server->stat == CONN_CLOSED) {
            // Delete server connection from the pool
            struct conn_table* t;
            HASH_FIND(hh, cxt->pool, 
                      &h->server->ep, sizeof(struct conn_endpoint), t);
            HASH_DEL(cxt->pool, t);
            mem_decref(h->server, conn_done);
            mem_free(t);
        } else if (ret == 100) {
            // HTTP 100 CONTINUE
            continue;
        }
        
        mem_decref(h->server, conn_done);
        mem_decref(h->req, net_req_done);
        cxt->head = h->next;
        if (cxt->head == NULL)
            cxt->tail = NULL;
        mem_free(h);
    }
    
    cxt->req_will_exit = 1;
    
    if (cxt->req_blocked)
        event_post(cxt->ev_notice_req, 0);
    
    event_free_id(cxt->ev_notice_rsp);
    
    mem_decref(client, conn_done);
    PLOGD("RSP_HANDLER RETURNED");
    return ret;
}

void net_handle_module_init() {
    memset(handlers, 0, sizeof(handlers));
    conn_set_req_handler(net_req_handler);
    conn_set_rsp_handler(net_rsp_handler);
}

struct conn* net_connect_to_server(struct net_handle_cxt* cxt,
                                   char* host, 
                                   in_port_t port) {
    
    struct addrinfo *res, *rp;
    
    // Try to get result from dns cache
    struct dns_table* h;
    HASH_FIND_STR(cxt->dns_cache, host, h);
    if (h) {
        res = h->result;
    } else {
        int ret = net_dns_lookup(host, NULL, &res);
        if (ret) {
            PLOGUE("getaddrinfo");
            return NULL;
        }
        h = mem_alloc(sizeof(struct dns_table));
        strcpy(h->host, host);
        h->result = res;
        HASH_ADD_STR(cxt->dns_cache, host, h);
    }

    // Try all the dns results. Try to get server connection.
    struct conn* server = NULL;
    struct conn_table* t = NULL;
    for (rp = res; rp; rp = rp->ai_next) {
        if (main_stat == EXITING)
            break;
            
        struct conn_endpoint ep;
        memset(&ep, 0, sizeof(ep));
        ep.family = rp->ai_family;
        ep.port = port;

        if (ep.family == AF_INET)
            memcpy(&ep.addr, 
                   &((struct sockaddr_in*)rp->ai_addr)->sin_addr,
                   sizeof(struct in_addr));
        else if (ep.family == AF_INET6)
            memcpy(&ep.addr,
                   &((struct sockaddr_in6*)rp->ai_addr)->sin6_addr, 
                   sizeof(struct in6_addr));
        else {
            PLOGE("getaddrinfo returned unknown network family");
            continue;
        }
        
        // Try local connection pool first
        HASH_FIND(hh, cxt->pool, &ep, sizeof(ep), t);
        if (t) {
            server = t->server;
            if (server->stat == CONN_CLOSED) {
                HASH_DEL(cxt->pool, t);
                mem_decref(server, conn_done);
                mem_free(t);
                PLOGD("Server closed connection.");
            } else {
                PLOGD("Got server connection from cxt pool");
                mem_incref(server);
                break;
            }
        }

        // Try to get from global pool
        server = conn_get_by_endpoint(&ep);
        if (server == NULL) {
            PLOGE("Cannot connect to %s", ep_tostring(&ep));
            continue;
        }
        
        if (server->stat == CONN_CLOSED) {
            PLOGE("Server connection closed");
            mem_decref(server, conn_done);
        }

        PLOGD("Got server connection from free pool");
        
        // Add connection to cxt pool
        t = mem_alloc(sizeof(struct conn_table));
        memcpy(&t->ep, &ep, sizeof(ep));
        t->server = server;
        PLOGD("ADD SERVER TO CONN POOL");
        HASH_ADD(hh, cxt->pool, ep, sizeof(struct conn_endpoint), t);
        // Server in the pool, server will be returned
        mem_incref(server);
        break;
    }
    
    return server;
}


void net_bad_request(struct conn* conn) {
    if (conn->stat == CONN_CLOSED)
        return;
    // TODO: Return a standard html page
    static char msg[] = 
        "HTTP/1.1 400 Bad Request\r\nServer: http-proxy\r\n\r\n";
    conn_write(conn, msg, strlen(msg));
    conn_close(conn);
}

void net_bad_gateway(struct conn* conn) {
    if (conn->stat == CONN_CLOSED)
        return;
    static char msg[] = 
        "HTTP/1.1 502 Bad Gateway\r\nServer: http-proxy\r\n\r\n";
    conn_write(conn, msg, strlen(msg));
}
