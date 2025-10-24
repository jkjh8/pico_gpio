#include "debug.h"
#include <string.h>

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
