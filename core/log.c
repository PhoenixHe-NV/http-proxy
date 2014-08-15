#include <stdio.h>
#include <time.h>
#include <string.h>
#include <stdarg.h>
#include <arpa/inet.h>

#include "arg.h"
#include "log.h"
#include "constants.h"

static char* week_str[] = {
    "Sun.",
    "Mon.",
    "Tue.",
    "Wed.",
    "Thu.",
    "Fri.",
    "Sat."
};

static char* month_str[] = {
    "Jan",
    "Feb",
    "Mar",
    "Apr",
    "May",
    "Jun",
    "Jul",
    "Aug",
    "Sep",
    "Oct",
    "Nov",
    "Dec"
};

static void gen_log_string(
        int level, 
        const char* desc, 
        char* out,
        int out_len,
        const char* file,
        int line,
        const char* format,
        va_list args) {

    // generate date info
    time_t raw_time;
    time(&raw_time);
    struct tm* t = localtime(&raw_time);
    int len = snprintf(out, out_len, "%s %2d %s %d %02d:%02d:%02d %s %s(%d): ", 
            week_str[t->tm_wday], t->tm_mday, month_str[t->tm_mon],
            t->tm_year+1900, t->tm_hour, t->tm_min, t->tm_sec, desc, file, line);
    out += len; out_len -= len;

    //write user info
    int end = vsnprintf(out, out_len-4, format, args);
    if (end >= out_len-4)
        strcat(out + out_len-5, " ...");
}


void _PROXY_LOG(int level, const char* file, int line, const char* desc, ...) {
    if (level < arg.verbose)
        return;
    
    static char buf[PROXY_LOG_LEN];
    va_list argptr;
    va_start(argptr, desc);
    char* format = va_arg(argptr, char*);
    gen_log_string(level, desc, buf, PROXY_LOG_LEN, file, line, format, argptr);
    va_end(argptr);

    fprintf(arg.log_file, "%s\n", buf);
    fflush(arg.log_file);
}

void log_http_req(struct conn_endpoint* ep, char* host, char* path, int size) {
    time_t now;
    static char time_str[PROXY_LOG_LEN];
    static char ip_str[PROXY_LOG_LEN];

    now = time(NULL);
    strftime(time_str, PROXY_LOG_LEN, "%a %d %b %Y %H:%M:%S %Z", localtime(&now));

    inet_ntop(ep->family, &ep->addr, ip_str, PROXY_LOG_LEN);

    fprintf(arg.log_file, "%s: %s http://%s%s %d\n", time_str, ip_str, host, path, size);
    fflush(arg.log_file);
}

void proxy_log_init() {
    if (arg.log_file)
        return;
    arg.log_file = fopen("proxy.log", "w");
    PLOGD("Init log file with proxy.log");
}

void proxy_log_done() {
    PLOGD("Closing log file");
    fclose(arg.log_file);
}
