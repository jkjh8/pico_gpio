#ifndef NETWORK_CONFIG_H
#define NETWORK_CONFIG_H

#include <stdint.h>
#include <stdbool.h>
#include "wizchip_conf.h"
#include "hardware/spi.h"
#include "hardware/gpio.h"
#include "main.h"

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
void network_monitor_process(void);
const char* network_get_state_string(void);

#endif // NETWORK_CONFIG_H
