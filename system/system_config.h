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

// 시스템 전체 설정 구조체
// 플래시 저장 형식: 슬롯 기반 (필드별 고정 오프셋 + 1바이트 XOR CRC)
// 매직/버전/전체 체크섬 없음 — 필드 추가 시 기존 슬롯 오프셋/크기 변경 금지
typedef struct
{
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

    // DHCP로 마지막에 받은 IP 정보 (빠른 부팅용)
    uint8_t last_dhcp_ip[4];
    uint8_t last_dhcp_gw[4];
    uint8_t last_dhcp_sn[4];
    uint8_t last_dhcp_dns[4];
    bool has_last_dhcp_ip;

    // 디버그 플래그
    uint32_t debug_flags;
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
