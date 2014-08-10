#ifndef _NET_HANDLE_H_
#define _NET_HANDLE_H_

#include <netinet/in.h>
#include <arpa/inet.h>
#include <stdint.h>

#include "net_data.h"
#include "conn.h"
#include "strbuf.h"
#include "utlib.h"
#include "constants.h"


struct conn_table {
    struct conn_endpoint ep;
    struct conn* server;
    UT_hash_handle hh;
};

struct dns_table {
    char host[DOMAIN_MAXLEN];
    struct addrinfo* result;
    UT_hash_handle hh;
};

struct net_handle_list {
    struct conn* server;
    // NOTE: autoptr for all the net_req*
    struct net_req* req;
    struct net_handle_list* next;
};

struct net_handle_cxt {
    struct conn* client;
    struct conn_table* pool;
    struct dns_table* dns_cache;
    struct net_handle_list *head, *tail;
    event_id ev_notice_req, ev_notice_rsp;
    uint8_t req_blocked, rsp_blocked, req_will_exit, rsp_will_exited;
};

typedef int (*net_req_handler_t)(struct net_handle_cxt* cxt, 
                               struct net_req* req);

typedef int (*net_rsp_handler_t)(struct net_handle_cxt* cxt,
                               struct conn* server);

void net_handle_module_init();

int net_fetch_headers(struct conn* conn, struct net_data* data);

int net_fetch_http(struct conn* conn, struct net_data* data);

void net_handle_register(char* method, 
                         net_req_handler_t req_handler,
                         net_rsp_handler_t rsp_handler);

struct conn* net_connect_to_server(struct net_handle_cxt* cxt,
                                   char* host, 
                                   in_port_t port);

void net_bad_request(struct conn* conn);

void net_bad_gateway(struct conn* conn);

#endif
