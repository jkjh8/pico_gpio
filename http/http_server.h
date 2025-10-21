// Minimal ioLibrary-based HTTP server wrapper header
#ifndef HTTP_SERVER_H
#define HTTP_SERVER_H

#include <stdint.h>
#include <stdbool.h>

// Configuration
#define HTTP_SERVER_PORT 80
#define HTTP_SOCKET_NUM  1
#define HTTP_BUF_SIZE    8192

// Server state
typedef enum {
    HTTP_SERVER_STATE_IDLE = 0,
    HTTP_SERVER_STATE_LISTENING,
    HTTP_SERVER_STATE_CONNECTED
} http_server_state_t;

// Public API - minimal wrapper around ioLibrary's httpServer
bool http_server_init(uint16_t port);
void http_server_process(void);
bool http_server_get_state(void);

#endif // HTTP_SERVER_H
