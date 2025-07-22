#include "hardware/flash.h"
#include "hardware/sync.h"
#include "pico/stdlib.h"

#include <stdio.h>
#include <string.h>
#include "network_config.h"
#include "socket.h"
#include "dhcp.h"

extern wiz_NetInfo g_net_info;
#define NETWORK_CONFIG_FLASH_OFFSET (PICO_FLASH_SIZE_BYTES - 4096)

void network_config_save_to_flash(const wiz_NetInfo* config) {
    uint32_t ints = save_and_disable_interrupts();
    flash_range_erase(NETWORK_CONFIG_FLASH_OFFSET, 4096);
    flash_range_program(NETWORK_CONFIG_FLASH_OFFSET, (const uint8_t*)config, sizeof(wiz_NetInfo));
    restore_interrupts(ints);
    printf("Network configuration saved to flash.\n");
}

void network_config_load_from_flash(wiz_NetInfo* config) {
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
    printf("Loaded network config from flash:\n");
    printf("MAC: %02X:%02X:%02X:%02X:%02X:%02X\n", 
           config->mac[0], config->mac[1], config->mac[2], 
           config->mac[3], config->mac[4], config->mac[5]);
    printf("IP: %d.%d.%d.%d\n", 
           config->ip[0], config->ip[1], config->ip[2], config->ip[3]);
    printf("Subnet: %d.%d.%d.%d\n", 
           config->sn[0], config->sn[1], config->sn[2], config->sn[3]);
    printf("Gateway: %d.%d.%d.%d\n", 
           config->gw[0], config->gw[1], config->gw[2], config->gw[3]);
    printf("DNS: %d.%d.%d.%d\n", 
           config->dns[0], config->dns[1], config->dns[2], config->dns[3]);
    printf("DHCP: %s\n", config->dhcp == NETINFO_DHCP ? "Enabled" : "Disabled");
    
}

// SPI 콜백 함수들
static void wizchip_select(void) {
    gpio_put(SPI_CS, 0);
}

static void wizchip_deselect(void) {
    gpio_put(SPI_CS, 1);
}

static uint8_t wizchip_read(void) {
    uint8_t data;
    spi_read_blocking(SPI_PORT, 0, &data, 1);
    return data;
}

static void wizchip_write(uint8_t wb) {
    spi_write_blocking(SPI_PORT, &wb, 1);
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
    uint8_t tx_sizes[8] = {2, 8, 2, 2, 2, 0, 0, 0};
    uint8_t rx_sizes[8] = {2, 8, 2, 2, 2, 0, 0, 0};
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
    // setSHAR(net_info->mac);
    // uint8_t temp_ip[4] = {169, 254, net_info->mac[4], net_info->mac[5]};
    // uint8_t temp_gw[4] = {0, 0, 0, 0};
    // uint8_t temp_sn[4] = {255, 255, 0, 0};
    // setSIPR(temp_ip);
    // setGAR(temp_gw);
    // setSUBR(temp_sn);
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
                dhcp_timeout -= 5;
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

// 네트워크 설정 적용
bool w5500_apply_network_config(wiz_NetInfo *net_info, network_mode_t mode) {
    if (mode == NETWORK_MODE_STATIC) {
        return w5500_set_static_ip(net_info);
    } else {
        return w5500_set_dhcp_mode(net_info);
    }
}

// 네트워크 상태 출력
void w5500_print_network_status(void) {
    wiz_NetInfo current_info;
    wizchip_getnetinfo(&current_info);
    
    printf("IP: %d.%d.%d.%d, Link: %s\n", 
           current_info.ip[0], current_info.ip[1], current_info.ip[2], current_info.ip[3],
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

// 네트워크 재초기화 함수
bool network_reinitialize(void) {
    // W5500 소프트 리셋
    setMR(MR_RST);
    sleep_ms(100);
    
    // 다시 초기화
    w5500_init_result_t result = w5500_initialize();
    if (result != W5500_INIT_SUCCESS) {
        return false;
    }
    
    // 네트워크 설정 재적용 (전역 변수 g_net_info와 DHCP 모드 사용)
    if (g_net_info.dhcp == NETINFO_STATIC) {
        if (!w5500_set_static_ip(&g_net_info)) {
            return false;
        }
    } else {
        if (!w5500_set_dhcp_mode(&g_net_info)) {
            return false;
        }
    }
    
    return true;
}

// 네트워크 상태 모니터링 및 자동 복구
typedef enum {
    NETWORK_STATE_DISCONNECTED,
    NETWORK_STATE_CONNECTING,
    NETWORK_STATE_CONNECTED,
    NETWORK_STATE_RECONNECTING
} network_monitor_state_t;

static network_monitor_state_t network_state = NETWORK_STATE_DISCONNECTED;
static uint32_t last_connection_check = 0;
static uint32_t reconnect_attempts = 0;
static bool cable_was_connected = false;

void network_monitor_process(void) {
    uint32_t current_time = to_ms_since_boot(get_absolute_time());
    bool cable_connected = network_is_cable_connected();
    bool network_connected = network_is_connected();
    
    // 케이블 연결 상태 변화 감지
    if (cable_connected != cable_was_connected) {
        if (cable_connected) {
            network_state = NETWORK_STATE_CONNECTING;
            reconnect_attempts = 0;
        } else {
            network_state = NETWORK_STATE_DISCONNECTED;
        }
        cable_was_connected = cable_connected;
    }
    
    // 주기적 상태 확인 (1초마다)
    if (current_time - last_connection_check < 1000) {
        return;
    }
    last_connection_check = current_time;
    
    switch (network_state) {
        case NETWORK_STATE_DISCONNECTED:
            if (cable_connected) {
                network_state = NETWORK_STATE_CONNECTING;
                reconnect_attempts = 0;
            }
            break;
            
        case NETWORK_STATE_CONNECTING:
            if (!cable_connected) {
                network_state = NETWORK_STATE_DISCONNECTED;
            } else if (network_connected) {
                network_state = NETWORK_STATE_CONNECTED;
                reconnect_attempts = 0;
            } else {
                reconnect_attempts++;
                if (reconnect_attempts > 10) {  // 10초 후 재초기화 시도
                    network_state = NETWORK_STATE_RECONNECTING;
                    reconnect_attempts = 0;
                }
            }
            break;
            
        case NETWORK_STATE_CONNECTED:
            if (!cable_connected) {
                network_state = NETWORK_STATE_DISCONNECTED;
            } else if (!network_connected) {
                network_state = NETWORK_STATE_RECONNECTING;
                reconnect_attempts = 0;
            }
            break;
            
        case NETWORK_STATE_RECONNECTING:
            if (!cable_connected) {
                network_state = NETWORK_STATE_DISCONNECTED;
            } else {
                if (network_reinitialize()) {
                    network_state = NETWORK_STATE_CONNECTING;
                } else {
                    reconnect_attempts++;
                    if (reconnect_attempts > 5) {  // 5번 시도 후 대기
                        network_state = NETWORK_STATE_DISCONNECTED;
                        sleep_ms(5000);  // 5초 대기
                    }
                }
            }
            break;
    }
}

// 현재 네트워크 상태 반환
const char* network_get_state_string(void) {
    switch (network_state) {
        case NETWORK_STATE_DISCONNECTED: return "연결 해제";
        case NETWORK_STATE_CONNECTING: return "연결 중";
        case NETWORK_STATE_CONNECTED: return "연결됨";
        case NETWORK_STATE_RECONNECTING: return "재연결 중";
        default: return "알 수 없음";
    }
}

// W5500 네트워크 리셋 함수 구현
void w5500_reset_network(void) {
    // 모든 소켓 닫기
    for (int i = 0; i < 8; i++) {
        close(i);
    }
    // 하드웨어 리셋
    gpio_put(SPI_RST, 0);
    sleep_ms(100);
    gpio_put(SPI_RST, 1);
    sleep_ms(100);
    // WIZchip 재초기화
    reg_wizchip_cs_cbfunc(wizchip_select, wizchip_deselect);
    reg_wizchip_spi_cbfunc(wizchip_read, wizchip_write);
    uint8_t tx_sizes[8] = {2, 8, 2, 2, 2, 0, 0, 0};
    uint8_t rx_sizes[8] = {2, 8, 2, 2, 2, 0, 0, 0};
    wizchip_init(tx_sizes, rx_sizes);
}