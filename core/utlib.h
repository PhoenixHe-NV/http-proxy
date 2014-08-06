#ifndef _PROXY_UTHASH_H_
#define _PROXY_UTHASH_H_

#include <stdio.h>

#include "log.h"
#include "mem.h"

#define uthash_fatal(msg) PLOGE(msg)
#define uthash_malloc(sz) mem_alloc(sz)
#define uthash_free(ptr, sz) mem_free(ptr)

#include "../uthash/uthash.h"
#include "../uthash/utstring.h"
#include "../uthash/utarray.h"

#endif
