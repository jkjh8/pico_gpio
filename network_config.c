

#include <stdbool.h>
#include <string.h>
#include <stdio.h>
#include "network_config.h"
#include "main.h"
#include "socket.h"
#include "dhcp.h"

// SPI 콜백 함수들 (효율적으로 구현)
static void wizchip_select(void) { gpio_put(SPI_CS, 0); }
static void wizchip_deselect(void) { gpio_put(SPI_CS, 1); }
static uint8_t wizchip_read(void) {
    uint8_t data = 0;
    spi_read_blocking(SPI_PORT, 0, &data, 1);
    return data;
}
static void wizchip_write(uint8_t wb) {
    spi_write_blocking(SPI_PORT, &wb, 1);
}

// 네트워크 정보 출력 및 전역 변수 업데이트
void network_print_and_update_info(bool is_initial) {
    wizchip_getnetinfo(&g_net_info);
    printf("IP:%d.%d.%d.%d SN:%d.%d.%d.%d GW:%d.%d.%d.%d DNS:%d.%d.%d.%d\n",
        g_net_info.ip[0], g_net_info.ip[1], g_net_info.ip[2], g_net_info.ip[3],
        g_net_info.sn[0], g_net_info.sn[1], g_net_info.sn[2], g_net_info.sn[3],
        g_net_info.gw[0], g_net_info.gw[1], g_net_info.gw[2], g_net_info.gw[3],
        g_net_info.dns[0], g_net_info.dns[1], g_net_info.dns[2], g_net_info.dns[3]);
    if (is_initial)
        printf("Web server: http://%d.%d.%d.%d\n", g_net_info.ip[0], g_net_info.ip[1], g_net_info.ip[2], g_net_info.ip[3]);
}

// W5500 하드웨어 초기화
static bool w5500_hardware_init(void) {
    // SPI 및 핀 설정만 담당 (최초 1회만 호출)
    spi_init(SPI_PORT, 5000 * 1000);
    gpio_set_function(SPI_SCK, GPIO_FUNC_SPI);
    gpio_set_function(SPI_MOSI, GPIO_FUNC_SPI);
    gpio_set_function(SPI_MISO, GPIO_FUNC_SPI);
    gpio_init(SPI_CS);
    gpio_set_dir(SPI_CS, GPIO_OUT);
    gpio_put(SPI_CS, 1);
    gpio_init(SPI_RST);
    gpio_set_dir(SPI_RST, GPIO_OUT);
    return true;
}

// W5500 초기화
w5500_init_result_t w5500_initialize(void) {
    // SPI 및 핀 설정 (최초 1회만)
    if (!w5500_hardware_init()) {
        return W5500_INIT_ERROR_SPI;
    }
    printf("W5500 hardware initialized\n");
    // 하드웨어 리셋 및 WIZchip 재초기화(콜백, 버퍼)는 통합 함수로 처리
    w5500_reset_network();

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
    net_info->dhcp = NETINFO_DHCP;
    setSHAR(net_info->mac);

    // 임시 Link-local IP, 게이트웨이, 서브넷 설정
    uint8_t temp_ip[4] = {169, 254, net_info->mac[4], net_info->mac[5]};
    uint8_t temp_gw[4] = {0, 0, 0, 0};
    uint8_t temp_sn[4] = {255, 255, 0, 0};
    setSIPR(temp_ip);
    setGAR(temp_gw);
    setSUBR(temp_sn);
    sleep_ms(100);

    // 모든 소켓 닫기
    for (int i = 0; i < 8; i++) close(i);

    // DHCP 소켓 오픈
    if (socket(0, Sn_MR_UDP, 68, 0) != 0) return false;

    // 링크 확인
    if (!(getPHYCFGR() & PHYCFGR_LNK_ON)) {
        close(0);
        return false;
    }

    DHCP_init(0, g_ethernet_buf);

    for (uint32_t t = 0; t < 60; t++) {
        if (!(getPHYCFGR() & PHYCFGR_LNK_ON)) {
            close(0);
            return false;
        }
        uint8_t dhcp_status = DHCP_run();
        if (dhcp_status == DHCP_IP_LEASED) {
            getIPfromDHCP(net_info->ip);
            getGWfromDHCP(net_info->gw);
            getSNfromDHCP(net_info->sn);
            getDNSfromDHCP(net_info->dns);
            wizchip_setnetinfo(net_info);
            close(0);
            return true;
        } else if (dhcp_status == DHCP_FAILED) {
            close(0);
            sleep_ms(500);
            if (socket(0, Sn_MR_UDP, 68, 0) != 0) return false;
            DHCP_init(0, g_ethernet_buf);
        }
        sleep_ms(1000);
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
    if (!network_is_cable_connected()) return false;

    uint8_t ip[4];
    getSIPR(ip);

    // IP가 0.0.0.0이 아니면 연결된 것으로 간주
    return *(uint32_t*)ip != 0;
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
    if (!w5500_apply_network_config(&g_net_info, NETWORK_MODE_DHCP)) {
        return false;
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
    static uint32_t last_check = 0;
    uint32_t now = to_ms_since_boot(get_absolute_time());

    if (now - last_check < 1000) return;
    last_check = now;

    bool cable_connected = network_is_cable_connected();
    bool net_connected = network_is_connected();

    // 케이블 연결 변화 감지
    if (cable_connected != cable_was_connected) {
        cable_was_connected = cable_connected;
        reconnect_attempts = 0;
        network_state = cable_connected ? NETWORK_STATE_CONNECTING : NETWORK_STATE_DISCONNECTED;
    }

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
            } else if (net_connected) {
                network_state = NETWORK_STATE_CONNECTED;
                reconnect_attempts = 0;
            } else if (++reconnect_attempts > 10) {
                network_state = NETWORK_STATE_RECONNECTING;
                reconnect_attempts = 0;
            }
            break;
        case NETWORK_STATE_CONNECTED:
            if (!cable_connected) {
                network_state = NETWORK_STATE_DISCONNECTED;
            } else if (!net_connected) {
                network_state = NETWORK_STATE_RECONNECTING;
                reconnect_attempts = 0;
            }
            break;
        case NETWORK_STATE_RECONNECTING:
            if (!cable_connected) {
                network_state = NETWORK_STATE_DISCONNECTED;
            } else if (network_reinitialize()) {
                network_state = NETWORK_STATE_CONNECTING;
                reconnect_attempts = 0;
            } else if (++reconnect_attempts > 5) {
                network_state = NETWORK_STATE_DISCONNECTED;
                sleep_ms(5000);
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

// W5500 네트워크 리셋 및 재초기화 (하드웨어 리셋 포함)
void w5500_reset_network(void) {
    // 모든 소켓 닫기
    // printf("Resetting W5500 network...\n");
    // for (int i = 0; i < 8; i++) {
    //     uint8_t status = getSn_SR(i);
    //     if (status != 0x00) { // 0x00: CLOSED
    //         int ret = close(i);
    //         printf("close(%d) ret=%d, status(before)=0x%02X\n", i, ret, status);
    //     } else {
    //         printf("socket(%d) already closed (status=0x%02X)\n", i, status);
    //     }
    // }
    // printf("All sockets checked and close() called if needed\n");
    // 하드웨어 리셋 시퀀스
    gpio_put(SPI_RST, 0);
    sleep_ms(100);
    gpio_put(SPI_RST, 1);
    sleep_ms(100);
    printf("W5500 hardware reset complete\n");
    // WIZchip 콜백 및 버퍼 재초기화
    reg_wizchip_cs_cbfunc(wizchip_select, wizchip_deselect);
    reg_wizchip_spi_cbfunc(wizchip_read, wizchip_write);
    uint8_t tx_sizes[8] = {2, 8, 2, 2, 2, 0, 0, 0};
    uint8_t rx_sizes[8] = {2, 8, 2, 2, 2, 0, 0, 0};
    wizchip_init(tx_sizes, rx_sizes);
    printf("W5500 network reset and reinitialized\n");
}