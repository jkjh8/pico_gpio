#include "tcp_server.h"
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
    // 유효성 검사: 0xFFFF면 기본값 사용
    if (port == 0xFFFF || port == 0) port = 5050;
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
                // 최초 연결 시에만 환영 메시지 전송
                if (getSn_IR(i) & Sn_IR_CON) {
                    send(i, (uint8_t*)"Welcome to TCP Server!\n", 24);
                    setSn_IR(i, Sn_IR_CON);
                }
                uint16_t rx_size = getSn_RX_RSR(i);
                if (rx_size > 0) {
                    uint8_t buf[512];
                    if (rx_size > sizeof(buf)) rx_size = sizeof(buf);
                    int len = recv(i, buf, rx_size);
                    buf[len] = 0;
                    printf("TCP[%d] 수신: %s\n", i, buf);
                    send(i, buf, len); // 에코
                }
                break;
            }
            case SOCK_CLOSE_WAIT:
                disconnect(i);
                break;
            case SOCK_CLOSED:
                close(i); // 안전하게 닫기
                socket(i, Sn_MR_TCP, tcp_port, 0x00);
                listen(i);
                printf("TCP 서버 재오픈 (소켓: %d, 포트: %d)\n", i, tcp_port);
                break;
            default:
                break;
        }
    }
}
