#include <ctype.h>
#include <regex.h>
#include <stdlib.h>

#include "mem.h"

#include "net_data.h"

void net_data_init(struct net_data* data) {
    strbuf_init(&data->buf);
    data->table = NULL;
}

void net_data_done(struct net_data* data) {
    strbuf_done(&data->buf);
    struct net_header_ent *h, *tmp;
    HASH_ITER(hh, data->table, h, tmp) {
        mem_free(h);
    }
    HASH_CLEAR(hh, data->table);
}

char* net_data_get_ent(struct net_data* data, char* key) {
    struct net_header_ent* h;
    HASH_FIND_STR(data->table, key, h);
    if (h == NULL)
        return NULL;
    return data->buf.p + h->val_offset;
}

int net_data_get_ent_offset(struct net_data* data, char* key) {
    struct net_header_ent* h;
    HASH_FIND_STR(data->table, key, h);
    if (h == NULL)
        return -1;
    return h->val_offset;
}

void net_data_del_ent(struct net_data* data, char* key) {
    struct net_header_ent* h;
    HASH_FIND_STR(data->table, key, h);
    if (h == NULL)
        return;
    HASH_DEL(data->table, h);
    mem_free(h);
}

void net_data_set_ent(struct net_data* data, char* key, char* val) {
    struct net_header_ent* h;
    HASH_FIND_STR(data->table, key, h);
    if (h == NULL) {
        h = mem_alloc(sizeof(struct net_header_ent));
        strncpy(h->key, key, HTTP_HEADER_KEY_MAXLEN);
        HASH_ADD_STR(data->table, key, h);
    }
    h->val_offset = data->buf.len;
    while (*val)
        strbuf_append(&data->buf, *val);
    strbuf_append(&data->buf, '\0');
}

void net_data_set_ent_offset(struct net_data* data, char* key,int offset) {
    struct net_header_ent* h;
    HASH_FIND_STR(data->table, key, h);
    if (h == NULL) {
        h = mem_alloc(sizeof(struct net_header_ent));
        strncpy(h->key, key, HTTP_HEADER_KEY_MAXLEN);
        HASH_ADD_STR(data->table, key, h);
    }
    h->val_offset = offset;
}

// Connection: keep-alive
// ^        $  ^        $
static char* header_patt =  "(\\w+): *(.*)$";
static regex_t header_re;

// GET http://www.google.com:80/?q=http-proxy HTTP/1.1
// ^ $ ^                                    $      ^ ^
static char* req_patt = "^([a-zA-Z]+) (.+) HTTP/([0-9]+)\\.([0-9]+)$";
static regex_t req_re;

// http://www.google.com:80/?q=http-proxy
// ^  $   ^               $^            $
static char* uri_patt = "^([a-z]+)://([a-z0-9\\-\\.]+)(:[0-9]+)?(\\/.*)?$";
static regex_t uri_re;

// HTTP/1.1 200 OK
//      ^ ^ ^ $ ^$
static char* rsp_patt = "^HTTP/([0-9]+)\\.([0-9]+) ([0-9]+) (.+)$";
static regex_t rsp_re;

// www.google.com:80
// ^            $ ^$
static char* host_patt = "^([a-z0-9\\-\\.]+)(:[0-9]+)?";
static regex_t host_re;

void net_data_module_init() {
    PLOGD("Compiling regex");
    int ret;
    ret = regcomp(&header_re, header_patt, REG_EXTENDED);
    ret |= regcomp(&req_re, req_patt, REG_EXTENDED);
    ret |= regcomp(&uri_re, uri_patt, REG_EXTENDED);
    ret |= regcomp(&rsp_re, rsp_patt, REG_EXTENDED);
    ret |= regcomp(&host_re, host_patt, REG_EXTENDED);
    PLOGD("%d", ret);
}

void net_data_module_done() {
    regfree(&header_re);
    regfree(&req_re);
    regfree(&uri_re);
    regfree(&rsp_re);
    regfree(&host_re);
}

int net_parse_header(struct net_data* data, int offset) {
    regmatch_t res[4];
    struct strbuf* buf = &data->buf;
    // Connection: keep-alive
    // ^        $  ^        $
    if (regexec(&header_re, buf->p + offset, 4, res, 0))
        return -1;
    buf->p[offset + res[1].rm_eo] = '\0';
    buf->p[offset + res[2].rm_eo] = '\0';
    net_data_set_ent_offset(data, 
            buf->p + offset + res[1].rm_so, offset + res[2].rm_so);
    return 0;
}


int net_parse_req(struct net_req* req) {
    regmatch_t res[8];
    int cnt = 8;
    struct strbuf* buf = &req->data->buf;

    // First, parse the startline
    // GET http://www.google.com:80/?q=http-proxy HTTP/1.1
    // ^ $ ^                                    $      ^ ^
    if (regexec(&req_re, buf->p, cnt, res, 0))
        return -1;

    buf->p[res[1].rm_eo] = '\0';
    buf->p[res[2].rm_eo] = '\0';
    buf->p[res[3].rm_eo] = '\0';
    buf->p[res[4].rm_eo] = '\0';

    // Get the http version
    req->ver_major = atoi(buf->p + res[3].rm_so);
    req->ver_minor = atoi(buf->p + res[4].rm_so);

    // Distinguish between absolute uri and relative uri
    req->path = res[2].rm_so;
    // http://www.google.com:80/?q=http-proxy
    // ^  $   ^            $^ $^            $
    int offset = res[2].rm_so;
    if (regexec(&uri_re, buf->p + res[2].rm_so, cnt, res, 0)) {
        // Failed, uri in startline is not absolute uri
        // Get from header
        req->host = net_data_get_ent_offset(req->data, "Host");
        if (req->host == -1) {
            PLOGE("Cannot find host");
            return -1;
        }
        req->protocol = -1;
    } else {
        req->protocol = offset + res[1].rm_so;
        buf->p[offset + res[1].rm_eo] = '\0';
        req->host = offset + res[2].rm_so;
        req->path = offset + res[4].rm_so;
        if (res[4].rm_so == -1) {
            req->path = buf->len;
            // Default path is '/'
            strbuf_cat(buf, "/\0");
        }
    }

    // Prase the host
    // www.google.com:80
    // ^            $^ $
    offset = req->host;
    if (regexec(&host_re, buf->p + req->host, cnt, res, 0)) {
        PLOGE("Error host format");
        return -1;
    }
    if (res[1].rm_eo + offset == req->path) {
        req->host = buf->len;
        // Copy host to end of buf
        for (int i = res[1].rm_so; i < res[1].rm_eo; ++i)
            strbuf_append(buf, buf->p[offset + i]);
        strbuf_append(buf, '\0');
    }
    if (res[2].rm_so == -1) {
        if (req->protocol == -1)
            req->port = HTTP_DEFAULT_PORT;
        else {
            // TODO: Check the protocol and set the correct port
            req->port = HTTP_DEFAULT_PORT;
        }
    } else {
        buf->p[offset + res[2].rm_so] = '\0';
        req->port = atoi(buf->p + offset + res[2].rm_so + 1);
    }

    return 0;
}

int net_parse_rsp(struct net_rsp* rsp) {
    return -1;
}
