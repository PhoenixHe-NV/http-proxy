#ifndef _NET_HANDLE_H_
#define _NET_HANDLE_H_

#include "conn.h"

struct net_req {
    UT_string* st;
    int method, prot, host, uri, http_ver, port;
};

typedef int (*net_handler_t)(struct conn* conn, struct net_req* req);

void net_req_fina(struct net_req* req);


void net_handle_module_init();

void net_handle_register(char* method, net_handler_t handler);

#endif
