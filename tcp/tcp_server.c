#include "tcp_server.h"
#include "system/system_config.h"
#include "handlers/command_handler.h"
#include "gpio/gpio.h"
#include "led/status_led.h"
// 필요 라이브러리 include는 헤더에서 처리됨
uint16_t tcp_port = 5050;

// 소켓별 연결 상태 추적
static bool socket_welcome_sent[8] = {false};

void save_tcp_port_to_flash(uint16_t port) {
    system_config_set_tcp_port(port);
    system_config_save_to_flash();
    DBG_MAIN_PRINT("[FLASH] TCP 포트 저장 (시스템 설정): %u\n", port);
}

void load_tcp_port_from_flash(void) {
    tcp_port = system_config_get_tcp_port();
    DBG_MAIN_PRINT("[FLASH] TCP 포트 로드 (시스템 설정): %u\n", tcp_port);
}


// 모든 연결된 TCP 클라이언트에 메시지 전송
void tcp_servers_broadcast(const uint8_t* data, uint16_t len) {
    int sent_count = 0;
    for (uint8_t i = TCP_SOCKET_START; i < TCP_SOCKET_START + TCP_SOCKET_COUNT; i++) {
        uint8_t status = getSn_SR(i);
        if (status == SOCK_ESTABLISHED) {
            int32_t result = send(i, (uint8_t*)data, len);
            DBG_TCP_PRINT("Broadcast to socket %d: sent %d bytes (status=ESTABLISHED)\n", i, result);
            sent_count++;
        } else {
            DBG_TCP_PRINT("Socket %d not available (status=0x%02X)\n", i, status);
        }
    }
    if (sent_count == 0) {
        DBG_TCP_PRINT("Warning: No active TCP connections to broadcast to!\n");
    } else {
        DBG_TCP_PRINT("Broadcast completed to %d connection(s)\n", sent_count);
    }
}

// TCP 클라이언트 연결 여부 확인
bool tcp_servers_has_clients(void) {
    for (uint8_t i = TCP_SOCKET_START; i < TCP_SOCKET_START + TCP_SOCKET_COUNT; i++) {
        if (getSn_SR(i) == SOCK_ESTABLISHED) {
            return true;
        }
    }
    return false;
}

// 모든 TCP 서버 소켓을 닫고 다시 여는 함수 (기존 포트)
void tcp_servers_restart(void) {
    for (uint8_t i = TCP_SOCKET_START; i < TCP_SOCKET_START + TCP_SOCKET_COUNT; i++) {
        close(i);
        socket(i, Sn_MR_TCP, tcp_port, 0x00);
        listen(i);
    DBG_TCP_PRINT("TCP 서버 재시작 (소켓: %d, 포트: %d)\n", i, tcp_port);
    }
}

// 새로운 포트 번호로 모든 TCP 서버 소켓을 닫고 다시 여는 함수
void tcp_servers_restart_with_port(uint16_t new_port) {
    tcp_port = new_port;
    for (uint8_t i = TCP_SOCKET_START; i < TCP_SOCKET_START + TCP_SOCKET_COUNT; i++) {
        close(i);
        socket(i, Sn_MR_TCP, tcp_port, 0x00);
        listen(i);
    DBG_TCP_PRINT("TCP 서버 재시작 (소켓: %d, 포트: %d)\n", i, tcp_port);
    }
}

void tcp_servers_init(uint16_t port) {
    for (uint8_t i = TCP_SOCKET_START; i < TCP_SOCKET_START + TCP_SOCKET_COUNT; i++) {
        if (getSn_SR(i) != SOCK_CLOSED) close(i);
        socket(i, Sn_MR_TCP, port, 0x00);
        listen(i);
    DBG_TCP_PRINT("TCP 서버 시작 (소켓: %d, 포트: %d)\n", i, port);
    }
}

void tcp_servers_process(void) {
    for (uint8_t i = TCP_SOCKET_START; i < TCP_SOCKET_START + TCP_SOCKET_COUNT; i++) {
        switch (getSn_SR(i)) {
            case SOCK_ESTABLISHED: {
                // 최초 연결 시에만 환영 메시지 전송 (한 번만)
                if (!socket_welcome_sent[i]) {
                    socket_welcome_sent[i] = true;
                    char welcome_text[64];
                    snprintf(welcome_text, sizeof(welcome_text), 
                            "Connected,%d,text\r\n", get_gpio_device_id());
                    send(i, (uint8_t*)welcome_text, strlen(welcome_text));
                    DBG_TCP_PRINT("TCP[%d] Welcome message sent\n", i);
                }
                uint16_t rx_size = getSn_RX_RSR(i);
                if (rx_size > 0) {
                    // TCP 데이터 수신 시 LED 깜빡임
                    status_led_activity_blink();
                    
                    uint8_t buf[512];
                    if (rx_size > sizeof(buf)) rx_size = sizeof(buf);
                    int len = recv(i, buf, rx_size);
                    buf[len] = 0;
                    DBG_TCP_PRINT("TCP[%d] 수신: %s\n", i, buf);
                    
                    // 텍스트 명령어 처리
                    char response[2048];
                    cmd_result_t result;
                    
                    result = process_command((char*)buf, response, sizeof(response));
                    
                    if (result == CMD_SUCCESS || result == CMD_ERROR_INVALID) {
                        size_t resp_len = strlen(response);
                        size_t sent = 0;
                        const size_t CHUNK_SIZE = 256;
                        while (sent < resp_len) {
                            size_t remaining = resp_len - sent;
                            uint16_t this_len = (uint16_t)(remaining > CHUNK_SIZE ? CHUNK_SIZE : remaining);
                            int s = send(i, (uint8_t*)response + sent, this_len);
                            if (s <= 0) break;
                            sent += (size_t)s;
                        }
                        // 줄바꿈 추가
                        const char* newline = "\r\n";
                        send(i, (uint8_t*)newline, 2);
                    } else {
                        char error_msg[128];
                        snprintf(error_msg, sizeof(error_msg), "Command error: %d\r\n", result);
                        size_t err_len = strlen(error_msg);
                        size_t sent = 0;
                        const size_t CHUNK_SIZE = 256;
                        while (sent < err_len) {
                            size_t remaining = err_len - sent;
                            uint16_t this_len = (uint16_t)(remaining > CHUNK_SIZE ? CHUNK_SIZE : remaining);
                            int s = send(i, (uint8_t*)error_msg + sent, this_len);
                            if (s <= 0) break;
                            sent += (size_t)s;
                        }
                    }
                }
                break;
            }
            case SOCK_CLOSE_WAIT:
                disconnect(i);
                socket_welcome_sent[i] = false;  // 연결 종료 시 플래그 리셋
                break;
            case SOCK_CLOSED:
                socket_welcome_sent[i] = false;  // 소켓 닫힘 시 플래그 리셋
                // 네트워크가 연결된 경우에만 재오픈
                if (network_is_connected()) {
                    close(i); // 안전하게 닫기
                    socket(i, Sn_MR_TCP, tcp_port, 0x00);
                    listen(i);
                    DBG_TCP_PRINT("TCP 서버 재오픈 (소켓: %d, 포트: %d)\n", i, tcp_port);
                }
                break;
            default:
                break;
        }
    }
}

