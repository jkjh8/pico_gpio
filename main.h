#ifndef PICO_GPIO_H
#define PICO_GPIO_H

#include <stdio.h>
#include <string.h>
#include "debug/debug.h"
#include "pico/stdlib.h"
#include "wizchip_conf.h"
#include "hardware/gpio.h"
#include "hardware/spi.h"
#include "hardware/sync.h"
#include "hardware/watchdog.h"
#include "FreeRTOS.h"
#include "semphr.h"
#include "network/mac_utils.h"
#include "network/network_config.h"
// #include "network/multicast.h"
// #include "http/http_server.h"  // HTTP 서버 비활성화
#include "tcp/tcp_server.h"
#include "uart/uart_rs232.h"
#include "gpio/gpio.h"

// GPIO 응답 메시지 큐 (통합 큐 구조)
#include "queue.h"
#define GPIO_QUEUE_SIZE 20       // 큐 크기 증가 (16채널 동시 변경 대응)
#define GPIO_MSG_MAX_LEN 64      // 실제 메시지는 최대 27바이트
#define MAX_GPIO_QUEUES 3        // UART(1) + TCP(1) + MCAST(1)

typedef enum {
    GPIO_QUEUE_UART = 0,
    GPIO_QUEUE_TCP = 1,
    GPIO_QUEUE_MCAST = 2
} gpio_queue_type_t;

extern QueueHandle_t gpio_queues[MAX_GPIO_QUEUES];
extern bool gpio_queues_enabled[MAX_GPIO_QUEUES];

// SPI CONFIGURATION
#define SPI_PORT spi0
#define SPI_SCK 6
#define SPI_MOSI 7
#define SPI_MISO 4
#define SPI_CS 5
#define SPI_RST 8

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
extern bool tcp_servers_initialized;

// 펌웨어 버전 문자열 (CMakeLists.txt에서 FW_VER_MAJOR/MINOR/PATCH 정의)
#define _FWVER_STR(x) #x
#define FWVER_STR(x)  _FWVER_STR(x)
#define FIRMWARE_VERSION \
    FWVER_STR(FW_VER_MAJOR) "." FWVER_STR(FW_VER_MINOR) "." FWVER_STR(FW_VER_PATCH)

// 시스템 재시작 함수
void system_restart_request(void);
void system_restart(void);
bool is_system_restart_requested(void);

#endif // PICO_GPIO_H