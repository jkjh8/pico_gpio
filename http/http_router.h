#ifndef HTTP_ROUTER_H
#define HTTP_ROUTER_H

#include <stdint.h>
#include <stdbool.h>
#include "http_server.h"

// 라우트 등록 함수
bool http_router_register(const char* uri, http_method_t method, http_handler_t handler);

// 핸들러 찾기 함수
http_handler_t http_router_find_handler(const char* uri, http_method_t method);

// 기본 라우트 처리 함수
void http_router_handle_default(const http_request_t* request, http_response_t* response);

// 라우터 초기화 함수
void http_router_init(void);

#endif // HTTP_ROUTER_H