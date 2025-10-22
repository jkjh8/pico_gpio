// Minimal wrapper around ioLibrary httpServer
#include "http_server.h"
#include "static_files.h"
#include "ioLibrary_Driver/Internet/httpServer/httpServer.h"
#include "ioLibrary_Driver/Internet/httpServer/httpUtil.h"
#include <string.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>

static uint8_t http_tx_buf[HTTP_BUF_SIZE];
static uint8_t http_rx_buf[HTTP_BUF_SIZE];
static uint8_t http_sock_list[1] = { HTTP_SOCKET_NUM };
static bool initialized = false;

bool http_server_init(uint16_t port)
{
    (void)port;
    httpServer_init(http_tx_buf, http_rx_buf, 1, http_sock_list);
    // reg_httpServer_cbfunc(NULL, NULL);

    // CGI 프로세서 등록 (predefined_cgi.c의 함수들이 자동으로 사용됨)
    // predefined_get_cgi_processor와 predefined_set_cgi_processor가 httpUtil.c에서 호출됨

    // 모든 비압축 임베드 파일을 HTTP 서버에 등록
    // 주: ioLibrary httpServer는 요청 URI에서 선행 '/'를 제거한 형태로 비교하므로
    // 등록 시에도 선행 '/'를 제거하고, 루트('/')는 INITIAL_WEBPAGE로 매핑합니다.
    for (size_t i = 0; i < embedded_files_count; i++) {
        if (!embedded_files[i].is_compressed) {
            const char *p = embedded_files[i].path;
            char namebuf[128] = {0};
            if (p && strcmp(p, "/") == 0) {
                // root는 index.html로 맵핑
                strncpy(namebuf, INITIAL_WEBPAGE, sizeof(namebuf) - 1);
            } else if (p && p[0] == '/') {
                // 선행 '/' 제거
                strncpy(namebuf, p + 1, sizeof(namebuf) - 1);
            } else if (p) {
                strncpy(namebuf, p, sizeof(namebuf) - 1);
            }

            // 등록 (httpServer가 내부적으로 이름을 복사하므로 stack buffer 사용 가능)
            reg_httpServer_webContent((uint8_t *)namebuf, (uint8_t *)embedded_files[i].data);
            printf("Registered web content: %s (%zu bytes)\n", namebuf, embedded_files[i].size);
        }
    }

    initialized = true;
    return true;
}

void http_server_process(void)
{
    if (!initialized) return;
    httpServer_time_handler();
    httpServer_run(0);
}

bool http_server_get_state(void)
{
    return initialized;
}

