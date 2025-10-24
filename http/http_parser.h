#ifndef HTTP_PARSER_H
#define HTTP_PARSER_H

#include <stdint.h>
#include <stdbool.h>
#include "http_server.h"

// HTTP 요청 파싱 함수들
http_method_t http_parse_method(const char* method_str);
void http_parse_request(const char* raw_request, http_request_t* request);

#endif // HTTP_PARSER_H