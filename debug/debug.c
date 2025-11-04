#include "debug/debug.h"
#include "system/system_config.h"
#include <string.h>
#include "pico/bootrom.h" // for XIP_BASE
#include "hardware/flash.h"
#include "hardware/regs/addressmap.h"
#include "pico/stdlib.h"

// Runtime flags array
static bool debug_flags[DBG_CAT_COUNT];

// Map category strings to enum
static const struct {
    const char* name;
    debug_category_t cat;
} debug_map[] = {
    {"MAIN", DBG_CAT_MAIN},
    {"NET", DBG_CAT_NET},
    {"TCP", DBG_CAT_TCP},
    {"HTTP", DBG_CAT_HTTP},
    {"UART", DBG_CAT_UART},
    {"JSON", DBG_CAT_JSON},
    {"GPIO", DBG_CAT_GPIO},
    {"DHCP", DBG_CAT_DHCP},
    {"WIZNET", DBG_CAT_WIZNET},
};

void debug_init_from_compile_time_defaults(void) {
#ifdef DBG_MAIN
    debug_flags[DBG_CAT_MAIN] = (DBG_MAIN) ? true : false;
#else
    debug_flags[DBG_CAT_MAIN] = false;
#endif
#ifdef DBG_NET
    debug_flags[DBG_CAT_NET] = (DBG_NET) ? true : false;
#else
    debug_flags[DBG_CAT_NET] = false;
#endif
#ifdef DBG_TCP
    debug_flags[DBG_CAT_TCP] = (DBG_TCP) ? true : false;
#else
    debug_flags[DBG_CAT_TCP] = false;
#endif
#ifdef DBG_HTTP
    debug_flags[DBG_CAT_HTTP] = (DBG_HTTP) ? true : false;
#else
    debug_flags[DBG_CAT_HTTP] = false;
#endif
#ifdef DBG_UART
    debug_flags[DBG_CAT_UART] = (DBG_UART) ? true : false;
#else
    debug_flags[DBG_CAT_UART] = false;
#endif
#ifdef DBG_JSON
    debug_flags[DBG_CAT_JSON] = (DBG_JSON) ? true : false;
#else
    debug_flags[DBG_CAT_JSON] = false;
#endif
#ifdef DBG_GPIO
    debug_flags[DBG_CAT_GPIO] = (DBG_GPIO) ? true : false;
#else
    debug_flags[DBG_CAT_GPIO] = false;
#endif
#ifdef DBG_DHCP
    debug_flags[DBG_CAT_DHCP] = (DBG_DHCP) ? true : false;
#else
    debug_flags[DBG_CAT_DHCP] = false;
#endif
#ifdef DBG_WIZNET
    debug_flags[DBG_CAT_WIZNET] = (DBG_WIZNET) ? true : false;
#else
    debug_flags[DBG_CAT_WIZNET] = false;
#endif
}

void debug_set(debug_category_t cat, bool enabled) {
    if (cat < 0 || cat >= DBG_CAT_COUNT) return;
    debug_flags[cat] = enabled;
}

bool debug_get(debug_category_t cat) {
    if (cat < 0 || cat >= DBG_CAT_COUNT) return false;
    return debug_flags[cat];
}

bool debug_set_by_name(const char* name, bool enabled) {
    if (name == NULL) return false;
    for (size_t i = 0; i < sizeof(debug_map)/sizeof(debug_map[0]); ++i) {
        if (strcasecmp(name, debug_map[i].name) == 0) {
            debug_set(debug_map[i].cat, enabled);
            return true;
        }
    }
    return false;
}

bool debug_get_by_name(const char* name, bool* out_enabled) {
    if (name == NULL || out_enabled == NULL) return false;
    for (size_t i = 0; i < sizeof(debug_map)/sizeof(debug_map[0]); ++i) {
        if (strcasecmp(name, debug_map[i].name) == 0) {
            *out_enabled = debug_get(debug_map[i].cat);
            return true;
        }
    }
    return false;
}

// Flash format for debug settings
struct debug_flash_t {
    uint32_t magic;
    uint16_t version;
    uint8_t flags[DBG_CAT_COUNT];
};

#define DEBUG_FLASH_MAGIC 0x44454247u // 'DEBG'
#define DEBUG_FLASH_VERSION 1

bool debug_save_to_flash(void) {
    uint32_t flags_value = 0;
    for (size_t i = 0; i < DBG_CAT_COUNT && i < 32; ++i) {
        if (debug_flags[i]) {
            flags_value |= (1u << i);
        }
    }
    
    system_config_set_debug_flags(flags_value);
    system_config_save_to_flash();
    DBG_MAIN_PRINT("[FLASH] Debug settings saved (system config)\n");
    return true;
}

bool debug_load_from_flash(void) {
    uint32_t flags_value = system_config_get_debug_flags();
    
    // 모든 비트가 1이면 기본값 (최초 실행)
    if (flags_value == 0xFFFFFFFF) {
        DBG_MAIN_PRINT("[FLASH] Using compile-time debug defaults\n");
        return false;
    }
    
    for (size_t i = 0; i < DBG_CAT_COUNT && i < 32; ++i) {
        debug_flags[i] = (flags_value & (1u << i)) != 0;
    }
    DBG_MAIN_PRINT("[FLASH] Debug settings loaded (system config)\n");
    return true;
}

void debug_init(void) {
    debug_init_from_compile_time_defaults();
    // Try to override compile-time defaults with stored runtime settings
    debug_load_from_flash();
}
