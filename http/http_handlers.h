#ifndef HTTP_HANDLERS_H
#define HTTP_HANDLERS_H

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "network/network_config.h"
#include "http/http_server.h"
#include "static_files.h"
#include "main.h"
#include "cJSON.h"

#include "tcp/tcp_server.h"
#include "uart/uart_rs232.h"

// HTML 템플릿 상수들
#define HTML_ROOT_TEMPLATE \
    "<!DOCTYPE html>\n" \
    "<html><head><title>Raspberry Pi Pico W5500</title></head>\n" \
    "<body>\n" \
    "<h1>Welcome to Raspberry Pi Pico W5500</h1>\n" \
    "<p>HTTP Server is running!</p>\n" \
    "<h2>System Pages</h2>\n" \
    "<ul>\n" \
    "<li><a href=\"/status\">System Status</a></li>\n" \
    "<li><a href=\"/\">SPA Application (if available)</a></li>\n" \
    "</ul>\n" \
    "<h2>API Endpoints</h2>\n" \
    "<ul>\n" \
    "<li><a href=\"/api/info\">System Info</a></li>\n" \
    "<li><a href=\"/api/network\">Network Info</a></li>\n" \
    "<li><a href=\"/api/gpio\">GPIO Status</a></li>\n" \
    "<li><a href=\"/api/files\">Embedded Files</a></li>\n" \
    "</ul>\n" \
    "<h2>GPIO Control</h2>\n" \
    "<p>Use POST requests to /api/gpio with JSON: {\"led\":\"on\"} or {\"led\":\"off\"}</p>\n" \
    "</body></html>"

// 네트워크 설정 관련 상수들
#define NETWORK_FIELD_COUNT 4
#define NETWORK_FIELDS {"ip", "subnet", "gateway", "dns"}
#define NETWORK_JSON_KEYS {"ip", "subnet", "gateway", "dns"}
#define NETWORK_INFO_KEYS {"mac", "ip", "subnet", "gateway", "dns"}
#define NETWORK_INFO_COUNT 5

// 유틸리티 매크로
#define PARSE_IP_ADDRESS(ip_str, target_array) \
    sscanf(ip_str, "%hhu.%hhu.%hhu.%hhu", \
           &target_array[0], &target_array[1], &target_array[2], &target_array[3])

// HTTP 핸들러 함수 선언
void http_handler_root(const http_request_t *request, http_response_t *response);
void http_handler_network_info(const http_request_t *request, http_response_t *response);
void http_handler_static_file(const http_request_t *request, http_response_t *response);
void http_handler_network_setup(const http_request_t *request, http_response_t *response);
void http_handler_control_info(const http_request_t *request, http_response_t *response);
void http_handler_control_setup(const http_request_t *request, http_response_t *response);

void http_handler_restart(const http_request_t *request, http_response_t *response);

#endif // HTTP_HANDLERS_H
