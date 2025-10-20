#include "gpio.h"
#include "tcp/tcp_server.h"
#include "uart/uart_rs232.h"
#include <stdio.h>
#include <string.h>
#include <pico/stdio.h>

uint16_t gpio_input_data = 0xFFFF; // HCT165 이전 데이터
uint16_t gpio_output_data = 0x0000; // HCT595 출력 데이터

// GPIO 설정 (기본값)
gpio_config_t gpio_config = {
    .device_id = 0x01,
    .comm_mode = GPIO_MODE_TEXT,
    .auto_response = true,
    .reserved = 0
};

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
    // HCT595는 MSB-first이므로 바이트 순서를 맞춰서 전송
    uint8_t buffer[2];
    buffer[0] = (data >> 8) & 0xFF;  // 상위 바이트 먼저
    buffer[1] = data & 0xFF;         // 하위 바이트 나중
    
    // 데이터를 시프트 레지스터에 전송
    spi_write_blocking(GPIO_PORT, buffer, 2);
    
    // 데이터를 출력 레지스터로 래치 (STCP 펄스: HIGH -> LOW)
    gpio_put(HCT595_LATCH_PIN, 0); // STCP low - 데이터 래치
    sleep_us(1);
    gpio_put(HCT595_LATCH_PIN, 1); // STCP high - 준비 상태
    
    // 전역 변수 업데이트
    gpio_output_data = data;
}

uint16_t hct165_read(void) {
    gpio_put(HCT165_LOAD_PIN, 0); // SH/LD low (load)
    sleep_us(1);
    gpio_put(HCT165_LOAD_PIN, 1); // SH/LD high (shift)
    
    // 바이트 순서를 맞춰서 읽기 (HCT165 연결 순서에 따라 조정)
    uint8_t buffer[2];
    spi_read_blocking(GPIO_PORT, 0x00, buffer, 2);
    uint16_t data = (buffer[0] << 8) | buffer[1];  // 상위 바이트를 buffer[0]으로
    
    // 값이 변경되었고 자동 응답이 활성화된 경우 피드백 전송
    if (data != gpio_input_data && gpio_config.auto_response) {
        printf("HCT165 value changed: 0x%04X -> 0x%04X\n", gpio_input_data, data);
        gpio_input_data = data;
        
        // 피드백 메시지 생성
        char feedback[1024];
        
        if (gpio_config.comm_mode == GPIO_MODE_JSON) {
            // JSON 모드 피드백 - 핀별 배열 형태 (16채널)
            char input_array[512] = "";
            char temp[32];
            
            // 입력 핀 상태를 배열로 구성 (1-16)
            for (int pin = 1; pin <= 16; pin++) {
                bool value = (data & (1 << (pin - 1))) ? true : false;
                snprintf(temp, sizeof(temp), "%s{\"%d\":%s}", 
                        (pin > 1) ? "," : "", pin, value ? "true" : "false");
                strcat(input_array, temp);
            }
            
            // 출력 핀 상태를 배열로 구성 (1-16)
            char output_array[512] = "";
            
            for (int pin = 1; pin <= 16; pin++) {
                bool value = (gpio_output_data & (1 << (pin - 1))) ? true : false;
                snprintf(temp, sizeof(temp), "%s{\"%d\":%s}", 
                        (pin > 1) ? "," : "", pin, value ? "true" : "false");
                strcat(output_array, temp);
            }
            
            snprintf(feedback, sizeof(feedback),
                    "{\"device_id\":%d,\"event\":\"input_changed\",\"input\":[%s],\"output\":[%s]}\r\n",
                    gpio_config.device_id, input_array, output_array);
        } else {
            // 텍스트 모드 피드백 - 2바이트로 분리 (low_byte, high_byte)
            uint8_t low_byte = (uint8_t)(data & 0xFF);
            uint8_t high_byte = (uint8_t)((data >> 8) & 0xFF);
            snprintf(feedback, sizeof(feedback),
                    "inputchanged,%d,%d,%d\r\n",
                    gpio_config.device_id, low_byte, high_byte);
        }
        
        // TCP 클라이언트들에게 브로드캐스트
        tcp_servers_broadcast((uint8_t*)feedback, strlen(feedback));
        
        // UART로 전송
        uart_rs232_write(RS232_PORT_1, (uint8_t*)feedback, strlen(feedback));
    } else if (data != gpio_input_data) {
        // 자동 응답이 비활성화되어 있어도 로그는 출력
        printf("HCT165 value changed: 0x%04X -> 0x%04X (auto_response OFF)\n", gpio_input_data, data);
        gpio_input_data = data;
    }
    
    return data;
}

// GPIO 설정을 플래시에 저장
void save_gpio_config_to_flash(void) {
    uint32_t ints = save_and_disable_interrupts();
    
    flash_range_erase(GPIO_CONFIG_FLASH_OFFSET, 4096);
    flash_range_program(GPIO_CONFIG_FLASH_OFFSET, (const uint8_t*)&gpio_config, sizeof(gpio_config));
    restore_interrupts(ints);
    
    printf("[FLASH] GPIO 설정 저장: ID=0x%02X, Mode=%s, AutoResp=%s\n", 
           gpio_config.device_id,
           gpio_config.comm_mode == GPIO_MODE_JSON ? "JSON" : "TEXT",
           gpio_config.auto_response ? "ON" : "OFF");
}

// GPIO 설정을 플래시에서 로드
void load_gpio_config_from_flash(void) {
    const uint8_t* flash_ptr = (const uint8_t*)(XIP_BASE + GPIO_CONFIG_FLASH_OFFSET);
    gpio_config_t stored_config;
    
    memcpy(&stored_config, flash_ptr, sizeof(stored_config));
    
    // 유효성 검사
    if (stored_config.device_id == 0xFF || stored_config.device_id == 0x00 ||
        stored_config.comm_mode > GPIO_MODE_JSON) {
        // 기본값 사용
        gpio_config.device_id = 0x01;
        gpio_config.comm_mode = GPIO_MODE_TEXT;
        gpio_config.auto_response = true;
        gpio_config.reserved = 0;
        printf("[FLASH] GPIO 설정 초기화: 기본값 사용\n");
    } else {
        gpio_config = stored_config;
        printf("[FLASH] GPIO 설정 불러오기: ID=0x%02X, Mode=%s, AutoResp=%s\n", 
               gpio_config.device_id,
               gpio_config.comm_mode == GPIO_MODE_JSON ? "JSON" : "TEXT",
               gpio_config.auto_response ? "ON" : "OFF");
    }
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
    printf("[GPIO] get_gpio_device_id: %d\n", gpio_config.device_id);
    return gpio_config.device_id;
}

// GPIO 통신 모드 설정
bool set_gpio_comm_mode(gpio_comm_mode_t mode) {
    if (mode > GPIO_MODE_JSON) {
        return false;
    }
    
    gpio_config.comm_mode = mode;
    save_gpio_config_to_flash();
    return true;
}

// GPIO 통신 모드 반환
gpio_comm_mode_t get_gpio_comm_mode(void) {
    return gpio_config.comm_mode;
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

// GPIO 설정 한번에 갱신 및 저장
bool update_gpio_config(uint8_t device_id, gpio_comm_mode_t comm_mode, bool auto_response) {
    // 유효성 검사
    if (device_id < 1 || device_id > 254) {
        printf("[GPIO] Invalid device_id: %d\n", device_id);
        return false;
    }
    if (comm_mode > GPIO_MODE_JSON) {
        printf("[GPIO] Invalid comm_mode: %d\n", comm_mode);
        return false;
    }
    
    // 설정 갱신
    gpio_config.device_id = device_id;
    gpio_config.comm_mode = comm_mode;
    gpio_config.auto_response = auto_response;
    
    printf("[GPIO] Config updated: ID=%d, Mode=%d, AutoResp=%d\n", 
           gpio_config.device_id, gpio_config.comm_mode, gpio_config.auto_response);
    
    // 플래시에 저장
    save_gpio_config_to_flash();
    return true;
}