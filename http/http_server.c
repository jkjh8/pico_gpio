#include "http_server.h"
#include "http_handlers.h"
#include "ota_handler.h"
#include "../main.h"
#include "lib/wiznet/socket.h"
#include "lib/wiznet/w5500.h"
#include "lib/wiznet/wizchip_conf.h"
#include "debug/debug.h"
#include "gpio/gpio.h"
#include "system/system_config.h"
#include "static_files.h"
#include "FreeRTOS.h"
#include "semphr.h"
#include "task.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

// HTTP 소켓
static const uint8_t http_sockets[] = {6, 7};

// Content-Length 헤더 값 파싱 (-1: 없음)
// HTTP 헤더는 대소문자 구분 없지만 실제 브라우저/클라이언트는 대문자 전송
static int parse_content_length(const char *request) {
    const char *p = strstr(request, "Content-Length:");
    if (!p) p = strstr(request, "content-length:");
    if (!p) return -1;
    p += 15; // "Content-Length:" 길이
    while (*p == ' ') p++;
    return atoi(p);
}
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
    
    // 큰 데이터는 분할 전송 (1KB 단위)
    if (body && body_len > 0) {
        const size_t chunk_size = 1024;
        size_t sent = 0;
        while (sent < body_len) {
            size_t to_send = (body_len - sent > chunk_size) ? chunk_size : (body_len - sent);
            int32_t result = send(sock, (uint8_t*)(body + sent), to_send);
            if (result <= 0) {
                break; // 전송 실패
            }
            sent += result;
            vTaskDelay(pdMS_TO_TICKS(5)); // 짧은 딜레이
        }
    }
}

// HTTP 응답 헤더 (문자열)
void http_send_response(uint8_t sock, const char* status, const char* content_type, const char* body) {
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
        uint32_t timeout_start = to_ms_since_boot(get_absolute_time());
        while (sent < file_size) {
            // 소켓 상태 확인
            uint8_t sock_status = getSn_SR(sock);
            if (sock_status == SOCK_CLOSED || sock_status == SOCK_CLOSE_WAIT) {
                DBG_HTTP_PRINT("Socket closed during send at offset %zu\n", sent);
                break;
            }

            // 전송 타임아웃 (10초)
            uint32_t now = to_ms_since_boot(get_absolute_time());
            if ((now - timeout_start) > 10000) {
                DBG_HTTP_PRINT("Send timeout at offset %zu/%zu\n", sent, file_size);
                break;
            }

            // TX 버퍼 공간 확인
            uint16_t free_size = getSn_TX_FSR(sock);
            if (free_size == 0) {
                vTaskDelay(pdMS_TO_TICKS(5));
                continue;
            }

            // 전송할 크기 결정 (버퍼 공간, 청크 크기, 남은 데이터 중 최소값)
            size_t remaining = file_size - sent;
            size_t to_send = (remaining > chunk_size) ? chunk_size : remaining;
            to_send = (to_send > free_size) ? free_size : to_send;

            int32_t result = send(sock, (uint8_t*)(file_data + sent), to_send);
            if (result < 0) {
                DBG_HTTP_PRINT("Send failed at offset %zu (err=%d)\n", sent, (int)result);
                break;
            }
            if (result == 0) {
                vTaskDelay(pdMS_TO_TICKS(5));
                continue;
            }
            sent += result;
            timeout_start = to_ms_since_boot(get_absolute_time()); // 진행 중이면 타임아웃 리셋
        }
        DBG_HTTP_PRINT("Sent %zu/%zu bytes\n", sent, file_size);
    } else {
        http_send_response(sock, "404 Not Found", "text/plain", "Not Found");
    }
}

// API 엔드포인트 처리 (GET 요청)
static void http_handle_api_get(uint8_t sock, const char* path) {
    if (strcmp(path, "/api/all") == 0) {
        http_handle_get_all(sock);
    } else if (strcmp(path, "/api/restart") == 0) {
        http_handle_get_restart(sock);
    } else {
        http_send_response(sock, "404 Not Found", "text/plain", "Not Found");
    }
}

// API 엔드포인트 처리 (POST 요청)
static void http_handle_api_post(uint8_t sock, const char* path, const char* body) {
    if (strcmp(path, "/api/network") == 0) {
        http_handle_post_network(sock, body);
    } else if (strcmp(path, "/api/control") == 0) {
        http_handle_post_control(sock, body);
    } else if (strcmp(path, "/api/gpio") == 0) {
        http_handle_post_gpio(sock, body);
    } else if (strcmp(path, "/api/command") == 0) {
        http_handle_post_command(sock, body);
    } else {
        http_send_response(sock, "404 Not Found", "text/plain", "Not Found");
    }
}

// HTTP 요청 처리 (len: recv()로 읽힌 실제 바이트 수)
static void http_handle_request(uint8_t sock, char* request, int len) {
    // GET /path HTTP/1.1 파싱
    char method[16], path[128];
    if (sscanf(request, "%15s %127s", method, path) != 2) {
        http_send_response(sock, "400 Bad Request", "text/plain", "Bad Request");
        return;
    }
    
    DBG_HTTP_PRINT("HTTP %s %s\n", method, path);
    
    // POST 요청인 경우 body 찾기
    char* body = NULL;
    if (strcmp(method, "POST") == 0) {
        body = strstr(request, "\r\n\r\n");
        if (body) {
            body += 4; // "\r\n\r\n" 건너뛰기
        }
    }
    
    // 라우팅
    if (strncmp(path, "/api/", 5) == 0) {
        if (strcmp(method, "GET") == 0) {
            http_handle_api_get(sock, path);
        } else if (strcmp(method, "POST") == 0) {
            // OTA 업데이트: 바이너리 스트리밍 처리 (body 포인터 + Content-Length 사용)
            if (strcmp(path, "/api/update") == 0) {
                int clen = parse_content_length(request);
                const uint8_t *ibody = body ? (const uint8_t *)body : NULL;
                int ilen = 0;
                if (body) {
                    // 버퍼 끝까지가 body 데이터
                    ilen = (int)(request + len - body);
                    if (ilen < 0) ilen = 0;
                }
                http_handle_post_update(sock, ibody, ilen, clen);
            } else if (strcmp(path, "/api/ota/chunk") == 0) {
                int clen = parse_content_length(request);
                const uint8_t *ibody = body ? (const uint8_t *)body : NULL;
                int ilen = 0;
                if (body) {
                    ilen = (int)(request + len - body);
                    if (ilen < 0) ilen = 0;
                    if (clen > 0 && ilen > clen) ilen = clen;
                }
                http_handle_post_ota_chunk(sock, request, ibody, ilen);
            } else if (body) {
                http_handle_api_post(sock, path, body);
            } else {
                http_send_response(sock, "400 Bad Request", "text/plain", "No body");
            }
        } else {
            http_send_response(sock, "405 Method Not Allowed", "text/plain", "Method Not Allowed");
        }
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
                // 데이터가 들어올 때까지 최대 300ms 대기 (busy-wait, no vTaskDelay)
                uint16_t len = 0;
                uint32_t t0 = to_ms_since_boot(get_absolute_time());
                while (len == 0) {
                    len = getSn_RX_RSR(sock);
                    if (len > 0) break;
                    if ((to_ms_since_boot(get_absolute_time()) - t0) > 300) break;
                }
                if (len == 0) break;

                if (len > HTTP_BUFFER_SIZE - 1) len = HTTP_BUFFER_SIZE - 1;
                int rlen = recv(sock, (uint8_t*)buffer, len);
                if (rlen <= 0) { disconnect(sock); break; }
                buffer[rlen] = '\0';

                // ── TCP 분할 수신 보완 ──────────────────────────────────────
                // W5500 RX 버퍼가 2KB이고 TCP는 패킷을 분할할 수 있으므로
                // Content-Length 만큼 body가 다 올 때까지 추가 수신 대기.
                // ex) 헤더 첫 세그먼트 479B 수신, 나머지 331B는 다음 세그먼트
                {
                    int clen2 = parse_content_length(buffer);
                    if (clen2 > 0) {
                        char *bp = strstr(buffer, "\r\n\r\n");
                        if (bp) {
                            int hdr_len      = (int)(bp + 4 - buffer);
                            int body_recvd   = rlen - hdr_len;
                            uint32_t t1      = to_ms_since_boot(get_absolute_time());
                            while (body_recvd < clen2 &&
                                   rlen < HTTP_BUFFER_SIZE - 1) {
                                if ((to_ms_since_boot(get_absolute_time()) - t1) > 2000)
                                    break; // 2초 타임아웃
                                uint16_t avail = getSn_RX_RSR(sock);
                                if (avail == 0) continue; // busy-wait
                                int space   = HTTP_BUFFER_SIZE - 1 - rlen;
                                int to_read = ((int)avail < space) ? (int)avail : space;
                                if (to_read <= 0) break;
                                int n = recv(sock, (uint8_t*)buffer + rlen, to_read);
                                if (n <= 0) break;
                                rlen += n;
                                buffer[rlen] = '\0';
                                body_recvd = rlen - hdr_len;
                            }
                        }
                    }
                }

                http_handle_request(sock, buffer, rlen);
                disconnect(sock);
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
