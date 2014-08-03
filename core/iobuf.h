#ifndef _PROXY_IOBUF_H_
#define _PROXY_IOBUF_H_

#include <stddef.h>
#include <stdbool.h>

#include "async.h"

struct iobuf {
    int fd_recv, fd_send, cap, head, tail, io_len;
    char *p, *io_p;
};

struct iobuf* iobuf_new(int fd_recv, int fd_send, int size);
void iobuf_free(struct iobuf* ptr);

int iobuf_init(struct iobuf* buf, int fd_recv, int fd_send, int size);
void iobuf_fina(struct iobuf* buf);

#define iobuf_readn(buf, p, len) ASYNC_WRAP(_iobuf_read, buf, p, len)
#define iobuf_writen(buf, p, len) ASYNC_WRAP(_iobuf_write, buf, p, len)
#define iobuf_getc(buf) ASYNC_WRAP(_iobuf_getc, buf)

int _iobuf_readn(struct iobuf* buf, char* p, int len);
int _iobuf_writen(struct iobuf* ptr, char* p, int len);
int _iobuf_getc(struct iobuf* ptr);

#endif
