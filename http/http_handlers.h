#ifndef HTTP_HANDLERS_H
#define HTTP_HANDLERS_H

#include <stdint.h>

// HTTP 핸들러 함수
void http_handle_get_restart(uint8_t sock);
void http_handle_post_network(uint8_t sock, const char* body);
void http_handle_post_control(uint8_t sock, const char* body);
void http_handle_post_gpio(uint8_t sock, const char* body);
void http_handle_get_all(uint8_t sock);

#endif // HTTP_HANDLERS_H
