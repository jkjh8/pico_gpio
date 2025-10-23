#ifndef NETWORK_CONFIG_H
#define NETWORK_CONFIG_H


#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>
#include "hardware/spi.h"
#include "hardware/gpio.h"
#include "hardware/flash.h"
#include "hardware/sync.h"
#include "wizchip_conf.h"
#include "dhcp.h"
#include "socket.h"
#include "pico/stdlib.h"
#include "main.h"
#include "network/mac_utils.h"

#define NETWORK_CONFIG_FLASH_OFFSET (PICO_FLASH_SIZE_BYTES - 4096)
#define SPI_PORT spi0
#define SPI_SCK 6
#define SPI_MOSI 7
#define SPI_MISO 4
#define SPI_CS 5
#define SPI_RST 8
// 네트워크 상태 모니터링 및 자동 복구
typedef enum {
    NETWORK_STATE_DISCONNECTED,
    NETWORK_STATE_CONNECTING,
    NETWORK_STATE_CONNECTED,
    NETWORK_STATE_RECONNECTING
} network_monitor_state_t;
// SPI CONFIGURATION
// Network monitor state globals removed; network module is event-driven now.

// SPI 콜백 함수들
void wizchip_select(void);
void wizchip_deselect(void);
uint8_t wizchip_read(void);
void wizchip_write(uint8_t wb);


// W5500 초기화 결과
typedef enum {
    W5500_INIT_SUCCESS = 0,
    W5500_INIT_ERROR_SPI,
    W5500_INIT_ERROR_CHIP,
    W5500_INIT_ERROR_NETWORK
} w5500_init_result_t;

// 네트워크 설정 타입
typedef enum {
    NETWORK_MODE_STATIC = 0,
    NETWORK_MODE_DHCP
} network_mode_t;
// 플래시 저장 함수
void network_config_save_to_flash(const wiz_NetInfo* config);
void network_config_load_from_flash(wiz_NetInfo* config);
// 함수 선언
w5500_init_result_t w5500_initialize(void);
bool w5500_set_static_ip(wiz_NetInfo *net_info);
bool w5500_set_dhcp_mode(wiz_NetInfo *net_info);
bool w5500_apply_network_config(wiz_NetInfo *net_info, network_mode_t mode);
void w5500_print_network_status(void);
bool w5500_check_link_status(void);
// Boot-time network setup: initialize W5500 and apply stored config (static or DHCP).
// Returns true if network configured (IP assigned or static applied), false otherwise.
bool network_boot_setup(void);

// Handle cable connect/disconnect events. When connected, applies DHCP or static IP as
// configured in g_net_info. When disconnected, stops DHCP and performs cleanup.
// Returns true if network is configured after handling the event.
bool network_on_cable_change(bool connected);

// Perform a synchronous DHCP attempt using the existing non-blocking state machine.
// Returns true on lease obtained, false on failure/timeout.
bool network_try_dhcp_sync(void);

// Periodic network handler: call from main loop to let network module detect
// cable connect/disconnect and handle transitions. Returns 0 for no event,
// 1 for connected event, 2 for disconnected event.
typedef enum {
    NETWORK_EVENT_NONE = 0,
    NETWORK_EVENT_CONNECTED = 1,
    NETWORK_EVENT_DISCONNECTED = 2
} network_event_t;

network_event_t network_periodic(void);

// 네트워크 모니터링 및 재연결 함수
bool network_is_cable_connected(void);
bool network_is_connected(void);
bool network_reinitialize(void);

// DHCP state-machine APIs (non-blocking)
// Call network_dhcp_start() to begin DHCP process (opens socket 68 and initializes DHCP)
// Call network_dhcp_process() periodically from main loop; returns:
//   0 = still running/not started, 1 = lease obtained, -1 = failed (socket closed)
int network_dhcp_process(void);
void network_dhcp_start(void);
void network_dhcp_stop(void);

// Serialize current network info into JSON. Returns number of bytes written (excluding null).
// buf: destination buffer, buf_len: size of buffer.
// The JSON format is: {"ip":"a.b.c.d","netmask":"a.b.c.d","gateway":"a.b.c.d","dns":"a.b.c.d","mac":"aa:bb:cc:dd:ee:ff","dhcp":"DHCP|Static","link":true}
size_t network_get_info_json(char *buf, size_t buf_len);

#endif // NETWORK_CONFIG_H
