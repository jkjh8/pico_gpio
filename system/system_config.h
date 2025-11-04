#ifndef SYSTEM_CONFIG_H
#define SYSTEM_CONFIG_H

#include <stdint.h>
#include <stdbool.h>
#include "wizchip_conf.h"
#include "gpio/gpio.h"

#ifdef __cplusplus
extern "C"
{
#endif

// 시스템 설정 버전 (구조체가 변경될 때마다 증가)
#define SYSTEM_CONFIG_VERSION 1

// 시스템 전체 설정 구조체
typedef struct
{
    uint32_t magic;              // 매직 넘버 (0x47504943 = "GPIC")
    uint32_t version;            // 설정 버전
    
    // GPIO 설정
    gpio_config_t gpio;
    
    // 네트워크 설정
    wiz_NetInfo network;
    
    // TCP 서버 설정
    uint16_t tcp_port;
    
    // UART RS232 설정
    uint32_t uart_baud;
    
    // 멀티캐스트 설정
    bool multicast_enabled;
    
    // 디버그 플래그
    uint32_t debug_flags;
    
    // 체크섬 (구조체 전체의 간단한 체크섬)
    uint32_t checksum;
} system_config_t;

// 시스템 설정 함수
void system_config_init(void);
bool system_config_save_to_flash(void);
bool system_config_load_from_flash(void);
void system_config_reset_to_defaults(void);

// 현재 시스템 설정 접근
system_config_t *system_config_get(void);

// 개별 설정 접근 헬퍼 함수
gpio_config_t *system_config_get_gpio(void);
wiz_NetInfo *system_config_get_network(void);
uint16_t system_config_get_tcp_port(void);
void system_config_set_tcp_port(uint16_t port);
uint32_t system_config_get_uart_baud(void);
void system_config_set_uart_baud(uint32_t baud);
bool system_config_get_multicast_enabled(void);
void system_config_set_multicast_enabled(bool enabled);
uint32_t system_config_get_debug_flags(void);
void system_config_set_debug_flags(uint32_t flags);

#ifdef __cplusplus
}
#endif

#endif // SYSTEM_CONFIG_H
