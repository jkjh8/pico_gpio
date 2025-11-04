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
void http_handler_network_info(const http_request_t *request, http_response_t *response);
void http_handler_static_file(const http_request_t *request, http_response_t *response);
void http_handler_network_setup(const http_request_t *request, http_response_t *response);
void http_handler_control_info(const http_request_t *request, http_response_t *response);
void http_handler_control_setup(const http_request_t *request, http_response_t *response);

// GPIO 설정 API 핸들러
void http_handler_gpio_config_info(const http_request_t *request, http_response_t *response);
void http_handler_gpio_config_setup(const http_request_t *request, http_response_t *response);

// 전체 시스템 상태 API 핸들러
void http_handler_get_status(const http_request_t *request, http_response_t *response);

void http_handler_restart(const http_request_t *request, http_response_t *response);

// 헬퍼 함수들
void http_init_response(http_response_t *response);
void http_send_json_object(http_response_t *response, cJSON *json);
void http_send_error_response(http_response_t *response, http_status_t status, const char *message);
void http_send_success_response(http_response_t *response);

#endif // HTTP_HANDLERS_H
