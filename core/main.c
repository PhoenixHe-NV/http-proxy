#include <stdio.h>
#include <signal.h>
#include <stdlib.h>
#include <string.h>

#include "log.h"
#include "arg.h"
#include "event.h"
#include "conn.h"
#include "net_http.h"

#include "main.h"

static int proxy_finalize(int exit_val) {
    PLOGD("Exiting");
    conn_module_fina();
    proxy_log_finalize();
    exit(exit_val);
    return 0;
}

static void sig_handler(int sig) {
    PLOGI("Recving signal %d: %s", sig, strsignal(sig));
    proxy_finalize(-1);
}

static void install_signal_handlers() {
    signal(SIGTERM, sig_handler);
    signal(SIGINT, sig_handler);
    //signal(SIGKILL, sig_handler);
    signal(SIGPIPE, SIG_IGN);
    signal(SIGTERM, sig_handler);
    signal(SIGSEGV, sig_handler);
    signal(SIGHUP, sig_handler);
}

int proxy_main(int argc, char** argv) {
    proxy_log_init();
    PLOGD("Starting");

    install_signal_handlers();

    int ret = proxy_prase_arg(argc, argv);
    if (ret) {
        PLOGD("Cannot prase arguments. Aborting");
        goto proxy_main_exit;
    }

    ret = net_pull_init();
    if (ret) {
        PLOGD("Cannot init epoll. Aborting");
        goto proxy_main_exit;
    }

    ret = conn_module_init();
    if (ret) {
        PLOGD("Cannot init connection module. Aborting");
        goto proxy_main_exit;
    }

    net_http_module_init();
    

    while (1) {
        ret = proxy_event_work();
        PLOGD("proxy_event_work() returns %d", ret);
        ret = net_pull_work();
        PLOGD("net_pull_work() returns %d", ret);
//        sleep(1);
    }

proxy_main_exit:
    proxy_finalize(ret);
    return 0;
}
