#include "tcp_server.h"
#include "protocol_handlers/command_handler.h"
#include "protocol_handlers/json_handler.h"
#include "gpio/gpio.h"
// 필요 라이브러리 include는 헤더에서 처리됨
uint16_t tcp_port = 5050;

void save_tcp_port_to_flash(uint16_t port) {
    uint32_t ints = save_and_disable_interrupts();
    flash_range_erase(TCP_PORT_FLASH_OFFSET, 4096);
    flash_range_program(TCP_PORT_FLASH_OFFSET, (const uint8_t*)&port, sizeof(port));
    restore_interrupts(ints);
    printf("[FLASH] TCP 포트 저장: %u\n", port);
}

void load_tcp_port_from_flash(void) {
    const uint8_t* flash_ptr = (const uint8_t*)(XIP_BASE + TCP_PORT_FLASH_OFFSET);
    uint16_t port;
    memcpy(&port, flash_ptr, sizeof(port));
    // 유효성 검사: 0, 0xFFFF, 또는 범위(1024~65535) 외 값이면 기본값 사용
    if (port == 0xFFFF || port == 0 || port < 1024 || port > 65535) port = 5050;
    tcp_port = port;
    printf("[FLASH] TCP 포트 불러오기: %u\n", tcp_port);
}


// 모든 연결된 TCP 클라이언트에 메시지 전송
void tcp_servers_broadcast(const uint8_t* data, uint16_t len) {
    for (uint8_t i = TCP_SOCKET_START; i < TCP_SOCKET_START + TCP_SOCKET_COUNT; i++) {
        if (getSn_SR(i) == SOCK_ESTABLISHED) {
            send(i, (uint8_t*)data, len);
        }
    }
}

// 모든 TCP 서버 소켓을 닫고 다시 여는 함수 (기존 포트)
void tcp_servers_restart(void) {
    for (uint8_t i = TCP_SOCKET_START; i < TCP_SOCKET_START + TCP_SOCKET_COUNT; i++) {
        close(i);
        socket(i, Sn_MR_TCP, tcp_port, 0x00);
        listen(i);
        printf("TCP 서버 재시작 (소켓: %d, 포트: %d)\n", i, tcp_port);
    }
}

// 새로운 포트 번호로 모든 TCP 서버 소켓을 닫고 다시 여는 함수
void tcp_servers_restart_with_port(uint16_t new_port) {
    tcp_port = new_port;
    for (uint8_t i = TCP_SOCKET_START; i < TCP_SOCKET_START + TCP_SOCKET_COUNT; i++) {
        close(i);
        socket(i, Sn_MR_TCP, tcp_port, 0x00);
        listen(i);
        printf("TCP 서버 재시작 (소켓: %d, 포트: %d)\n", i, tcp_port);
    }
}

void tcp_servers_init(uint16_t port) {
    for (uint8_t i = TCP_SOCKET_START; i < TCP_SOCKET_START + TCP_SOCKET_COUNT; i++) {
        if (getSn_SR(i) != SOCK_CLOSED) close(i);
        socket(i, Sn_MR_TCP, port, 0x00);
        listen(i);
        printf("TCP 서버 시작 (소켓: %d, 포트: %d)\n", i, port);
    }
}

void tcp_servers_process(void) {
    for (uint8_t i = TCP_SOCKET_START; i < TCP_SOCKET_START + TCP_SOCKET_COUNT; i++) {
        switch (getSn_SR(i)) {
            case SOCK_ESTABLISHED: {
                // 최초 연결 시에만 환영 메시지 전송 (통신 모드에 따라 다르게)
                if (getSn_IR(i) & Sn_IR_CON) {
                    if (get_gpio_comm_mode() == GPIO_MODE_JSON) {
                        // JSON 모드 웰컴 메시지
                        char welcome_json[128];
                        snprintf(welcome_json, sizeof(welcome_json), 
                                "{\"device_id\":%d,\"event\":\"connected\",\"mode\":\"json\"}\r\n", 
                                get_gpio_device_id());
                        send(i, (uint8_t*)welcome_json, strlen(welcome_json));
                    } else {
                        // TEXT 모드 웰컴 메시지
                        char welcome_text[64];
                        snprintf(welcome_text, sizeof(welcome_text), 
                                "Connected,%d,text\r\n", get_gpio_device_id());
                        send(i, (uint8_t*)welcome_text, strlen(welcome_text));
                    }
                    setSn_IR(i, Sn_IR_CON);
                }
                uint16_t rx_size = getSn_RX_RSR(i);
                if (rx_size > 0) {
                    uint8_t buf[512];
                    if (rx_size > sizeof(buf)) rx_size = sizeof(buf);
                    int len = recv(i, buf, rx_size);
                    buf[len] = 0;
                    printf("TCP[%d] 수신: %s\n", i, buf);
                    
                    // JSON 모드 확인 후 적절한 명령어 처리 함수 호출
                    char response[512];
                    cmd_result_t result;
                    
                    // JSON 모드인지 확인 (첫 문자가 '{' 인지 또는 모드 설정 확인)
                    if (get_gpio_comm_mode() == GPIO_MODE_JSON && buf[0] == '{') {
                        // JSON 명령어 처리
                        result = process_json_command((char*)buf, response, sizeof(response));
                    } else {
                        // 일반 텍스트 명령어 처리
                        result = process_command((char*)buf, response, sizeof(response));
                    }
                    
                    if (result == CMD_SUCCESS || result == CMD_ERROR_INVALID) {
                        send(i, (uint8_t*)response, strlen(response));
                    } else {
                        char error_msg[128];
                        snprintf(error_msg, sizeof(error_msg), "Command error: %d\r\n", result);
                        send(i, (uint8_t*)error_msg, strlen(error_msg));
                    }
                }
                break;
            }
            case SOCK_CLOSE_WAIT:
                disconnect(i);
                break;
            case SOCK_CLOSED:
                // 네트워크가 연결된 경우에만 재오픈
                if (network_is_connected()) {
                    close(i); // 안전하게 닫기
                    socket(i, Sn_MR_TCP, tcp_port, 0x00);
                    listen(i);
                    printf("TCP 서버 재오픈 (소켓: %d, 포트: %d)\n", i, tcp_port);
                }
                break;
            default:
                break;
        }
    }
}

