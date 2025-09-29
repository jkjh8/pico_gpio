#ifndef GPIO_H
#define GPIO_H

#include <stdint.h>
#include <stdbool.h>
#include "hardware/spi.h"
#include "hardware/gpio.h"
#include "pico/stdlib.h"

// GPIO CONFIGURATION
#define GPIO_PORT spi1
#define GPIO_SCK 10
#define GPIO_MOSI 11
#define GPIO_MISO 12  // MISO on GP12
#define HCT595_CLEAR_PIN 14  // HCT595 SRCLR on GP14
#define HCT595_LATCH_PIN 3  // HCT595 RCLK on GP15
#define HCT165_LOAD_PIN 13   // HCT165 SH/LD on GP13

// Functions
bool gpio_spi_init(void);
void hct595_write(uint16_t data);
uint16_t hct165_read(void);

// Global variables
extern uint16_t gpio_input_data;
extern uint16_t gpio_output_data;

#endif // GPIO_H