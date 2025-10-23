#include "network_config.h"

// NOTE: unused monitor variables removed to reduce global state surface.

// Ethernet buffer and global network info previously defined in main.c
uint8_t g_ethernet_buf[2048];

// 네트워크 정보 전역 변수 (초기값 유지)
wiz_NetInfo g_net_info = {
    .mac = { 0x00, 0x08, 0xDC, 0x00, 0x00, 0x00 },
    .ip = { 192, 168, 1, 100 },
    .sn = { 255, 255, 255, 0 },
    .gw = { 0, 0, 0, 0 },
    .dns = { 0, 0, 0, 0 },
    .dhcp = NETINFO_STATIC
};

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
    const uint32_t timeout_ms = 30000; // 30 seconds

    if (!net_info) return false;
    net_info->dhcp = NETINFO_DHCP;

    // If there's no link, fail fast
    if (!w5500_check_link_status()) {
        printf("[DHCP] Link is down, aborting DHCP attempt\n");
        return false;
    }

    network_dhcp_start();
    uint32_t start = to_ms_since_boot(get_absolute_time());

    while ((to_ms_since_boot(get_absolute_time()) - start) < timeout_ms) {
        // If link drops while waiting, stop and fail
        if (!w5500_check_link_status()) {
            printf("[DHCP] Link dropped during DHCP attempt\n");
            network_dhcp_stop();
            return false;
        }

        int res = network_dhcp_process();
        if (res == 1) {
            memcpy(net_info, &g_net_info, sizeof(wiz_NetInfo));
            printf("[DHCP] Lease obtained\n");
            return true;
        } else if (res == -1) {
            // failure from the state machine
            network_dhcp_stop();
            printf("[DHCP] DHCP process reported failure\n");
            return false;
        }
        sleep_ms(200);
    }

    // timeout
    network_dhcp_stop();
    printf("[DHCP] Timeout after %u ms\n", (unsigned)timeout_ms);
    return false;
}

// 네트워크 설정 적용
bool w5500_apply_network_config(wiz_NetInfo *net_info, network_mode_t mode) {
    if (mode == NETWORK_MODE_STATIC) {
        return w5500_set_static_ip(net_info);
    } else {
        return w5500_set_dhcp_mode(net_info);
    }
}

// Boot-time network setup: initialize W5500 and apply stored config (static or DHCP).
// Returns true if network configured (IP assigned or static applied), false otherwise.
bool network_boot_setup(void) {
    if (w5500_initialize() != W5500_INIT_SUCCESS) {
        printf("ERROR: W5500 initialization failed\n");
        return false;
    }

    // Apply saved network config to W5500
    wizchip_setnetinfo(&g_net_info);
    sleep_ms(10);
    // repeat once to ensure registers take the values (historical workaround)
    wizchip_setnetinfo(&g_net_info);

    // Show applied config
    wiz_NetInfo applied_config;
    wizchip_getnetinfo(&applied_config);
    uint8_t ip_reg[4], gw_reg[4], sn_reg[4];
    getSIPR(ip_reg);
    getGAR(gw_reg);
    getSUBR(sn_reg);

    printf("Applied network config to W5500:\n");
    printf("  DHCP Mode: %s\n", applied_config.dhcp == NETINFO_DHCP ? "DHCP" : "Static");
    printf("  IP Address: %d.%d.%d.%d (reg: %d.%d.%d.%d)\n", applied_config.ip[0], applied_config.ip[1], applied_config.ip[2], applied_config.ip[3], ip_reg[0], ip_reg[1], ip_reg[2], ip_reg[3]);

    bool configured = false;
    // Apply static IP at boot if configured; do not perform DHCP here.
    if (g_net_info.dhcp == NETINFO_STATIC) {
        w5500_set_static_ip(&g_net_info);
        configured = true;
    }

    w5500_print_network_status();
    return configured;
}

// Synchronous DHCP attempt wrapper. Returns true on lease obtained.
bool network_try_dhcp_sync(void) {
    return w5500_set_dhcp_mode(&g_net_info);
}

// Handle cable connect/disconnect events. Returns true if configured after event.
bool network_on_cable_change(bool connected) {
    static bool tcp_servers_initialized_local = false; // internal guard if needed

    if (connected) {
        if (g_net_info.dhcp == NETINFO_DHCP) {
            printf("Ethernet cable connected, attempting DHCP...\n");
            if (network_try_dhcp_sync()) {
                printf("DHCP successful, network connected\n");
                w5500_print_network_status();
                return true;
            } else {
                printf("DHCP failed, keeping current IP\n");
                return false;
            }
        } else if (g_net_info.dhcp == NETINFO_STATIC) {
            printf("Ethernet cable reconnected, applying static IP\n");
            w5500_set_static_ip(&g_net_info);
            w5500_print_network_status();
            return true;
        }
        return false;
    } else {
        printf("Ethernet cable disconnected\n");
        network_dhcp_stop();
        // caller should use network_is_connected() to manage tcp servers
        return false;
    }
}

// Periodic handler called from main loop. Detects cable state changes and
// invokes network_on_cable_change(). Returns an event code for caller
// convenience.
network_event_t network_periodic(void) {
    static bool last_cable_state = false;
    bool cable_connected = network_is_cable_connected();
    if (cable_connected != last_cable_state) {
        network_on_cable_change(cable_connected);
        last_cable_state = cable_connected;
        return cable_connected ? NETWORK_EVENT_CONNECTED : NETWORK_EVENT_DISCONNECTED;
    }
    return NETWORK_EVENT_NONE;
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

// Serialize current network info into JSON into the provided buffer.
// Returns number of bytes written (excluding terminating null). Does not write more than buf_len.
size_t network_get_info_json(char *buf, size_t buf_len) {
    if (!buf || buf_len == 0) return 0;

    wiz_NetInfo current_info;
    wizchip_getnetinfo(&current_info);

    // MAC string
    char mac_str[18] = {0};
    sprintf(mac_str, "%02X:%02X:%02X:%02X:%02X:%02X",
            current_info.mac[0], current_info.mac[1], current_info.mac[2],
            current_info.mac[3], current_info.mac[4], current_info.mac[5]);

    const char *dhcp_str = (g_net_info.dhcp == NETINFO_DHCP) ? "DHCP" : "Static";
    bool link = w5500_check_link_status();

    int written = snprintf(buf, buf_len,
        "{\"ip\":\"%d.%d.%d.%d\",\"sn\":\"%d.%d.%d.%d\",\"gw\":\"%d.%d.%d.%d\",\"dns\":\"%d.%d.%d.%d\",\"mac\":\"%s\",\"dhcp\":\"%s\",\"link\":%s}",
        current_info.ip[0], current_info.ip[1], current_info.ip[2], current_info.ip[3],
        current_info.sn[0], current_info.sn[1], current_info.sn[2], current_info.sn[3],
        current_info.gw[0], current_info.gw[1], current_info.gw[2], current_info.gw[3],
        current_info.dns[0], current_info.dns[1], current_info.dns[2], current_info.dns[3],
        mac_str, dhcp_str, link ? "true" : "false");

    if (written < 0) return 0;
    if ((size_t)written >= buf_len) {
        // truncated
        buf[buf_len-1] = '\0';
        return buf_len-1;
    }
    return (size_t)written;
}

// DHCP implementation moved to network_dhcp.c
