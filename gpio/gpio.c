#include "gpio.h"
#include "system/system_config.h"
#include "tcp/tcp_server.h"
#include "uart/uart_rs232.h"
#include "debug/debug.h"
#include "led/status_led.h"
#include <stdio.h>
#include <string.h>
#include <pico/stdio.h>
#include "FreeRTOS.h"
#include "task.h"

uint16_t gpio_input_data = 0xFFFF; // 입력 레지스터 이전 데이터
uint16_t gpio_output_data = 0x0000; // 출력 레지스터 데이터

// GPIO 설정에 대한 매크로 (시스템 설정 참조)
#define gpio_config (*system_config_get_gpio())

// ============================================================================
// GPIO 피드백 전송 (내부 함수)
// ============================================================================

// GPIO 입출력 피드백 전송 (통합)
static void send_gpio_feedback(bool is_input, uint16_t data, uint16_t changed_bits) {
    if (!gpio_config.auto_response) {
        return;
    }
    
    char feedback[GPIO_MSG_MAX_LEN];
    int i;
    BaseType_t result;
    
    // 입력일 경우 trigger_mode 처리
    uint16_t bits_to_send = changed_bits;
    bool trigger_event = false;
    if (is_input && gpio_config.rt_mode == GPIO_RT_MODE_CHANNEL) {
        if (gpio_config.trigger_mode == GPIO_MODE_TRIGGER) {
            // TRIGGER 모드: 버튼 눌림 시점(1->0, 풀업 회로의 falling edge)에 즉시 전송.
            // 릴리즈(0->1)는 무시 — 사이클당 1회만 전송되며, 예전처럼 릴리즈까지
            // 기다리지 않으므로 반응이 빠르다. 메시지 값은 프로토콜 호환을 위해
            // 항상 1(트리거 발생)로 전송한다.
            bits_to_send = changed_bits & (uint16_t)~data;  // falling edge만 (눌림)
            if (bits_to_send == 0) {
                return;  // 전송할 눌림 이벤트가 없으면 리턴 (릴리즈 무시)
            }
            trigger_event = true;
            DBG_GPIO_PRINT("Trigger mode: pressed=0x%04X\n", bits_to_send);
        }
    }
    
    // 모드에 따라 피드백 형식 결정
    if (gpio_config.rt_mode == GPIO_RT_MODE_CHANNEL) {
        // CHANNEL 모드
        if (is_input) {
            // 입력: 변경된 각 채널에 대해 개별 메시지 전송
            int channel;
            for (channel = 1; channel <= 16; channel++) {
                uint16_t mask = (1 << (channel - 1));
                if (bits_to_send & mask) {
                    // 트리거 이벤트는 항상 1로 전송 (눌림 시점의 레지스터 값은 0이지만
                    // 컨트롤러는 in,id,ch,1 을 "버튼 눌림"으로 해석함)
                    bool value = trigger_event ? true : ((data & mask) != 0);

                    snprintf(feedback, sizeof(feedback),
                            "in,%d,%d,%s\r\n",
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
            // 출력: 바이너리 형식으로 전체 상태 전송
            char binary[17];
            for (i = 0; i < 16; i++) {
                binary[i] = (data & (1 << i)) ? '1' : '0';
            }
            binary[16] = '\0';
            
            snprintf(feedback, sizeof(feedback),
                    "out,%d,%s\r\n",
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
    } else {
        // BYTES 모드: 2바이트 형식
        uint8_t low_byte = (uint8_t)(data & 0xFF);
        uint8_t high_byte = (uint8_t)((data >> 8) & 0xFF);
        
        snprintf(feedback, sizeof(feedback),
                "%s,%d,%d,%d\r\n",
                is_input ? "inb" : "outb",
                gpio_config.device_id, low_byte, high_byte);
        
        DBG_GPIO_PRINT("%s feedback: %s", is_input ? "Input" : "Output", feedback);
        
        // 모든 활성화된 큐에 전송
        for (i = 0; i < MAX_GPIO_QUEUES; i++) {
            if (gpio_queues[i] != NULL && gpio_queues_enabled[i]) {
                result = xQueueSend(gpio_queues[i], feedback, 0);
                DBG_GPIO_PRINT("  -> Q[%d] %s\n", i, result == pdTRUE ? "OK" : "FULL");
            }
        }
    }
}

// ============================================================================
// GPIO 초기화 및 I/O 함수
// ============================================================================

bool gpio_spi_init(void) {
    // SPI0 초기화 (Mode 0: CPOL=0, CPHA=0)
    spi_init(GPIO_PORT, 1000000); // 1MHz
    spi_set_format(GPIO_PORT, 8, SPI_CPOL_0, SPI_CPHA_0, SPI_MSB_FIRST);
    gpio_set_function(GPIO_SCK, GPIO_FUNC_SPI);
    gpio_set_function(GPIO_MOSI, GPIO_FUNC_SPI);
    gpio_set_function(GPIO_MISO, GPIO_FUNC_SPI);

    // 595 래치 핀 (STCP/RCLK) - GP8
    gpio_init(OUTPUT_REG_LATCH_PIN);
    gpio_set_dir(OUTPUT_REG_LATCH_PIN, GPIO_OUT);
    gpio_put(OUTPUT_REG_LATCH_PIN, 1); // 초기 high

    // 165 로드 핀 (SH/LD) - GP9
    gpio_init(INPUT_REG_LOAD_PIN);
    gpio_set_dir(INPUT_REG_LOAD_PIN, GPIO_OUT);
    gpio_put(INPUT_REG_LOAD_PIN, 1); // 초기 high

    return true;
}

void output_reg_write(uint16_t data) {
    // MSB-first로 바이트 순서를 맞춰서 전송
    uint8_t buffer[2];
    uint16_t out = gpio_config.output_invert ? (uint16_t)(~data) : data;
    buffer[0] = (out >> 8) & 0xFF;   // 상위 바이트 먼저
    buffer[1] = out & 0xFF;          // 하위 바이트 나중
    
    // 데이터를 시프트 레지스터에 전송
    spi_write_blocking(GPIO_PORT, buffer, 2);
    
    // 데이터를 출력 레지스터로 래치 (STCP 펄스: HIGH -> LOW)
    gpio_put(OUTPUT_REG_LATCH_PIN, 0); // STCP low - 데이터 래치
    gpio_put(OUTPUT_REG_LATCH_PIN, 1); // STCP high - 준비 상태
    
    // 전역 변수 업데이트
    gpio_output_data = data;
    
    // GPIO 출력 활동 LED 깜박임
    status_led_activity_blink();
    
    // 출력 피드백 전송
    send_gpio_feedback(false, gpio_output_data, 0);
}

uint16_t input_reg_read(void) {
    gpio_put(INPUT_REG_LOAD_PIN, 0); // SH/LD low (load)
    gpio_put(INPUT_REG_LOAD_PIN, 1); // SH/LD high (shift)
    
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
        
        // 피드백 전송 (trigger_mode 처리는 send_gpio_feedback 내부에서)
        send_gpio_feedback(true, current_data, changed_channels);
        
        gpio_input_data = current_data;
    } else if (changed_channels != 0) {
        DBG_GPIO_PRINT("Input: 0x%04X->0x%04X (auto_resp OFF)\n", gpio_input_data, current_data);
        gpio_input_data = current_data;
    }
    
    return current_data;
}

// ============================================================================
// GPIO 설정 저장/로드
// ============================================================================

void save_gpio_config_to_flash(void) {
    system_config_save_to_flash();
    DBG_GPIO_PRINT("[FLASH] GPIO 설정 저장 (시스템 설정): ID=0x%02X\n", 
        gpio_config.device_id);
}

void load_gpio_config_from_flash(void) {
    // 시스템 설정 초기화 시 자동으로 로드됨
    DBG_GPIO_PRINT("[FLASH] GPIO 설정 로드 (시스템 설정): ID=0x%02X, RT=%s, Trigger=%s\n", 
        gpio_config.device_id,
        gpio_config.rt_mode == GPIO_RT_MODE_CHANNEL ? "CHANNEL" : "BYTES",
        gpio_config.trigger_mode == GPIO_MODE_TRIGGER ? "TRIGGER" : "TOGGLE");
}

// ============================================================================
// GPIO 설정 Getter/Setter
// ============================================================================

bool set_gpio_device_id(uint8_t new_id) {
    if (new_id == 0x00 || new_id == 0xFF) {
        return false; // 유효하지 않은 ID
    }
    
    gpio_config.device_id = new_id;
    save_gpio_config_to_flash();
    return true;
}

uint8_t get_gpio_device_id(void) {
    return gpio_config.device_id;
}

bool set_gpio_auto_response(bool enabled) {
    gpio_config.auto_response = enabled;
    save_gpio_config_to_flash();
    return true;
}

bool get_gpio_auto_response(void) {
    return gpio_config.auto_response;
}

bool set_gpio_rt_mode(gpio_rt_mode_t mode) {
    if (mode > GPIO_RT_MODE_CHANNEL) {
        return false;
    }
    
    gpio_config.rt_mode = mode;
    save_gpio_config_to_flash();
    return true;
}

gpio_rt_mode_t get_gpio_rt_mode(void) {
    return gpio_config.rt_mode;
}

bool set_gpio_trigger_mode(gpio_trigger_mode_t mode) {
    if (mode > GPIO_MODE_TRIGGER) {
        return false;
    }
    
    gpio_config.trigger_mode = mode;
    save_gpio_config_to_flash();
    return true;
}

gpio_trigger_mode_t get_gpio_trigger_mode(void) {
    return gpio_config.trigger_mode;
}

bool set_gpio_output_invert(bool invert) {
    gpio_config.output_invert = invert;
    // 현재 출력값을 새 극성으로 즉시 재출력
    output_reg_write(gpio_output_data);
    save_gpio_config_to_flash();
    return true;
}

bool get_gpio_output_invert(void) {
    return gpio_config.output_invert;
}

bool update_gpio_config(uint8_t device_id, bool auto_response,
                        gpio_rt_mode_t rt_mode, gpio_trigger_mode_t trigger_mode,
                        bool output_invert) {
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
    gpio_config.output_invert = output_invert;

    DBG_GPIO_PRINT("[GPIO] Config updated: ID=%d, AutoResp=%d, RT=%d, Trigger=%d, Invert=%d\n",
        gpio_config.device_id, gpio_config.auto_response,
        gpio_config.rt_mode, gpio_config.trigger_mode, gpio_config.output_invert);

    // 극성 변경 즉시 반영
    output_reg_write(gpio_output_data);

    // 플래시에 저장
    save_gpio_config_to_flash();
    return true;
}

void gpio_task(void *pvParameters)
{
    while (true) {
        input_reg_read();
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}