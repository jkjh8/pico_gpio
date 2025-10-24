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

// SPI callback functions
void wizchip_select(void);
void wizchip_deselect(void);
uint8_t wizchip_read(void);
void wizchip_write(uint8_t wb);

// Global network information
extern wiz_NetInfo g_net_info;

// DHCP configuration flag
extern bool dhcp_configured;

// Ethernet buffer
extern uint8_t g_ethernet_buf[2048];


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
// Function declarations
w5500_init_result_t w5500_initialize(void);
bool w5500_set_static_ip(wiz_NetInfo *net_info);
bool w5500_set_dhcp_mode(wiz_NetInfo *net_info);
void w5500_print_network_status(void);
bool w5500_check_link_status(void);

// Network monitoring functions
bool network_is_cable_connected(void);
bool network_is_connected(void);

// IP Address Utility Functions
bool is_ip_zero(const uint8_t ip[4]);
void print_ip_address(const char* label, const uint8_t ip[4]);
void set_default_ip(uint8_t ip[4], uint8_t default_ip[4]);

// Network Status Printing Functions
void print_network_mac_address(const char* label, const uint8_t mac[6]);
void print_dhcp_mode(void);
void print_link_status(void);

// Network Configuration Application Functions
bool is_mac_invalid(const uint8_t mac[6]);
void apply_network_config(const wiz_NetInfo* config);

// Network initialization and processing functions
void network_init(void);
void network_process(void);

#endif // NETWORK_CONFIG_H
