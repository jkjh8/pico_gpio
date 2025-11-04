#include "http_response.h"
#include <string.h>
#include <stdio.h>
#include "debug/debug.h"

// HTTP 상태 코드 텍스트 변환 함수
const char* http_get_status_text(http_status_t status)
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

// HTTP 응답 전송 함수
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
        http_get_status_text(response->status),
        actual_content_type,
        is_gzipped ? "Content-Encoding: gzip\r\n" : "",
        response->content_length
    );

    DBG_HTTP_PRINT("Sending response: status=%d, type=%s, gzipped=%s, content_length=%d \n",
           response->status, actual_content_type, is_gzipped ? "yes" : "no", response->content_length);

    // 헤더 및 콘텐츠 전송
    send(sock, (uint8_t*)header, header_len);
    if(response->content_length > 0) {
        send(sock, (uint8_t*)response->content, response->content_length);
    }

    // 활동 시간 업데이트
    // last_activity_time = to_ms_since_boot(get_absolute_time());
}

// 404 응답 전송 함수
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

// 대용량 파일 스트리밍 전송 함수
void http_send_large_file_stream(uint8_t sock, const char* file_data, size_t file_size, const char* content_type, bool is_compressed)
{
    char header[512];
    size_t bytes_sent = 0;
    int retry_count = 0;
    const int max_retries = 200;  // 재시도 횟수 증가

    // 소켓 상태 확인
    uint8_t socket_status = getSn_SR(sock);
    if (socket_status != SOCK_ESTABLISHED) {
        DBG_HTTP_PRINT("Socket not in ESTABLISHED state, aborting\n");
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
        DBG_HTTP_PRINT("Failed to send header: %d\n", header_result);
        return;
    }

    // 파일 데이터를 안전한 청크 단위로 전송 (ioLibrary 스타일로 개선)
    size_t last_log_bytes = 0;
    uint32_t start_time = http_server_get_timecount();

    while (bytes_sent < file_size && retry_count < max_retries) {
        // 소켓 상태 확인
        uint8_t socket_status = getSn_SR(sock);
        if (socket_status != SOCK_ESTABLISHED) {
            DBG_HTTP_PRINT("Socket disconnected during transfer: %d\n", socket_status);
            break;
        }

        // 타임아웃 체크 (ioLibrary 스타일)
        if ((http_server_get_timecount() - start_time) > HTTP_MAX_TIMEOUT_SEC) {
            DBG_HTTP_PRINT("Transfer timeout after %d seconds\n", HTTP_MAX_TIMEOUT_SEC);
            break;
        }

        // 타임아웃 체크 (ioLibrary 스타일)
        if ((http_server_get_timecount() - start_time) > HTTP_MAX_TIMEOUT_SEC) {
            DBG_HTTP_PRINT("Transfer timeout after %d seconds\n", HTTP_MAX_TIMEOUT_SEC);
            break;
        }

        // TX 버퍼 여유 공간 확인 (ioLibrary처럼 개선)
        uint16_t free_size = getSn_TX_FSR(sock);
        if (free_size < 1024) {  // 최소 1KB 여유 공간 필요
            sleep_ms(5);
            retry_count++;
            continue;
        }

        // 전송할 청크 크기 결정 (ioLibrary 스타일)
        size_t remaining = file_size - bytes_sent;
        size_t chunk_size = (remaining > STREAM_CHUNK_SIZE) ? STREAM_CHUNK_SIZE : remaining;

        // TX 버퍼 크기에 맞춰 조정 (ioLibrary처럼)
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
            DBG_HTTP_PRINT("Send failed: %d during transfer at bytes_sent=%zu\n", ret, bytes_sent);
            break;
        }

        bytes_sent += ret;
        retry_count = 0;  // 성공적으로 전송되면 재시도 카운터 리셋

        // 전송 속도 조절 (ioLibrary처럼)
        sleep_ms(2);

        // 진행상황 로그 (약 4KB 단위로 출력)
        if (bytes_sent - last_log_bytes >= 4096 || bytes_sent == file_size) {
            DBG_HTTP_PRINT("Streaming progress: sent %zu / %zu bytes\n", bytes_sent, file_size);
            last_log_bytes = bytes_sent;
        }
    }

    // last_activity_time = to_ms_since_boot(get_absolute_time());

    // 전송이 끝난 후 TX 버퍼가 비워질 때까지 잠시 대기하여
    // W5500가 모든 데이터를 물리적으로 전송할 시간을 줍니다.
    // 이로써 브라우저 쪽에서의 connection reset 가능성을 줄입니다.
    uint32_t gettime = http_server_get_timecount();
    int flush_wait_ms = 0;
    int max_flush_wait_ms = 200; // 최대 200ms 대기
    while (flush_wait_ms < max_flush_wait_ms) {
        uint16_t free_tx = getSn_TX_FSR(sock);
        // TX 버퍼가 충분히 비워졌거나 타임아웃 발생시 루프 종료
        if (free_tx >= STREAM_CHUNK_SIZE || (http_server_get_timecount() - gettime) > HTTP_MAX_TIMEOUT_SEC) {
            break;
        }
        sleep_ms(5);
        flush_wait_ms += 5;
    }

    if (bytes_sent < file_size) {
        DBG_HTTP_PRINT("Streaming incomplete: sent %zu of %zu bytes\n", bytes_sent, file_size);
    } else {
        DBG_HTTP_PRINT("Streaming complete: sent %zu bytes, waited %d ms for TX flush\n", bytes_sent, flush_wait_ms);
    }
}