#include "gpio.h"
#include "system/system_config.h"
#include "tcp/tcp_server.h"
#include "uart/uart_rs232.h"
#include "debug/debug.h"
#include "led/status_led.h"
#include <stdio.h>
#include <string.h>
#include <pico/stdio.h>

uint16_t gpio_input_data = 0xFFFF; // HCT165 이전 데이터
uint16_t gpio_output_data = 0x0000; // HCT595 출력 데이터

// GPIO 설정에 대한 매크로 (시스템 설정 참조)
#define gpio_config (*system_config_get_gpio())

// Forward declaration
static void send_gpio_response(uint16_t changed_bits, uint16_t current_data);

bool gpio_spi_init(void) {
    // SPI0 초기화 (Mode 0: CPOL=0, CPHA=0)
    spi_init(GPIO_PORT, 1000000); // 1MHz
    spi_set_format(GPIO_PORT, 8, SPI_CPOL_0, SPI_CPHA_0, SPI_MSB_FIRST);
    gpio_set_function(GPIO_SCK, GPIO_FUNC_SPI);
    gpio_set_function(GPIO_MOSI, GPIO_FUNC_SPI);
    gpio_set_function(GPIO_MISO, GPIO_FUNC_SPI);

    // 595 래치 핀 (STCP/RCLK) - GP8
    gpio_init(HCT595_LATCH_PIN);
    gpio_set_dir(HCT595_LATCH_PIN, GPIO_OUT);
    gpio_put(HCT595_LATCH_PIN, 1); // 초기 high

    // 595 클리어 핀 (SRCLR) - GP10
    gpio_init(HCT595_CLEAR_PIN);
    gpio_set_dir(HCT595_CLEAR_PIN, GPIO_OUT);
    gpio_put(HCT595_CLEAR_PIN, 1); // 클리어 해제 (HIGH)

    // 165 로드 핀 (SH/LD) - GP9
    gpio_init(HCT165_LOAD_PIN);
    gpio_set_dir(HCT165_LOAD_PIN, GPIO_OUT);
    gpio_put(HCT165_LOAD_PIN, 1); // 초기 high

    return true;
}

void hct595_write(uint16_t data) {
    // 출력 반전 (0이 ON, 1이 OFF인 경우 사용)
    uint16_t inverted_data = ~data;
    
    // HCT595는 MSB-first이므로 바이트 순서를 맞춰서 전송
    uint8_t buffer[2];
    buffer[0] = (inverted_data >> 8) & 0xFF;  // 상위 바이트 먼저
    buffer[1] = inverted_data & 0xFF;         // 하위 바이트 나중
    
    // 데이터를 시프트 레지스터에 전송
    spi_write_blocking(GPIO_PORT, buffer, 2);
    
    // 데이터를 출력 레지스터로 래치 (STCP 펄스: HIGH -> LOW)
    gpio_put(HCT595_LATCH_PIN, 0); // STCP low - 데이터 래치
    busy_wait_us(1);
    gpio_put(HCT595_LATCH_PIN, 1); // STCP high - 준비 상태
    
    // 전역 변수 업데이트
    gpio_output_data = data;
    
    // GPIO 출력 활동 LED 깜박임
    status_led_activity_blink();
    
    // 출력 피드백 전송 (바이너리 형식)
    if (gpio_config.auto_response) {
        char feedback[GPIO_MSG_MAX_LEN];
        char binary[17];
        int i;
        BaseType_t result;
        
        // 16비트를 바이너리 문자열로 변환 (LSB first)
        for (i = 0; i < 16; i++) {
            binary[i] = (gpio_output_data & (1 << i)) ? '1' : '0';
        }
        binary[16] = '\0';
        
        snprintf(feedback, sizeof(feedback),
                "outputs,%d,%s\r\n",
                gpio_config.device_id, binary);
        
        DBG_GPIO_PRINT("Output feedback: %s", feedback);
        
        // 모든 활성화된 큐에 전송
        for (i = 0; i < MAX_GPIO_QUEUES; i++) {
            if (gpio_queues[i] != NULL && gpio_queues_enabled[i]) {
                result = xQueueSend(gpio_queues[i], feedback, 0);
                DBG_GPIO_PRINT("  -> Q[%d] %s\n", i, result == pdTRUE ? "OK" : "FULL");
            }
        }
    }
}

// GPIO 입력 변경 응답 전송 (rt_mode에 따라 포맷 결정)
static void send_gpio_response(uint16_t changed_bits, uint16_t current_data) {
    char feedback[GPIO_MSG_MAX_LEN];
    BaseType_t result;
    int i;
    
    DBG_GPIO_PRINT("send_gpio_response: IN changed=0x%04X, data=0x%04X, mode=%d\n", 
                   changed_bits, current_data, gpio_config.rt_mode);
    
    // 입력 피드백 처리
    if (gpio_config.rt_mode == GPIO_RT_MODE_CHANNEL) {
        // CHANNEL 모드: 변경된 각 채널에 대해 개별 메시지 전송
        int channel;
        for (channel = 1; channel <= 16; channel++) {
            uint16_t mask = (1 << (channel - 1));
            if (changed_bits & mask) {
                bool value = (current_data & mask) ? true : false;
                
                snprintf(feedback, sizeof(feedback),
                        "input_channel,%d,%d,%s\r\n",
                        gpio_config.device_id, channel, value ? "1" : "0");
                
                DBG_GPIO_PRINT("Queue IN CH%d: %s", channel, feedback);
                
                // 모든 활성화된 큐에 전송
                for (i = 0; i < MAX_GPIO_QUEUES; i++) {
                    if (gpio_queues[i] != NULL && gpio_queues_enabled[i]) {
                        result = xQueueSend(gpio_queues[i], feedback, 0);
                        DBG_GPIO_PRINT("  -> Q[%d] %s\n", i, result == pdTRUE ? "OK" : "FULL");
                    }
                }
            }
        }
    } else {
        // BYTES 모드: 변경된 각 채널에 대해 개별 메시지 전송 (CHANNEL 모드와 동일)
        int channel;
        for (channel = 1; channel <= 16; channel++) {
            uint16_t mask = (1 << (channel - 1));
            if (changed_bits & mask) {
                bool value = (current_data & mask) ? true : false;
                
                snprintf(feedback, sizeof(feedback),
                        "input_channel,%d,%d,%s\r\n",
                        gpio_config.device_id, channel, value ? "1" : "0");
                
                DBG_GPIO_PRINT("Queue IN CH%d: %s", channel, feedback);
                
                // 모든 활성화된 큐에 전송
                for (i = 0; i < MAX_GPIO_QUEUES; i++) {
                    if (gpio_queues[i] != NULL && gpio_queues_enabled[i]) {
                        result = xQueueSend(gpio_queues[i], feedback, 0);
                        DBG_GPIO_PRINT("  -> Q[%d] %s\n", i, result == pdTRUE ? "OK" : "FULL");
                    }
                }
            }
        }
    }
}

uint16_t hct165_read(void) {
    gpio_put(HCT165_LOAD_PIN, 0); // SH/LD low (load)
    busy_wait_us(1);
    gpio_put(HCT165_LOAD_PIN, 1); // SH/LD high (shift)
    
    // 바이트 순서를 맞춰서 읽기
    uint8_t buffer[2];
    spi_read_blocking(GPIO_PORT, 0x00, buffer, 2);
    uint16_t current_data = (buffer[0] << 8) | buffer[1];
    
    // 변경 감지
    uint16_t changed_channels = gpio_input_data ^ current_data;
    
    // 값이 변경되었고 자동 응답이 활성화된 경우 피드백 전송
    if (changed_channels != 0 && gpio_config.auto_response) {
        // GPIO 입력 활동 LED 깜박임
        status_led_activity_blink();
        
        DBG_GPIO_PRINT("Input: 0x%04X->0x%04X\n", gpio_input_data, current_data);
        
        // rt_mode가 CHANNEL일 때만 trigger_mode 적용
        if (gpio_config.rt_mode == GPIO_RT_MODE_CHANNEL) {
            // CHANNEL 모드: trigger_mode에 따른 처리
            if (gpio_config.trigger_mode == GPIO_MODE_TRIGGER) {
                // TRIGGER 모드: 0->1 (OFF->ON) 변화 시 즉시 전송
                uint16_t rising_edge = changed_channels & current_data;
                
                // rising edge가 있으면 즉시 전송
                if (rising_edge != 0) {
                    DBG_GPIO_PRINT("Rising: 0x%04X\n", rising_edge);
                    send_gpio_response(rising_edge, current_data);
                }
            } else {
                // TOGGLE 모드: 변경된 채널 즉시 응답
                send_gpio_response(changed_channels, current_data);
            }
        } else {
            // BYTES 모드: 전체 상태 변경 시 즉시 응답 (trigger_mode 무시)
            send_gpio_response(changed_channels, current_data);
        }
        
        gpio_input_data = current_data;
    } else if (changed_channels != 0) {
        // 자동 응답이 비활성화되어 있음
        if (!gpio_config.auto_response) {
            DBG_GPIO_PRINT("Input: 0x%04X->0x%04X (auto_resp OFF)\n", gpio_input_data, current_data);
        }
        gpio_input_data = current_data;
    }
    
    return current_data;
}

// GPIO 설정을 플래시에 저장 (시스템 설정으로 통합)
void save_gpio_config_to_flash(void) {
    system_config_save_to_flash();
    DBG_GPIO_PRINT("[FLASH] GPIO 설정 저장 (시스템 설정): ID=0x%02X\n", 
        gpio_config.device_id);
}

// GPIO 설정을 플래시에서 로드 (시스템 설정에서 자동 로드됨)
void load_gpio_config_from_flash(void) {
    // 시스템 설정 초기화 시 자동으로 로드됨
    DBG_GPIO_PRINT("[FLASH] GPIO 설정 로드 (시스템 설정): ID=0x%02X, RT=%s, Trigger=%s\n", 
        gpio_config.device_id,
        gpio_config.rt_mode == GPIO_RT_MODE_CHANNEL ? "CHANNEL" : "BYTES",
        gpio_config.trigger_mode == GPIO_MODE_TRIGGER ? "TRIGGER" : "TOGGLE");
}

// GPIO 디바이스 ID 설정
bool set_gpio_device_id(uint8_t new_id) {
    if (new_id == 0x00 || new_id == 0xFF) {
        return false; // 유효하지 않은 ID
    }
    
    gpio_config.device_id = new_id;
    save_gpio_config_to_flash();
    return true;
}

// GPIO 디바이스 ID 반환
uint8_t get_gpio_device_id(void) {
    return gpio_config.device_id;
}

// GPIO 자동 응답 설정
bool set_gpio_auto_response(bool enabled) {
    gpio_config.auto_response = enabled;
    save_gpio_config_to_flash();
    return true;
}

// GPIO 자동 응답 반환
bool get_gpio_auto_response(void) {
    return gpio_config.auto_response;
}

// GPIO RT 모드 설정
bool set_gpio_rt_mode(gpio_rt_mode_t mode) {
    if (mode > GPIO_RT_MODE_CHANNEL) {
        return false;
    }
    
    gpio_config.rt_mode = mode;
    save_gpio_config_to_flash();
    return true;
}

// GPIO RT 모드 반환
gpio_rt_mode_t get_gpio_rt_mode(void) {
    return gpio_config.rt_mode;
}

// GPIO Trigger 모드 설정
bool set_gpio_trigger_mode(gpio_trigger_mode_t mode) {
    if (mode > GPIO_MODE_TRIGGER) {
        return false;
    }
    
    gpio_config.trigger_mode = mode;
    save_gpio_config_to_flash();
    return true;
}

// GPIO Trigger 모드 반환
gpio_trigger_mode_t get_gpio_trigger_mode(void) {
    return gpio_config.trigger_mode;
}

// GPIO 설정 한번에 갱신 및 저장
bool update_gpio_config(uint8_t device_id, bool auto_response,
                        gpio_rt_mode_t rt_mode, gpio_trigger_mode_t trigger_mode) {
    // 유효성 검사
    if (device_id < 1 || device_id > 254) {
        DBG_GPIO_PRINT("[GPIO] Invalid device_id: %d\n", device_id);
        return false;
    }
    if (rt_mode > GPIO_RT_MODE_CHANNEL) {
        DBG_GPIO_PRINT("[GPIO] Invalid rt_mode: %d\n", rt_mode);
        return false;
    }
    if (trigger_mode > GPIO_MODE_TRIGGER) {
        DBG_GPIO_PRINT("[GPIO] Invalid trigger_mode: %d\n", trigger_mode);
        return false;
    }
    
    // 설정 갱신
    gpio_config.device_id = device_id;
    gpio_config.auto_response = auto_response;
    gpio_config.rt_mode = rt_mode;
    gpio_config.trigger_mode = trigger_mode;
    
    DBG_GPIO_PRINT("[GPIO] Config updated: ID=%d, AutoResp=%d, RT=%d, Trigger=%d\n", 
        gpio_config.device_id, gpio_config.auto_response,
        gpio_config.rt_mode, gpio_config.trigger_mode);
    
    // 플래시에 저장
    save_gpio_config_to_flash();
    return true;
}