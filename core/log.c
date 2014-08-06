#include <stdio.h>
#include <time.h>
#include <string.h>
#include <stdarg.h>

#include "arg.h"
#include "log.h"

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

void gen_log_string(
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
    char buf[PROXY_LOG_LEN];
    va_list argptr;
    va_start(argptr, desc);
    char* format = va_arg(argptr, char*);
    gen_log_string(level, desc, buf, PROXY_LOG_LEN, file, line, format, argptr);
    va_end(argptr);

    fprintf(arg.log_file, "%s\n", buf);
    fflush(arg.log_file);
}

void proxy_log_init() {
    if (arg.log_file)
        return;
    arg.log_file = stderr;
    PLOGD("Init log file with stderr");
}

void proxy_log_done() {
    PLOGD("Closing log file");
    fclose(arg.log_file);
}
