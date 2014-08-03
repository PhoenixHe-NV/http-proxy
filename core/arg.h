#ifndef _PROXY_ARG_H_
#define _PROXY_ARG_H_

#include <stdio.h>
#include "log.h"

struct proxy_arg_t {
    int port;
    char* addr;
    int verbose;
    FILE* log_file;
};

extern struct proxy_arg_t arg;

int proxy_prase_arg(int argc, char** argv);

#endif
