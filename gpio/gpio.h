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
#define HCT595_LATCH_PIN 15  // HCT595 RCLK on GP15
#define HCT165_LOAD_PIN 13   // HCT165 SH/LD on GP13

// GPIO 통신 프로토콜
#define GPIO_MIN_PIN 1
#define GPIO_MAX_PIN 8

// GPIO 리턴 모드 (입력 변경 시 응답 포맷)
typedef enum {
    GPIO_RT_MODE_BYTES = 0,   // 2바이트로 리턴 (deviceid, low_byte, high_byte)
    GPIO_RT_MODE_CHANNEL = 1  // 채널별 리턴 (deviceid, channel, value)
} gpio_rt_mode_t;

// GPIO 동작 모드 (입력 변경 감지 방식)
typedef enum {
    GPIO_MODE_TOGGLE = 0,     // 입력 신호 변경 시마다 리턴
    GPIO_MODE_TRIGGER = 1     // ON->OFF 한 사이클 완료 시 리턴
} gpio_trigger_mode_t;

// GPIO 설정 구조체
typedef struct {
    uint8_t device_id;                // 디바이스 ID (1-254)
    bool auto_response;               // 자동 응답 여부
    gpio_rt_mode_t rt_mode;           // 리턴 모드 (BYTES/CHANNEL)
    gpio_trigger_mode_t trigger_mode; // 동작 모드 (TOGGLE/TRIGGER)
    uint32_t reserved;                // 향후 확장용
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
bool set_gpio_auto_response(bool enabled);
bool get_gpio_auto_response(void);
bool set_gpio_rt_mode(gpio_rt_mode_t mode);
gpio_rt_mode_t get_gpio_rt_mode(void);
bool set_gpio_trigger_mode(gpio_trigger_mode_t mode);
gpio_trigger_mode_t get_gpio_trigger_mode(void);

// GPIO 설정 한번에 갱신 및 저장
bool update_gpio_config(uint8_t device_id, bool auto_response, 
                        gpio_rt_mode_t rt_mode, gpio_trigger_mode_t trigger_mode);

// Global variables
extern uint16_t gpio_input_data;
extern uint16_t gpio_output_data;

#endif // GPIO_H