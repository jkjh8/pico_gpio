#ifndef HTTP_SERVER_H
#define HTTP_SERVER_H

#include <stdint.h>
#include <stdbool.h>

// HTTP 서버 설정
#define HTTP_PORT 80
#define HTTP_MAX_CLIENTS 4
#define HTTP_BUFFER_SIZE 2048

// HTTP 서버 초기화
bool http_server_init(void);

// HTTP 서버 처리 (FreeRTOS 태스크에서 호출)
void http_server_process(void);

#endif // HTTP_SERVER_H
