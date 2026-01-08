#include "network_config.h"
#include "system/system_config.h"
#include "debug/debug.h"
#include "../uart/uart_rs232.h"
#include "../tcp/tcp_server.h"
#include "../led/status_led.h"
#include "FreeRTOS.h"
#include "semphr.h"

// =============================================================================
// W5500 SPI Mutex for FreeRTOS
// =============================================================================
static SemaphoreHandle_t w5500_mutex = NULL;
static bool w5500_mutex_initialized = false;

static void w5500_critical_enter(void) {
    if (w5500_mutex != NULL && xTaskGetSchedulerState() == taskSCHEDULER_RUNNING) {
        xSemaphoreTakeRecursive(w5500_mutex, portMAX_DELAY);  // Recursive로 변경
    }
}

static void w5500_critical_exit(void) {
    if (w5500_mutex != NULL && xTaskGetSchedulerState() == taskSCHEDULER_RUNNING) {
        xSemaphoreGiveRecursive(w5500_mutex);  // Recursive로 변경
    }
}

// 뮤텍스 초기화 (스케줄러 시작 후 호출)
void network_enable_spi_mutex(void) {
    if (!w5500_mutex_initialized) {
        w5500_mutex = xSemaphoreCreateRecursiveMutex();  // Recursive로 변경
        if (w5500_mutex != NULL) {
            w5500_mutex_initialized = true;
            reg_wizchip_cris_cbfunc(w5500_critical_enter, w5500_critical_exit);
            DBG_NET_PRINT("W5500 SPI recursive mutex initialized and registered\n");
        }
    }
}

// =============================================================================
// DHCP State Management
// =============================================================================
static bool dhcp_in_progress = false;
static uint32_t dhcp_start_time = 0;
static uint32_t dhcp_last_check = 0;
static uint32_t dhcp_last_tick = 0; // DHCP_time_handler용

// =============================================================================
// IP Address Utility Functions
// =============================================================================

// Check if IP address is all zeros (0.0.0.0)
bool is_ip_zero(const uint8_t ip[4]) {
    return (ip[0] == 0 && ip[1] == 0 && ip[2] == 0 && ip[3] == 0);
}

// Print IP address in dotted decimal format
void print_ip_address(const char* label, const uint8_t ip[4]) {
    DBG_NET_PRINT("%s: %d.%d.%d.%d\n", label, ip[0], ip[1], ip[2], ip[3]);
}

// Set default IP address values
void set_default_ip(uint8_t ip[4], uint8_t default_ip[4]) {
    memcpy(ip, default_ip, 4);
}

// Print MAC address in colon-separated format
void print_network_mac_address(const char* label, const uint8_t mac[6]) {
    DBG_NET_PRINT("%s: %02X:%02X:%02X:%02X:%02X:%02X\n", label,
           mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
}

// Print DHCP mode status
void print_dhcp_mode(void) {
    DBG_NET_PRINT("DHCP Mode       : %s\n",
           g_net_info.dhcp == NETINFO_DHCP ? "DHCP" : "Static");
}

// Print link status
void print_link_status(void) {
    DBG_NET_PRINT("Link Status     : %s\n",
           w5500_check_link_status() ? "UP" : "DOWN");
}

// =============================================================================
// Network Configuration Application Functions
// =============================================================================

// Check if MAC address is all 0xFF (invalid)
bool is_mac_invalid(const uint8_t mac[6]) {
    for (int i = 0; i < 6; i++) {
        if (mac[i] != 0xFF) {
            return false;
        }
    }
    return true;
}

// Apply network configuration to W5500 chip
void apply_network_config(const wiz_NetInfo* config) {
    // WIZnet 라이브러리 함수를 사용하여 설정 (내부 상태 업데이트 포함)
    wizchip_setnetinfo((wiz_NetInfo*)config);
    
    DBG_NET_PRINT("Network configuration applied to W5500\n");
    DBG_NET_PRINT("IP: %d.%d.%d.%d, DHCP: %s\n", 
        config->ip[0], config->ip[1], config->ip[2], config->ip[3],
        config->dhcp == NETINFO_DHCP ? "DHCP" : "Static");
}

// Apply network configuration with status message
void apply_network_config_with_status(const wiz_NetInfo* config, const char* status_message) {
    apply_network_config(config);
    DBG_NET_PRINT("%s\n", status_message);
}

// =============================================================================
// Global Variables
// =============================================================================

// Network configuration (loaded from/saved to flash)
wiz_NetInfo g_net_info = {
    .mac = { 0x00, 0x08, 0xDC, 0x00, 0x00, 0x00 },
    .ip = { 192, 168, 1, 100 },
    .sn = { 255, 255, 255, 0 },
    .gw = { 0, 0, 0, 0 },
    .dns = { 0, 0, 0, 0 },
    .dhcp = NETINFO_STATIC
};

// DHCP configuration flag
bool dhcp_configured = false;

// Ethernet buffer for network operations
uint8_t g_ethernet_buf[2048];

// SPI 콜백 함수 구현
void wizchip_select(void) {
    gpio_put(SPI_CS, 0);
    sleep_us(1);  // CS 안정화를 위한 짧은 지연
}

void wizchip_deselect(void) {
    sleep_us(1);  // 데이터 전송 완료 대기
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

void network_config_save_to_flash(const wiz_NetInfo* config) {
    wiz_NetInfo* sys_net = system_config_get_network();
    memcpy(sys_net, config, sizeof(wiz_NetInfo));
    system_config_save_to_flash();
    DBG_NET_PRINT("Network configuration saved to flash (system config).\n");
}

void network_config_load_from_flash(wiz_NetInfo* config) {
    wiz_NetInfo* sys_net = system_config_get_network();
    uint8_t mac[6];
    
    // 시스템 설정에서 복사
    memcpy(config, sys_net, sizeof(wiz_NetInfo));

    // 유효성 검사: MAC이 모두 0xFF 또는 0x00이면 기본값으로 초기화
    if (is_mac_invalid(config->mac)) {
        DBG_NET_PRINT("Flash config invalid, using default config\n");
        memset(config, 0, sizeof(wiz_NetInfo));
        config->mac[0] = 0x00; config->mac[1] = 0x08; config->mac[2] = 0xDC;
        config->ip[0] = 192; config->ip[1] = 168; config->ip[2] = 1; config->ip[3] = 100;
        config->sn[0] = 255; config->sn[1] = 255; config->sn[2] = 255; config->sn[3] = 0;
        config->gw[0] = 192; config->gw[1] = 168; config->gw[2] = 1; config->gw[3] = 1;
        config->dns[0] = 8; config->dns[1] = 8; config->dns[2] = 8; config->dns[3] = 8;
        config->dhcp = NETINFO_STATIC;
    }
    // 보드 고유 ID로 MAC 생성 및 설정
    generate_mac_from_board_id(mac);
    memcpy(config->mac, mac, 6);
    // 고정 IP 모드일 때, IP/GW/SN/DNS가 0.0.0.0 이면 기본값으로 보정
    if (config->dhcp == NETINFO_STATIC) {
        if (is_ip_zero(config->ip)) {
            uint8_t default_ip[4] = {192, 168, 1, 100};
            set_default_ip(config->ip, default_ip);
            DBG_NET_PRINT("Static IP was 0.0.0.0, set to default 192.168.1.100\n");
        }
        if (is_ip_zero(config->gw)) {
            uint8_t default_gw[4] = {192, 168, 1, 1};
            set_default_ip(config->gw, default_gw);
            DBG_NET_PRINT("Gateway was 0.0.0.0, set to default 192.168.1.1\n");
        }
        if (is_ip_zero(config->sn)) {
            uint8_t default_sn[4] = {255, 255, 255, 0};
            set_default_ip(config->sn, default_sn);
            DBG_NET_PRINT("Subnet Mask was 0.0.0.0, set to default 255.255.255.0\n");
        }
        if (is_ip_zero(config->dns)) {
            uint8_t default_dns[4] = {8, 8, 8, 8};
            set_default_ip(config->dns, default_dns);
            DBG_NET_PRINT("DNS was 0.0.0.0, set to default 8.8.8.8\n");
        }
    }
    DBG_NET_PRINT("Network configuration loaded from flash (system config)\n");
}

w5500_init_result_t w5500_initialize(void) {
    DBG_WIZNET_PRINT("Starting W5500 initialization...\n");
    DBG_WIZNET_PRINT("=== Hardware Pin Test ===\n");
    
    // MISO 핀 풀업 테스트 (W5500 연결 전)
    gpio_init(SPI_MISO);
    gpio_set_dir(SPI_MISO, GPIO_IN);
    gpio_pull_up(SPI_MISO);
    sleep_ms(10);
    bool miso_pullup = gpio_get(SPI_MISO);
    DBG_WIZNET_PRINT("MISO pin %d pull-up test: %s\n", SPI_MISO, miso_pullup ? "OK" : "FAIL");
    
    gpio_pull_down(SPI_MISO);
    sleep_ms(10);
    bool miso_pulldown = gpio_get(SPI_MISO);
    DBG_WIZNET_PRINT("MISO pin %d pull-down test: %s\n", SPI_MISO, miso_pulldown ? "FAIL" : "OK");
    gpio_disable_pulls(SPI_MISO);
    
    // 하드웨어 초기화 (직접 인라인)
    // SPI 속도를 5MHz로 설정
    DBG_WIZNET_PRINT("Initializing SPI at 5MHz...\n");
    uint32_t actual_baudrate = spi_init(SPI_PORT, 5000 * 1000);
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
    DBG_WIZNET_PRINT("CS pin configured: CS=%d (initial HIGH)\n", SPI_CS);
    
    // W5500 리셋 (하드웨어 진단 포함)
    DBG_WIZNET_PRINT("=== W5500 Reset Sequence ===\n");
    gpio_init(SPI_RST);
    gpio_set_dir(SPI_RST, GPIO_OUT);
    
    // 리셋 전 상태 확인
    gpio_put(SPI_RST, 1);
    sleep_ms(100);
    DBG_WIZNET_PRINT("RST pin HIGH - checking MISO state...\n");
    uint8_t miso_before = gpio_get(SPI_MISO);
    DBG_WIZNET_PRINT("MISO state (RST=HIGH): %d\n", miso_before);
    
    // 리셋 활성화
    gpio_put(SPI_RST, 0);
    sleep_ms(500);
    DBG_WIZNET_PRINT("RST pin LOW - checking MISO state...\n");
    uint8_t miso_reset = gpio_get(SPI_MISO);
    DBG_WIZNET_PRINT("MISO state (RST=LOW): %d\n", miso_reset);
    
    // 리셋 해제
    gpio_put(SPI_RST, 1);
    sleep_ms(500);
    DBG_WIZNET_PRINT("W5500 reset complete\n");
    
    // SPI 루프백 테스트 (MISO 핀 확인)
    DBG_WIZNET_PRINT("=== SPI Loopback Test ===\n");
    uint8_t test_patterns[4] = {0x00, 0xFF, 0xAA, 0x55};
    for (int i = 0; i < 4; i++) {
        gpio_put(SPI_CS, 0);
        sleep_us(10);
        uint8_t rx_data = 0;
        spi_write_read_blocking(SPI_PORT, &test_patterns[i], &rx_data, 1);
        sleep_us(10);
        gpio_put(SPI_CS, 1);
        DBG_WIZNET_PRINT("Pattern 0x%02X -> Received 0x%02X\n", test_patterns[i], rx_data);
        sleep_ms(10);
    }
    
    // W5500 버전 레지스터 읽기 (여러 방법 시도)
    DBG_WIZNET_PRINT("=== W5500 Version Register Test ===\n");
    
    // 방법 1: 일반 읽기
    gpio_put(SPI_CS, 0);
    sleep_us(10);
    uint8_t cmd1[3] = {0x00, 0x39, 0x00};  // 주소 0x0039, 제어 바이트 0x00 (읽기)
    spi_write_blocking(SPI_PORT, cmd1, 3);
    uint8_t ver1 = 0xFF;
    spi_read_blocking(SPI_PORT, 0xFF, &ver1, 1);
    sleep_us(10);
    gpio_put(SPI_CS, 1);
    DBG_WIZNET_PRINT("Method 1 (addr 0x0039): 0x%02X\n", ver1);
    sleep_ms(50);
    
    // 방법 2: 다른 주소 방식
    gpio_put(SPI_CS, 0);
    sleep_us(10);
    uint8_t cmd2[3] = {0x00, 0x39, 0x01};  // VDM 읽기 모드
    spi_write_blocking(SPI_PORT, cmd2, 3);
    uint8_t ver2 = 0xFF;
    spi_read_blocking(SPI_PORT, 0xFF, &ver2, 1);
    sleep_us(10);
    gpio_put(SPI_CS, 1);
    DBG_WIZNET_PRINT("Method 2 (VDM mode): 0x%02X\n", ver2);
    sleep_ms(50);
    
    // W5500 소켓 버퍼 초기화 및 설정 (최적화 - HTTP 서버에 더 많은 버퍼 할당)
    uint8_t tx_sizes[8] = {2, 2, 2, 2, 2, 2, 2, 2};
    uint8_t rx_sizes[8] = {2, 2, 2, 2, 2, 2, 2, 2};
    DBG_WIZNET_PRINT("Initializing WIZchip buffers...\n");
    
    int init_result = wizchip_init(tx_sizes, rx_sizes);
    DBG_WIZNET_PRINT("wizchip_init returned: %d\n", init_result);
    
    if (init_result == -1) {
        DBG_WIZNET_PRINT("ERROR: wizchip_init failed\n");
        return W5500_INIT_ERROR_CHIP;
    }
    
    // wizchip_init 후 충분한 안정화 시간
    sleep_ms(200);
    
    // 콜백 함수 재등록 (wizchip_init이 손상시켰을 수 있음)
    DBG_WIZNET_PRINT("Re-registering WIZchip callbacks...\n");
    reg_wizchip_cs_cbfunc(wizchip_select, wizchip_deselect);
    reg_wizchip_spi_cbfunc(wizchip_read, wizchip_write);
    sleep_ms(100);
    
    // 버전 확인 (직접 SPI로)
    DBG_WIZNET_PRINT("=== Reading W5500 Version (after wizchip_init) ===\n");
    uint8_t version = 0;
    for (int retry = 0; retry < 3; retry++) {
        gpio_put(SPI_CS, 0);
        sleep_us(10);
        uint8_t cmd[3] = {0x00, 0x39, 0x00};
        spi_write_blocking(SPI_PORT, cmd, 3);
        spi_read_blocking(SPI_PORT, 0xFF, &version, 1);
        sleep_us(10);
        gpio_put(SPI_CS, 1);
        
        DBG_WIZNET_PRINT("W5500 version read attempt %d: 0x%02X\n", retry + 1, version);
        if (version == 0x04) {
            DBG_WIZNET_PRINT("Version check PASSED!\n");
            break;
        }
        sleep_ms(100);
    }
    
    if (version != 0x04) {
        DBG_WIZNET_PRINT("WARNING: W5500 version mismatch (expected 0x04, got 0x%02X)\n", version);
        DBG_WIZNET_PRINT("Continuing initialization anyway...\n");
        // 버전 불일치 시에도 계속 진행 (일부 W5500 클론 칩은 다른 버전 코드를 반환할 수 있음)
    }
    
    // 링크 상태 확인 (최대 10초 대기)
    DBG_WIZNET_PRINT("Waiting for Ethernet link...\n");
    for (int i = 0; i < 20; i++) {
        uint8_t phycfg = getPHYCFGR();
        if (phycfg & PHYCFGR_LNK_ON) {
            DBG_WIZNET_PRINT("Ethernet link detected (PHYCFGR: 0x%02X)\n", phycfg);
            break;
        }
        sleep_ms(500);
    }
    
    return W5500_INIT_SUCCESS;
}

// Static IP 설정
bool w5500_set_static_ip(wiz_NetInfo *net_info) {
    // DHCP 모드를 Static으로 변경
    net_info->dhcp = NETINFO_STATIC;
    
    // 네트워크 정보를 W5500에 설정
    apply_network_config(net_info);
    
    return true;
}

// DHCP 설정 (W5500용 수정된 코드 - 빠른 부팅 지원)
bool w5500_set_dhcp_mode(wiz_NetInfo *net_info) {
    DBG_DHCP_PRINT("Starting DHCP process...\n");
    
    // 저장된 DHCP IP가 있으면 먼저 적용 (빠른 네트워크 연결)
    system_config_t *sys_cfg = system_config_get();
    if (sys_cfg->has_last_dhcp_ip) {
        DBG_DHCP_PRINT("Found last DHCP IP, applying temporarily...\n");
        memcpy(net_info->ip, sys_cfg->last_dhcp_ip, 4);
        memcpy(net_info->gw, sys_cfg->last_dhcp_gw, 4);
        memcpy(net_info->sn, sys_cfg->last_dhcp_sn, 4);
        memcpy(net_info->dns, sys_cfg->last_dhcp_dns, 4);
        net_info->dhcp = NETINFO_STATIC;  // 임시로 Static으로 설정
        apply_network_config(net_info);
        DBG_DHCP_PRINT("Last IP applied: %d.%d.%d.%d\n", 
            net_info->ip[0], net_info->ip[1], net_info->ip[2], net_info->ip[3]);
    }
    
    // DHCP 모드로 변경
    net_info->dhcp = NETINFO_DHCP;
    
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
    
    // 타임아웃 체크 (20초)
    if (current_time - dhcp_start_time > 20000) {
        DBG_DHCP_PRINT("DHCP timeout after 20 seconds\n");
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
    
    // DHCP 내부 타이머 핸들러 (1초마다 호출 필수)
    if (current_time - dhcp_last_tick >= 1000) {
        DHCP_time_handler();
        dhcp_last_tick = current_time;
        // 진행 상태 표시
        uint32_t elapsed_sec = (current_time - dhcp_start_time) / 1000;
        DBG_DHCP_PRINT("DHCP waiting for IP... (elapsed: %lus)\n", elapsed_sec);
    }
    
    // 100ms마다 한 번만 체크
    if (current_time - dhcp_last_check < 100) {
        return false;
    }
    dhcp_last_check = current_time;
    
    // DHCP 상태 확인
    uint8_t dhcp_status = DHCP_run();
    
    switch(dhcp_status) {
        case DHCP_IP_LEASED:
            DBG_DHCP_PRINT("DHCP SUCCESS: IP leased!\n");
            getIPfromDHCP(net_info->ip);
            getGWfromDHCP(net_info->gw);
            getSNfromDHCP(net_info->sn);
            getDNSfromDHCP(net_info->dns);
            memcpy(&g_net_info, net_info, sizeof(wiz_NetInfo));
            
            // IP 저장
            system_config_t *sys_cfg = system_config_get();
            if (!sys_cfg->has_last_dhcp_ip || 
                memcmp(sys_cfg->last_dhcp_ip, net_info->ip, 4) != 0) {
                memcpy(sys_cfg->last_dhcp_ip, net_info->ip, 4);
                memcpy(sys_cfg->last_dhcp_gw, net_info->gw, 4);
                memcpy(sys_cfg->last_dhcp_sn, net_info->sn, 4);
                memcpy(sys_cfg->last_dhcp_dns, net_info->dns, 4);
                sys_cfg->has_last_dhcp_ip = true;
                system_config_save_to_flash();
                DBG_DHCP_PRINT("New DHCP IP saved\n");
            }
            
            apply_network_config(net_info);
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
    wiz_NetInfo current_info;
    wizchip_getnetinfo(&current_info);

    print_ip_address("IP Address", current_info.ip);
    print_ip_address("Subnet Mask", current_info.sn);
    print_ip_address("Gateway", current_info.gw);
    print_ip_address("DNS Server", current_info.dns);
    print_network_mac_address("MAC Address", current_info.mac);
    print_dhcp_mode();
    print_link_status();
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
    // 저장된 네트워크 설정 로드 및 MAC 주소 설정
    DBG_NET_PRINT("Loading network configuration from flash...\n");
    network_config_load_from_flash(&g_net_info);
    
    // W5500 및 네트워크 초기화
    if (w5500_initialize() == W5500_INIT_SUCCESS) {
        DBG_WIZNET_PRINT("W5500 initialization successful\n");
        // W5500에 네트워크 설정 적용
        apply_network_config(&g_net_info);
    } else {
        DBG_WIZNET_PRINT("ERROR: W5500 initialization failed\n");
    }
    
    // 케이블 연결 상태 확인
    bool cable_connected = network_is_cable_connected();
    w5500_print_network_status();
}

// 네트워크 처리 함수 (메인 루프에서 호출)
void network_process(void) {
    // 케이블 연결 상태 모니터링
    bool cable_connected = network_is_cable_connected();
    static bool last_cable_state = false;
    
    // 케이블 연결 상태 변경 감지
    if (cable_connected != last_cable_state) {
        if (cable_connected) {
            const char *conn_msg = "Ethernet cable connected\r\n";
            printf("%s", conn_msg);
            DBG_WIZNET_PRINT("%s", conn_msg);
            uart_rs232_write(RS232_PORT_1, (const uint8_t*)conn_msg, (uint32_t)strlen(conn_msg));
            tcp_servers_broadcast((const uint8_t*)conn_msg, (uint16_t)strlen(conn_msg));
            // 케이블이 연결되면 DHCP 플래그 리셋하여 IP 배분 재시도 가능하도록 함
            dhcp_configured = false;
        } else {
            const char *disc_msg = "Ethernet cable disconnected\r\n";
            printf("%s", disc_msg);
            DBG_WIZNET_PRINT("%s", disc_msg);
            uart_rs232_write(RS232_PORT_1, (const uint8_t*)disc_msg, (uint32_t)strlen(disc_msg));
            tcp_servers_broadcast((const uint8_t*)disc_msg, (uint16_t)strlen(disc_msg));
            // 케이블이 연결 해제되면 DHCP 플래그 리셋
            dhcp_configured = false;
        }
        last_cable_state = cable_connected;
    }
    
    // 케이블이 연결되어 있고 IP가 배분되지 않은 경우 IP 배분 시도
    static uint32_t dhcp_retry_time = 0;
    uint32_t current_time = to_ms_since_boot(get_absolute_time());
    
    // DHCP가 진행 중이면 상태 체크
    if (dhcp_in_progress) {
        static uint32_t last_debug = 0;
        uint32_t now = to_ms_since_boot(get_absolute_time());
        if (now - last_debug >= 5000) {  // 5초마다 체크 메시지
            DBG_DHCP_PRINT("network_process: calling dhcp_process_check (dhcp_in_progress=true)\n");
            last_debug = now;
        }
        
        if (dhcp_process_check(&g_net_info)) {
            printf("DHCP successful, IP assigned\n");
            dhcp_configured = true;
            w5500_print_network_status();
        }
        return; // DHCP 진행 중이면 다른 처리 건너뛰기
    }
    
    if (cable_connected && !network_is_connected()) {
        if (g_net_info.dhcp == NETINFO_DHCP && !dhcp_configured) {
            // DHCP 재시도 간격 체크 (1초)
            if (dhcp_retry_time == 0 || (current_time - dhcp_retry_time) >= 1000) {
                printf("Attempting DHCP for IP assignment...\n");
                
                // DHCP 시작 (non-blocking)
                w5500_set_dhcp_mode(&g_net_info);
                dhcp_retry_time = current_time;
            }
        } else if (g_net_info.dhcp == NETINFO_STATIC) {
            // 고정 IP 모드: 설정된 고정 IP 적용
            printf("Applying static IP configuration...\n");
            
            if (w5500_set_static_ip(&g_net_info)) {
                printf("Static IP applied successfully\n");
                w5500_print_network_status();
            } else {
                printf("Failed to apply static IP\n");
            }
        }
    }
}
