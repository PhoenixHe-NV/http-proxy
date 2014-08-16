#include <stdio.h>

#include "conn.h"
#include "log.h"
#include "strbuf.h"
#include "conn.h"
#include "net_utils.h"
#include "net_handle.h"
#include "net_data.h"
#include "conn_io.h"
#include "constants.h"

#include "net_http.h"
#include "../csapp.h"


static int net_transfer_body_HTTP_1_1(struct conn* dst,
                             struct conn* src,
                             struct net_data* data) {
    if (strcmp(data->buf.p, "HEAD") == 0)
        return 0;
    
    int ret = 0;
    char* keyval = net_data_get_ent(data, "Transfer-Encoding");
    if (keyval == NULL || strcmp(keyval, "chunked")) {
        // Not chunked encoding
        keyval = net_data_get_ent(data, "Content-Length");
        int len = 0;
        if (keyval)
            len += atoi(keyval);
        if (len > 0) {
            ret = conn_copy(dst, src, len);
            if (ret)
                return ret;
        }
        return 0;
    }
    
    // Transfer-Encoding: chunked
    // FIXME HTTP rfc tells that proxy should forward decoded body
    struct strbuf buf;
    strbuf_init(&buf);
    while (1) {
        strbuf_reset(&buf);
        ret = conn_gets(src, HTTP_CHUNK_HEADER_MAXLEN, &buf);
        if (ret <= 0) {
            ret = -1;
            break;
        }
        
        strbuf_cat(&buf, "\r\n");
        ret = conn_write(dst, buf.p, buf.len);
        if (ret) {
            break;
        }
        
        int len = -1;
        sscanf(buf.p, "%x", &len);
        if (len == 0) {
            // Last chunck
            ret = 0;
            break;
        }
        
        // Also copy the \r\n
        ret = conn_copy(dst, src, len+2);
        if (ret)
            break;
    }
    if (ret == 0) {
        // Chunked trailer part
        while (1) {
            strbuf_reset(&buf);
            ret = conn_gets(src, HTTP_HEADER_MAXLEN, &buf);
            if (ret < 0)
                return ret;
            strbuf_cat(&buf, "\r\n");
            if (conn_write(dst, buf.p, buf.len)) {
                ret = -1;
                break;
            }
            if (ret == 0)
                break;
        }
    }
    strbuf_done(&buf);
    return ret;
}

static int net_http_req_handler(struct net_handle_cxt* cxt, 
                                struct net_req* req) {
    struct conn* client = cxt->client;
    
    PLOGD("Entering HTTP handler!");
    struct strbuf* buf = &req->data->buf;
    PLOGD("HTTP/%d.%d", req->ver_major, req->ver_minor);
    PLOGD("Method: %s", buf->p);
    PLOGD("Host: %s", buf->p + req->host);
    PLOGD("Port: %d", req->port);
    if (req->protocol > 0)
        PLOGD("Protocol: %s", buf->p + req->protocol);
    PLOGD("Path: %s", buf->p + req->path);

    PLOGD("From client: %s", ep_tostring(&client->ep));
    
//    net_bad_gateway(client);
//    return -1;

    struct conn* server = NULL;
    int ret = 0;
    while (1) {
        server = net_connect_to_server(cxt, buf->p + req->host, 
                                       htons(req->port));
        if (server == NULL) {
            // No server available
            net_bad_gateway(client);
            return -1;
        }
        
        // TODO Check headers and http version
        
        if (net_data_get_ent(req->data, "Host") == NULL)
            net_data_set_ent_offset(req->data, "Host", req->host);
        
        ret = net_forward_req_header(req, server);
        
        if (ret == 0)
            break;
        mem_decref(server, conn_done);
    }
    
    // Add req to the cxt list
    struct net_handle_list* p = mem_alloc(sizeof(struct net_handle_list));
    p->server = server;
    mem_incref(server);
    p->next = NULL;
    p->req = req;
    mem_incref(req);
    
    if (cxt->head == NULL)
        cxt->head = cxt->tail = p;
    else {
        cxt->tail->next = p;
        cxt->tail = p;
    }
    
    if (cxt->rsp_blocked)
        event_post(cxt->ev_notice_rsp, (void*)1);
    
    ret = net_transfer_body_HTTP_1_1(server, client, req->data);
    
    if (ret)
        net_bad_gateway(client);
    mem_decref(server, conn_done);
    return ret;
}

static int net_http_rsp_handler(struct net_handle_cxt* cxt,
                                struct conn* server) {
    struct conn* client = cxt->client;
    
    client->tx = 0;
    
    struct net_data* data = mem_alloc(sizeof(struct net_data));
    net_data_init(data);
    int ret = net_fetch_http(server, data);
    if (ret) {
        // Failed to fetch response from server
        conn_close(server);
        net_bad_gateway(client);
        net_data_done(data);
        mem_free(data);
        return ret;
    }
    
    struct net_rsp rsp;
    memset(&rsp, 0, sizeof(rsp));
    rsp.data = data;
    ret = net_parse_rsp(&rsp);
    if (ret) {
        conn_close(server);
        net_bad_gateway(client);
        net_rsp_done(&rsp);
        return ret;
    }
    
    // TODO check headers and http version
    
    ret = net_forward_rsp_header(&rsp, client);
    if (ret) {
        net_rsp_done(&rsp);
        return ret;
    }
    
    if (rsp.ver_major*100 + rsp.ver_minor > 100) {
        // Higher than http 1.0
        ret = net_transfer_body_HTTP_1_1(client, server, rsp.data);
    } else {
        PLOGD("Forwarding http 1.0 response");
        while (1) {
            ret = conn_copy(client, server, 64*1024);
            if (ret < 0) {
                ret = 0;
                break;
            }
        }
        conn_close(client);
        conn_close(server);
    }
    
    struct net_req* req = cxt->head->req;
    
    if (ret == 0)
        ret = rsp.code;
    
    log_http_req(&client->ep, req->data->buf.p + req->host, 
                 req->data->buf.p + req->path, client->tx);

    PLOGI("%s %s %s:%d%s %d", ep_tostring(&client->ep),
                            req->data->buf.p,
                            req->data->buf.p + req->host,
                            req->port,
                            req->data->buf.p + req->path,
                            client->tx);
    net_rsp_done(&rsp);
    return ret;
}

static int net_connect_req_handler(struct net_handle_cxt* cxt,
                                   struct net_req* req) {
    PLOGD("Entering CONNECT handler!");
    struct strbuf* buf = &req->data->buf;
    PLOGD("HTTP/%d.%d", req->ver_major, req->ver_minor);
    req->host = req->path;
    PLOGD("Host: %s", buf->p + req->host);
    PLOGD("Port: %d", req->port);

    struct conn* client = cxt->client;
    PLOGD("From client: %s", ep_tostring(&client->ep));
    
    struct conn* server = net_connect_to_server(cxt, buf->p + req->host, 
                                                htons(req->port));
    
    if (server == NULL) {
        // No server available
        net_bad_gateway(client);
        return -1;
    }
    
    struct net_handle_list* p = mem_alloc(sizeof(struct net_handle_list));
    p->server = server;
    mem_incref(server);
    p->next = NULL;
    p->req = req;
    mem_incref(req);
    
    if (cxt->head == NULL)
        cxt->head = cxt->tail = p;
    else {
        cxt->tail->next = p;
        cxt->tail = p;
    }
    
    event_post(cxt->ev_notice_rsp, (void*)1);
    
    static char msg[] = "HTTP/1.1 200 OK\r\n\r\n";
    int ret = conn_write(client, msg, strlen(msg));
    
    if (ret) {
        net_bad_gateway(client);
        mem_decref(server, conn_done);
        return -1;
    }
    
    while (1) {
        ret = conn_copy(server, client, 64*1024);
        if (ret)
            break;
    }
    
    PLOGI("%s CONNECT %s %d", ep_tostring(client), 
                              buf->p + req->host,
                              client->tx);
    mem_decref(server, conn_done);
    return ret;
}

static int net_connect_rsp_handler(struct net_handle_cxt* cxt,
                          struct conn* server) {
    struct conn* client = cxt->client;
    int ret;
    while (1) {
        ret = conn_copy(client, server, 64*1024);
        if (ret)
            break;
    }
    return ret;
}

void net_http_module_init() {
    net_handle_register("GET", net_http_req_handler, net_http_rsp_handler);
    net_handle_register("POST", net_http_req_handler, net_http_rsp_handler);
    net_handle_register("HEAD", net_http_req_handler, net_http_rsp_handler);
    net_handle_register("PUT", net_http_req_handler, net_http_rsp_handler);
    net_handle_register("DELETE", net_http_req_handler, net_http_rsp_handler);
    net_handle_register("CONNECT", net_connect_req_handler, 
                        net_connect_rsp_handler);
}
