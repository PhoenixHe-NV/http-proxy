#ifndef _PROXY_ASYNC_H_
#define _PROXY_ASYNC_H_

#define _BSD_SOURCE 1

#include <ucontext.h>
#include <stdlib.h>

enum async_stat {
    ASYNC_INIT, ASYNC_RUNNING, ASYNC_PAUSE, ASYNC_FINISH
};

enum async_yield_type {
    YIELD_NONE,
    CONN_IO_WILL_BLOCK,
    CONN_WAIT_FOR_EVENT
};

struct async_cxt {
    ucontext_t uc;
    enum async_stat stat;
    enum async_yield_type  yield_type;
    void* yield_data;
};

void async_init(struct async_cxt* cxt);

void async_done(void* cxt);

void async_call(struct async_cxt* cxt, void (*func)(void), int argc, ...);

int async_yield(int data_type, void* data);

void async_resume(struct async_cxt* cxt, int retval);

#endif
