#ifndef _PROXY_STRBUF_H_
#define _PROXY_STRBUF_H_

#include <string.h>

#include "mem.h"

#define STRBUF_INIT_LEN 16

struct strbuf {
    char* p;
    int len, cap;
};

#define strbuf_init(buf) ({                         \
    (buf)->p = mem_alloc(STRBUF_INIT_LEN);          \
    (buf)->len = 0;                                 \
    (buf)->cap = STRBUF_INIT_LEN;                   \
    (buf)->p[0] = '\0';                             \
})

#define strbuf_done(buf) mem_free((buf)->p)

#define strbuf_append(buf, ch) ({                   \
    char x = ch;                                    \
    if ((buf)->len+1 == (buf)->cap) {               \
        char* new_p = mem_alloc((buf)->cap * 2);    \
        memcpy(new_p, (buf)->p, (buf)->cap);        \
        mem_free((buf)->p);                         \
        (buf)->p = new_p;                           \
        (buf)->cap *= 2;                            \
    }                                               \
    (buf)->p[(buf)->len++] = x;                     \
    (buf)->p[(buf)->len] = '\0';                    \
})

#define strbuf_cat(buf, s) ({                       \
    /* TODO : Optimize this part */                 \
    char* p = s;                                    \
    while (*p) {                                    \
        strbuf_append(buf, *p++);                   \
    }                                               \
})

#endif
