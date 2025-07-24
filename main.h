#ifndef PICO_GPIO_H
#define PICO_GPIO_H

#include <stdio.h>
#include <string.h>
#include "pico/stdlib.h"
#include "wizchip_conf.h"
#include "hardware/gpio.h"
#include "hardware/spi.h"
#include "hardware/sync.h"
#include "hardware/watchdog.h"
#include "network/mac_utils.h"
#include "network/network_config.h"
#include "http/http_server.h"
#include "tcp/tcp_server.h"

// SPI CONFIGURATION
#define SPI_PORT spi0
#define SPI_SCK 18
#define SPI_MOSI 19
#define SPI_MISO 16
#define SPI_CS 17
#define SPI_RST 20

// LED CONFIGURATION (RP2350 Pico 2 호환)
#ifdef PICO_DEFAULT_LED_PIN
#define LED_PIN PICO_DEFAULT_LED_PIN
#else
#define LED_PIN 25  // 기본값 (RP2040 호환)
#endif

// WIZnet 공식 OUI: 00:08:DC (고정)
#define WIZNET_OUI_0    0x00
#define WIZNET_OUI_1    0x08
#define WIZNET_OUI_2    0xDC

// tcp port
extern uint16_t tcp_port;
// Ethernet buffer (외부 선언)
extern uint8_t g_ethernet_buf[2048];

// ipaddress (외부 선언)
extern wiz_NetInfo g_net_info;


// 시스템 재시작 함수
void system_restart_request(void);
void system_restart(void);

bool is_system_restart_requested(void);


#endif // PICO_GPIO_H