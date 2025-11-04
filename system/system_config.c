#include "system_config.h"
#include "debug/debug.h"
#include "hardware/flash.h"
#include "hardware/sync.h"
#include "pico/stdlib.h"
#include <string.h>

// =============================================================================
// 상수 정의
// =============================================================================

#define FLASH_TARGET_OFFSET (PICO_FLASH_SIZE_BYTES - FLASH_SECTOR_SIZE)
#define SYSTEM_CONFIG_MAGIC 0x47504943  // "GPIC"

// =============================================================================
// 전역 변수
// =============================================================================

static system_config_t g_system_config;
static bool g_config_initialized = false;

// =============================================================================
// 내부 함수
// =============================================================================

// XOR 체크섬 계산
static uint32_t calculate_checksum(const system_config_t* config) {
    uint32_t checksum = 0;
    const uint32_t* data = (const uint32_t*)config;
    size_t word_count = (sizeof(system_config_t) - sizeof(uint32_t)) / sizeof(uint32_t);
    
    for (size_t i = 0; i < word_count; i++) {
        checksum ^= data[i];
    }
    
    return checksum;
}

// =============================================================================
// 공개 함수
// =============================================================================

// 기본값으로 초기화
void system_config_reset_to_defaults(void) {
    memset(&g_system_config, 0, sizeof(system_config_t));
    
    g_system_config.magic = SYSTEM_CONFIG_MAGIC;
    g_system_config.version = SYSTEM_CONFIG_VERSION;
    
    // GPIO 기본값
    g_system_config.gpio.device_id = 0x01;
    g_system_config.gpio.auto_response = true;
    g_system_config.gpio.rt_mode = GPIO_RT_MODE_CHANNEL;
    g_system_config.gpio.trigger_mode = GPIO_MODE_TOGGLE;
    g_system_config.gpio.reserved = 0;
    
    // 네트워크 기본값 (DHCP 활성화)
    g_system_config.network.dhcp = NETINFO_DHCP;
    memset(g_system_config.network.ip, 0, 4);
    memset(g_system_config.network.sn, 0, 4);
    memset(g_system_config.network.gw, 0, 4);
    memset(g_system_config.network.dns, 0, 4);
    
    // TCP 포트 기본값
    g_system_config.tcp_port = 5050;
    
    // UART 보드레이트 기본값
    g_system_config.uart_baud = 115200;
    
    // 멀티캐스트 기본값
    g_system_config.multicast_enabled = true;
    
    // 디버그 플래그 기본값 (모두 활성화)
    g_system_config.debug_flags = 0xFFFFFFFF;
    
    // 체크섬 계산
    g_system_config.checksum = calculate_checksum(&g_system_config);
    
    DBG_MAIN_PRINT("System config reset to defaults\n");
}

// Flash에 저장
bool system_config_save_to_flash(void) {
    // 체크섬 업데이트
    g_system_config.checksum = calculate_checksum(&g_system_config);
    
    // Flash 쓰기 (인터럽트 비활성화 필요)
    uint32_t ints = save_and_disable_interrupts();
    flash_range_erase(FLASH_TARGET_OFFSET, FLASH_SECTOR_SIZE);
    flash_range_program(FLASH_TARGET_OFFSET, (const uint8_t*)&g_system_config, sizeof(system_config_t));
    restore_interrupts(ints);
    
    DBG_MAIN_PRINT("System config saved to flash (size: %d bytes)\n", sizeof(system_config_t));
    return true;
}

// Flash에서 로드
bool system_config_load_from_flash(void) {
    const system_config_t* flash_config = (const system_config_t*)(XIP_BASE + FLASH_TARGET_OFFSET);
    
    // 매직 넘버 확인
    if (flash_config->magic != SYSTEM_CONFIG_MAGIC) {
        DBG_MAIN_PRINT("Invalid magic number in flash, using defaults\n");
        system_config_reset_to_defaults();
        return false;
    }
    
    // 버전 확인
    if (flash_config->version != SYSTEM_CONFIG_VERSION) {
        DBG_MAIN_PRINT("Config version mismatch (%u != %u), using defaults\n", 
                       flash_config->version, SYSTEM_CONFIG_VERSION);
        system_config_reset_to_defaults();
        return false;
    }
    
    // 체크섬 확인
    uint32_t stored_checksum = flash_config->checksum;
    system_config_t temp_config;
    memcpy(&temp_config, flash_config, sizeof(system_config_t));
    uint32_t calculated_checksum = calculate_checksum(&temp_config);
    
    if (stored_checksum != calculated_checksum) {
        DBG_MAIN_PRINT("Checksum mismatch (0x%08X != 0x%08X), using defaults\n", 
                       stored_checksum, calculated_checksum);
        system_config_reset_to_defaults();
        return false;
    }
    
    // 설정 복사
    memcpy(&g_system_config, flash_config, sizeof(system_config_t));
    
    DBG_MAIN_PRINT("System config loaded from flash\n");
    DBG_MAIN_PRINT("  GPIO Device ID: 0x%02X\n", g_system_config.gpio.device_id);
    DBG_MAIN_PRINT("  TCP Port: %u\n", g_system_config.tcp_port);
    DBG_MAIN_PRINT("  UART Baud: %u\n", g_system_config.uart_baud);
    DBG_MAIN_PRINT("  Multicast: %s\n", g_system_config.multicast_enabled ? "enabled" : "disabled");
    DBG_MAIN_PRINT("  Network DHCP: %s\n", g_system_config.network.dhcp == NETINFO_DHCP ? "enabled" : "disabled");
    
    return true;
}

// =============================================================================
// 초기화 및 접근 함수
// =============================================================================

// 시스템 설정 초기화
void system_config_init(void) {
    if (!g_config_initialized) {
        if (!system_config_load_from_flash()) {
            system_config_reset_to_defaults();
        }
        g_config_initialized = true;
    }
}

// 전체 설정 구조체 접근
system_config_t* system_config_get(void) {
    return &g_system_config;
}

// =============================================================================
// 개별 설정 접근 함수
// =============================================================================

gpio_config_t* system_config_get_gpio(void) {
    return &g_system_config.gpio;
}

wiz_NetInfo* system_config_get_network(void) {
    return &g_system_config.network;
}

uint16_t system_config_get_tcp_port(void) {
    return g_system_config.tcp_port;
}

void system_config_set_tcp_port(uint16_t port) {
    g_system_config.tcp_port = port;
}

uint32_t system_config_get_uart_baud(void) {
    return g_system_config.uart_baud;
}

void system_config_set_uart_baud(uint32_t baud) {
    g_system_config.uart_baud = baud;
}

bool system_config_get_multicast_enabled(void) {
    return g_system_config.multicast_enabled;
}

void system_config_set_multicast_enabled(bool enabled) {
    g_system_config.multicast_enabled = enabled;
}

uint32_t system_config_get_debug_flags(void) {
    return g_system_config.debug_flags;
}

void system_config_set_debug_flags(uint32_t flags) {
    g_system_config.debug_flags = flags;
}
