#include "network_config.h"

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

void network_config_save_to_flash(const wiz_NetInfo* config) {
    uint32_t ints = save_and_disable_interrupts();
    flash_range_erase(NETWORK_CONFIG_FLASH_OFFSET, 4096);
    flash_range_program(NETWORK_CONFIG_FLASH_OFFSET, (const uint8_t*)config, sizeof(wiz_NetInfo));
    restore_interrupts(ints);
    printf("Network configuration saved to flash.\n");
}

void network_config_load_from_flash(wiz_NetInfo* config) {
    uint8_t mac[6];
    const uint8_t* flash_ptr = (const uint8_t*)(XIP_BASE + NETWORK_CONFIG_FLASH_OFFSET);
    memcpy(config, flash_ptr, sizeof(wiz_NetInfo));
    // 유효성 검사: MAC이 모두 0xFF면 기본값으로 초기화
    bool invalid = true;
    for (int i = 0; i < 6; i++) {
        if (config->mac[i] != 0xFF) {
            invalid = false;
            break;
        }
    }
    if (invalid) {
        printf("Flash config invalid, using default config\n");
        memset(config, 0, sizeof(wiz_NetInfo));
        config->mac[0] = 0x00; config->mac[1] = 0x08; config->mac[2] = 0xDC;
        config->sn[0] = 255; config->sn[1] = 255; config->sn[2] = 0; config->sn[3] = 0;
        config->dhcp = NETINFO_DHCP;
    }
    // 출력
    generate_mac_from_board_id(mac);
    memcpy(config->mac, mac, 6);
    
    // DHCP가 static이면 IP 값이 0.0.0.0이 아니도록 확인하고 복사
    if (config->dhcp == NETINFO_STATIC) {
        // IP가 0.0.0.0이면 기본값으로 설정
        if (config->ip[0] == 0 && config->ip[1] == 0 && config->ip[2] == 0 && config->ip[3] == 0) {
            config->ip[0] = 192; config->ip[1] = 168; config->ip[2] = 1; config->ip[3] = 100;
            printf("Static IP was 0.0.0.0, set to default 192.168.1.100\n");
        }
        // 게이트웨이도 확인
        if (config->gw[0] == 0 && config->gw[1] == 0 && config->gw[2] == 0 && config->gw[3] == 0) {
            config->gw[0] = 192; config->gw[1] = 168; config->gw[2] = 1; config->gw[3] = 1;
            printf("Gateway was 0.0.0.0, set to default 192.168.1.1\n");
        }
        // 넷마스크도 확인
        if (config->sn[0] == 0 && config->sn[1] == 0 && config->sn[2] == 0 && config->sn[3] == 0) {
            config->sn[0] = 255; config->sn[1] = 255; config->sn[2] = 255; config->sn[3] = 0;
            printf("Subnet Mask was 0.0.0.0, set to default 255.255.255.0\n");
        }
        // DNS도 확인
        if (config->dns[0] == 0 && config->dns[1] == 0 && config->dns[2] == 0 && config->dns[3] == 0) {
            config->dns[0] = 8; config->dns[1] = 8; config->dns[2] = 8; config->dns[3] = 8;
            printf("DNS was 0.0.0.0, set to default 8.8.8.8\n");
        }
    }
}


// W5500 초기화
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
    wizchip_setnetinfo(net_info);
    
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
                wizchip_setnetinfo(net_info);
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

    printf("IP Address      : %d.%d.%d.%d\n", 
           current_info.ip[0], current_info.ip[1], current_info.ip[2], current_info.ip[3]);
    printf("Subnet Mask     : %d.%d.%d.%d\n", 
           current_info.sn[0], current_info.sn[1], current_info.sn[2], current_info.sn[3]);
    printf("Gateway         : %d.%d.%d.%d\n", 
           current_info.gw[0], current_info.gw[1], current_info.gw[2], current_info.gw[3]);
    printf("DNS Server      : %d.%d.%d.%d\n", 
           current_info.dns[0], current_info.dns[1], current_info.dns[2], current_info.dns[3]);
    printf("MAC Address     : %02X:%02X:%02X:%02X:%02X:%02X\n", 
           current_info.mac[0], current_info.mac[1], current_info.mac[2], 
           current_info.mac[3], current_info.mac[4], current_info.mac[5]);
    printf("DHCP Mode       : %s\n", 
           g_net_info.dhcp == NETINFO_DHCP ? "DHCP" : "Static");
    printf("Link Status     : %s\n", 
           w5500_check_link_status() ? "UP" : "DOWN");
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
    return !(ip[0] == 0 && ip[1] == 0 && ip[2] == 0 && ip[3] == 0);
}

// 네트워크 초기화 함수
void network_init(void) {
    // 저장된 네트워크 설정 로드 및 MAC 주소 설정
    printf("Loading network configuration from flash...\n");
    network_config_load_from_flash(&g_net_info);
    
    // W5500 및 네트워크 초기화
    if (w5500_initialize() == W5500_INIT_SUCCESS) {
        printf("W5500 initialization successful\n");
        
        // W5500에 네트워크 설정 적용
        wizchip_setnetinfo(&g_net_info);
        sleep_ms(10);  // 설정 적용을 위한 지연
        wizchip_setnetinfo(&g_net_info);  // 다시 설정하여 확실히 적용
        
        // 적용 후 값들을 확인하여 출력
        wiz_NetInfo applied_config;
        wizchip_getnetinfo(&applied_config);
        
        // 직접 레지스터에서 값 확인
        uint8_t ip_reg[4], gw_reg[4], sn_reg[4];
        getSIPR(ip_reg);
        getGAR(gw_reg);
        getSUBR(sn_reg);
        
        printf("Applied network config to W5500:\n");
        printf("  DHCP Mode: %s\n", applied_config.dhcp == NETINFO_DHCP ? "DHCP" : "Static");
        printf("  IP Address: %d.%d.%d.%d (reg: %d.%d.%d.%d)\n", applied_config.ip[0], applied_config.ip[1], applied_config.ip[2], applied_config.ip[3], ip_reg[0], ip_reg[1], ip_reg[2], ip_reg[3]);
        printf("  Subnet Mask: %d.%d.%d.%d (reg: %d.%d.%d.%d)\n", applied_config.sn[0], applied_config.sn[1], applied_config.sn[2], applied_config.sn[3], sn_reg[0], sn_reg[1], sn_reg[2], sn_reg[3]);
        printf("  Gateway: %d.%d.%d.%d (reg: %d.%d.%d.%d)\n", applied_config.gw[0], applied_config.gw[1], applied_config.gw[2], applied_config.gw[3], gw_reg[0], gw_reg[1], gw_reg[2], gw_reg[3]);
        printf("  DNS: %d.%d.%d.%d\n", applied_config.dns[0], applied_config.dns[1], applied_config.dns[2], applied_config.dns[3]);
        
        // 값이 제대로 설정되지 않았으면 다시 시도
        if (applied_config.ip[0] == 0 && applied_config.ip[1] == 0 && applied_config.ip[2] == 0 && applied_config.ip[3] == 0) {
            printf("Network config not applied correctly, retrying...\n");
            wizchip_setnetinfo(&g_net_info);
            sleep_ms(10);
            wizchip_getnetinfo(&applied_config);
            printf("Retried network config to W5500:\n");
            printf("  DHCP Mode: %s\n", applied_config.dhcp == NETINFO_DHCP ? "DHCP" : "Static");
            printf("  IP Address: %d.%d.%d.%d\n", applied_config.ip[0], applied_config.ip[1], applied_config.ip[2], applied_config.ip[3]);
            printf("  Subnet Mask: %d.%d.%d.%d\n", applied_config.sn[0], applied_config.sn[1], applied_config.sn[2], applied_config.sn[3]);
            printf("  Gateway: %d.%d.%d.%d\n", applied_config.gw[0], applied_config.gw[1], applied_config.gw[2], applied_config.gw[3]);
            printf("  DNS: %d.%d.%d.%d\n", applied_config.dns[0], applied_config.dns[1], applied_config.dns[2], applied_config.dns[3]);
        }
    } else {
        printf("ERROR: W5500 initialization failed\n");
    }
    
    // 케이블 연결 상태 확인
    bool cable_connected = network_is_cable_connected();

    if (cable_connected) {
        if (g_net_info.dhcp != NETINFO_DHCP) {
            // 플래시 설정이 DHCP가 아닌데 케이블이 연결되어 있으면 DHCP로 변경
            w5500_set_static_ip(&g_net_info);
            dhcp_configured = true;
        } else if (w5500_set_dhcp_mode(&g_net_info)) {
            dhcp_configured = true;  // DHCP 성공 시 플래그 설정
        } else {
            // DHCP 실패 시 설정만 해두고 재시도 대기
            printf("DHCP failed, will retry when cable is connected\n");
        }
    } else {
        printf("Ethernet cable not connected, DHCP will be attempted when cable is connected\n");
        // DHCP 모드로 설정만 해두고 실제 DHCP는 케이블 연결 시 수행
        // wizchip_setnetinfo(&g_net_info);
    }
    // 네트워크 상태 출력
    w5500_print_network_status();
}

// 네트워크 처리 함수 (메인 루프에서 호출)
void network_process(void) {
    // 케이블 연결 상태 모니터링
    bool cable_connected = network_is_cable_connected();
    static bool last_cable_state = false;
    
    if (cable_connected != last_cable_state) {
        if (cable_connected) {
            if (g_net_info.dhcp == NETINFO_DHCP && !dhcp_configured) {
                // DHCP 모드: 기존 설정 정리 후 시간 차를 두고 DHCP 시도
                printf("Ethernet cable connected, cleaning up previous config and attempting DHCP...\n");
                
                // 기존 네트워크 설정 정리
                wiz_NetInfo zero_config = {0};
                memcpy(&zero_config.mac, g_net_info.mac, 6);  // MAC은 유지
                zero_config.dhcp = NETINFO_DHCP;
                wizchip_setnetinfo(&zero_config);
                
                // 정리 후 대기
                sleep_ms(1000);
                
                // DHCP 시도
                if (w5500_set_dhcp_mode(&g_net_info)) {
                    printf("DHCP successful, network connected\n");
                    dhcp_configured = true;
                    w5500_print_network_status();
                } else {
                    printf("DHCP failed, keeping current IP\n");
                }
            } else if (g_net_info.dhcp == NETINFO_STATIC) {
                // Static 모드: IP 그대로 유지
                printf("Ethernet cable reconnected, keeping static IP\n");
                w5500_print_network_status();
            } else {
                printf("Ethernet cable reconnected, network already configured\n");
            }
        } else {
            printf("Ethernet cable disconnected\n");
            // DHCP 재설정 가능하도록 플래그 리셋
            dhcp_configured = false;
        }
        last_cable_state = cable_connected;
    }
}