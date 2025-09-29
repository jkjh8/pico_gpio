#include "gpio.h"
#include <stdio.h>
#include <pico/stdio.h>

uint16_t gpio_input_data = 0xFFFF; // HCT165 이전 데이터
uint16_t gpio_output_data = 0x0000; // HCT595 출력 데이터

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
    uint16_t data;
    spi_read_blocking(GPIO_PORT, 0x00, (uint8_t*)&data, 2);
    if (data != gpio_input_data) {
        printf("HCT165 value changed: 0x%04X\n", data);
        gpio_input_data = data;
    }
    return data;
}