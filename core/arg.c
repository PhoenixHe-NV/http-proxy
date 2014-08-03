#include <string.h>
#include <errno.h>
#include <stdlib.h>
#include <sys/socket.h>

#include "arg.h"

struct proxy_arg_t arg = {
    -1, "127.0.0.1", PROXY_LOG_LEVEL_DEBUG, NULL
};

static int usage(char* reason, char* exe) {
    if (reason) 
        puts(reason);
    printf("\nUsage: %s [OPTION]... PORT\n", exe);
    puts("A simple http(s) proxy listen on PORT\n");
    puts("-v                Logging becomes VERY verbose");
    puts("-a <addrress>     Bind to specific address (can be ipv6 address)");
    puts("                      Default: 127.0.0.1");
    puts("-l <file>         Logging to log file");
    puts("                      Default: stderr");
    puts("-h                Print this help and exit");
    return -1;
}

static int prase_addr(char* addr) {
    if (addr == NULL)
        return -1;
    arg.addr = addr;
    PLOGD("Set bind address to %s", addr);
    return 0;
}

static int prase_log_file(char* file) {
    if (file == NULL)
        return -1;
    arg.log_file = fopen(file, "w");
    if (arg.log_file == NULL) {
        perror("open log file");
        return -1;
    }
    PLOGD("Open file %s as log file", file);
    return 0;
}

static int prase_port(char* port) {
    arg.port = atoi(port);
    PLOGD("Get port number %d", arg.port);
    if (arg.port < 1 || arg.port > 65535)
        return -1;
    return 0;
}

int proxy_prase_arg(int argc, char** argv) {
    PLOGD("Start to prase arguments");
    int x = 1;
    while (x < argc) {
        if (strcmp(argv[x], "-v") == 0) {
            arg.verbose = PROXY_LOG_LEVEL_DEBUG;
            ++x; continue;
        }
        if (strcmp(argv[x], "-a") == 0) {
            if (prase_addr(argv[x+1]))
                return usage("Cannot prase address", argv[0]);
            x += 2; continue;
        }
        if (strcmp(argv[x], "-l") == 0) {
            if (prase_log_file(argv[x+1]))
                return usage("Cannot open log file", argv[0]);
            x += 2; continue;
        }
        if (strcmp(argv[x], "-h") == 0) {
            usage(NULL, argv[0]);
            return 0;
        }
        if (prase_port(argv[x]))
            return usage("Please specific port number between 1 and 65535", argv[0]);
        else
            break;
    }
    if (arg.port == -1)
        return usage("Please specific port number between 1 and 65535", argv[0]);
    PLOGD("Successfully prase arguments");
    return 0;
}
