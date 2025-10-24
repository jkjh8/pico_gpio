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
    if (strstr((const char *)uri_name, "network.cgi")) {
        printf("[CGI] network.cgi requested\n");

        char json_buf[1024];
        memset(json_buf, 0, sizeof(json_buf));
        size_t json_len = network_get_info_json(json_buf, sizeof(json_buf));

        if (json_len == 0) {
            printf("[CGI] network_get_info_json failed or empty\n");
            strcpy((char *)buf, "{\"status\":\"error\"}");
            *len = strlen((char *)buf);
            return 1;
        }

        const size_t max_out = 1024;
        int l = snprintf((char *)buf, max_out, "%s", json_buf);
        if (l < 0) l = 0;
        if ((size_t)l >= max_out) l = max_out - 1;

        *len = (uint16_t)l;
        buf[*len] = 0;

        // W5500 내부가 len==0이면 500을 리턴하므로 안전장치
        if (*len == 0) {
            strcpy((char *)buf, "{}");
            *len = 2;
        }

        printf("[CGI] network.cgi response OK, %d bytes\n", *len);
        return 1;
    }




    printf("[CGI] Unknown CGI: %s\n", uri_name);
    return 0;
}

uint8_t predefined_set_cgi_processor(uint8_t * uri_name, uint8_t * uri, uint8_t * buf, uint16_t * len) {
    if (!uri_name || !uri || !buf || !len) return 0;

    printf("[CGI] Processing POST request: %s, URI: %s\n", uri_name, uri);

    // gpio.cgi POST handling: look in uri (may contain body) for pin/value
    if (strstr((const char *)uri_name, "gpio.cgi")) {
        const char *s = (const char *)uri;
        printf("[CGI] gpio.cgi POST, URI content: %s\n", s);
        
        const char *pinp = strstr(s, "pin=");
        const char *valp = strstr(s, "value=");
        if (pinp && valp) {
            int pin = atoi(pinp + 4);
            int val = atoi(valp + 6);
            printf("[CGI] gpio.cgi POST pin=%d, value=%d\n", pin, val);
            
            gpio_pin_set(pin, val != 0);
            char json_buf[128];
            int json_len = snprintf(json_buf, sizeof(json_buf), "{\"pin\":%d,\"value\":%d,\"status\":\"ok\"}", pin, val);
            if (json_len < 0) return 0;
            int l = snprintf((char *)buf, *len, "%s", json_buf);
            if (l < 0 || l >= *len) return 0;
            *len = (uint16_t)l;
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
