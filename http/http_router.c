#include "http_router.h"
#include "http_handlers.h"
#include <string.h>

// 간단한 핸들러 테이블 (실제 구현에서는 더 동적인 방식 사용 가능)
typedef struct {
    char uri[64];
    http_method_t method;
    http_handler_t handler;
} http_route_t;

#define MAX_ROUTES 10
static http_route_t routes[MAX_ROUTES];
static uint8_t route_count = 0;

// 라우터 초기화
void http_router_init(void)
{
    route_count = 0;

    // 기본 라우트 등록 (루트 경로는 정적 파일로 처리하므로 제외)
    // 백업용 시스템 라우트들 (SPA가 없을 경우를 위한)
    http_router_register("/api/network", HTTP_GET, http_handler_network_info);
    http_router_register("/api/network", HTTP_POST, http_handler_network_setup);
    http_router_register("/api/control", HTTP_GET, http_handler_control_info);
    http_router_register("/api/control", HTTP_POST, http_handler_control_setup);
    // GPIO 설정 API
    http_router_register("/api/gpio", HTTP_GET, http_handler_gpio_config_info);
    http_router_register("/api/gpio", HTTP_POST, http_handler_gpio_config_setup);
    // 시스템 재시작 API
    http_router_register("/api/restart", HTTP_GET, http_handler_restart);
}

// 라우트 등록 함수
bool http_router_register(const char* uri, http_method_t method, http_handler_t handler)
{
    if(route_count < MAX_ROUTES)
    {
        strncpy(routes[route_count].uri, uri, sizeof(routes[route_count].uri) - 1);
        routes[route_count].method = method;
        routes[route_count].handler = handler;
        route_count++;
        return true;
    }
    return false;
}

// 핸들러 찾기 함수
http_handler_t http_router_find_handler(const char* uri, http_method_t method)
{
    for(uint8_t i = 0; i < route_count; i++)
    {
        if(strcmp(routes[i].uri, uri) == 0 && routes[i].method == method)
        {
            return routes[i].handler;
        }
    }
    return NULL;
}

// 기본 라우트 처리 함수
void http_router_handle_default(const http_request_t* request, http_response_t* response)
{
    if(strncmp(request->uri, "/api/", 5) == 0) {
        // API 엔드포인트이지만 핸들러가 없는 경우
        response->status = HTTP_NOT_FOUND;
        strcpy(response->content_type, "application/json");
        strcpy(response->content, "{\"error\":\"API endpoint not found\"}");
        response->content_length = strlen(response->content);
    }
    else if(strcmp(request->uri, "/") == 0) {
        // 루트 경로는 index.html로 처리
        http_request_t index_request = *request;
        strcpy(index_request.uri, "/index.html");
        http_handler_static_file(&index_request, response);
    }
    else {
        // 정적 파일 시도
        http_handler_static_file(request, response);

        // SPA 폴백 처리
        if (response->status == HTTP_NOT_FOUND) {
            const char* file_ext = strrchr(request->uri, '.');
            if (!file_ext || strcmp(file_ext, ".html") == 0) {
                // SPA 라우트로 판단되면 index.html로 폴백
                http_request_t fallback_request = *request;
                strcpy(fallback_request.uri, "/index.html");
                http_handler_static_file(&fallback_request, response);
            }
        }
    }
}