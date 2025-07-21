#include <stdio.h>
#include <string.h>
#include "pico/stdlib.h"
#include "main.h"
#include "mac_utils.h"
#include "network_config.h"
#include "http_server.h"

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

// DHCP IP 정보 출력 및 전역 변수 업데이트 (공통 함수)
static void display_and_update_dhcp_info(bool is_initial) {
    wiz_NetInfo current_info;
    wizchip_getnetinfo(&current_info);
    
    printf("=== DHCP IP ASSIGNED ===\n");
    printf("IP: %d.%d.%d.%d\n", current_info.ip[0], current_info.ip[1], current_info.ip[2], current_info.ip[3]);
    printf("Subnet: %d.%d.%d.%d\n", current_info.sn[0], current_info.sn[1], current_info.sn[2], current_info.sn[3]);
    printf("Gateway: %d.%d.%d.%d\n", current_info.gw[0], current_info.gw[1], current_info.gw[2], current_info.gw[3]);
    printf("DNS: %d.%d.%d.%d\n", current_info.dns[0], current_info.dns[1], current_info.dns[2], current_info.dns[3]);
    
    if (is_initial) {
        printf("Web server will be available at: http://%d.%d.%d.%d\n", 
               current_info.ip[0], current_info.ip[1], current_info.ip[2], current_info.ip[3]);
    }
    printf("======================\n");
    
    // 전역 변수 업데이트
    memcpy(&g_net_info, &current_info, sizeof(wiz_NetInfo));
}

// 네트워크 설정 재시도 로직 (공통 함수)
static bool apply_network_config_with_retry(network_mode_t mode, bool is_initial) {
    for (int retry = 0; retry < 3; retry++) {
        printf("Network config attempt %d/3...\n", retry + 1);
        
        setup_wiznet_network(&g_net_info);
        if (w5500_apply_network_config(&g_net_info, mode)) {
            // DHCP인 경우 IP 정보 출력
            if (mode == NETWORK_MODE_DHCP) {
            display_and_update_dhcp_info(is_initial);
            }
            return true;
        } else {
            if (retry < 2) {
                sleep_ms(1000);  // 재시도 대기 시간 최적화
                w5500_reset_network();
                sleep_ms(500);   // 리셋 후 대기 시간 최적화
            }
        }
    }
    return false;
}

// 네트워크 재시작 처리 함수 (최적화)
static void handle_network_restart(void) {
    printf("=== NETWORK RESTART ===\n");
    
    // HTTP 서버 정리
    http_server_cleanup();
    
    // 네트워크 재설정
    w5500_reset_network();
    sleep_ms(1500);  // 하드웨어 안정화를 위한 최적화된 대기 시간
    
    // 네트워크 재초기화 (전역 변수의 DHCP 플래그 확인)
    network_mode_t network_mode = (g_net_info.dhcp == NETINFO_DHCP) ? NETWORK_MODE_DHCP : NETWORK_MODE_STATIC;
    printf("Restart mode: %s\n", (network_mode == NETWORK_MODE_DHCP) ? "DHCP" : "STATIC");
    
    bool config_success = apply_network_config_with_retry(network_mode, false);
    
    if (config_success) {
        // HTTP 서버 재시작
        if (http_server_init(80)) {
            printf("=== RESTART COMPLETED ===\n");
            w5500_print_network_status();
        } else {
            printf("ERROR: HTTP server failed\n");
        }
    } else {
        printf("ERROR: Network config failed after 3 attempts\n");
        // 실패해도 HTTP 서버는 시작 (자동 복구 대기)
        if (http_server_init(80)) {
            printf("HTTP server started despite network config failure\n");
        }
    }
    
    // 재시작 플래그 리셋
    restart_requested = false;
}

int main()
{
    // RP2350 호환 초기화
    stdio_init_all();
    sleep_ms(2000);  // UART 안정화 대기
    
    // RP2350 안정화를 위한 더 긴 시작 대기
    printf("Starting Pico GPIO Server...\n");
    printf("Board: %s\n", PICO_BOARD);
    sleep_ms(3000);  // 추가 안정화 시간
    
    // MAC 주소 초기화
    generate_mac_from_board_id(&g_net_info);
    
    // W5500 및 네트워크 초기화
    if (w5500_initialize() == W5500_INIT_SUCCESS) {
        printf("W5500 initialization successful\n");
        
        // 초기 네트워크 설정 (공통 함수 사용)
        bool initial_config_success = apply_network_config_with_retry(NETWORK_MODE_DHCP, true);
        
        // DHCP 성공 여부와 관계없이 HTTP 서버 시작 (네트워크 복구 시 자동 재시작됨)
        if (http_server_init(80)) {
            printf("HTTP server started on port 80\n");
            if (initial_config_success) {
                printf("Network configuration successful - ready to serve\n");
            } else {
                printf("Network configuration failed - server will retry automatically\n");
            }
        } else {
            printf("ERROR: HTTP server failed to start\n");
        }
    } else {
        printf("ERROR: W5500 initialization failed\n");
    }
    while (true) {
        // 재시작 요청 확인 및 처리
        if (is_restart_requested()) {
            handle_network_restart();
        }
        
        // HTTP 서버 처리
        http_server_process();
    }
}

// 시스템 재시작 요청 함수
void system_restart_request(void) {
    restart_requested = true;
}

// 재시작 요청 상태 확인 함수
bool is_restart_requested(void) {
    return restart_requested;
}
