
#include "main.h"
#include "command_handler.h"
#include "ioLibrary_Driver/Internet/httpServer/httpServer.h"

// 전역 변수 정의
uint8_t g_ethernet_buf[2048];

// 네트워크 정보 전역 변수
wiz_NetInfo g_net_info = {
    .mac = { 0x00, 0x08, 0xDC, 0x00, 0x00, 0x00 },
    .ip = { 192, 168, 1, 100 },
    .sn = { 255, 255, 255, 0 },
    .gw = { 0, 0, 0, 0 },
    .dns = { 0, 0, 0, 0 },
    .dhcp = NETINFO_STATIC
};

// 시스템 재시작 요청 플래그
static volatile bool restart_requested = false;
// TCP 서버 초기화 플래그
static bool tcp_servers_initialized = false;
// DHCP 설정 완료 플래그
static bool dhcp_configured = false;
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
    
    // DHCP 성공 여부와 관계없이 HTTP 서버 시작 (네트워크 복구 시 자동 재시작됨)
    if (http_server_init(80)) {
        printf("HTTP server started on port 80\n");
    } else {
        printf("ERROR: HTTP server failed to start\n");
    }

    // 정적 파일 등록은 http_server_init에서 수행됨
    // TCP 포트 로드
    load_tcp_port_from_flash();
    printf("TCP port loaded: %u\n", tcp_port);
    // TCP 서버 초기화는 네트워크 연결 후 메인 루프에서 수행
    // tcp_servers_init(tcp_port);
    // printf("TCP servers initialized on port %u\n", tcp_port);

    // UART RS232 설정 로드
    load_uart_rs232_baud_from_flash();
    printf("UART RS232 baud rate loaded: Port 1 - %u\n", uart_rs232_1_baud);
    // 네트워크 상태 모니터링 및 자동 복구 설정

    // UART RS232 초기화
    uart_rs232_init(RS232_PORT_1, uart_rs232_1_baud);
    printf("UART RS232 initialized: Port 1 at %u baud\n", uart_rs232_1_baud);
    // GPIO 초기화
    gpio_spi_init();
    load_gpio_config_from_flash();
    printf("GPIO SPI initialized, Device ID: 0x%02X, Mode: %s\n", 
           get_gpio_device_id(),
           get_gpio_comm_mode() == GPIO_MODE_JSON ? "JSON" : "TEXT");

    while (true) {
        // 시스템 재시작 요청 확인 및 처리
        if (is_system_restart_requested()) {
            system_restart();
        }
        
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
                // TCP 서버 초기화 플래그 리셋
                tcp_servers_initialized = false;
                // DHCP 재설정 가능하도록 플래그 리셋
                dhcp_configured = false;
            }
            last_cable_state = cable_connected;
        }
        
        // 네트워크 연결 확인 및 TCP 서버 초기화
        if (!tcp_servers_initialized && network_is_connected()) {
            tcp_servers_init(tcp_port);
            printf("TCP servers initialized on port %u\n", tcp_port);
            tcp_servers_initialized = true;
        }
        
        // HTTP 서버 처리
        http_server_process();
        tcp_servers_process();
        // HCT165 읽기
        hct165_read();
        // UART RS232 데이터 처리
        uart_rs232_process();

    }
}

