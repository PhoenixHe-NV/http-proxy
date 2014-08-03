#include <errno.h>
#include <sys/socket.h>

#include "mem.h"
#include "log.h"

#include "iobuf.h"

int iobuf_init(struct iobuf* buf, int fd_recv, int fd_send, int size) {
    if (size > 0) {
        buf->p = mem_alloc(size);
        if (!(buf->p))
            return -1;
    } else
        buf->p = NULL;
    buf->fd_recv = fd_recv;
    buf->fd_send = fd_send;
    buf->cap = size;
    buf->head = buf->tail = 0;
    buf->io_len = -1;
    buf->io_p = NULL;
    return 0;
}

struct iobuf* iobuf_new(int fd_recv, int fd_send, int size) {
    struct iobuf* ret = mem_alloc(sizeof(struct iobuf));
    if (!ret) return NULL;
    if (iobuf_init(ret, fd_recv, fd_send, size) == -1) {
        mem_free(ret);
        return NULL;
    }
    return ret;
}

void iobuf_fina(struct iobuf* buf) {
    mem_free(buf->p);
}

void iobuf_free(struct iobuf* buf) {
    mem_free(buf->p);
    mem_free(buf);
}

int _iobuf_readn(struct iobuf* buf, char* p, int len) {
    if (!(buf->io_p)) {
        buf->io_p = p;
        buf->io_len = len;
    }
    int copy_len = (buf->io_len > (buf->tail-buf->head) ?
                    (buf->tail-buf->head) : buf->io_len);
    if (copy_len) {
        memcpy(buf->io_p, buf->p+buf->head, copy_len);
        buf->io_p += copy_len;
        buf->io_len -= copy_len;
        buf->head += copy_len;
        if (buf->head == buf->tail)
            buf->head = buf->tail = 0;
    }
    while (buf->io_len) {
        copy_len = recv(buf->fd_recv, buf->io_p, buf->io_len, 0);
        if (copy_len == -1) {
            if (errno == EAGAIN)
                // Will block
                return CONN_IO_WILL_BLOCK;
            PLOGUE("recv");
            return -1;
        }
        buf->io_p += copy_len;
        buf->io_len -= copy_len;
    }
    buf->io_p = NULL;
    buf->io_len = -1;
    return 0;
}

int _iobuf_writen(struct iobuf* buf, char* p, int len) {
    if (!(buf->io_p)) {
        buf->io_p = p;
        buf->io_len = len;
    }
    while (buf->io_len) {
        int copy_len = send(buf->fd_send, buf->io_p, buf->io_len, 0);
        if (copy_len == -1) {
            if (errno == EAGAIN)
                // Will block
                return CONN_IO_WILL_BLOCK;
            PLOGUE("recv");
            return -1;
        }
        buf->io_p += copy_len;
        buf->io_len -= copy_len;
    }
    buf->io_p = NULL;
    buf->io_len = -1;
    return 0;
}

int _iobuf_getc(struct iobuf* buf) {
    if (buf->head < buf->tail)
        return *(buf->p + (buf->head)++);
    buf->head = buf->tail = 0;
    int copy_len = recv(buf->fd_recv, buf->p, buf->cap, 0);
    if (copy_len == -1) {
        if (errno == EAGAIN)
            return CONN_IO_WILL_BLOCK;
        PLOGUE("recv");
        return -1;
    }
    buf->tail = copy_len;
    return *(buf->p + (buf->head)++);
}
