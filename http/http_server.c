// Minimal wrapper around ioLibrary httpServer
#include "http_server.h"
#include "ioLibrary_Driver/Internet/httpServer/httpServer.h"
#include "ioLibrary_Driver/Internet/httpServer/httpUtil.h"
#include <string.h>
#include <stdint.h>
#include <stdbool.h>

static uint8_t http_tx_buf[HTTP_BUF_SIZE];
static uint8_t http_rx_buf[HTTP_BUF_SIZE];
static uint8_t http_sock_list[1] = { HTTP_SOCKET_NUM };
static bool initialized = false;

bool http_server_init(uint16_t port)
{
    (void)port;
    httpServer_init(http_tx_buf, http_rx_buf, 1, http_sock_list);
    reg_httpServer_cbfunc(NULL, NULL);
    initialized = true;
    return true;
}

void http_server_process(void)
{
    if (!initialized) return;
    httpServer_time_handler();
    httpServer_run(0);
}

bool http_server_get_state(void)
{
    return initialized;
}

