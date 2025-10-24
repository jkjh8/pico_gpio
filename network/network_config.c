#include "network_config.h"

void network_config_save_to_flash(const wiz_NetInfo* config) {
    memcpy(&g_net_info, config, sizeof(wiz_NetInfo));
    config_storage_save();
    DBG_NET_PRINT("Network configuration saved to flash.\n");
}

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
    wiz_NetInfo temp_config;
    memcpy(&temp_config, config, sizeof(wiz_NetInfo));
    wizchip_setnetinfo(&temp_config);
    sleep_ms(10);  // Allow configuration to take effect
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
}

void wizchip_deselect(void) {
    gpio_put(SPI_CS, 1);
}

uint8_t wizchip_read(void) {
    uint8_t data;
    spi_read_blocking(SPI_PORT, 0, &data, 1);
    return data;
}

void wizchip_write(uint8_t wb) {
    spi_write_blocking(SPI_PORT, &wb, 1);
}


w5500_init_result_t w5500_initialize(void) {
    // 하드웨어 초기화 (직접 인라인)
    spi_init(SPI_PORT, 5000 * 1000);
    gpio_set_function(SPI_SCK, GPIO_FUNC_SPI);
    gpio_set_function(SPI_MOSI, GPIO_FUNC_SPI);
    gpio_set_function(SPI_MISO, GPIO_FUNC_SPI);
    gpio_init(SPI_CS);
    gpio_set_dir(SPI_CS, GPIO_OUT);
    gpio_put(SPI_CS, 1);
    gpio_init(SPI_RST);
    gpio_set_dir(SPI_RST, GPIO_OUT);
    gpio_put(SPI_RST, 0);
    sleep_ms(100);
    gpio_put(SPI_RST, 1);
    sleep_ms(100);
    // WIZchip 콜백 함수 등록
    reg_wizchip_cs_cbfunc(wizchip_select, wizchip_deselect);
    reg_wizchip_spi_cbfunc(wizchip_read, wizchip_write);
    // W5500 소켓 버퍼 초기화 및 설정 (최적화 - HTTP 서버에 더 많은 버퍼 할당)
    uint8_t tx_sizes[8] = {2, 2, 2, 2, 2, 2, 2, 2};
    uint8_t rx_sizes[8] = {2, 2, 2, 2, 2, 2, 2, 2};
    if (wizchip_init(tx_sizes, rx_sizes) == -1) {
        return W5500_INIT_ERROR_CHIP;
    }
    // 버전 확인
    uint8_t version = getVERSIONR();
    if (version != 0x04) {
        return W5500_INIT_ERROR_CHIP;
    }
    // 링크 상태 확인 (최대 10초 대기)
    for (int i = 0; i < 20; i++) {
        if (getPHYCFGR() & PHYCFGR_LNK_ON) {
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

// DHCP 설정 (W5500용 수정된 코드)
bool w5500_set_dhcp_mode(wiz_NetInfo *net_info) {
    // DHCP 모드로 변경
    net_info->dhcp = NETINFO_DHCP;
    sleep_ms(200);
    for(int i = 0; i < 8; i++) close(i);
    sleep_ms(100);
    uint8_t sock_ret = socket(0, Sn_MR_UDP, 68, 0);
    if(sock_ret != 0) return false;
    if(sock_ret != 0) return false;
    uint8_t phy_status = getPHYCFGR();
    if(!(phy_status & 0x01)) { close(0); return false; }
    DHCP_init(0, g_ethernet_buf);
    uint32_t dhcp_timeout = 0;
    while (dhcp_timeout < 60) {
        uint8_t dhcp_status = DHCP_run();
        uint8_t phy_status = getPHYCFGR();
        if(!(phy_status & 0x01)) { close(0); return false; }
        switch(dhcp_status) {
            case DHCP_IP_LEASED:
                // DHCP에서 받은 정보를 net_info와 g_net_info에 모두 복사
                getIPfromDHCP(net_info->ip);
                getGWfromDHCP(net_info->gw);
                getSNfromDHCP(net_info->sn);
                getDNSfromDHCP(net_info->dns);
                memcpy(&g_net_info, net_info, sizeof(wiz_NetInfo));
                apply_network_config(net_info);
                close(0);
                return true;
            case DHCP_FAILED:
                close(0);
                sleep_ms(1000);
                if(socket(0, Sn_MR_UDP, 68, 0) != 0) return false;
                DHCP_init(0, g_ethernet_buf);
                // dhcp_timeout을 리셋하지 말고 계속 증가
                break;
            default:
                break;
        }
        sleep_ms(1000);
        dhcp_timeout++;
    }
    close(0);
    return false;
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
    uint8_t phy_status = getPHYCFGR();
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
    memcpy(&g_net_info, &g_net_info, sizeof(wiz_NetInfo));
    // 보드 고유 ID로 MAC 생성 및 설정
    uint8_t mac[6];
    generate_mac_from_board_id(mac);
    memcpy(&g_net_info.mac, mac, 6);
    DBG_NET_PRINT("Network configuration loaded from flash\n");
    
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
            printf("Ethernet cable connected\n");
            // 케이블이 연결되면 DHCP 플래그 리셋하여 IP 배분 재시도 가능하도록 함
            dhcp_configured = false;
        } else {
            printf("Ethernet cable disconnected\n");
            // 케이블이 연결 해제되면 DHCP 플래그 리셋
            dhcp_configured = false;
        }
        last_cable_state = cable_connected;
    }
    
    // 케이블이 연결되어 있고 IP가 배분되지 않은 경우 IP 배분 시도
    if (cable_connected && !network_is_connected()) {
        if (g_net_info.dhcp == NETINFO_DHCP && !dhcp_configured) {
            // DHCP 모드: DHCP를 통한 IP 배분 시도
            printf("Attempting DHCP for IP assignment...\n");
            
            // DHCP 시도
            if (w5500_set_dhcp_mode(&g_net_info)) {
                printf("DHCP successful, IP assigned\n");
                dhcp_configured = true;
                w5500_print_network_status();
            } else {
                printf("DHCP failed, will retry...\n");
                // DHCP 실패시 잠시 대기 후 재시도 (다음 루프에서)
                sleep_ms(1000);
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
