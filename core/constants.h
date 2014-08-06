#ifndef _PROXY_CONSTANT_H_
#define _PROXY_CONSTANT_H_

#define HTTP_DEFAULT_PORT           80
#define HTTPS_DEFAULT_PORT          443

#define HTTP_HEADER_MAXLEN          4096
#define HTTP_HEADER_KEY_MAXLEN      32

#define SOCKADDR_LEN                sizeof(sockaddr_in6)

#define CONN_MAX_HANDLER_NUM        32

#define SERVER_MAX_FD               32

#define ASYNC_STACK_SIZE            64*1024     // 64k

#endif