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

    printf("[CGI] Processing GET request: %s\n", uri_name);

    // network.cgi
    if (strncmp((const char *)uri_name, "network.cgi", strlen("network.cgi")) == 0) {
        printf("[CGI] network.cgi requested\n");
        size_t n = network_get_info_json((char *)buf, 512);
        if (n == 0) {
            printf("[CGI] network_get_info_json failed\n");
            return 0;
        }
        *len = (uint16_t)n;
        printf("[CGI] network.cgi response: %d bytes\n", *len);
        return 1;
    }

    // gpio.cgi?pin=1&action=get
    if (strncmp((const char *)uri_name, "gpio.cgi", strlen("gpio.cgi")) == 0) {
        const char *s = (const char *)uri_name;
        printf("[CGI] gpio.cgi requested: %s\n", s);
        
        const char *pinp = strstr(s, "pin=");
        const char *actionp = strstr(s, "action=");
        
        // 파라미터가 없는 경우 기본 GPIO 정보 반환
        if (!pinp || !actionp) {
            printf("[CGI] gpio.cgi without parameters, returning GPIO info\n");
            int l = snprintf((char *)buf, 512, 
                "{\"status\":\"ok\",\"message\":\"Use ?pin=1&action=get or ?pin=1&action=set&value=0/1\"}");
            if (l < 0) return 0;
            *len = (uint16_t)l;
            return 1;
        }
        
        if (pinp && actionp) {
            int pin = atoi(pinp + 4);
            char action[16] = {0};
            sscanf(actionp + 7, "%15s", action);
            printf("[CGI] gpio.cgi pin=%d, action=%s\n", pin, action);
            
            if (strcmp(action, "get") == 0) {
                bool v = gpio_pin_get(pin);
                int l = snprintf((char *)buf, 512, "{\"pin\":%d,\"value\":%s}", pin, v ? "true" : "false");
                if (l < 0) return 0;
                *len = (uint16_t)l;
                printf("[CGI] gpio.cgi get response: %s\n", buf);
                return 1;
            } else if (strcmp(action, "set") == 0) {
                const char *valp = strstr(s, "value=");
                if (valp) {
                    int val = atoi(valp + 6);
                    gpio_pin_set(pin, val != 0);
                    int l = snprintf((char *)buf, 512, "{\"pin\":%d,\"value\":%d,\"status\":\"ok\"}", pin, val);
                    if (l < 0) return 0;
                    *len = (uint16_t)l;
                    printf("[CGI] gpio.cgi set response: %s\n", buf);
                    return 1;
                }
            }
        }
        
        printf("[CGI] gpio.cgi invalid parameters\n");
        return 0;
    }

    printf("[CGI] Unknown CGI: %s\n", uri_name);
    return 0;
}

uint8_t predefined_set_cgi_processor(uint8_t * uri_name, uint8_t * uri, uint8_t * buf, uint16_t * en) {
    if (!uri_name || !uri || !buf || !en) return 0;

    printf("[CGI] Processing POST request: %s, URI: %s\n", uri_name, uri);

    // gpio.cgi POST handling: look in uri (may contain body) for pin/value
    if (strncmp((const char *)uri_name, "gpio.cgi", strlen("gpio.cgi")) == 0) {
        const char *s = (const char *)uri;
        printf("[CGI] gpio.cgi POST, URI content: %s\n", s);
        
        const char *pinp = strstr(s, "pin=");
        const char *valp = strstr(s, "value=");
        if (pinp && valp) {
            int pin = atoi(pinp + 4);
            int val = atoi(valp + 6);
            printf("[CGI] gpio.cgi POST pin=%d, value=%d\n", pin, val);
            
            gpio_pin_set(pin, val != 0);
            int l = snprintf((char *)buf, 128, "{\"pin\":%d,\"value\":%d,\"status\":\"ok\"}", pin, val);
            if (l < 0) return 0;
            *en = (uint16_t)l;
            printf("[CGI] gpio.cgi POST response: %s\n", buf);
            return 1;
        } else {
            printf("[CGI] gpio.cgi POST missing pin/value parameters\n");
        }
        return 0;
    }

    printf("[CGI] Unknown POST CGI: %s\n", uri_name);
    return 0;
}
