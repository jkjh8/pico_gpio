// Centralized debug toggles and macros (moved to debug/debug.h)
#ifndef DEBUG_DEBUG_H
#define DEBUG_DEBUG_H

#include <stdio.h>
#include <stdbool.h>
#include "hardware/flash.h"

// Store debug settings near end of flash (5th 4KB page from the end)
#define DEBUG_SETTINGS_FLASH_OFFSET (PICO_FLASH_SIZE_BYTES - 20480)

// Per-category compile-time enable flags (set to 1 to include code, 0 to remove entirely)
#ifndef DBG_MAIN
#define DBG_MAIN 0
#endif
#ifndef DBG_NET
#define DBG_NET 0
#endif
#ifndef DBG_TCP
#define DBG_TCP 0
#endif
#ifndef DBG_HTTP
#define DBG_HTTP 0
#endif
#ifndef DBG_UART
#define DBG_UART 0
#endif
#ifndef DBG_JSON
#define DBG_JSON 0
#endif
#ifndef DBG_DHCP
#define DBG_DHCP 0
#endif
#ifndef DBG_WIZNET
#define DBG_WIZNET 0
#endif
#ifndef DBG_GPIO
#define DBG_GPIO 0
#endif

// Enable runtime toggling by default
#ifndef DBG_RUNTIME
#define DBG_RUNTIME 1
#endif

// Category identifiers
typedef enum {
    DBG_CAT_MAIN = 0,
    DBG_CAT_NET,
    DBG_CAT_TCP,
    DBG_CAT_HTTP,
    DBG_CAT_UART,
    DBG_CAT_JSON,
    DBG_CAT_GPIO,
    DBG_CAT_DHCP,
    DBG_CAT_WIZNET,
    DBG_CAT_COUNT
} debug_category_t;

// Runtime API (implemented in debug/debug.c)
void debug_set(debug_category_t cat, bool enabled);
bool debug_get(debug_category_t cat);
// Set by category name (case-insensitive). Returns true on success, false if unknown category.
bool debug_set_by_name(const char* name, bool enabled);
bool debug_get_by_name(const char* name, bool* out_enabled);

// Initialize runtime flags from compile-time defaults
void debug_init_from_compile_time_defaults(void);

// Initialize debug system (apply compile-time defaults, then override from flash)
void debug_init(void);

// Persist/restore runtime debug settings
bool debug_save_to_flash(void);
bool debug_load_from_flash(void);

// Internal helper used by macros
static inline bool debug_is_enabled(debug_category_t cat) {
#if DBG_RUNTIME
    return debug_get(cat);
#else
    (void)cat;
    return true;
#endif
}

// Per-category print macros: if compile-time flag is 0, remove completely
#if DBG_MAIN
#define DBG_MAIN_PRINT(fmt, ...) do { if (debug_is_enabled(DBG_CAT_MAIN)) printf("[MAIN] " fmt, ##__VA_ARGS__); } while(0)
#else
#define DBG_MAIN_PRINT(fmt, ...) ((void)0)
#endif

#if DBG_NET
#define DBG_NET_PRINT(fmt, ...) do { if (debug_is_enabled(DBG_CAT_NET)) printf("[NET] " fmt, ##__VA_ARGS__); } while(0)
#else
#define DBG_NET_PRINT(fmt, ...) ((void)0)
#endif

#if DBG_TCP
#define DBG_TCP_PRINT(fmt, ...) do { if (debug_is_enabled(DBG_CAT_TCP)) printf("[TCP] " fmt, ##__VA_ARGS__); } while(0)
#else
#define DBG_TCP_PRINT(fmt, ...) ((void)0)
#endif

#if DBG_HTTP
#define DBG_HTTP_PRINT(fmt, ...) do { if (debug_is_enabled(DBG_CAT_HTTP)) printf("[HTTP] " fmt, ##__VA_ARGS__); } while(0)
#else
#define DBG_HTTP_PRINT(fmt, ...) ((void)0)
#endif

#if DBG_UART
#define DBG_UART_PRINT(fmt, ...) do { if (debug_is_enabled(DBG_CAT_UART)) printf("[UART] " fmt, ##__VA_ARGS__); } while(0)
#else
#define DBG_UART_PRINT(fmt, ...) ((void)0)
#endif

#if DBG_JSON
#define DBG_JSON_PRINT(fmt, ...) do { if (debug_is_enabled(DBG_CAT_JSON)) printf("[JSON] " fmt, ##__VA_ARGS__); } while(0)
#else
#define DBG_JSON_PRINT(fmt, ...) ((void)0)
#endif

#if DBG_DHCP
#define DBG_DHCP_PRINT(fmt, ...) do { if (debug_is_enabled(DBG_CAT_DHCP)) printf("[DHCP] " fmt, ##__VA_ARGS__); } while(0)
#else
#define DBG_DHCP_PRINT(fmt, ...) ((void)0)
#endif

#if DBG_WIZNET
#define DBG_WIZNET_PRINT(fmt, ...) do { if (debug_is_enabled(DBG_CAT_WIZNET)) printf("[WIZNET] " fmt, ##__VA_ARGS__); } while(0)
#else
#define DBG_WIZNET_PRINT(fmt, ...) ((void)0)
#endif

#if DBG_GPIO
#define DBG_GPIO_PRINT(fmt, ...) do { if (debug_is_enabled(DBG_CAT_GPIO)) printf("[GPIO] " fmt, ##__VA_ARGS__); } while(0)
#else
#define DBG_GPIO_PRINT(fmt, ...) ((void)0)
#endif

// Helper: default mapping for generic logs
#define DBG_PRINT(fmt, ...) DBG_MAIN_PRINT(fmt, ##__VA_ARGS__)

#endif // DEBUG_DEBUG_H
