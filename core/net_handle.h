#ifndef _NET_HANDLE_H_
#define _NET_HANDLE_H_

#include "conn.h"
#include "strbuf.h"
#include "net_data.h"

typedef int (*net_handler_t)(struct conn* conn, struct net_req* req);

void net_handle_module_init();

int net_fetch_headers(struct conn* conn, struct net_data* data);

void net_handle_register(char* method, net_handler_t handler);

void net_bad_request(struct conn* conn);

int net_fetch_headers(struct conn* conn, struct net_data* data);

#endif
