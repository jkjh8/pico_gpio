#include "http_parser.h"
#include <string.h>
#include <stdio.h>

// HTTP 메소드 문자열을 enum으로 변환
http_method_t http_parse_method(const char* method_str)
{
    if(strncmp(method_str, "GET", 3) == 0) return HTTP_GET;
    if(strncmp(method_str, "POST", 4) == 0) return HTTP_POST;
    if(strncmp(method_str, "PUT", 3) == 0) return HTTP_PUT;
    if(strncmp(method_str, "DELETE", 6) == 0) return HTTP_DELETE;
    return HTTP_UNKNOWN;
}

// HTTP 요청을 파싱하여 구조체에 저장
void http_parse_request(const char* raw_request, http_request_t* request)
{
    char method_str[16] = {0};
    char version[16] = {0};

    // 첫 번째 줄 파싱: "METHOD URI HTTP/1.1"
    sscanf(raw_request, "%15s %511s %15s", method_str, request->uri, version);
    request->method = http_parse_method(method_str);

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