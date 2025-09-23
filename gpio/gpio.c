#include "gpio.h"
#include <stdio.h>
#include <pico/stdio.h>

uint16_t gpio_input_data = 0xFFFF; // HCT165 이전 데이터
uint16_t gpio_output_data = 0x0000; // HCT595 출력 데이터

bool gpio_spi_init(void) {
    // SPI1 초기화
    spi_init(GPIO_PORT, 1000000); // 1MHz
    gpio_set_function(GPIO_SCK, GPIO_FUNC_SPI);
    gpio_set_function(GPIO_MOSI, GPIO_FUNC_SPI);
    gpio_set_function(GPIO_MISO, GPIO_FUNC_SPI);

    // 595 클리어 핀 (SRCLR)
    gpio_init(HCT595_CLEAR_PIN);
    gpio_set_dir(HCT595_CLEAR_PIN, GPIO_OUT);
    gpio_put(HCT595_CLEAR_PIN, 0); // 클리어
    sleep_us(1);
    gpio_put(HCT595_CLEAR_PIN, 1); // not clear

    // 165 로드 핀 (SH/LD)
    gpio_init(HCT165_LOAD_PIN);
    gpio_set_dir(HCT165_LOAD_PIN, GPIO_OUT);
    gpio_put(HCT165_LOAD_PIN, 1); // 초기 high

    return true;
}

void hct595_write(uint16_t data) {
    spi_write_blocking(GPIO_PORT, &data, 2);
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