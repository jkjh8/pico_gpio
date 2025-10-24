
#include "main.h"
#include "handlers/command_handler.h"
#include <stdio.h>
#include "pico/stdio.h"
#include <string.h>

// 시스템 재시작 요청 플래그
static volatile bool restart_requested = false;
// TCP 서버 초기화 플래그
static bool tcp_servers_initialized = false;
// 시스템 재시작 요청 함수
void system_restart_request(void) {
    restart_requested = true;
}

bool is_system_restart_requested(void) {
    return restart_requested;
}

void system_restart(void) {
    DBG_MAIN_PRINT("System restart requested...\n");
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
    // initialize debug runtime flags from compile-time defaults and then load saved runtime settings
    debug_init();
    DBG_MAIN_PRINT("Starting Pico GPIO Server...\n");
    DBG_MAIN_PRINT("Board: %s\n", PICO_BOARD);
    
    // 네트워크 초기화
    network_init();
    
    // DHCP 성공 여부와 관계없이 HTTP 서버 시작 (네트워크 복구 시 자동 재시작됨)
    if (http_server_init(80)) {
        DBG_MAIN_PRINT("HTTP server started on port 80\n");
    } else {
        DBG_MAIN_PRINT("ERROR: HTTP server failed to start\n");
    }
    // TCP 포트 로드
    load_tcp_port_from_flash();
    DBG_MAIN_PRINT("TCP port loaded: %u\n", tcp_port);
    // UART RS232 설정 로드
    load_uart_rs232_baud_from_flash();
    DBG_MAIN_PRINT("UART RS232 baud rate loaded: Port 1 - %u\n", uart_rs232_1_baud);
    // 네트워크 상태 모니터링 및 자동 복구 설정

    // UART RS232 초기화
    uart_rs232_init(RS232_PORT_1, uart_rs232_1_baud);
    DBG_MAIN_PRINT("UART RS232 initialized: Port 1 at %u baud\n", uart_rs232_1_baud);
    // GPIO 초기화
    gpio_spi_init();
    load_gpio_config_from_flash();
    DBG_MAIN_PRINT("GPIO SPI initialized, Device ID: 0x%02X, Mode: %s\n", 
        get_gpio_device_id(),
        get_gpio_comm_mode() == GPIO_MODE_JSON ? "JSON" : "TEXT");

    while (true) {
        // 시스템 재시작 요청 확인 및 처리
        if (is_system_restart_requested()) {
            system_restart();
        }
        
        // 네트워크 처리
        network_process();
        
        // 네트워크 연결 확인 및 TCP 서버 초기화
        if (!tcp_servers_initialized && network_is_connected()) {
            tcp_servers_init(tcp_port);
            DBG_MAIN_PRINT("TCP servers initialized on port %u\n", tcp_port);
            tcp_servers_initialized = true;
        }
        
        // HTTP 서버 처리
        http_server_process();
        tcp_servers_process();
        // HCT165 읽기
        hct165_read();
        // UART RS232 데이터 처리
        uart_rs232_process();

        // USB CDC (stdio) 입력 처리: non-blocking으로 한 줄씩 읽어 command handler로 전달
        {
            static char usb_line[512];
            static size_t usb_pos = 0;
            int ch = getchar_timeout_us(0);
            while (ch != PICO_ERROR_TIMEOUT) {
                if (ch == '\r' || ch == '\n') {
                    if (usb_pos > 0) {
                        usb_line[usb_pos] = '\0';
                        /* Make response buffer larger to accommodate long help text */
                        char response[2048] = {0};
                        cmd_result_t res = process_command(usb_line, response, sizeof(response));
                        // print response back to the terminal (chunked to avoid truncation)
                        if (response[0] != '\0') {
                            size_t rlen = strlen(response);
                            const char *p = response;
                            const size_t CHUNK_SIZE = 256;
                            size_t sent = 0;
                            while (sent < rlen) {
                                size_t to_send = rlen - sent;
                                if (to_send > CHUNK_SIZE) to_send = CHUNK_SIZE;
                                // print chunk (respecting not-null-terminated slice)
                                printf("%.*s", (int)to_send, p + sent);
                                fflush(stdout);
                                sent += to_send;
                                // small yield for host to process USB CDC traffic
                                sleep_ms(1);
                            }
                            // ensure trailing newline
                            if (rlen == 0 || response[rlen - 1] != '\n') {
                                printf("\n");
                                fflush(stdout);
                            }
                        } else {
                            // no response (e.g., addressed to other device)
                        }
                        usb_pos = 0;
                    }
                } else if (usb_pos + 1 < sizeof(usb_line)) {
                    usb_line[usb_pos++] = (char)ch;
                }
                ch = getchar_timeout_us(0);
            }
        }

    }
}

