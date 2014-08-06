#ifndef _PROXY_LOG_H_
#define _PROXY_LOG_H_

#include <string.h>
#include <errno.h>

#define PROXY_LOG_LEN           256

#define PROXY_LOG_LEVEL_DEBUG   2
#define PROXY_LOG_LEVEL_INFO    3
#define PROXY_LOG_LEVEL_WARNING 4
#define PROXY_LOG_LEVEL_ERROR   5
#define PROXY_LOG_LEVEL_FATAL   6

void _PROXY_LOG(int level, const char* extra_info, int line, const char* desc, ...);

void proxy_log_init();
void proxy_log_done();


#ifdef NDEBUG
#define PROXY_LOG_INFO __FILE__
#define PLOGD(...)
#else
#define PROXY_LOG_INFO __FUNCTION__
#define PLOGD(...) \
    _PROXY_LOG(PROXY_LOG_LEVEL_DEBUG, PROXY_LOG_INFO, __LINE__, "D", __VA_ARGS__)
#endif

#define PLOGI(...) \
    _PROXY_LOG(PROXY_LOG_LEVEL_INFO, PROXY_LOG_INFO, __LINE__, "I", __VA_ARGS__)
#define PLOGW(...) \
    _PROXY_LOG(PROXY_LOG_LEVEL_WARNING, PROXY_LOG_INFO, __LINE__, "W", __VA_ARGS__)
#define PLOGE(...) \
    _PROXY_LOG(PROXY_LOG_LEVEL_ERROR, PROXY_LOG_INFO, __LINE__, "E", __VA_ARGS__)
#define PLOGF(...) \
    _PROXY_LOG(PROXY_LOG_LEVEL_FATAL, PROXY_LOG_INFO, __LINE__, "F", __VA_ARGS__)

#define PLOGUE(desc) PLOGE("%s : %s", desc, strerror(errno))

#endif
