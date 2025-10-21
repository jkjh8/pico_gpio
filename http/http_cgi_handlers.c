#include "http_cgi_handlers.h"
#include "http/http_server.h"
#include "ioLibrary_Driver/Internet/httpServer/httpParser.h"
#include "network/network_config.h"
#include "gpio/gpio.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h> // for atoi()

// Simple CGI handler: uri starts with '/cgi/' and next token indicates action
void http_cgi_handler(st_http_request *request) {
    // request->URI is a byte array, null-terminated
    uint8_t *uri = request->URI;

    // If URI contains "gpio.cgi" handle gpio
    if (strstr((char*)uri, "gpio.cgi") == (char*)uri) {
        // very small parser: find 'pin=' and 'action='
        uint8_t *pinp = (uint8_t *)strstr((char*)uri, "pin=");
        uint8_t *actionp = (uint8_t *)strstr((char*)uri, "action=");
        if (pinp && actionp) {
            int pin = atoi((char*)pinp + 4);
            char action[16];
            sscanf((char*)actionp + 7, "%15s", action);
            if (strcmp(action, "get") == 0) {
                bool v = gpio_pin_get(pin);
                char buf[128];
                sprintf(buf, "HTTP/1.1 200 OK\r\nContent-Type: application/json\r\n\r\n{\"pin\":%d,\"value\":%s}", pin, v?"true":"false");
                send(HTTP_SOCKET_NUM, (uint8_t*)buf, strlen(buf));
                return;
            } else if (strcmp(action, "set") == 0) {
                uint8_t *valp = (uint8_t *)strstr((char*)uri, "value=");
                if (valp) {
                    int val = atoi((char*)valp + 6);
                    gpio_pin_set(pin, val!=0);
                    char buf[128];
                    sprintf(buf, "HTTP/1.1 200 OK\r\nContent-Type: application/json\r\n\r\n{\"pin\":%d,\"value\":%d,\"status\":\"ok\"}", pin, val);
                    send(HTTP_SOCKET_NUM, (uint8_t*)buf, strlen(buf));
                    return;
                }
            }
        }

        // If URI contains "network.cgi" handle network info
        if (strstr((char*)uri, "network.cgi") == (char*)uri) {
            char jsonbuf[256];
            size_t n = network_get_info_json(jsonbuf, sizeof(jsonbuf));
            if (n > 0) {
                char hdr[64];
                int hl = sprintf(hdr, "HTTP/1.1 200 OK\r\nContent-Type: application/json\r\n\r\n");
                send(HTTP_SOCKET_NUM, (uint8_t*)hdr, hl);
                send(HTTP_SOCKET_NUM, (uint8_t*)jsonbuf, (uint16_t)n);
                return;
            }
        }
    }

    // Default: 404
    send(HTTP_SOCKET_NUM, (uint8_t*)"HTTP/1.1 404 Not Found\r\nContent-Type: text/plain\r\n\r\nCGI not found", 55);
}
