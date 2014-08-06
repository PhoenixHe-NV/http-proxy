#include <stdarg.h>

#include "log.h"
#include "mem.h"
#include "constants.h"

#include "async.h"

static ucontext_t main_cxt;
static struct async_cxt* call_cxt = NULL;
static int yield_ret = 0;

void async_init(struct async_cxt* cxt) {
    cxt->stat = ASYNC_INIT;
    cxt->yield_type = -1;
    cxt->yield_data = NULL;
    getcontext(&cxt->uc);
    cxt->uc.uc_stack.ss_sp = mem_alloc(ASYNC_STACK_SIZE);
    cxt->uc.uc_stack.ss_size = ASYNC_STACK_SIZE;
    cxt->uc.uc_link = &main_cxt;
}

void async_done(struct async_cxt* cxt) {
    PLOGD("free async_cxt");
    mem_free(cxt->uc.uc_stack.ss_sp);
}

void async_call(struct async_cxt* cxt, int (*func)(void*), void* data) {
    if (cxt->stat != ASYNC_INIT)
        return;

    makecontext(&cxt->uc, func, 1, data);

    cxt->stat = ASYNC_PAUSE;
    async_resume(cxt, 0);
}

int async_yield(int data_type, void* data) {
    call_cxt->stat = ASYNC_PAUSE;
    call_cxt->yield_type = data_type;
    call_cxt->yield_data = data;
    PLOGD("Trying to switch back");
    swapcontext(&call_cxt->uc, &main_cxt);
    return yield_ret;
}

void async_resume(struct async_cxt* cxt, int retval) {
    if (cxt->stat != ASYNC_PAUSE)
        return;
    getcontext(&main_cxt);
    call_cxt = cxt;
    cxt->stat = ASYNC_RUNNING;
    cxt->yield_type = YIELD_NONE;
    cxt->yield_data = NULL;
    yield_ret = retval;

    PLOGD("Switching to handler context!");
    swapcontext(&main_cxt, &cxt->uc);
    PLOGD("Handler returned");

    if (cxt->stat == ASYNC_RUNNING)
        cxt->stat = ASYNC_FINISH;
    call_cxt = NULL;
}
