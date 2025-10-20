#include "http_server.h"
#include "http_handlers.h"

// 디버그 매크로

#undef HTTP_LOG
#define HTTP_LOG(fmt, ...)
#undef HTTP_DEBUG
#define HTTP_DEBUG(fmt, ...)

// 전역 변수
static uint8_t http_buf[HTTP_BUF_SIZE];
static uint16_t http_port = HTTP_SERVER_PORT;
static http_server_state_t server_state = HTTP_SERVER_IDLE;
static uint32_t last_activity_time = 0;
static bool keep_alive_enabled = true;

#define KEEP_ALIVE_TIMEOUT_MS 5000  // 5초 Keep-Alive 타임아웃

// 간단한 핸들러 테이블 (실제 구현에서는 더 동적인 방식 사용 가능)
typedef struct {
    char uri[64];
    http_method_t method;
    http_handler_t handler;
} http_route_t;

#define MAX_ROUTES 10
static http_route_t routes[MAX_ROUTES];
static uint8_t route_count = 0;

// 내부 함수 선언
static http_method_t parse_http_method(const char* method_str);
static void parse_http_request(const char* raw_request, http_request_t* request);
static const char* get_status_text(http_status_t status);
static http_handler_t find_handler(const char* uri, http_method_t method);
static void handle_default_routes(const http_request_t* request, http_response_t* response);

bool http_server_init(uint16_t port)
{
    // printf("HTTP 서버 시작 (포트: %d)\n", port);
    
    http_port = port;
    server_state = HTTP_SERVER_IDLE;
    route_count = 0;
    
    // 소켓 상태 확인 및 초기화
    uint8_t sock = HTTP_SOCKET_NUM;
    uint8_t sock_status = getSn_SR(sock);
    
    if (sock_status != SOCK_CLOSED) {
        HTTP_LOG("소켓 %d 닫는 중\n", sock);
        close(sock);
        sleep_ms(100);
    }
    
    // 기본 라우트 등록 (루트 경로는 정적 파일로 처리하므로 제외)
    // 백업용 시스템 라우트들 (SPA가 없을 경우를 위한)
    http_register_handler("/system", HTTP_GET, http_handler_root);
    http_register_handler("/api/network", HTTP_GET, http_handler_network_info);
    http_register_handler("/api/network", HTTP_POST, http_handler_network_setup);
    http_register_handler("/api/control", HTTP_GET, http_handler_control_info);
    http_register_handler("/api/control", HTTP_POST, http_handler_control_setup);
    
    // GPIO 설정 API
    http_register_handler("/api/gpio", HTTP_GET, http_handler_gpio_config_info);
    http_register_handler("/api/gpio", HTTP_POST, http_handler_gpio_config_setup);

    http_register_handler("/api/restart", HTTP_GET, http_handler_restart);

    // printf("HTTP 서버 초기화 완료 (포트: %d)\n", port);
    return true;
}

void http_server_process(void)
{
    uint8_t sock = HTTP_SOCKET_NUM;
    uint16_t size = 0;
    uint8_t current_status = getSn_SR(sock);
    static bool server_ready_logged = false;
    
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
                server_state = HTTP_SERVER_IDLE;
            }
            break;
            
        case SOCK_INIT:
            if(listen(sock) == SOCK_OK) {
                server_state = HTTP_SERVER_LISTENING;
                if (!server_ready_logged) {
                    // printf("HTTP 서버 준비 완료 (포트: %d)\n", http_port);
                    server_ready_logged = true;
                }
            }
            break;
            
        case SOCK_LISTEN:
            server_state = HTTP_SERVER_LISTENING;
            break;
            
        case SOCK_ESTABLISHED:
            if(getSn_IR(sock) & Sn_IR_CON) {
                setSn_IR(sock, Sn_IR_CON);
                server_state = HTTP_SERVER_CONNECTED;
                last_activity_time = to_ms_since_boot(get_absolute_time());
            }
            
            // 수신된 데이터 처리
            if((size = getSn_RX_RSR(sock)) > 0) {
                if(size > HTTP_BUF_SIZE) size = HTTP_BUF_SIZE - 1;
                
                size = recv(sock, http_buf, size);
                http_buf[size] = '\0';
                
                // printf("Received %d bytes\n", size);
                
                // POST 요청인지 확인하고 Content-Length 헤더가 있는지 체크
                char* method_check = strstr((char*)http_buf, "POST");
                char* content_length_check = strstr((char*)http_buf, "Content-Length:");
                if (!content_length_check) {
                    content_length_check = strstr((char*)http_buf, "content-length:");
                }
                
                // POST 요청이고 Content-Length가 있지만 완전한 요청이 아닐 수 있음
                if (method_check && content_length_check) {
                    // Content-Length 값 추출
                    uint16_t expected_length = 0;
                    sscanf(content_length_check, "%*[^:]: %hu", &expected_length);
                    
                    // 헤더 끝 위치 찾기
                    char* header_end = strstr((char*)http_buf, "\r\n\r\n");
                    if (!header_end) {
                        header_end = strstr((char*)http_buf, "\n\n");
                    }
                    
                    if (header_end) {
                        int header_length = header_end - (char*)http_buf + (strstr((char*)http_buf, "\r\n\r\n") ? 4 : 2);
                        int received_body_length = size - header_length;
                        
                        // printf("Expected body: %d bytes, Received body: %d bytes\n", expected_length, received_body_length);
                        
                        // 본문이 부족하면 추가로 읽기
                        if (received_body_length < expected_length) {
                            int remaining = expected_length - received_body_length;
                            // printf("Waiting for %d more bytes...\n", remaining);
                            
                            // 잠시 대기 후 추가 데이터 확인
                            sleep_ms(10);
                            int additional_size = getSn_RX_RSR(sock);
                            if (additional_size > 0) {
                                if (size + additional_size > HTTP_BUF_SIZE - 1) {
                                    additional_size = HTTP_BUF_SIZE - 1 - size;
                                }
                                int extra = recv(sock, http_buf + size, additional_size);
                                size += extra;
                                http_buf[size] = '\0';
                                // printf("Received additional %d bytes, total: %d\n", extra, size);
                            }
                        }
                    }
                }
                
                // HTTP 요청 파싱 및 처리
                http_request_t request;
                http_response_t response;
                
                parse_http_request((char*)http_buf, &request);
                
                // 핸들러 실행 또는 기본 처리
                http_handler_t handler = find_handler(request.uri, request.method);
                if(handler) {
                    handler(&request, &response);
                }
                else {
                    handle_default_routes(&request, &response);
                }
                
                // 응답 전송
                http_send_response(sock, &response);
                
                // 연결 종료 (간단한 HTTP/1.0 방식)
                disconnect(sock);
            }
            break;
            
        case SOCK_CLOSE_WAIT:
            if((size = getSn_RX_RSR(sock)) > 0)
            {
                if(size > HTTP_BUF_SIZE) size = HTTP_BUF_SIZE - 1;
                recv(sock, http_buf, size);
            }
            disconnect(sock);
            break;
            
        default:
            break;
    }
}

void http_server_stop(void)
{
    close(HTTP_SOCKET_NUM);
    server_state = HTTP_SERVER_IDLE;
            // printf("HTTP 서버 중지\n");
}

void http_server_cleanup(void)
{
    // printf("HTTP 서버 정리 중...\n");
    
    // 소켓 정리
    if (getSn_SR(HTTP_SOCKET_NUM) != SOCK_CLOSED) {
        disconnect(HTTP_SOCKET_NUM);
        close(HTTP_SOCKET_NUM);
    }
    
    // 서버 상태 리셋
    server_state = HTTP_SERVER_IDLE;
    
    // printf("HTTP 서버 정리 완료\n");
}

http_server_state_t http_server_get_state(void)
{
    return server_state;
}

void http_register_handler(const char* uri, http_method_t method, http_handler_t handler)
{
    if(route_count < MAX_ROUTES)
    {
        strncpy(routes[route_count].uri, uri, sizeof(routes[route_count].uri) - 1);
        routes[route_count].method = method;
        routes[route_count].handler = handler;
        route_count++;
    }
}

void http_send_response(uint8_t sock, const http_response_t *response)
{
    // 스트리밍이 필요한 경우
    if (response->stream_required && response->stream_data) {
        // printf("Sending large file via streaming: %zu bytes\n", response->stream_size);
        http_send_large_file_stream(sock, response->stream_data, response->stream_size, 
                                   response->content_type, response->stream_compressed);
        return;
    }
    
    char header[512];
    char actual_content_type[64];
    bool is_gzipped = false;
    
    // Content-Type에서 압축 정보 분리
    const char* pipe_pos = strchr(response->content_type, '|');
    if (pipe_pos && strcmp(pipe_pos, "|gzip") == 0) {
        size_t content_type_len = pipe_pos - response->content_type;
        strncpy(actual_content_type, response->content_type, content_type_len);
        actual_content_type[content_type_len] = '\0';
        is_gzipped = true;
    } else {
        strcpy(actual_content_type, response->content_type);
    }
    
    // HTTP 헤더 생성 (단순하고 안정적인 방식)
    int header_len = sprintf(header, 
        "HTTP/1.1 %d %s\r\n"
        "Content-Type: %s\r\n"
        "%s"
        "Content-Length: %d\r\n"
        "Connection: close\r\n"
        "\r\n",
        response->status,
        get_status_text(response->status),
        actual_content_type,
        is_gzipped ? "Content-Encoding: gzip\r\n" : "",
        response->content_length
    );
    
    // 헤더 및 콘텐츠 전송
    send(sock, (uint8_t*)header, header_len);
    if(response->content_length > 0) {
        send(sock, (uint8_t*)response->content, response->content_length);
    }
    
    // 활동 시간 업데이트
    last_activity_time = to_ms_since_boot(get_absolute_time());
}

void http_send_json_response(uint8_t sock, http_status_t status, const char* json_data)
{
    http_response_t response;
    response.status = status;
    strcpy(response.content_type, "application/json");
    strncpy(response.content, json_data, sizeof(response.content) - 1);
    response.content_length = strlen(response.content);
    
    http_send_response(sock, &response);
}

void http_send_html_response(uint8_t sock, http_status_t status, const char* html_data)
{
    http_response_t response;
    response.status = status;
    strcpy(response.content_type, "text/html");
    strncpy(response.content, html_data, sizeof(response.content) - 1);
    response.content_length = strlen(response.content);
    
    http_send_response(sock, &response);
}

void http_send_404(uint8_t sock)
{
    const char* html_404 = 
        "<!DOCTYPE html>\n"
        "<html><head><title>404 Not Found</title></head>\n"
        "<body><h1>404 Not Found</h1>\n"
        "<p>The requested resource was not found on this server.</p>\n"
        "</body></html>";
    
    http_send_html_response(sock, HTTP_NOT_FOUND, html_404);
}

void http_send_large_file_stream(uint8_t sock, const char* file_data, size_t file_size, const char* content_type, bool is_compressed)
{
    char header[512];
    size_t bytes_sent = 0;
    int retry_count = 0;
    const int max_retries = 200;  // 재시도 횟수 증가
    
    // 소켓 상태 확인
    uint8_t socket_status = getSn_SR(sock);
    if (socket_status != SOCK_ESTABLISHED) {
        printf("Socket not in ESTABLISHED state, aborting\n");
        return;
    }
    
    // HTTP 헤더 구성 및 전송
    int header_len = snprintf(header, sizeof(header),
        "HTTP/1.1 200 OK\r\n"
        "Content-Type: %s\r\n"
        "Content-Length: %zu\r\n"
        "%s"
        "Connection: close\r\n"
        "Cache-Control: public, max-age=3600\r\n"
        "\r\n",
        content_type ? content_type : "application/octet-stream",
        file_size,
        is_compressed ? "Content-Encoding: gzip\r\n" : ""
    );
    
   
    int32_t header_result = send(sock, (uint8_t*)header, header_len);
    if (header_result <= 0) {
        // printf("Failed to send header: %d\n", header_result);
        return;
    }
    
    // 파일 데이터를 안전한 청크 단위로 전송
    while (bytes_sent < file_size && retry_count < max_retries) {
        // 소켓 상태 확인
        uint8_t socket_status = getSn_SR(sock);
        if (socket_status != SOCK_ESTABLISHED) {
            printf("Socket disconnected during transfer: %d\n", socket_status);
            break;
        }
        
        // TX 버퍼 여유 공간 확인
        uint16_t free_size = getSn_TX_FSR(sock);
        if (free_size < 1024) {  // 최소 1KB 여유 공간 필요
            sleep_ms(5);
            retry_count++;
            continue;
        }
        
        // 전송할 청크 크기 결정
        size_t remaining = file_size - bytes_sent;
        size_t chunk_size = (remaining > STREAM_CHUNK_SIZE) ? STREAM_CHUNK_SIZE : remaining;
        
        // TX 버퍼 크기에 맞춰 조정
        if (chunk_size > free_size - 256) {  // 256바이트 여유 공간 확보
            chunk_size = free_size - 256;
        }
        
        if (chunk_size == 0) {
            sleep_ms(5);
            retry_count++;
            continue;
        }
        
        // 데이터 전송
        int32_t ret = send(sock, (uint8_t*)(file_data + bytes_sent), chunk_size);
        if (ret <= 0) {
            // printf("Send failed: %d\n", ret);
            break;
        }
        
        bytes_sent += ret;
        retry_count = 0;  // 성공적으로 전송되면 재시도 카운터 리셋
        
        // 전송 속도 조절
        sleep_ms(2);

    }

    last_activity_time = to_ms_since_boot(get_absolute_time());
}

const char* get_embedded_file(const char* path, size_t* file_size, bool* is_compressed, size_t* original_size)
{
    const char* content_type = NULL;
    return get_embedded_file_with_content_type(path, file_size, is_compressed, original_size, &content_type);
}

// 내부 함수 구현
static http_method_t parse_http_method(const char* method_str)
{
    if(strncmp(method_str, "GET", 3) == 0) return HTTP_GET;
    if(strncmp(method_str, "POST", 4) == 0) return HTTP_POST;
    if(strncmp(method_str, "PUT", 3) == 0) return HTTP_PUT;
    if(strncmp(method_str, "DELETE", 6) == 0) return HTTP_DELETE;
    return HTTP_UNKNOWN;
}

static void parse_http_request(const char* raw_request, http_request_t* request)
{
    char method_str[16] = {0};
    char version[16] = {0};
    // 첫 번째 줄 파싱: "METHOD URI HTTP/1.1"
    sscanf(raw_request, "%15s %511s %15s", method_str, request->uri, version);
    request->method = parse_http_method(method_str);
    // Keep-Alive 헤더 확인
    const char* connection_header = strstr(raw_request, "Connection:");
    if (connection_header) {
        if (strstr(connection_header, "keep-alive") || strstr(connection_header, "Keep-Alive")) {
            // Keep-Alive 요청 감지 (현재는 무시하고 close로 처리)
        }
    }
    // Content-Length 찾기 (POST 요청용) - 대소문자 구분 없이
    const char* content_length_str = strstr(raw_request, "Content-Length:");
    if (!content_length_str) {
        content_length_str = strstr(raw_request, "content-length:");
    }
    if (!content_length_str) {
        content_length_str = strstr(raw_request, "Content-length:");
    }
    
    if(content_length_str) {
        sscanf(content_length_str, "%*[^:]: %hu", &request->content_length);
        const char* body_start = strstr(raw_request, "\r\n\r\n");
        if (!body_start) {
            body_start = strstr(raw_request, "\n\n");
            if (body_start) body_start += 2;
        } else {
            body_start += 4;
        }
        if(body_start && request->content_length > 0) {
            uint16_t copy_length = request->content_length;
            if(copy_length > MAX_CONTENT_SIZE - 1)
                copy_length = MAX_CONTENT_SIZE - 1;
            
            strncpy(request->content, body_start, copy_length);
            request->content[copy_length] = '\0';
        } else {
            // printf("No body found or content length is 0\n");
        }
    } else {
        // printf("No Content-Length header found\n");
        request->content_length = 0;
        request->content[0] = '\0';
    }
}

static const char* get_status_text(http_status_t status)
{
    switch(status)
    {
        case HTTP_OK: return "OK";
        case HTTP_NOT_FOUND: return "Not Found";
        case HTTP_METHOD_NOT_ALLOWED: return "Method Not Allowed";
        case HTTP_INTERNAL_ERROR: return "Internal Server Error";
        default: return "Unknown";
    }
}

static http_handler_t find_handler(const char* uri, http_method_t method)
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

static void handle_default_routes(const http_request_t* request, http_response_t* response)
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
