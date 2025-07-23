#include <stdio.h>
#include <string.h>
#include "pico/stdlib.h"
#include "main.h"
#include "network/mac_utils.h"
#include "network/network_config.h"
#include "http_server.h"
#include "hardware/watchdog.h"
#include "pico/stdlib.h"
#include "hardware/sync.h"
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
static volatile bool network_restarted = false;

// DHCP IP 정보 출력 및 전역 변수 업데이트 (공통 함수)
// DHCP IP 출력 함수 제거, w5500_print_network_status로 통일

// 네트워크 설정 재시도 로직 (공통 함수)
static bool apply_network_config_with_retry(network_mode_t mode, bool is_initial) {
    for (int retry = 0; retry < 3; retry++) {
        printf("Network config attempt %d/3...\n", retry + 1);
        setup_wiznet_network(&g_net_info);
        bool result = w5500_apply_network_config(&g_net_info, mode);
        printf("w5500_apply_network_config() returned: %s\n", result ? "SUCCESS" : "FAIL");
        if (result) {
            // DHCP/STATIC 모두 네트워크 상태 출력
            w5500_print_network_status();
            return true;
        } else {
            if (retry < 2) {
                sleep_ms(1000);
                http_server_cleanup();
                w5500_reset_network();
                sleep_ms(500);
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
    sleep_ms(1000);  // 하드웨어 안정화를 위한 최적화된 대기 시간
    
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
    network_restarted = false;
}

int main()
{
    // RP2350 호환 초기화
    stdio_init_all();
    sleep_ms(2000);  // UART 안정화 대기
    
    // RP2350 안정화를 위한 더 긴 시작 대기
    printf("Starting Pico GPIO Server...\n");
    printf("Board: %s\n", PICO_BOARD);
    
    // 저장된 네트워크 설정 로드
    printf("Loading network configuration from flash...\n");
    network_config_load_from_flash(&g_net_info);

    // MAC 주소 초기화
    generate_mac_from_board_id(&g_net_info);
    
    // W5500 및 네트워크 초기화
    if (w5500_initialize() == W5500_INIT_SUCCESS) {
        printf("W5500 initialization successful\n");
        
        // 초기 네트워크 설정 (공통 함수 사용)
        network_mode_t mode = (g_net_info.dhcp == NETINFO_DHCP) ? NETWORK_MODE_DHCP : NETWORK_MODE_STATIC;
        printf("Network mode: %s\n", (mode == NETWORK_MODE_DHCP) ? "DHCP" : "STATIC");
        bool initial_config_success = apply_network_config_with_retry(mode, true);
        
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
        // 네트워크 재시작 요청 확인 및 처리
        // if (is_network_restart_requested()) {
        //     handle_network_restart();
        // }
        
        // 시스템 재시작 요청 확인 및 처리
        if (is_system_restart_requested()) {
            system_restart();
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
bool is_network_restart_requested(void) {
   return network_restarted;
}

// 네트워크 재시작 요청 함수
void network_restart_request(void) {
    network_restarted = true;
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