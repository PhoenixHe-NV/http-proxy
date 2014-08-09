#include "log.h"
#include "async.h"
#include "strbuf.h"

#include "conn.h"

static int conn_try_io(int if_recv, int fd, void* buf, int len) {
    int ret = 0;
    while (1) {
        if (if_recv)
            ret = recv(fd, buf, len, 0);
        else    
            ret = send(fd, buf, len, 0);
        if (ret == 0) {
            // Socket closed
            PLOGD("Socket closed", ret);
            return -1;
        }
        if (ret == -1 && errno == EAGAIN) {
            // Will block
            conn_notice notice;
            notice.io_block.fd = fd;
            notice.io_block.flag = if_recv ? EPOLLIN : EPOLLOUT;
            PLOGD("WILL BLOCK");
            ret = async_yield(CONN_IO_WILL_BLOCK, &notice);
            PLOGD("BACK");
            if (ret == 0)
                // Try again
                continue;
            PLOGD("async_yield returned %d", ret);
        }
        break;
    }
    return ret;
}

// Read all
int conn_read(struct conn* conn, void* buf, int len) {
    if (conn->stat == CONN_CLOSED)
        goto conn_read_failed;
    if (conn->buf_s < conn->buf_e) {
        // Copy from conn buf
        int buf_len = conn->buf_e - conn->buf_s;
        int copy_len = (buf_len > len ? len : buf_len);
        PLOGD("copy %d bytes from conn_buf", copy_len);
        memcpy(buf, conn->buf+conn->buf_s, copy_len);
        buf += copy_len;
        len -= copy_len;
        conn->buf_s += copy_len;
    }
    while (len) {
        int rlen = conn_try_io(1, conn->fd, buf, len);
        if (rlen == -1 || rlen == 0)
            goto conn_read_failed;
        buf += rlen;
        len -= rlen;
    }
    return 0;

conn_read_failed:
    PLOGD("FAILED");
    conn_close(conn);
    return -1;
}

int conn_getc(struct conn* conn) {
    if (conn->buf_s < conn->buf_e)
        // Read from conn buf
        return conn->buf[conn->buf_s++];
    if (conn->stat == CONN_CLOSED)
        goto conn_getc_failed;
    // Buf is empty. Read some from socket
    conn->buf_s = conn->buf_e = 0;
    int rlen = conn_try_io(1, conn->fd, conn->buf, conn->buf_cap);
    if (rlen == -1)
        goto conn_getc_failed;
    conn->buf_e = rlen;
    return conn->buf[conn->buf_s++];

conn_getc_failed:
    PLOGD("FAILED");
    conn_close(conn);
    return -1;
}

void conn_ungetc(struct conn* conn) {
    if (conn->buf_s)
        --conn->buf_s;
}

int conn_gets(struct conn* conn, int len_limit, struct strbuf* buf) {
    int ret = 0, ch;
    while (1) {
        if (buf->len+1 == len_limit)
            return -1;
        ch = conn_getc(conn);
        if (ch == -1)
            return -1;
        if (ch == '\r' || ch == '\n')
            break;
        strbuf_append(buf, ch);
        ++ret;
    }
    if (ch != '\r')
        return ret;
    // Try to consume \n
    ch = conn_getc(conn);
    if (ch == -1)
        return -1;
    if (ch != '\n')
        conn_ungetc(conn);
    return ret;
}


int conn_write(struct conn* conn, void* buf, int len) {
    if (conn->stat == CONN_CLOSED)
        goto conn_write_failed;
    while (len) {
        int rlen = conn_try_io(0, conn->fd, buf, len);
        if (rlen == -1)
            goto conn_write_failed;
        buf += rlen;
        len -= rlen;
    }
    return 0;

conn_write_failed:
    PLOGD("FAILED");
    conn_close(conn);
    return -1;
}

int conn_copy(struct conn* conn_in, struct conn* conn_out, int len) {
    if (conn_in->buf_s < conn_in->buf_e) {
        // Copy from conn_in buf
        int buf_len = conn_in->buf_e - conn_in->buf_s;
        int copy_len = (buf_len > len ? len : buf_len);
        conn_write(conn_out, conn_in->buf+conn_in->buf_s, copy_len);
        conn_in->buf_s += copy_len;
        len -= copy_len;
    }
    while (len) {
        // Read buf is empty
        conn_in->buf_s = conn_in->buf_e = 0;
        int rlen = conn_try_io(1, conn_in->fd, conn_in->buf, conn_in->buf_cap);
        // conn_in error or reaches EOF
        if (rlen == -1 || rlen == 0) {
            conn_close(conn_in);
            return -1;
        }
        conn_in->buf_e = rlen;
        int copy_len = len > rlen ? rlen : len;
        int ret = conn_write(conn_out, conn_in->buf, copy_len);
        if (ret == -1)
            return -1;
        conn_in->buf_s += copy_len;
        len -= copy_len;
    }
    return 0;
}
