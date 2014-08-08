#ifndef _PROXY_STRBUF_H_
#define _PROXY_STRBUF_H_


struct strbuf {
    char* p;
    int len, cap;
};

void strbuf_init(struct strbuf* buf);

void strbuf_done(struct strbuf* buf);

void strbuf_append(struct strbuf* buf, char ch);

void strbuf_cat(struct strbuf* buf, char* s);

#endif
