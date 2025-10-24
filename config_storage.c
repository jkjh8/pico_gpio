#include "config_storage.h"
#include "hardware/flash.h"
#include "pico/bootrom.h"
#include <string.h>
#include <stdio.h>
#include "debug/debug.h"

// Global config instance
static config_storage_t g_config;

// Load defaults for all configs
static void load_defaults(void) {
    g_config.tcp_port = 5050;
    g_config.uart_rs232_baud = 9600;
    memset(&g_config.network, 0, sizeof(wiz_NetInfo));
    g_config.network.mac[0] = 0x00;
    g_config.network.mac[1] = 0x08;
    g_config.network.mac[2] = 0xDC;
    g_config.network.ip[0] = 192; g_config.network.ip[1] = 168; g_config.network.ip[2] = 1; g_config.network.ip[3] = 100;
    g_config.network.sn[0] = 255; g_config.network.sn[1] = 255; g_config.network.sn[2] = 255; g_config.network.sn[3] = 0;
    g_config.network.dhcp = NETINFO_STATIC;
    g_config.gpio.device_id = 1;
    g_config.gpio.comm_mode = GPIO_MODE_TEXT;
    g_config.gpio.auto_response = true;
    g_config.gpio.reserved = 0;
}

// Validate loaded config
static bool validate_config(const config_storage_t* cfg) {
    if (cfg->magic != CONFIG_STORAGE_MAGIC) return false;
    if (cfg->version != CONFIG_STORAGE_VERSION) return false;
    // Add more validation as needed
    return true;
}

// Load from flash
static bool load_from_flash(void) {
    const uint8_t* flash_ptr = (const uint8_t*)(XIP_BASE + CONFIG_STORAGE_FLASH_OFFSET);
    config_storage_t temp;
    memcpy(&temp, flash_ptr, sizeof(temp));
    if (!validate_config(&temp)) {
        DBG_MAIN_PRINT("[CONFIG] No valid config found, using defaults\n");
        return false;
    }
    memcpy(&g_config, &temp, sizeof(g_config));
    DBG_MAIN_PRINT("[CONFIG] Config loaded from flash\n");
    return true;
}

// Save to flash
static void save_to_flash(void) {
    g_config.magic = CONFIG_STORAGE_MAGIC;
    g_config.version = CONFIG_STORAGE_VERSION;

    uint32_t ints = save_and_disable_interrupts();
    flash_range_erase(CONFIG_STORAGE_FLASH_OFFSET, 4096);
    flash_range_program(CONFIG_STORAGE_FLASH_OFFSET, (const uint8_t*)&g_config, sizeof(g_config));
    restore_interrupts(ints);
    DBG_MAIN_PRINT("[CONFIG] Config saved to flash\n");
}

// Public API
void config_storage_init(void) {
    load_defaults();
    load_from_flash();
    // Now apply loaded values to global variables
    extern uint16_t tcp_port;
    extern uint32_t uart_rs232_1_baud;
    extern wiz_NetInfo g_net_info;
    extern gpio_config_t gpio_config;

    tcp_port = g_config.tcp_port;
    uart_rs232_1_baud = g_config.uart_rs232_baud;
    memcpy(&g_net_info, &g_config.network, sizeof(wiz_NetInfo));
    memcpy(&gpio_config, &g_config.gpio, sizeof(gpio_config_t));
}

void config_storage_save(void) {
    // Update g_config from current globals
    extern uint16_t tcp_port;
    extern uint32_t uart_rs232_1_baud;
    extern wiz_NetInfo g_net_info;
    extern gpio_config_t gpio_config;

    g_config.tcp_port = tcp_port;
    g_config.uart_rs232_baud = uart_rs232_1_baud;
    memcpy(&g_config.network, &g_net_info, sizeof(wiz_NetInfo));
    memcpy(&g_config.gpio, &gpio_config, sizeof(gpio_config_t));

    save_to_flash();
}