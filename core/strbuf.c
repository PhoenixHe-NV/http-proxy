#include <string.h>
#include <stdio.h>
#include "constants.h"
#include "mem.h"

#include "strbuf.h"


void strbuf_init(struct strbuf* buf) {
    buf->p = mem_alloc(STRBUF_INIT_LEN);          
    buf->len = 0;                                 
    buf->cap = STRBUF_INIT_LEN;                   
    buf->p[0] = '\0';                             
}

void strbuf_done(struct strbuf* buf) {
    mem_free(buf->p);
}

void strbuf_reset(struct strbuf* buf) {
    buf->len = 0;
}

void strbuf_append(struct strbuf* buf, char ch) {
    if (buf->len+1 == buf->cap) {               
        char* new_p = mem_alloc((buf)->cap * 2);    
        memcpy(new_p, buf->p, (buf)->cap);        
        mem_free((buf)->p);                         
        buf->p = new_p;                           
        buf->cap *= 2;                            
    }                                               
    buf->p[buf->len++] = ch;                     
    buf->p[buf->len] = '\0';                    
}

void strbuf_cat(struct strbuf* buf, char* s) {
    // TODO Optimize this part
    char* p = s;                                    
    while (*p) {                                    
        strbuf_append(buf, *p++);                   
    }                                               
}

void strbuf_append_num(struct strbuf* buf, int x) {
    char res[16];
    sprintf(res, "%d", x);
    strbuf_cat(buf, res);
}