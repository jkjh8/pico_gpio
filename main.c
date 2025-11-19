#include "main.h"
#include "handlers/command_handler.h"
#include "system/system_config.h"
#include "led/status_led.h"
#include <stdio.h>
#include "pico/stdio.h"
#include <string.h>

// =============================================================================
// 시스템 재시작 관리
// =============================================================================

static volatile bool restart_requested = false;
static bool tcp_servers_initialized = false;

void system_restart_request(void) {
    restart_requested = true;
}

bool is_system_restart_requested(void) {
    return restart_requested;
}

void system_restart(void) {
    DBG_MAIN_PRINT("System restart requested...\n");
    sleep_ms(500);
    fflush(stdout);
    sleep_ms(500);
    watchdog_reboot(0, 0, 0);
    while (1) { __wfi(); }
}

// =============================================================================
// USB CDC 명령 처리
// =============================================================================

static void process_usb_cdc_commands(void)
{
    static char usb_line[512];
    static size_t usb_pos = 0;
    int ch = getchar_timeout_us(0);
    
    while (ch != PICO_ERROR_TIMEOUT) {
        if (ch == '\r' || ch == '\n') {
            if (usb_pos > 0) {
                usb_line[usb_pos] = '\0';
                
                // 명령 처리
                char response[2048] = {0};
                cmd_result_t res = process_command(usb_line, response, sizeof(response));
                
                // 응답 출력 (청크 단위)
                if (response[0] != '\0') {
                    size_t rlen = strlen(response);
                    const char *p = response;
                    const size_t CHUNK_SIZE = 256;
                    size_t sent = 0;
                    
                    while (sent < rlen) {
                        size_t to_send = rlen - sent;
                        if (to_send > CHUNK_SIZE) {
                            to_send = CHUNK_SIZE;
                        }
                        
                        printf("%.*s", (int)to_send, p + sent);
                        fflush(stdout);
                        sent += to_send;
                        sleep_ms(1);
                    }
                    
                    // 줄바꿈 확인
                    if (rlen == 0 || response[rlen - 1] != '\n') {
                        printf("\n");
                        fflush(stdout);
                    }
                }
                usb_pos = 0;
            }
        } else if (usb_pos + 1 < sizeof(usb_line)) {
            usb_line[usb_pos++] = (char)ch;
        }
        ch = getchar_timeout_us(0);
    }
}

// =============================================================================
// 메인 함수
// =============================================================================

int main()
{
    // 1. 기본 초기화
    stdio_init_all();
    sleep_ms(2000);
    
    // 2. 상태 표시 LED 초기화 (부팅중: 녹색 LED 깜박임)
    status_led_init();
    
    // 부팅 시퀀스: 녹색 LED 3회 깜박임
    for (int i = 0; i < 3; i++) {
        status_led_green_on();
        sleep_ms(200);
        status_led_green_off();
        sleep_ms(200);
    }
    status_led_green_on();  // 깜박임 완료 후 켜둠
    
    // 3. 시스템 설정 로드 (통합 Flash 설정)
    system_config_init();
    debug_init();
    
    DBG_MAIN_PRINT("=== Pico GPIO Server v%s ===\n", PICO_PROGRAM_VERSION_STRING);
    DBG_MAIN_PRINT("Board: %s\n", PICO_BOARD);
    
    // 4. 설정 적용
    tcp_port = system_config_get_tcp_port();
    uart_rs232_1_baud = system_config_get_uart_baud();
    
    DBG_MAIN_PRINT("TCP port: %u\n", tcp_port);
    DBG_MAIN_PRINT("UART baud: %u\n", uart_rs232_1_baud);
    DBG_MAIN_PRINT("GPIO Device ID: 0x%02X\n", get_gpio_device_id());
    
    // 5. 네트워크 초기화
    network_init();
    
    // 6. HTTP 서버 시작
    if (http_server_init(80)) {
        DBG_MAIN_PRINT("HTTP server started on port 80\n");
    } else {
        DBG_MAIN_PRINT("ERROR: HTTP server failed to start\n");
    }
    
    // 7. UART 초기화
    uart_rs232_init(RS232_PORT_1, uart_rs232_1_baud);
    DBG_MAIN_PRINT("UART RS232 initialized at %u baud\n", uart_rs232_1_baud);
    
    // 8. GPIO 초기화
    gpio_spi_init();
    DBG_MAIN_PRINT("GPIO SPI initialized\n");
    
    // GPIO 출력 모두 끄기 (초기 상태)
    hct595_write(0x0000);
    DBG_MAIN_PRINT("GPIO outputs initialized (all OFF)\n");
    
    // 시스템 준비 완료: 녹색 LED 계속 켜짐
    status_led_set_state(STATUS_LED_GREEN_ON);
    DBG_MAIN_PRINT("System ready - Status LED green\n");

    // =============================================================================
    // 메인 루프
    // =============================================================================
    
    while (true) {
        // 재시작 요청 확인
        if (is_system_restart_requested()) {
            system_restart();
        }
        
        // 네트워크 처리
        network_process();
        
        // 네트워크 연결 상태에 따라 LED 제어
        static bool prev_connected = false;
        bool current_connected = network_is_connected();
        
        if (current_connected != prev_connected) {
            if (current_connected) {
                status_led_set_state(STATUS_LED_GREEN_ON);  // 연결됨: 녹색
                DBG_MAIN_PRINT("Network connected - Status LED green\n");
            } else {
                status_led_set_state(STATUS_LED_RED_ON);   // 연결 끊김: 빨강색
                DBG_MAIN_PRINT("Network disconnected - Status LED red\n");
            }
            prev_connected = current_connected;
        }
        
        // TCP 서버 초기화 (네트워크 연결 후)
        if (!tcp_servers_initialized && network_is_connected()) {
            tcp_servers_init(tcp_port);
            tcp_servers_initialized = true;
            multicast_init();
            DBG_MAIN_PRINT("TCP servers initialized on port %u\n", tcp_port);
        }
        
        // 멀티캐스트 처리
        if (network_is_connected()) {
            multicast_process();
        }
        
        // 서버 처리
        http_server_process();
        tcp_servers_process();
        
        // GPIO 입력 읽기
        hct165_read();
        
        // UART 데이터 처리
        uart_rs232_process();

        // USB CDC 명령 처리
        process_usb_cdc_commands();
    }
}

