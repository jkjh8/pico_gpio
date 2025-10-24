#ifndef CONFIG_STORAGE_H
#define CONFIG_STORAGE_H

#include <stdint.h>
#include <stdbool.h>
#include "wizchip_conf.h"
#include "gpio/gpio.h"
#include "debug/debug.h"

// Unified configuration storage (single 4KB flash block)
#define CONFIG_STORAGE_FLASH_OFFSET (PICO_FLASH_SIZE_BYTES - 4096) // Last 4KB block

// Configuration structure
typedef struct {
    uint32_t magic;
    uint16_t version;
    uint16_t tcp_port;
    uint32_t uart_rs232_baud;
    wiz_NetInfo network;
    gpio_config_t gpio;
    uint8_t debug_flags[DBG_CAT_COUNT];
} config_storage_t;

#define CONFIG_STORAGE_MAGIC 0x434F4E46u // 'CONF'
#define CONFIG_STORAGE_VERSION 1

// API
void config_storage_init(void); // Load all configs on boot
void config_storage_save(void); // Save all configs to flash

#endif // CONFIG_STORAGE_H