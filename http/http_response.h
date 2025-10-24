#ifndef HTTP_RESPONSE_H
#define HTTP_RESPONSE_H

#include <stdint.h>
#include <stdbool.h>
#include "http_server.h"

// HTTP 상태 코드 텍스트 변환 함수
const char* http_get_status_text(http_status_t status);

// HTTP 응답 전송 함수들
void http_send_response(uint8_t sock, const http_response_t *response);
void http_send_404(uint8_t sock);
void http_send_large_file_stream(uint8_t sock, const char* file_data, size_t file_size,
                                const char* content_type, bool is_compressed);

#endif // HTTP_RESPONSE_H