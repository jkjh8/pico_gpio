#include "http_server.h"
#include "http_parser.h"
#include "http_router.h"
#include "http_response.h"
#include "debug/debug.h"
#include "led/status_led.h"
#include <string.h>
#include "http_parser.h"
#include "http_router.h"
#include "http_response.h"
#include "http_handlers.h"
#include "../network/network_config.h"

// 디버그 매크로

#undef HTTP_LOG
#define HTTP_LOG(fmt, ...)
#undef HTTP_DEBUG
#define HTTP_DEBUG(fmt, ...)

// 전역 변수
static uint8_t http_buf[HTTP_BUF_SIZE];
static uint16_t http_port = HTTP_SERVER_PORT;
static http_process_state_t http_process_state = STATE_HTTP_IDLE;  // HTTP 처리 상태 추가

// 타임아웃 관리 (ioLibrary 스타일)
volatile uint32_t http_server_tick_1s = 0;

// 콜백 함수 포인터 (ioLibrary 스타일)
static http_server_mcu_reset_callback http_server_mcu_reset_cb = NULL;
static http_server_wdt_reset_callback http_server_wdt_reset_cb = NULL;

#define KEEP_ALIVE_TIMEOUT_MS 5000  // 5초 Keep-Alive 타임아웃

// 내부 함수 선언

bool http_server_init(uint16_t port)
{
    // printf("HTTP 서버 시작 (포트: %d)\n", port);

    http_port = port;

    // 라우터 초기화
    http_router_init();

    // printf("HTTP 서버 초기화 완료 (포트: %d)\n", port);
    return true;
}

void http_server_process(void)
{
    uint8_t sock = HTTP_SOCKET_NUM;
    uint16_t size = 0;
    uint8_t current_status = getSn_SR(sock);
    
    // 네트워크 연결 상태 확인 - 재시작 로직 제거
    bool network_connected = network_is_connected();
    
    // 네트워크가 연결되지 않은 경우 서버 처리 중단
    if (!network_connected) {
        return;
    }
    
    switch(current_status)
    {
        case SOCK_CLOSED:
            if(socket(sock, Sn_MR_TCP, http_port, 0x00) == sock) {
                http_process_state = STATE_HTTP_IDLE;
            }
            break;
            
        case SOCK_INIT:
            if(listen(sock) == SOCK_OK) {
                http_process_state = STATE_HTTP_IDLE;
            }
            break;
            
        case SOCK_LISTEN:
            // server_state = HTTP_SERVER_LISTENING;
            break;
            
        case SOCK_ESTABLISHED:
            // HTTP Process states (ioLibrary 스타일)
            switch(http_process_state)
            {
                case STATE_HTTP_IDLE:
                    if ((size = getSn_RX_RSR(sock)) > 0) {
                        // HTTP 요청 수신 시 LED 깜빡임
                        status_led_activity_blink();
                        
                        if(size > HTTP_BUF_SIZE) size = HTTP_BUF_SIZE - 1;
                        
                        size = recv(sock, http_buf, size);
                        http_buf[size] = '\0';
                        
                        // POST 요청 처리 로직 (기존 코드 유지)
                        char* method_check = strstr((char*)http_buf, "POST");
                        char* content_length_check = strstr((char*)http_buf, "Content-Length:");
                        if (!content_length_check) {
                            content_length_check = strstr((char*)http_buf, "content-length:");
                        }
                        
                        if (method_check && content_length_check) {
                            uint16_t expected_length = 0;
                            sscanf(content_length_check, "%*[^:]: %hu", &expected_length);
                            
                            char* header_end = strstr((char*)http_buf, "\r\n\r\n");
                            if (!header_end) {
                                header_end = strstr((char*)http_buf, "\n\n");
                            }
                            
                            if (header_end) {
                                int header_length = header_end - (char*)http_buf + (strstr((char*)http_buf, "\r\n\r\n") ? 4 : 2);
                                int received_body_length = size - header_length;
                                
                                if (received_body_length < expected_length) {
                                    int remaining = expected_length - received_body_length;
                                    sleep_ms(10);
                                    int additional_size = getSn_RX_RSR(sock);
                                    if (additional_size > 0) {
                                        if (size + additional_size > HTTP_BUF_SIZE - 1) {
                                            additional_size = HTTP_BUF_SIZE - 1 - size;
                                        }
                                        int extra = recv(sock, http_buf + size, additional_size);
                                        size += extra;
                                        http_buf[size] = '\0';
                                    }
                                }
                            }
                        }
                        
                        // HTTP 요청 파싱 및 처리
                        http_request_t request;
                        http_response_t response;
                        
                        http_parse_request((char*)http_buf, &request);
                        
                        // 핸들러 실행 또는 기본 처리
                        http_handler_t handler = http_router_find_handler(request.uri, request.method);
                        if(handler) {
                            handler(&request, &response);
                        }
                        else {
                            http_router_handle_default(&request, &response);
                        }
                        
                        // 응답 전송
                        http_send_response(sock, &response);
                        
                        // 파일 스트리밍이 필요한 경우 상태 변경
                        if (response.stream_required && response.stream_size > 0) {
                            http_process_state = STATE_HTTP_RES_INPROC;
                        } else {
                            http_process_state = STATE_HTTP_RES_DONE;
                        }
                    }
                    break;
                    
                case STATE_HTTP_RES_INPROC:
                    // 스트리밍이 완료되었는지 확인하고 처리
                    // 현재는 간단하게 바로 완료로 처리 (추후 개선)
                    http_process_state = STATE_HTTP_RES_DONE;
                    break;
                    
                case STATE_HTTP_RES_DONE:
                    // 연결 종료 (간단한 HTTP/1.0 방식)
                    disconnect(sock);
                    http_process_state = STATE_HTTP_IDLE;
                    break;
                    
                default:
                    http_process_state = STATE_HTTP_IDLE;
                    break;
            }
            break;
            
        case SOCK_CLOSE_WAIT:
            if((size = getSn_RX_RSR(sock)) > 0)
            {
                if(size > HTTP_BUF_SIZE) size = HTTP_BUF_SIZE - 1;
                recv(sock, http_buf, size);
            }
            disconnect(sock);
            http_process_state = STATE_HTTP_IDLE;
            break;
            
        default:
            break;
    }
}







void http_register_handler(const char* uri, http_method_t method, http_handler_t handler)
{
    http_router_register(uri, method, handler);
}

// 콜백 함수 등록 (ioLibrary 스타일)
void http_server_register_callbacks(http_server_mcu_reset_callback mcu_reset_cb, 
                                   http_server_wdt_reset_callback wdt_reset_cb)
{
    http_server_mcu_reset_cb = mcu_reset_cb;
    http_server_wdt_reset_cb = wdt_reset_cb;
}

// HTTP 서버 타임아웃 관련 함수 (ioLibrary 스타일)
void http_server_time_handler(void) {
    http_server_tick_1s++;
}

uint32_t http_server_get_timecount(void) {
    return http_server_tick_1s;
}
