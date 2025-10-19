#ifndef GPIO_H
#define GPIO_H

#include <stdint.h>
#include <stdbool.h>
#include "hardware/spi.h"
#include "hardware/gpio.h"
#include "hardware/flash.h"
#include "hardware/sync.h"
#include "pico/stdlib.h"

// GPIO CONFIGURATION
#define GPIO_PORT spi1
#define GPIO_SCK 10
#define GPIO_MOSI 11
#define GPIO_MISO 12  // MISO on GP12
#define HCT595_CLEAR_PIN 14  // HCT595 SRCLR on GP14
#define HCT595_LATCH_PIN 3  // HCT595 RCLK on GP15
#define HCT165_LOAD_PIN 13   // HCT165 SH/LD on GP13

// GPIO 통신 프로토콜
#define GPIO_MIN_PIN 1
#define GPIO_MAX_PIN 8

// GPIO 통신 모드
typedef enum {
    GPIO_MODE_TEXT = 0,     // 텍스트 기반 명령 (기본값)
    GPIO_MODE_JSON = 1      // JSON 기반 명령
} gpio_comm_mode_t;

// GPIO 설정 구조체
typedef struct {
    uint8_t device_id;          // 디바이스 ID (1-254)
    gpio_comm_mode_t comm_mode; // 통신 모드
    bool auto_response;         // 자동 응답 여부
    uint32_t reserved;          // 향후 확장용
} gpio_config_t;

// GPIO 디바이스 ID (여러 디바이스 구분용)
extern gpio_config_t gpio_config;
#define GPIO_CONFIG_FLASH_OFFSET (PICO_FLASH_SIZE_BYTES - 16384) // 마지막에서 네 번째 4KB

// GPIO Functions
bool gpio_spi_init(void);
void hct595_write(uint16_t data);
uint16_t hct165_read(void);

// GPIO 설정 관리 함수
void save_gpio_config_to_flash(void);
void load_gpio_config_from_flash(void);
bool set_gpio_device_id(uint8_t new_id);
uint8_t get_gpio_device_id(void);
bool set_gpio_comm_mode(gpio_comm_mode_t mode);
gpio_comm_mode_t get_gpio_comm_mode(void);
bool set_gpio_auto_response(bool enabled);
bool get_gpio_auto_response(void);

// Global variables
extern uint16_t gpio_input_data;
extern uint16_t gpio_output_data;

#endif // GPIO_H