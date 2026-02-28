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

#ifdef __cplusplus
extern "C"
{
#endif

#define TCP_SOCKET_START 2
#define TCP_SOCKET_COUNT 3  // 소켓 2, 3, 4 사용 (5번은 멀티캐스트용으로 예약)

  extern uint16_t tcp_port;
  void save_tcp_port_to_flash(uint16_t port);
  void tcp_servers_init(uint16_t port);
  void tcp_servers_process(void);
  void tcp_servers_restart(void);
  void tcp_servers_restart_with_port(uint16_t new_port);
  void tcp_servers_broadcast(const uint8_t *data, uint16_t len);
  bool tcp_servers_has_clients(void);  // 클라이언트 연결 여부 확인

#ifdef __cplusplus
}
#endif

#endif // TCP_SERVER_H
