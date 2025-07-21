#ifndef HTTP_SERVER_H
#define HTTP_SERVER_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include "wizchip_conf.h"
#include "socket.h"

// ========================
// HTTP 서버 설정
// ========================
#define HTTP_SERVER_PORT        80
#define HTTP_SOCKET_NUM         1       // W5500 소켓 1번 사용 (8KB TX/RX 버퍼)
#define HTTP_BUF_SIZE           8192
#define MAX_URI_SIZE            512
#define MAX_CONTENT_SIZE        4096    // 안전한 크기로 설정 (W5500 버퍼의 절반)
#define STREAM_CHUNK_SIZE       2048    // 스트리밍 전송 시 청크 크기 (안전한 크기)

// 디버그 모드 (0: 비활성화, 1: 기본, 2: 상세)
#define HTTP_DEBUG_LEVEL        0

// ========================
// HTTP 열거형 타입
// ========================
typedef enum {
    HTTP_GET = 0,
    HTTP_POST,
    HTTP_PUT,
    HTTP_DELETE,
    HTTP_UNKNOWN
} http_method_t;

typedef enum {
    HTTP_OK = 200,
    HTTP_BAD_REQUEST = 400,
    HTTP_NOT_FOUND = 404,
    HTTP_METHOD_NOT_ALLOWED = 405,
    HTTP_INTERNAL_ERROR = 500
} http_status_t;

typedef enum {
    HTTP_SERVER_IDLE = 0,
    HTTP_SERVER_LISTENING,
    HTTP_SERVER_CONNECTED,
    HTTP_SERVER_ERROR
} http_server_state_t;

// ========================
// HTTP 구조체
// ========================
typedef struct {
    http_method_t method;
    char uri[MAX_URI_SIZE];
    char content[MAX_CONTENT_SIZE];
    uint16_t content_length;
} http_request_t;

typedef struct {
    http_status_t status;
    char content_type[64];
    char content[MAX_CONTENT_SIZE];
    uint16_t content_length;
    
    // 스트리밍 관련 필드
    bool stream_required;
    const char* stream_data;
    size_t stream_size;
    bool stream_compressed;
} http_response_t;

// ========================
// 콜백 함수 타입
// ========================
typedef void (*http_handler_t)(const http_request_t *request, http_response_t *response);

// ========================
// 서버 제어 함수
// ========================
bool http_server_init(uint16_t port);
void http_server_process(void);
void http_server_stop(void);
void http_server_cleanup(void);
http_server_state_t http_server_get_state(void);
void http_register_handler(const char* uri, http_method_t method, http_handler_t handler);

// ========================
// HTTP 응답 전송 함수
// ========================
void http_send_response(uint8_t sock, const http_response_t *response);
void http_send_json_response(uint8_t sock, http_status_t status, const char* json_data);
void http_send_html_response(uint8_t sock, http_status_t status, const char* html_data);
void http_send_404(uint8_t sock);
void http_send_large_file_stream(uint8_t sock, const char* file_data, size_t file_size, 
                                const char* content_type, bool is_compressed);

// ========================
// API 핸들러 함수
// ========================
void http_handler_root(const http_request_t *request, http_response_t *response);
void http_handler_status(const http_request_t *request, http_response_t *response);
void http_handler_api_info(const http_request_t *request, http_response_t *response);
void http_handler_api_network(const http_request_t *request, http_response_t *response);
void http_handler_api_gpio(const http_request_t *request, http_response_t *response);
void http_handler_api_gpio_control(const http_request_t *request, http_response_t *response);
void http_handler_api_files(const http_request_t *request, http_response_t *response);

// ========================
// 정적 파일 처리 함수
// ========================
void http_handler_static_file(const http_request_t *request, http_response_t *response);
const char* get_content_type(const char* file_extension);
const char* get_embedded_file(const char* path, size_t* file_size, bool* is_compressed, size_t* original_size);
const char* get_embedded_file_with_content_type(const char* path, size_t* file_size, 
                                               bool* is_compressed, size_t* original_size, 
                                               const char** content_type);

#endif // HTTP_SERVER_H
