#include "system_config.h"
#include "debug/debug.h"
#include "hardware/flash.h"
#include "hardware/sync.h"
#include "pico/stdlib.h"
#include "FreeRTOS.h"
#include "task.h"
#include <string.h>

// =============================================================================
// 플래시 주소
// =============================================================================

#define FLASH_TARGET_OFFSET (PICO_FLASH_SIZE_BYTES - FLASH_SECTOR_SIZE)

// =============================================================================
// 슬롯 레이아웃
//
// 각 슬롯: [데이터 N바이트][XOR CRC 1바이트]  — 합계 N+1 바이트
// 오프셋은 영구 고정. 필드 추가 시 반드시 끝(다음_오프셋: 85)에만 추가.
// 기존 슬롯의 offset/size 절대 변경 금지.
//
//  offset  size  field
//   0       1     gpio.device_id
//   2       1     gpio.auto_response
//   4       4     gpio.rt_mode
//   9       4     gpio.trigger_mode
//  14       1     gpio.output_invert
//  16       6     network.mac
//  23       4     network.ip
//  28       4     network.sn
//  33       4     network.gw
//  38       4     network.dns
//  43       4     network.dhcp  (uint32_t)
//  48       2     tcp_port
//  51       4     uart_baud
//  56       1     multicast_enabled
//  58       1     has_last_dhcp_ip
//  60       4     last_dhcp_ip
//  65       4     last_dhcp_gw
//  70       4     last_dhcp_sn
//  75       4     last_dhcp_dns
//  80       4     debug_flags
//  다음 오프셋: 85
// =============================================================================

// =============================================================================
// 구버전 바이너리 포맷 (OTA 후 마이그레이션 전용)
// 매직/버전/XOR 체크섬을 포함한 원래 구조체
// =============================================================================

#define LEGACY_MAGIC   0x47504943UL  // "GPIC"

typedef struct {
    uint32_t magic;
    uint32_t version;
    gpio_config_t gpio;
    wiz_NetInfo network;
    uint16_t tcp_port;
    uint32_t uart_baud;
    bool multicast_enabled;
    uint8_t last_dhcp_ip[4];
    uint8_t last_dhcp_gw[4];
    uint8_t last_dhcp_sn[4];
    uint8_t last_dhcp_dns[4];
    bool has_last_dhcp_ip;
    uint32_t debug_flags;
    uint32_t checksum;
} system_config_legacy_t;

// =============================================================================
// 전역 변수
// =============================================================================

static system_config_t g_system_config;
static bool g_config_initialized = false;

// =============================================================================
// 슬롯 헬퍼
// =============================================================================

// 데이터 바이트 XOR
static uint8_t slot_crc(const uint8_t *data, size_t len) {
    uint8_t crc = 0;
    for (size_t i = 0; i < len; i++) crc ^= data[i];
    return crc;
}

// 슬롯 유효성: CRC 일치 AND all-0xFF(소거 상태) 아닐 것
static bool slot_valid(const uint8_t *base, uint16_t offset, size_t len) {
    const uint8_t *d = base + offset;
    bool all_ff = true;
    for (size_t i = 0; i < len; i++) {
        if (d[i] != 0xFF) { all_ff = false; break; }
    }
    if (all_ff) return false;
    return d[len] == slot_crc(d, len);
}

// 버퍼에 슬롯(데이터 + CRC) 기록
static void slot_write(uint8_t *buf, uint16_t offset, const uint8_t *data, size_t len) {
    memcpy(buf + offset, data, len);
    buf[offset + len] = slot_crc(data, len);
}

// =============================================================================
// 기본값 초기화
// =============================================================================

void system_config_reset_to_defaults(void) {
    memset(&g_system_config, 0, sizeof(system_config_t));

    g_system_config.gpio.device_id    = 0x01;
    g_system_config.gpio.auto_response = true;
    g_system_config.gpio.rt_mode      = GPIO_RT_MODE_CHANNEL;
    g_system_config.gpio.trigger_mode = GPIO_MODE_TOGGLE;
    g_system_config.gpio.output_invert = false;

    g_system_config.network.dhcp = NETINFO_DHCP;
    g_system_config.tcp_port     = 5050;
    g_system_config.uart_baud    = 115200;
    g_system_config.multicast_enabled = true;
    g_system_config.debug_flags  = 0xFFFFFFFF;

    DBG_MAIN_PRINT("System config reset to defaults\n");
}

// =============================================================================
// 플래시 저장 (슬롯 방식)
// =============================================================================

bool system_config_save_to_flash(void) {
    uint8_t buf[FLASH_PAGE_SIZE];
    memset(buf, 0xFF, sizeof(buf));  // 미기록 슬롯은 소거 상태 유지

    uint8_t  u8;
    uint16_t u16;
    uint32_t u32;

    // gpio
    u8 = g_system_config.gpio.device_id;
    slot_write(buf, 0, &u8, 1);

    u8 = g_system_config.gpio.auto_response ? 1 : 0;
    slot_write(buf, 2, &u8, 1);

    u32 = (uint32_t)g_system_config.gpio.rt_mode;
    slot_write(buf, 4, (uint8_t *)&u32, 4);

    u32 = (uint32_t)g_system_config.gpio.trigger_mode;
    slot_write(buf, 9, (uint8_t *)&u32, 4);

    u8 = g_system_config.gpio.output_invert ? 1 : 0;
    slot_write(buf, 14, &u8, 1);

    // network
    slot_write(buf, 16, g_system_config.network.mac, 6);
    slot_write(buf, 23, g_system_config.network.ip,  4);
    slot_write(buf, 28, g_system_config.network.sn,  4);
    slot_write(buf, 33, g_system_config.network.gw,  4);
    slot_write(buf, 38, g_system_config.network.dns, 4);

    u32 = (uint32_t)g_system_config.network.dhcp;
    slot_write(buf, 43, (uint8_t *)&u32, 4);

    // misc
    u16 = g_system_config.tcp_port;
    slot_write(buf, 48, (uint8_t *)&u16, 2);

    u32 = g_system_config.uart_baud;
    slot_write(buf, 51, (uint8_t *)&u32, 4);

    u8 = g_system_config.multicast_enabled ? 1 : 0;
    slot_write(buf, 56, &u8, 1);

    u8 = g_system_config.has_last_dhcp_ip ? 1 : 0;
    slot_write(buf, 58, &u8, 1);

    slot_write(buf, 60, g_system_config.last_dhcp_ip,  4);
    slot_write(buf, 65, g_system_config.last_dhcp_gw,  4);
    slot_write(buf, 70, g_system_config.last_dhcp_sn,  4);
    slot_write(buf, 75, g_system_config.last_dhcp_dns, 4);

    u32 = g_system_config.debug_flags;
    slot_write(buf, 80, (uint8_t *)&u32, 4);

    // 플래시 쓰기
    bool scheduler_running = (xTaskGetSchedulerState() != taskSCHEDULER_NOT_STARTED);
    if (scheduler_running) vTaskSuspendAll();
    uint32_t ints = save_and_disable_interrupts();
    flash_range_erase(FLASH_TARGET_OFFSET, FLASH_SECTOR_SIZE);
    flash_range_program(FLASH_TARGET_OFFSET, buf, FLASH_PAGE_SIZE);
    restore_interrupts(ints);
    if (scheduler_running) xTaskResumeAll();

    // 검증
    const uint8_t *rb = (const uint8_t *)(XIP_BASE + FLASH_TARGET_OFFSET);
    if (memcmp(buf, rb, FLASH_PAGE_SIZE) != 0) {
        DBG_MAIN_PRINT("[FLASH] ERROR: write verification failed\n");
        return false;
    }
    DBG_MAIN_PRINT("[FLASH] Config saved OK\n");
    return true;
}

// =============================================================================
// 플래시 로드 (슬롯 방식)
//
// 호출 전 system_config_reset_to_defaults() 필수.
// 유효한 슬롯만 g_system_config에 덮어씀. 무효 슬롯은 기본값 유지.
// 반환값: true = 모든 슬롯 유효, false = 하나 이상 무효(기본값 사용)
// =============================================================================

bool system_config_load_from_flash(void) {
    const uint8_t *fl = (const uint8_t *)(XIP_BASE + FLASH_TARGET_OFFSET);
    bool all_ok = true;
    uint32_t u32;
    uint16_t u16;

    // gpio.device_id
    if (slot_valid(fl, 0, 1)) {
        g_system_config.gpio.device_id = fl[0];
    } else { all_ok = false; }

    // gpio.auto_response
    if (slot_valid(fl, 2, 1)) {
        g_system_config.gpio.auto_response = (fl[2] != 0);
    } else { all_ok = false; }

    // gpio.rt_mode
    if (slot_valid(fl, 4, 4)) {
        memcpy(&u32, fl + 4, 4);
        g_system_config.gpio.rt_mode = (gpio_rt_mode_t)u32;
    } else { all_ok = false; }

    // gpio.trigger_mode
    if (slot_valid(fl, 9, 4)) {
        memcpy(&u32, fl + 9, 4);
        g_system_config.gpio.trigger_mode = (gpio_trigger_mode_t)u32;
    } else { all_ok = false; }

    // gpio.output_invert
    if (slot_valid(fl, 14, 1)) {
        g_system_config.gpio.output_invert = (fl[14] != 0);
    } else { all_ok = false; }

    // network.mac
    if (slot_valid(fl, 16, 6)) {
        memcpy(g_system_config.network.mac, fl + 16, 6);
    } else { all_ok = false; }

    // network.ip
    if (slot_valid(fl, 23, 4)) {
        memcpy(g_system_config.network.ip, fl + 23, 4);
    } else { all_ok = false; }

    // network.sn
    if (slot_valid(fl, 28, 4)) {
        memcpy(g_system_config.network.sn, fl + 28, 4);
    } else { all_ok = false; }

    // network.gw
    if (slot_valid(fl, 33, 4)) {
        memcpy(g_system_config.network.gw, fl + 33, 4);
    } else { all_ok = false; }

    // network.dns
    if (slot_valid(fl, 38, 4)) {
        memcpy(g_system_config.network.dns, fl + 38, 4);
    } else { all_ok = false; }

    // network.dhcp
    if (slot_valid(fl, 43, 4)) {
        memcpy(&u32, fl + 43, 4);
        g_system_config.network.dhcp = (dhcp_mode)u32;
    } else { all_ok = false; }

    // tcp_port
    if (slot_valid(fl, 48, 2)) {
        memcpy(&u16, fl + 48, 2);
        g_system_config.tcp_port = u16;
    } else { all_ok = false; }

    // uart_baud
    if (slot_valid(fl, 51, 4)) {
        memcpy(&g_system_config.uart_baud, fl + 51, 4);
    } else { all_ok = false; }

    // multicast_enabled
    if (slot_valid(fl, 56, 1)) {
        g_system_config.multicast_enabled = (fl[56] != 0);
    } else { all_ok = false; }

    // has_last_dhcp_ip
    if (slot_valid(fl, 58, 1)) {
        g_system_config.has_last_dhcp_ip = (fl[58] != 0);
    } else { all_ok = false; }

    // last_dhcp_ip/gw/sn/dns
    if (slot_valid(fl, 60, 4)) {
        memcpy(g_system_config.last_dhcp_ip, fl + 60, 4);
    } else { all_ok = false; }

    if (slot_valid(fl, 65, 4)) {
        memcpy(g_system_config.last_dhcp_gw, fl + 65, 4);
    } else { all_ok = false; }

    if (slot_valid(fl, 70, 4)) {
        memcpy(g_system_config.last_dhcp_sn, fl + 70, 4);
    } else { all_ok = false; }

    if (slot_valid(fl, 75, 4)) {
        memcpy(g_system_config.last_dhcp_dns, fl + 75, 4);
    } else { all_ok = false; }

    // debug_flags
    if (slot_valid(fl, 80, 4)) {
        memcpy(&g_system_config.debug_flags, fl + 80, 4);
    } else { all_ok = false; }

    if (all_ok) {
        DBG_MAIN_PRINT("[FLASH] Config loaded OK (slot format)\n");
    } else {
        DBG_MAIN_PRINT("[FLASH] Config loaded with some defaults (dirty)\n");
    }
    return all_ok;
}

// =============================================================================
// OTA 후 구버전 바이너리 포맷 마이그레이션
//
// 신규 슬롯 CRC 검사 실패 시 호출.
// 구버전 매직/버전/체크섬을 확인하고 g_system_config에 복사.
// 성공하면 true 반환 (caller가 새 포맷으로 저장).
// =============================================================================

static uint32_t legacy_checksum(const system_config_legacy_t *cfg) {
    uint32_t chk = 0;
    const uint32_t *p = (const uint32_t *)cfg;
    size_t words = (sizeof(system_config_legacy_t) - sizeof(uint32_t)) / sizeof(uint32_t);
    for (size_t i = 0; i < words; i++) chk ^= p[i];
    return chk;
}

static bool try_migrate_legacy(void) {
    const system_config_legacy_t *old =
        (const system_config_legacy_t *)(XIP_BASE + FLASH_TARGET_OFFSET);

    if (old->magic != LEGACY_MAGIC) return false;
    if (old->version < 1 || old->version > 10) return false;  // 범위 초과는 손상으로 간주
    if (old->checksum != legacy_checksum(old)) return false;

    // 유효한 구버전 데이터 → 마이그레이션
    g_system_config.gpio             = old->gpio;
    g_system_config.network          = old->network;
    g_system_config.tcp_port         = old->tcp_port;
    g_system_config.uart_baud        = old->uart_baud;
    g_system_config.multicast_enabled = old->multicast_enabled;
    memcpy(g_system_config.last_dhcp_ip,  old->last_dhcp_ip,  4);
    memcpy(g_system_config.last_dhcp_gw,  old->last_dhcp_gw,  4);
    memcpy(g_system_config.last_dhcp_sn,  old->last_dhcp_sn,  4);
    memcpy(g_system_config.last_dhcp_dns, old->last_dhcp_dns, 4);
    g_system_config.has_last_dhcp_ip = old->has_last_dhcp_ip;
    g_system_config.debug_flags      = old->debug_flags;

    DBG_MAIN_PRINT("[FLASH] Legacy config (v%u) migrated to slot format\n",
                   (unsigned)old->version);
    return true;
}

// =============================================================================
// 초기화
// =============================================================================

void system_config_init(void) {
    if (g_config_initialized) return;

    system_config_reset_to_defaults();

    if (!system_config_load_from_flash()) {
        // 슬롯 CRC 불일치 → 구버전 포맷 마이그레이션 시도
        if (try_migrate_legacy()) {
            DBG_MAIN_PRINT("[FLASH] Migration OK, saving slot format\n");
        } else {
            DBG_MAIN_PRINT("[FLASH] No valid config, saving defaults\n");
        }
        system_config_save_to_flash();
    }

    g_config_initialized = true;

    DBG_MAIN_PRINT("  GPIO Device ID: 0x%02X\n", g_system_config.gpio.device_id);
    DBG_MAIN_PRINT("  TCP Port: %u\n", g_system_config.tcp_port);
    DBG_MAIN_PRINT("  UART Baud: %u\n", g_system_config.uart_baud);
    DBG_MAIN_PRINT("  Multicast: %s\n",
                   g_system_config.multicast_enabled ? "enabled" : "disabled");
    DBG_MAIN_PRINT("  Network DHCP: %s\n",
                   g_system_config.network.dhcp == NETINFO_DHCP ? "enabled" : "disabled");
}

// =============================================================================
// 개별 설정 접근 함수
// =============================================================================

system_config_t *system_config_get(void)         { return &g_system_config; }
gpio_config_t   *system_config_get_gpio(void)    { return &g_system_config.gpio; }
wiz_NetInfo     *system_config_get_network(void) { return &g_system_config.network; }

uint16_t system_config_get_tcp_port(void)              { return g_system_config.tcp_port; }
void     system_config_set_tcp_port(uint16_t port)     { g_system_config.tcp_port = port; }
uint32_t system_config_get_uart_baud(void)             { return g_system_config.uart_baud; }
void     system_config_set_uart_baud(uint32_t baud)    { g_system_config.uart_baud = baud; }
bool     system_config_get_multicast_enabled(void)     { return g_system_config.multicast_enabled; }
void     system_config_set_multicast_enabled(bool en)  { g_system_config.multicast_enabled = en; }
uint32_t system_config_get_debug_flags(void)           { return g_system_config.debug_flags; }
void     system_config_set_debug_flags(uint32_t flags) { g_system_config.debug_flags = flags; }
