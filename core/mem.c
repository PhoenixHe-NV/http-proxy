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
    return ret + 1;
}

void mem_incref(void* ptr) {
    ++MEM_CNT(ptr);
}

int mem_decref(void* ptr, void (*finalize)(void*)) {
    if (--MEM_CNT(ptr) == 0) {
        if (finalize) finalize(ptr);
        mem_free((ssize_t*)ptr-1);
        return 0;
    }
    return 1;
}

void mem_finalize() {
}
