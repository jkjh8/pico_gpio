#include "ioLibrary_Driver/Internet/httpServer/httpUtil.h"
#include "network/network_config.h"
#include "gpio/gpio.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

// Provide application-level implementations of the predefined CGI hooks.
// These will be linked instead of modifying vendor code.

uint8_t predefined_get_cgi_processor(uint8_t * uri_name, uint8_t * buf, uint16_t * len) {
    if (!uri_name || !buf || !len) return 0;

    // network.cgi
    if (strncmp((const char *)uri_name, "network.cgi", strlen("network.cgi")) == 0) {
        size_t n = network_get_info_json((char *)buf, 512);
        if (n == 0) return 0;
        *len = (uint16_t)n;
        return 1;
    }

    // gpio.cgi?pin=1&action=get
    if (strncmp((const char *)uri_name, "gpio.cgi", strlen("gpio.cgi")) == 0) {
        const char *s = (const char *)uri_name;
        const char *pinp = strstr(s, "pin=");
        const char *actionp = strstr(s, "action=");
        if (pinp && actionp) {
            int pin = atoi(pinp + 4);
            char action[16] = {0};
            sscanf(actionp + 7, "%15s", action);
            if (strcmp(action, "get") == 0) {
                bool v = gpio_pin_get(pin);
                int l = snprintf((char *)buf, 512, "{\"pin\":%d,\"value\":%s}", pin, v ? "true" : "false");
                if (l < 0) return 0;
                *len = (uint16_t)l;
                return 1;
            } else if (strcmp(action, "set") == 0) {
                const char *valp = strstr(s, "value=");
                if (valp) {
                    int val = atoi(valp + 6);
                    gpio_pin_set(pin, val != 0);
                    int l = snprintf((char *)buf, 512, "{\"pin\":%d,\"value\":%d,\"status\":\"ok\"}", pin, val);
                    if (l < 0) return 0;
                    *len = (uint16_t)l;
                    return 1;
                }
            }
        }
    }

    return 0;
}

uint8_t predefined_set_cgi_processor(uint8_t * uri_name, uint8_t * uri, uint8_t * buf, uint16_t * en) {
    if (!uri_name || !uri || !buf || !en) return 0;

    // gpio.cgi POST handling: look in uri (may contain body) for pin/value
    if (strncmp((const char *)uri_name, "gpio.cgi", strlen("gpio.cgi")) == 0) {
        const char *s = (const char *)uri;
        const char *pinp = strstr(s, "pin=");
        const char *valp = strstr(s, "value=");
        if (pinp && valp) {
            int pin = atoi(pinp + 4);
            int val = atoi(valp + 6);
            gpio_pin_set(pin, val != 0);
            int l = snprintf((char *)buf, 128, "{\"pin\":%d,\"value\":%d,\"status\":\"ok\"}", pin, val);
            if (l < 0) return 0;
            *en = (uint16_t)l;
            return 1;
        }
        return 0;
    }

    return 0;
}
