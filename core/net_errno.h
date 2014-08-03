#ifndef _PROXY_NET_ERRNO_H_
#define _PROXY_NET_ERRNO_H_

extern int net_errno;

#define NET_SERVER_DISCONNECTED             1
#define NET_CLIENT_DISCONNECTED             2
#define NET_UNKNOWN_METHOD                  3
#define NET_UNKNOWN_PROTOCOL                4
#define NET_UNKNOWN_HTTP_VER                5
#define NET_PROTOCOL_ERROR                  6

char* net_err_getmsg(int net_errno);

#endif
