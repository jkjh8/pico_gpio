
#include "main.h"

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
    } else {
        printf("ERROR: W5500 initialization failed\n");
    }
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

    // TCP 서버 초기화
    tcp_servers_init(tcp_port);

    while (true) {
        // 시스템 재시작 요청 확인 및 처리
        if (is_system_restart_requested()) {
            system_restart();
        }
        // HTTP 서버 처리
        http_server_process();
        tcp_servers_process();
    }
}

