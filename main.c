
#include "main.h"
#include "protocol_handlers/command_handler.h"
#include "ioLibrary_Driver/Internet/httpServer/httpServer.h"

// 전역 변수는 network_config.c로 이동했습니다 (g_ethernet_buf, g_net_info)

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
    
    // Boot-time network initialization and apply saved config
    bool net_ok = network_boot_setup();
    if (net_ok) {
        dhcp_configured = (g_net_info.dhcp == NETINFO_DHCP);
    }
    
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
        
        // 네트워크 모듈에 주기적 처리를 위임 (케이블 연결/해제 감지 및 처리)
        network_event_t evt = network_periodic();
        if (evt == NETWORK_EVENT_DISCONNECTED) {
            tcp_servers_initialized = false;
            dhcp_configured = false;
        } else if (evt == NETWORK_EVENT_CONNECTED) {
            // On connect, if network_is_connected becomes true, tcp servers will be initialized below
        }
        
        // 네트워크 연결 확인 및 TCP 서버 초기화
        if (!tcp_servers_initialized && network_is_connected()) {
            tcp_servers_init(tcp_port);
            printf("TCP servers initialized on port %u\n", tcp_port);
            tcp_servers_initialized = true;
        }
        
        // HTTP 서버 처리
        http_server_process();
        // DHCP now handled synchronously on connect/reconnect; nothing to poll here
        tcp_servers_process();
        // HCT165 읽기
        hct165_read();
        // UART RS232 데이터 처리
        uart_rs232_process();

    }
}

