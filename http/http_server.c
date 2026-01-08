#include "http_server.h"
#include "lib/wiznet/socket.h"
#include "lib/wiznet/w5500.h"
#include "debug/debug.h"
#include "gpio/gpio.h"
#include "system/system_config.h"
#include "static_files.h"
#include "FreeRTOS.h"
#include "task.h"
#include <string.h>
#include <stdio.h>

// HTTP 소켓 (소켓 1번만 사용 - 간단한 사용 시)
static const uint8_t http_sockets[] = {1};
#define HTTP_SOCKET_COUNT (sizeof(http_sockets) / sizeof(http_sockets[0]))

// HTTP 응답 헤더 (바이너리 데이터 지원)
static void http_send_response_binary(uint8_t sock, const char* status, const char* content_type, 
                                      const uint8_t* body, size_t body_len) {
    char header[256];
    
    snprintf(header, sizeof(header),
        "HTTP/1.1 %s\r\n"
        "Content-Type: %s\r\n"
        "Content-Length: %d\r\n"
        "Connection: close\r\n"
        "\r\n",
        status, content_type, (int)body_len);
    
    send(sock, (uint8_t*)header, strlen(header));
    if (body && body_len > 0) {
        send(sock, (uint8_t*)body, body_len);
    }
}

// HTTP 응답 헤더 (문자열)
static void http_send_response(uint8_t sock, const char* status, const char* content_type, const char* body) {
    int body_len = body ? strlen(body) : 0;
    http_send_response_binary(sock, status, content_type, (const uint8_t*)body, body_len);
}

// 정적 파일 처리
static void http_handle_static_file(uint8_t sock, const char* path) {
    size_t file_size, original_size;
    bool is_compressed;
    const char* content_type;
    
    // 임베드된 파일 찾기
    const char* file_data = get_embedded_file_with_content_type(path, &file_size, &is_compressed, &original_size, &content_type);
    
    if (file_data) {
        DBG_HTTP_PRINT("Serving %s: %zu bytes (compressed: %d)\n", path, file_size, is_compressed);
        
        // HTTP 헤더 생성
        char header[256];
        if (is_compressed) {
            snprintf(header, sizeof(header),
                "HTTP/1.1 200 OK\r\n"
                "Content-Type: %s\r\n"
                "Content-Encoding: gzip\r\n"
                "Content-Length: %d\r\n"
                "Connection: close\r\n"
                "\r\n",
                content_type, (int)file_size);
        } else {
            snprintf(header, sizeof(header),
                "HTTP/1.1 200 OK\r\n"
                "Content-Type: %s\r\n"
                "Content-Length: %d\r\n"
                "Connection: close\r\n"
                "\r\n",
                content_type, (int)file_size);
        }
        
        // 헤더 전송
        send(sock, (uint8_t*)header, strlen(header));
        
        // 파일 데이터를 청크 단위로 전송 (512 바이트씩)
        const size_t chunk_size = 512;
        size_t sent = 0;
        while (sent < file_size) {
            // TX 버퍼 공간 확인
            uint16_t free_size = getSn_TX_FSR(sock);
            if (free_size == 0) {
                vTaskDelay(pdMS_TO_TICKS(10));
                continue;
            }
            
            // 전송할 크기 결정 (버퍼 공간, 청크 크기, 남은 데이터 중 최소값)
            size_t remaining = file_size - sent;
            size_t to_send = (remaining > chunk_size) ? chunk_size : remaining;
            to_send = (to_send > free_size) ? free_size : to_send;
            
            int32_t result = send(sock, (uint8_t*)(file_data + sent), to_send);
            if (result <= 0) {
                DBG_HTTP_PRINT("Send failed at offset %zu\n", sent);
                break;
            }
            sent += result;
        }
        DBG_HTTP_PRINT("Sent %zu/%zu bytes\n", sent, file_size);
    } else {
        http_send_response(sock, "404 Not Found", "text/plain", "Not Found");
    }
}

// API 엔드포인트 처리
static void http_handle_api(uint8_t sock, const char* path) {
    char response[512];
    
    if (strcmp(path, "/api/status") == 0) {
        // GPIO 상태 반환
        extern uint16_t gpio_output_data;
        gpio_config_t* cfg = system_config_get_gpio();
        
        snprintf(response, sizeof(response),
            "{\"id\":%d,\"outputs\":\"%04X\"}",
            cfg->device_id, gpio_output_data);
        
        http_send_response(sock, "200 OK", "application/json", response);
    } else {
        http_send_response(sock, "404 Not Found", "text/plain", "Not Found");
    }
}

// HTTP 요청 처리
static void http_handle_request(uint8_t sock, char* request) {
    // GET /path HTTP/1.1 파싱
    char method[16], path[128];
    if (sscanf(request, "%15s %127s", method, path) != 2) {
        http_send_response(sock, "400 Bad Request", "text/plain", "Bad Request");
        return;
    }
    
    DBG_HTTP_PRINT("HTTP %s %s\n", method, path);
    
    if (strcmp(method, "GET") != 0) {
        http_send_response(sock, "405 Method Not Allowed", "text/plain", "Method Not Allowed");
        return;
    }
    
    // 라우팅
    if (strncmp(path, "/api/", 5) == 0) {
        http_handle_api(sock, path);
    } else if (strcmp(path, "/") == 0) {
        // 루트 경로는 index.html로 리다이렉트
        http_handle_static_file(sock, "/index.html");
    } else {
        // 모든 다른 경로는 static_files에서 찾기
        http_handle_static_file(sock, path);
    }
}

bool http_server_init(void) {
    for (int i = 0; i < HTTP_SOCKET_COUNT; i++) {
        uint8_t sock = http_sockets[i];
        socket(sock, Sn_MR_TCP, HTTP_PORT, 0);
        listen(sock);
        DBG_HTTP_PRINT("HTTP server listening on socket %d, port %d\n", sock, HTTP_PORT);
    }
    return true;
}

void http_server_process(void) {
    static char buffer[HTTP_BUFFER_SIZE];
    for (int i = 0; i < HTTP_SOCKET_COUNT; i++) {
        uint8_t sock = http_sockets[i];
        uint8_t status = getSn_SR(sock);
        
        switch (status) {
            case SOCK_ESTABLISHED: {
                int len = getSn_RX_RSR(sock);
                if (len > 0) {
                    if (len > HTTP_BUFFER_SIZE - 1) len = HTTP_BUFFER_SIZE - 1;
                    len = recv(sock, (uint8_t*)buffer, len);
                    if (len > 0) {
                        buffer[len] = '\0';
                        http_handle_request(sock, buffer);
                    }
                    disconnect(sock);
                }
                break;
            }
            case SOCK_CLOSE_WAIT:
                disconnect(sock);
                break;
            case SOCK_CLOSED:
                socket(sock, Sn_MR_TCP, HTTP_PORT, 0);
                listen(sock);
                break;
        }
    }
}
