#include "network_config.h"
#include "system/system_config.h"
#include "debug/debug.h"
#include "../uart/uart_rs232.h"
#include "../tcp/tcp_server.h"
#include "../led/status_led.h"
#include "../main.h"
#include "multicast_server.h"
#include "FreeRTOS.h"
#include "semphr.h"
#include "task.h"
#include "network/mdns.h"

// Network configuration pointer (points to system_config network)
wiz_NetInfo* g_net_info = NULL;

// DHCP State Management
static bool dhcp_in_progress = false;
static uint32_t dhcp_start_time = 0;
static uint32_t dhcp_last_check = 0;
static uint32_t dhcp_last_tick = 0; // DHCP_time_handler용

bool is_ip_zero(const uint8_t ip[4]) {
    return (ip[0] == 0 && ip[1] == 0 && ip[2] == 0 && ip[3] == 0);
}

// Set default IP address values
void set_default_ip(uint8_t ip[4], uint8_t default_ip[4]) {
    memcpy(ip, default_ip, 4);
}

// Check if MAC address is all 0xFF or all 0x00 (invalid)
bool is_mac_invalid(const uint8_t mac[6]) {
    bool all_ff = true;
    bool all_00 = true;
    
    for (int i = 0; i < 6; i++) {
        if (mac[i] != 0xFF) all_ff = false;
        if (mac[i] != 0x00) all_00 = false;
    }
    
    return all_ff || all_00;
}

// Apply network configuration to W5500 chip
void apply_network_config(const wiz_NetInfo* config) {

    // W5500에 설정 적용
    wizchip_setnetinfo((wiz_NetInfo*)config);
    
    // W5500에서 읽어서 system_config에 저장
    // wizchip_getnetinfo(g_net_info);/
    DBG_NET_PRINT("Network configuration applied to W5500\n");
    w5500_print_network_status();
}
// DHCP configuration flag
bool dhcp_configured = false;

// Ethernet buffer for network operations
uint8_t g_ethernet_buf[2048];

// SPI 콜백 함수 구현
void wizchip_select(void) {
    gpio_put(SPI_CS, 0);
}

void wizchip_deselect(void) {
    gpio_put(SPI_CS, 1);
}

uint8_t wizchip_read(void) {
    uint8_t data = 0xFF;
    spi_read_blocking(SPI_PORT, 0xFF, &data, 1);
    return data;
}

void wizchip_write(uint8_t wb) {
    spi_write_blocking(SPI_PORT, &wb, 1);
}

w5500_init_result_t w5500_initialize(void) {
    DBG_WIZNET_PRINT("Starting W5500 initialization...\n");
    DBG_WIZNET_PRINT("Initializing SPI at 25MHz...\n");
    uint32_t actual_baudrate = spi_init(SPI_PORT, 5000 * 1000 * 5);
    DBG_WIZNET_PRINT("SPI baudrate set to: %u Hz\n", actual_baudrate);
    
    // SPI 포맷 설정: 8비트, SPI Mode 0 (CPOL=0, CPHA=0)
    spi_set_format(SPI_PORT, 8, SPI_CPOL_0, SPI_CPHA_0, SPI_MSB_FIRST);
    DBG_WIZNET_PRINT("SPI format: 8-bit, Mode 0, MSB first\n");
    
    // GPIO 핀 설정
    gpio_set_function(SPI_SCK, GPIO_FUNC_SPI);
    gpio_set_function(SPI_MOSI, GPIO_FUNC_SPI);
    gpio_set_function(SPI_MISO, GPIO_FUNC_SPI);
    DBG_WIZNET_PRINT("SPI pins configured: SCK=%d, MOSI=%d, MISO=%d\n", SPI_SCK, SPI_MOSI, SPI_MISO);
    
    // CS 핀 설정 (초기 상태 HIGH)
    gpio_init(SPI_CS);
    gpio_set_dir(SPI_CS, GPIO_OUT);
    gpio_put(SPI_CS, 1);
    gpio_init(SPI_RST);
    gpio_set_dir(SPI_RST, GPIO_OUT);
    
    // 리셋 전 상태 확인
    gpio_put(SPI_RST, 1);
    sleep_ms(100);
    // 리셋 활성화
    gpio_put(SPI_RST, 0);
    sleep_ms(500);
    // 리셋 해제
    gpio_put(SPI_RST, 1);
    sleep_ms(500);
  
    // W5500 소켓 버퍼 할당 (각 16KB 널이 TX/RX)
    // 소켓 0 (DHCP UDP ~300B), 1 (mDNS UDP ~512B), 2-4 (TCP JSON ~256B)
    // 소켓 5 (Multicast UDP), 6-7 (HTTP TCP — OTA/정적파일)
    // 합계: 1+1+1+1+1+1+4+4 = 14KB ≤ 16KB ✔ (HTTP에 4KB씩 집중)
    uint8_t tx_sizes[8] = {1, 1, 1, 1, 1, 1, 4, 4};
    uint8_t rx_sizes[8] = {1, 1, 1, 1, 1, 1, 4, 4};
    DBG_WIZNET_PRINT("Initializing WIZchip buffers...\n");
    
    int init_result = wizchip_init(tx_sizes, rx_sizes);
    if (init_result == -1) {
        DBG_WIZNET_PRINT("ERROR: wizchip_init failed\n");
        return W5500_INIT_ERROR_CHIP;
    }
    reg_wizchip_cs_cbfunc(wizchip_select, wizchip_deselect);
    reg_wizchip_spi_cbfunc(wizchip_read, wizchip_write);
    sleep_ms(100);
    
    DBG_WIZNET_PRINT("W5500 initialization completed successfully\n");
    return W5500_INIT_SUCCESS;
}

// Static IP 설정
bool w5500_set_static_ip(wiz_NetInfo *net_info) {
    net_info->dhcp = NETINFO_STATIC;
    apply_network_config(net_info);
    DBG_NET_PRINT("Static IP configuration applied\n");
    return true;
}

// DHCP 설정 (W5500용 수정된 코드 - 빠른 부팅 지원)
bool w5500_set_dhcp_mode(wiz_NetInfo *net_info) {
    DBG_DHCP_PRINT("Starting DHCP process...\n");
    
    // DHCP 모드로 설정 (먼저 설정)
    net_info->dhcp = NETINFO_DHCP;
    
    // 저장된 DHCP IP가 있으면 먼저 적용 (빠른 네트워크 연결)
    system_config_t *sys_cfg = system_config_get();
    if (sys_cfg->has_last_dhcp_ip) {
        DBG_DHCP_PRINT("Found last DHCP IP, applying temporarily...\n");
        memcpy(net_info->ip, sys_cfg->last_dhcp_ip, 4);
        memcpy(net_info->gw, sys_cfg->last_dhcp_gw, 4);
        memcpy(net_info->sn, sys_cfg->last_dhcp_sn, 4);
        memcpy(net_info->dns, sys_cfg->last_dhcp_dns, 4);
        // W5500에만 임시로 적용 (flash에 저장하지 않음)
        wizchip_setnetinfo(net_info);
        DBG_DHCP_PRINT("Last IP applied: %d.%d.%d.%d\n", 
            net_info->ip[0], net_info->ip[1], net_info->ip[2], net_info->ip[3]);
    }
    
    DBG_DHCP_PRINT("Closing all sockets...\n");
    for(int i = 0; i < 8; i++) close(i);
    
    DBG_DHCP_PRINT("Opening DHCP socket on port 68...\n");
    uint8_t sock_ret = socket(0, Sn_MR_UDP, 68, 0);
    DBG_DHCP_PRINT("Socket open result: %d\n", sock_ret);
    
    if(sock_ret != 0) { 
        DBG_DHCP_PRINT("ERROR: Failed to open DHCP socket\n");
        return false; 
    }
    
    uint8_t phy_status = getPHYCFGR();
    DBG_DHCP_PRINT("PHY status: 0x%02X\n", phy_status);
    
    if(!(phy_status & 0x01)) { 
        DBG_DHCP_PRINT("ERROR: No link detected\n");
        close(0); 
        return false; 
    }
    
    DBG_DHCP_PRINT("Initializing DHCP...\n");
    DHCP_init(0, g_ethernet_buf);
    DBG_DHCP_PRINT("DHCP_init completed\n");
    
    dhcp_in_progress = true;
    dhcp_start_time = to_ms_since_boot(get_absolute_time());
    dhcp_last_check = dhcp_start_time;
    dhcp_last_tick = dhcp_start_time;
    status_led_set_mode(LED_MODE_DHCP);  // DHCP 모드: 녹색 깜박임
    DBG_DHCP_PRINT("DHCP state variables initialized\n");
    
    DBG_DHCP_PRINT("DHCP started (non-blocking mode)\n");
    return false; // 아직 완료 안 됨, 다음 호출에서 상태 체크
}

// DHCP 진행 상태 체크 및 처리 (non-blocking)
bool dhcp_process_check(wiz_NetInfo *net_info) {
    if (!dhcp_in_progress) {
        return false; // DHCP가 시작되지 않음
    }
    
    uint32_t current_time = to_ms_since_boot(get_absolute_time());
    
    // 타임아웃 체크 (10초)
    if (current_time - dhcp_start_time > 10000) {
        DBG_DHCP_PRINT("DHCP timeout after 10 seconds\n");
        close(0);
        dhcp_in_progress = false;
        status_led_set_mode(LED_MODE_NORMAL);  // 일반 모드로 복귀
        return false;
    }
    
    // 링크 체크
    uint8_t phy_status = getPHYCFGR();
    if (!(phy_status & 0x01)) {
        DBG_DHCP_PRINT("ERROR: Link lost during DHCP\n");
        close(0);
        dhcp_in_progress = false;
        return false;
    }
    
    // DHCP 내부 타이머 핸들러 (500ms마다 호출)
    if (current_time - dhcp_last_tick >= 500) {
        DHCP_time_handler();
        dhcp_last_tick = current_time;
        // 진행 상태 표시
        uint32_t elapsed_sec = (current_time - dhcp_start_time) / 1000;
        DBG_DHCP_PRINT("DHCP waiting for IP... (elapsed: %lus)\n", elapsed_sec);
    }
    
    // 50ms마다 한 번만 체크
    if (current_time - dhcp_last_check < 50) {
        return false;
    }
    dhcp_last_check = current_time;
    
    // DHCP 상태 확인
    uint8_t dhcp_status = DHCP_run();
    
    switch(dhcp_status) {
        case DHCP_IP_LEASED:
            DBG_DHCP_PRINT("DHCP SUCCESS: IP leased!\n");
            getIPfromDHCP(g_net_info->ip);
            getGWfromDHCP(g_net_info->gw);
            getSNfromDHCP(g_net_info->sn);
            getDNSfromDHCP(g_net_info->dns);
            g_net_info->dhcp = NETINFO_DHCP;  // DHCP 모드로 설정
            
            // DHCP IP를 last_dhcp_ip에도 저장 (빠른 부팅용)
            system_config_t *sys_cfg = system_config_get();
            memcpy(sys_cfg->last_dhcp_ip, g_net_info->ip, 4);
            memcpy(sys_cfg->last_dhcp_gw, g_net_info->gw, 4);
            memcpy(sys_cfg->last_dhcp_sn, g_net_info->sn, 4);
            memcpy(sys_cfg->last_dhcp_dns, g_net_info->dns, 4);
            sys_cfg->has_last_dhcp_ip = true;
            
            apply_network_config(g_net_info);
            
            // Flash에 저장
            system_config_save_to_flash();
            DBG_DHCP_PRINT("DHCP configuration saved to flash\n");
            
            close(0);
            dhcp_in_progress = false;
            status_led_set_mode(LED_MODE_CONNECTED);  // 연결 모드: 녹색 고정
            
            return true;
            
        case DHCP_FAILED:
            DBG_DHCP_PRINT("DHCP FAILED\n");
            close(0);
            dhcp_in_progress = false;
            status_led_set_mode(LED_MODE_NORMAL);  // 일반 모드로 복귀
            return false;
            
        default:
            // 진행 중 - 1초마다 로그 출력
            uint32_t elapsed = (current_time - dhcp_start_time) / 1000;
            static uint32_t last_log_sec = 0;
            if (elapsed != last_log_sec) {
                DBG_DHCP_PRINT("DHCP waiting... %lu/20s\n", elapsed);
                last_log_sec = elapsed;
            }
            return false;
    }
}

// 네트워크 상태 출력
void w5500_print_network_status(void) {
    DBG_NET_PRINT("IP Address: %d.%d.%d.%d\n", g_net_info->ip[0], g_net_info->ip[1], g_net_info->ip[2], g_net_info->ip[3]);
    DBG_NET_PRINT("Subnet Mask: %d.%d.%d.%d\n", g_net_info->sn[0], g_net_info->sn[1], g_net_info->sn[2], g_net_info->sn[3]);
    DBG_NET_PRINT("Gateway: %d.%d.%d.%d\n", g_net_info->gw[0], g_net_info->gw[1], g_net_info->gw[2], g_net_info->gw[3]);
    DBG_NET_PRINT("DNS Server: %d.%d.%d.%d\n", g_net_info->dns[0], g_net_info->dns[1], g_net_info->dns[2], g_net_info->dns[3]);
    DBG_NET_PRINT("MAC Address: %02X:%02X:%02X:%02X:%02X:%02X\n", 
           g_net_info->mac[0], g_net_info->mac[1], g_net_info->mac[2], g_net_info->mac[3], g_net_info->mac[4], g_net_info->mac[5]);
    DBG_NET_PRINT("DHCP Mode       : %s\n",
           g_net_info->dhcp == NETINFO_DHCP ? "DHCP" : "Static");
    DBG_NET_PRINT("Link Status     : %s\n",
           w5500_check_link_status() ? "UP" : "DOWN");
}

// 링크 상태 확인
bool w5500_check_link_status(void) {
    // 방법 1: 라이브러리 함수 사용
    uint8_t phy_status = getPHYCFGR();
    
    // 방법 2: 직접 SPI로 읽기 (라이브러리 함수 실패 시)
    if (phy_status == 0x00 || phy_status == 0xFF) {
        gpio_put(SPI_CS, 0);
        sleep_us(10);
        
        // PHYCFGR 레지스터 읽기 (주소: 0x002E, 공통 레지스터)
        uint8_t cmd[3] = {0x00, 0x2E, 0x00};  // 주소 0x002E, 읽기 모드
        spi_write_blocking(SPI_PORT, cmd, 3);
        spi_read_blocking(SPI_PORT, 0xFF, &phy_status, 1);
        
        sleep_us(10);
        gpio_put(SPI_CS, 1);
    }
    
    return (phy_status & PHYCFGR_LNK_ON) ? true : false;
}

// 네트워크 케이블 연결 상태 확인
bool network_is_cable_connected(void) {
    return w5500_check_link_status();
}

// 네트워크 연결 상태 확인 (IP 할당 포함)
bool network_is_connected(void) {
    if (!network_is_cable_connected()) {
        return false;
    }
    
    uint8_t ip[4];
    getSIPR(ip);
    
    // IP가 할당되었는지 확인 (0.0.0.0이 아님)
    return !is_ip_zero(ip);
}

// 네트워크 초기화 함수
void network_init(void) {
    uint8_t mac[6];
    
    // g_net_info를 system_config의 network로 초기화 (이미 Flash에서 로드됨)
    g_net_info = system_config_get_network();
    
    // 유효성 검사: MAC이 모두 0xFF 또는 0x00이면 기본값으로 초기화
    if (is_mac_invalid(g_net_info->mac)) {
        DBG_NET_PRINT("Flash config invalid, using default config\n");
        memset(g_net_info, 0, sizeof(wiz_NetInfo));
        g_net_info->dhcp = NETINFO_DHCP;  // 기본값은 DHCP 모드
        g_net_info->mac[0] = 0x00; g_net_info->mac[1] = 0x08; g_net_info->mac[2] = 0xDC;
        g_net_info->ip[0] = 192; g_net_info->ip[1] = 168; g_net_info->ip[2] = 1; g_net_info->ip[3] = 100;
        g_net_info->sn[0] = 255; g_net_info->sn[1] = 255; g_net_info->sn[2] = 255; g_net_info->sn[3] = 0;
        g_net_info->gw[0] = 192; g_net_info->gw[1] = 168; g_net_info->gw[2] = 1; g_net_info->gw[3] = 1;
        g_net_info->dns[0] = 8; g_net_info->dns[1] = 8; g_net_info->dns[2] = 8; g_net_info->dns[3] = 8;
    }
    
    // 보드 고유 ID로 MAC 생성 및 설정
    generate_mac_from_board_id(mac);
    memcpy(g_net_info->mac, mac, 6);
    
    // 고정 IP 모드일 때, IP/GW/SN/DNS가 0.0.0.0 이면 기본값으로 보정
    if (g_net_info->dhcp == NETINFO_STATIC) {
        if (is_ip_zero(g_net_info->ip)) {
            uint8_t default_ip[4] = {192, 168, 1, 100};
            set_default_ip(g_net_info->ip, default_ip);
            DBG_NET_PRINT("Static IP was 0.0.0.0, set to default 192.168.1.100\n");
        }
        if (is_ip_zero(g_net_info->gw)) {
            uint8_t default_gw[4] = {192, 168, 1, 1};
            set_default_ip(g_net_info->gw, default_gw);
            DBG_NET_PRINT("Gateway was 0.0.0.0, set to default 192.168.1.1\n");
        }
        if (is_ip_zero(g_net_info->sn)) {
            uint8_t default_sn[4] = {255, 255, 255, 0};
            set_default_ip(g_net_info->sn, default_sn);
            DBG_NET_PRINT("Subnet Mask was 0.0.0.0, set to default 255.255.255.0\n");
        }
        if (is_ip_zero(g_net_info->dns)) {
            uint8_t default_dns[4] = {8, 8, 8, 8};
            set_default_ip(g_net_info->dns, default_dns);
            DBG_NET_PRINT("DNS was 0.0.0.0, set to default 8.8.8.8\n");
        }
    }
    DBG_NET_PRINT("Network configuration loaded from flash (system config)\n");
    
    // W5500 및 네트워크 초기화
    if (w5500_initialize() == W5500_INIT_SUCCESS) {
        DBG_WIZNET_PRINT("W5500 initialization successful\n");
        
        // MAC을 W5500에 먼저 적용 (DHCP 소켓 사용 전 MAC이 설정되어야 함)
        // DHCP 모드일 때는 IP/SN/GW/DNS를 0으로 두어야
        // getSIPR()이 0.0.0.0을 반환 → network_is_connected() false → 링크 업 후 DHCP 재시도 정상 동작
        if (g_net_info->dhcp == NETINFO_DHCP) {
            wiz_NetInfo mac_only = {0};
            mac_only.dhcp = NETINFO_DHCP;
            memcpy(mac_only.mac, g_net_info->mac, 6);
            wizchip_setnetinfo(&mac_only);
        } else {
            wizchip_setnetinfo(g_net_info);
        }

        // DHCP 또는 Static IP 모드에 따라 설정 적용
        if (g_net_info->dhcp == NETINFO_DHCP) {
            DBG_NET_PRINT("Starting DHCP mode...\n");
            w5500_set_dhcp_mode(g_net_info);
        } else {
            DBG_NET_PRINT("Applying Static IP mode...\n");
            w5500_set_static_ip(g_net_info);
        }
        
        // wizchip_getnetinfo() 호출 금지: DHCP 미완료 상태에서 읽으면 g_net_info가 0으로 덮어씌워짐
        DBG_NET_PRINT("[NET] Network info initialized\n");
    } else {
        DBG_WIZNET_PRINT("ERROR: W5500 initialization failed\n");
    }
    
    // 케이블 연결 상태 확인
    bool cable_connected = network_is_cable_connected();
    w5500_print_network_status();
}

// 네트워크 처리 헬퍼 함수들

// 네트워크 상태 업데이트
static void network_update_status(network_status_t* status) {
    static bool last_cable_state = false;
    static bool last_connected_state = false;
    
    bool cable_connected = network_is_cable_connected();
    status->current_link_up = cable_connected;
    status->current_connected = network_is_connected();
    
    // 케이블 상태 변경 감지
    if (cable_connected != last_cable_state) {
        status->link_changed = true;
        DBG_NET_PRINT("[NETWORK] Link %s detected\n", cable_connected ? "up" : "down");
        dhcp_configured = false;
        last_cable_state = cable_connected;

        // 링크 다운 시 DHCP 모드면 W5500 IP를 0으로 초기화
        // → getSIPR()이 0.0.0.0 반환 → 링크 업 후 network_is_connected()=false → DHCP 재시도 트리거
        if (!cable_connected && g_net_info->dhcp == NETINFO_DHCP) {
            dhcp_in_progress = false;  // 진행 중인 DHCP 중단
            close(0);                  // DHCP 소켓 닫기
            memset(g_net_info->ip,  0, 4);
            memset(g_net_info->gw,  0, 4);
            memset(g_net_info->sn,  0, 4);
            memset(g_net_info->dns, 0, 4);
            wiz_NetInfo mac_only = {0};
            mac_only.dhcp = NETINFO_DHCP;
            memcpy(mac_only.mac, g_net_info->mac, 6);
            wizchip_setnetinfo(&mac_only);  // W5500 IP 레지스터도 0으로 클리어
            DBG_DHCP_PRINT("Link down: DHCP IP cleared, will retry on link up\n");
        }
    }
    
    // 연결 상태 변경 감지
    if (status->current_connected != last_connected_state) {
        status->connection_changed = true;
        last_connected_state = status->current_connected;
    }
    // 상태 LED 업데이트
    status_led_set_network_connected(status->current_connected);
}

// IP 할당 처리
static void network_handle_ip_assignment(bool cable_connected, network_status_t* status) {
    static uint32_t dhcp_retry_time = 0;
    uint32_t current_time = to_ms_since_boot(get_absolute_time());
    
    // DHCP 진행 중이면 체크
    if (dhcp_in_progress) {
        static uint32_t last_debug = 0;
        if (current_time - last_debug >= 5000) {
            DBG_DHCP_PRINT("DHCP in progress...\n");
            last_debug = current_time;
        }
        
        if (dhcp_process_check(g_net_info)) {
            printf("DHCP successful, IP assigned\n");
            dhcp_configured = true;
            w5500_print_network_status();
            status->connection_changed = true;
        }
        return;
    }
    
    // IP가 없으면 할당 시도
    if (cable_connected && !network_is_connected()) {
        if (g_net_info->dhcp == NETINFO_DHCP && !dhcp_configured) {
            if (dhcp_retry_time == 0 || (current_time - dhcp_retry_time) >= 1000) {
                printf("Attempting DHCP for IP assignment...\n");
                w5500_set_dhcp_mode(g_net_info);
                dhcp_retry_time = current_time;
            }
        } else if (g_net_info->dhcp == NETINFO_STATIC) {
            printf("Applying static IP configuration...\n");
            if (w5500_set_static_ip(g_net_info)) {
                printf("Static IP applied successfully\n");
                w5500_print_network_status();
                status->connection_changed = true;
            }
        }
    }
}

// LED 제어 처리
static void network_handle_led(const network_status_t* status) {
    if (status->link_changed && !status->current_link_up) {
        status_led_set_mode(LED_MODE_BOOT);
    }
    
    if (status->connection_changed && status->current_connected) {
        status_led_set_mode(LED_MODE_CONNECTED);
        
        // TCP 큐 초기화
        if (gpio_queues[GPIO_QUEUE_TCP] != NULL) {
            xQueueReset(gpio_queues[GPIO_QUEUE_TCP]);
            DBG_NET_PRINT("[TCP] Queue reset on network connect\n");
        }
    }
}

// TCP 큐 처리
static void network_handle_tcp_queue(bool connected) {
    if (connected && tcp_servers_initialized && 
        tcp_servers_has_clients() && gpio_queues[GPIO_QUEUE_TCP] != NULL) {
        gpio_queues_enabled[GPIO_QUEUE_TCP] = true;
        char msg_buffer[GPIO_MSG_MAX_LEN];
        while (xQueueReceive(gpio_queues[GPIO_QUEUE_TCP], msg_buffer, 0) == pdTRUE) {
            tcp_servers_broadcast((uint8_t*)msg_buffer, strlen(msg_buffer));
        }
    } else {
        gpio_queues_enabled[GPIO_QUEUE_TCP] = false;
    }
}

// 서버 초기화 및 처리
static bool multicast_initialized = false;

static void network_handle_servers(bool connected) {
    // TCP/HTTP 서버 초기화 (한 번만, 케이블 연결 시)
    if (!tcp_servers_initialized && connected && !is_system_restart_requested()) {
        tcp_servers_init(tcp_port);
        tcp_servers_initialized = true;
        http_server_init();
        printf("[TCP] TCP servers initialized\n");
        printf("[HTTP] HTTP server started on port 80\n");
        fflush(stdout);
    }
    
    // 서버 처리
    if (connected && !is_system_restart_requested()) {
        // mDNS와 멀티캐스트는 network_is_connected() (IP 할당 완료) 상태에서만 동작
        if (network_is_connected()) {
            // mDNS가 초기화되지 않았으면 초기화 (5분 제한 해제 — 상시 동작)
            if (!mdns_is_initialized()) {
                mdns_init();
            }
            mdns_process();
            
            // 멀티캐스트는 설정(multicast_enabled)이 켜져 있을 때만 동작
            // 웹UI/CLI에서 언제든 켜고 끌 수 있으며, 다음 루프(10ms)에 바로 반영됨
            if (system_config_get_multicast_enabled()) {
                // 멀티캐스트 서버 초기화 (DHCP IP 할당 후)
                if (!multicast_initialized) {
                    if (multicast_server_init()) {
                        multicast_initialized = true;
                        printf("[MCAST] Multicast server initialized after IP assignment\n");
                        fflush(stdout);
                    }
                }

                // 멀티캐스트 서버 처리
                multicast_server_process();

                // 멀티캐스트 GPIO 메시지 큐 처리
                if (gpio_queues[GPIO_QUEUE_MCAST] != NULL) {
                    char msg_buffer[GPIO_MSG_MAX_LEN];
                    while (xQueueReceive(gpio_queues[GPIO_QUEUE_MCAST], msg_buffer, 0) == pdTRUE) {
                        multicast_send_feedback(msg_buffer);
                    }
                }
            } else if (multicast_initialized) {
                // 런타임에 비활성화됨 → 소켓 닫기
                multicast_server_close();
                multicast_initialized = false;
                printf("[MCAST] Disabled by configuration, socket closed\n");
                fflush(stdout);
            }
        }
        tcp_servers_process();
        http_server_process();
    }
}

// 네트워크 처리 메인 함수

network_status_t network_process(void) {
    network_status_t status = {
        .link_changed = false,
        .connection_changed = false,
        .current_link_up = false,
        .current_connected = false
    };
    
    // 1. 상태 업데이트
    network_update_status(&status);
    
    // 2. IP 할당 처리
    network_handle_ip_assignment(status.current_link_up, &status);
    
    // 3. LED 제어
    network_handle_led(&status);
    
    // 4. TCP 큐 처리
    network_handle_tcp_queue(status.current_connected);
    
    // 5. 서버 초기화 및 처리
    network_handle_servers(status.current_connected);

    return status;
}

void network_task(void *pvParameters)
{
    DBG_MAIN_PRINT("[TASK] network_task started\n");
    fflush(stdout);

    while (true) {
        if (is_system_restart_requested()) {
            DBG_MAIN_PRINT("[RESTART] System monitor task detected restart request\n");
            system_restart();
        }
        network_process();
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}
