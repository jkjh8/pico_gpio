#include <stdio.h>
#include <string.h>
#include "pico/stdlib.h"
#include "main.h"
#include "network/mac_utils.h"
#include "network/network_config.h"
#include "http/http_server.h"
#include "hardware/watchdog.h"
#include "pico/stdlib.h"
#include "hardware/sync.h"

#define TCP_SOCKET_START 2
#define TCP_SOCKET_COUNT 3 // 2, 3, 4번 소켓 사용
// 전역 변수 정의
uint8_t g_ethernet_buf[2048];

// 네트워크 정보 전역 변수
wiz_NetInfo g_net_info = {
    .mac = { 0x00, 0x08, 0xDC, 0x00, 0x00, 0x00 },
    .ip = { 0, 0, 0, 0 },
    .sn = { 255, 255, 0, 0 },
    .gw = { 0, 0, 0, 0 },
    .dns = { 0, 0, 0, 0 },
    .dhcp = NETINFO_DHCP
};

// 시스템 재시작 요청 플래그
static volatile bool restart_requested = false;

// tcp port 번호
uint16_t tcp_port = 5050;

void tcp_server_init(uint16_t port) {
    uint8_t sock = 2; // HTTP와 다른 소켓 번호 사용
    if (getSn_SR(sock) != SOCK_CLOSED) close(sock);
    socket(sock, Sn_MR_TCP, port, 0x00);
    listen(sock);
    printf("TCP 서버 시작 (포트: %d)\n", port);
}

void tcp_server_process(void) {
    uint8_t sock = 2;
    switch (getSn_SR(sock)) {
        case SOCK_ESTABLISHED: {
            send(sock, (uint8_t*)"Hello from TCP server!", 24);
            if (getSn_IR(sock) & Sn_IR_CON) setSn_IR(sock, Sn_IR_CON);
            uint16_t rx_size = getSn_RX_RSR(sock);
            if (rx_size > 0) {
                uint8_t buf[512];
                if (rx_size > sizeof(buf)) rx_size = sizeof(buf);
                int len = recv(sock, buf, rx_size);
                buf[len] = 0;
                printf("TCP 수신: %s\n", buf);
                // 에코 응답
                send(sock, buf, len);
            }
            break;
        }
        case SOCK_CLOSE_WAIT:
            disconnect(sock);
            break;
        case SOCK_CLOSED:
            tcp_server_init(tcp_port);
            break;
        default:
            break;
    }
}

// 연결된 tcp 포트 전체에 메시지 보내기



int main()
{
    // RP2350 호환 초기화
    stdio_init_all();
    sleep_ms(2000);  // UART 안정화 대기
    printf("Starting Pico GPIO Server...\n");
    printf("Board: %s\n", PICO_BOARD);
    
    // 저장된 네트워크 설정 로드 및 MAC 주소 설정
    printf("Loading network configuration from flash...\n");
    network_config_load_from_flash(&g_net_info);
    
    // W5500 및 네트워크 초기화
    if (w5500_initialize() == W5500_INIT_SUCCESS) {
        printf("W5500 initialization successful\n");
        if (g_net_info.dhcp == NETINFO_STATIC) {
            w5500_set_static_ip(&g_net_info);
        } else {
            w5500_set_dhcp_mode(&g_net_info);
        }
        // 네트워크 상태 출력
        w5500_print_network_status();
        
        // DHCP 성공 여부와 관계없이 HTTP 서버 시작 (네트워크 복구 시 자동 재시작됨)
        if (http_server_init(80)) {
            printf("HTTP server started on port 80\n");
        } else {
            printf("ERROR: HTTP server failed to start\n");
        }
    } else {
        printf("ERROR: W5500 initialization failed\n");
    }

    // TCP 서버 초기화
    tcp_server_init(tcp_port);

    while (true) {
        // 시스템 재시작 요청 확인 및 처리
        if (is_system_restart_requested()) {
            system_restart();
        }
        // HTTP 서버 처리
        http_server_process();
        tcp_server_process();
    }
}

// 시스템 재시작 요청 함수
void system_restart_request(void) {
    restart_requested = true;
}

bool is_system_restart_requested(void) {
    return restart_requested;
}

void system_restart(void) {
    printf("System restart requested...\n");
    sleep_ms(500);  // 안정화를 위한 대기
    fflush(stdout);
    sleep_ms(500);
    watchdog_reboot(0, 0, 0);
    while (1) { __wfi(); }  // 무한 대기
}