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
// 네트워크 상태 모니터링 및 자동 복구
typedef enum {
    NETWORK_STATE_DISCONNECTED,
    NETWORK_STATE_CONNECTING,
    NETWORK_STATE_CONNECTED,
    NETWORK_STATE_RECONNECTING
} network_monitor_state_t;

extern network_monitor_state_t network_state;
extern uint32_t last_connection_check;
extern uint32_t reconnect_attempts;
extern bool cable_was_connected;

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
void w5500_reset_network(void);

// 네트워크 모니터링 및 재연결 함수
bool network_is_cable_connected(void);
bool network_is_connected(void);
bool network_reinitialize(void);

// Serialize current network info into JSON. Returns number of bytes written (excluding null).
// buf: destination buffer, buf_len: size of buffer.
// The JSON format is: {"ip":"a.b.c.d","netmask":"a.b.c.d","gateway":"a.b.c.d","dns":"a.b.c.d","mac":"aa:bb:cc:dd:ee:ff","dhcp":"DHCP|Static","link":true}
size_t network_get_info_json(char *buf, size_t buf_len);

#endif // NETWORK_CONFIG_H
