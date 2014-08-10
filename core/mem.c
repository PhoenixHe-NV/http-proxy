#include <stdlib.h>

#include "utlib.h"
#include "mem.h"
#include "log.h"

void mem_init() {
    PLOGD("Initialize memory management");
}

void* mem_alloc(ssize_t size) {
    // use glibc malloc()
    void* ret = malloc(size);
    if (ret == NULL)
        PLOGF("Out of memory!");
    return ret;
}

void mem_free(void* ptr) {
    // use glibc free()
    free(ptr);
}

#define MEM_CNT_SIZE sizeof(ssize_t)
#define MEM_CNT(ptr) (*((ssize_t*)(ptr)-1))

void* mem_alloc_auto(ssize_t size) {
    ssize_t* ret = (ssize_t*) mem_alloc(size + MEM_CNT_SIZE);
    *ret = 1;
    PLOGF("ALLOC AUTO!!!!!!!!!! size: %d", size);
    return ret + 1;
}

void mem_incref(void* ptr) {
    ++MEM_CNT(ptr);
    PLOGF("INCREF!");
}

int mem_decref(void* ptr, void (*done)(void*)) {
    if (--MEM_CNT(ptr) == 0) {
        if (done) done(ptr);
        mem_free((ssize_t*)ptr-1);
        PLOGF("DECREF FREE!!!!!!!!!!!!!");
        return 0;
    }
    PLOGD("DECREF!");
    return 1;
}

void mem_done() {
}
