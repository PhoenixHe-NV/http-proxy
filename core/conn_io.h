#ifndef _PROXY_CONN_IO_H_
#define _PROXY_CONN_IO_H_

#include "conn.h"

// All of these are ASYNC functions

// Read all
int conn_read(struct conn* conn, void* buf, int len);

int conn_getc(struct conn* conn);

// Write all
int conn_write(struct conn* conn, void* buf, int len);

int conn_copy(struct conn* conn_in, struct conn* conn_out, int len);

void conn_close(struct conn* conn);

#endif
