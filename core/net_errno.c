#include "net_errno.h"

char* err_msg[] = {
// 0 NO ERROR 
    "No error"
// 1 NET_SERVER_DISCONNECTED             
    "Server closed connection",
// 2 NET_CLIENT_DISCONNECTED
    "Client closed connection",    
// 3 NET_UNKNOWN_METHOD
    "Unknown method",
// 4 NET_UNKNOWN_PROTOCOL
    "Unknown protocol",
// 5 NET_UNKNOWN_HTTP_VER
    "Http version not supported",
// 6 NET_PROTOCOL_ERROR
    "Protocol error"
};

char* net_err_getmsg(int net_errno) {
    return err_msg[err_errno];
}
