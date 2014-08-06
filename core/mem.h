#ifndef _PROXY_MEM_H_
#define _PROXY_MEM_H_

#include <unistd.h>

void mem_init();

void* mem_alloc(ssize_t size);

void mem_free(void* ptr);

void* mem_alloc_auto(ssize_t size);

void mem_incref(void* ptr);

int mem_decref(void* ptr, void (*done)(void*));

void mem_done();

#endif
