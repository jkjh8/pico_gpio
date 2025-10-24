#ifndef TCP_SERVER_H
#define TCP_SERVER_H

#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include "wizchip_conf.h"
#include "socket.h"
#include "hardware/flash.h"
#include "hardware/sync.h"
#include "pico/stdlib.h"
#include "../network/network_config.h"

#define TCP_PORT_FLASH_OFFSET (PICO_FLASH_SIZE_BYTES - 8192) // 마지막에서 두 번째 4KB

#ifdef __cplusplus
extern "C"
{
#endif

#define TCP_SOCKET_START 2
#define TCP_SOCKET_COUNT 2

  extern uint16_t tcp_port;
  void save_tcp_port_to_flash(uint16_t port);
  void tcp_servers_init(uint16_t port);
  void tcp_servers_process(void);
  void tcp_servers_broadcast(const uint8_t *data, uint16_t len);

#ifdef __cplusplus
}
#endif

#endif // TCP_SERVER_H
