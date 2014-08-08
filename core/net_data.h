#ifndef _PROXY_NET_DATA_H_
#define _PROXY_NET_DATA_H_

#include "utlib.h"
#include "constants.h"
#include "strbuf.h"

struct net_header_ent {
    char key[HTTP_HEADER_KEY_MAXLEN];
    // Store all the value in net_data.buf
    // This is the value offset
    int val_offset;
    UT_hash_handle hh;
};

struct net_data {
    // The first thing in buf is startline
    struct strbuf buf;
    struct net_header_ent* table;
};

struct net_req {
    struct net_data* data;
    int port, ver_major, ver_minor;
    // offset of key in net_req.data->buf
    // Method always start at pos 0
    int host, protocol, path;
    // NOTICE: host will not be end with '\0'
    // because it may overrite the first 
    // char in path
};

struct net_rsp {
    struct net_data* data;
    int code, ver_major, ver_minor;
    // offset in net_rsp.data->buf
    int http_msg;
};

void net_data_module_init();

void net_data_module_done();

void net_data_init(struct net_data* data);

void net_data_done(struct net_data* data);

char* net_data_get_ent(struct net_data* data, char* key);

int net_data_get_ent_offset(struct net_data* data, char* key);

void net_data_del_ent(struct net_data* data, char* key);

void net_data_set_ent(struct net_data* data, char* key, char* val);

void net_data_set_ent_offset(struct net_data* data, char* key, int offset);

int net_parse_header(struct net_data* data, int offset);

int net_parse_req(struct net_req* req);

int net_parse_rsp(struct net_rsp* rsp);

#endif
