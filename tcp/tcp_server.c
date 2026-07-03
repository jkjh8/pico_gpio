#include "tcp_server.h"
#include "system/system_config.h"
#include "handlers/command_handler.h"
#include "gpio/gpio.h"
#include "led/status_led.h"
// 필요 라이브러리 include는 헤더에서 처리됨
uint16_t tcp_port = 5050;

// 소켓별 연결 상태 추적
static bool socket_welcome_sent[8] = {false};

// =============================================================================
// 소켓별 미전송 응답 대기열 (논블로킹 send() 재시도용)
//
// 소켓이 SF_IO_NONBLOCK이면 send()는 상대가 안 읽어도 절대 멈추지 않고 그 자리에서
// SOCK_BUSY(0)를 반환한다. 대신 "이 소켓에 아직 못 보낸 응답이 있다"는 상태를
// 기억해뒀다가 다음 network_task 루프(10ms 후)에 이어서 보내야 데이터 유실이 없다.
// 소켓별로 독립적이라 느린 클라이언트 하나가 다른 클라이언트/서비스를 막지 않는다.
// =============================================================================
#define TCP_PENDING_MAX 4096

typedef struct {
    char   data[TCP_PENDING_MAX];
    size_t len;   // 응답 전체 길이
    size_t sent;  // 지금까지 전송한 바이트 수 (sent == len 이면 다 보낸 것)
} tcp_pending_t;

static tcp_pending_t tcp_pending[TCP_SOCKET_COUNT];

static inline tcp_pending_t* tcp_pending_for(uint8_t sock) {
    return &tcp_pending[sock - TCP_SOCKET_START];
}

static void tcp_pending_reset(uint8_t sock) {
    tcp_pending_t* p = tcp_pending_for(sock);
    p->len = 0;
    p->sent = 0;
}

// 새 응답을 대기열에 등록 (기존 미전송 데이터는 없다고 가정하고 호출할 것)
static void tcp_queue_response(uint8_t sock, const char* data, size_t len) {
    tcp_pending_t* p = tcp_pending_for(sock);
    if (len > TCP_PENDING_MAX) len = TCP_PENDING_MAX;  // 방어적 절단 (현재 응답 최대 크기 이내)
    memcpy(p->data, data, len);
    p->len = len;
    p->sent = 0;
}

// 대기 중인 응답을 가능한 만큼 전송. 상대가 못 받아주면(SOCK_BUSY) 다음 루프에서 재시도.
static void tcp_flush_pending(uint8_t sock) {
    tcp_pending_t* p = tcp_pending_for(sock);
    const size_t CHUNK_SIZE = 256;

    while (p->sent < p->len) {
        size_t remaining = p->len - p->sent;
        uint16_t this_len = (uint16_t)(remaining > CHUNK_SIZE ? CHUNK_SIZE : remaining);
        int32_t s = send(sock, (uint8_t*)p->data + p->sent, this_len);
        if (s > 0) {
            p->sent += (size_t)s;
            continue;  // 더 보낼 수 있으면 이어서 시도
        }
        if (s == SOCK_BUSY) {
            break;  // 논블로킹: 다음 루프에서 이어서 시도 (데이터 유실 없음)
        }
        // 진짜 소켓 에러(연결 끊김 등) — 응답 포기, 다음에 새 데이터부터 처리
        p->sent = p->len;
        break;
    }
}

void save_tcp_port_to_flash(uint16_t port) {
    system_config_set_tcp_port(port);
    system_config_save_to_flash();
    DBG_MAIN_PRINT("[FLASH] TCP 포트 저장 (시스템 설정): %u\n", port);
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
// SF_IO_NONBLOCK 필수: 블로킹 모드면 클라이언트가 응답을 안 읽을 때 send()가
// network_task(최고 우선순위) 안에서 무한 대기해 전체 네트워크/GPIO/UART까지 멈춤
void tcp_servers_restart(void) {
    for (uint8_t i = TCP_SOCKET_START; i < TCP_SOCKET_START + TCP_SOCKET_COUNT; i++) {
        close(i);
        socket(i, Sn_MR_TCP, tcp_port, SF_IO_NONBLOCK);
        listen(i);
        tcp_pending_reset(i);
    DBG_TCP_PRINT("TCP 서버 재시작 (소켓: %d, 포트: %d)\n", i, tcp_port);
    }
}

// 새로운 포트 번호로 모든 TCP 서버 소켓을 닫고 다시 여는 함수
void tcp_servers_restart_with_port(uint16_t new_port) {
    tcp_port = new_port;
    for (uint8_t i = TCP_SOCKET_START; i < TCP_SOCKET_START + TCP_SOCKET_COUNT; i++) {
        close(i);
        socket(i, Sn_MR_TCP, tcp_port, SF_IO_NONBLOCK);
        listen(i);
        tcp_pending_reset(i);
    DBG_TCP_PRINT("TCP 서버 재시작 (소켓: %d, 포트: %d)\n", i, tcp_port);
    }
}

void tcp_servers_init(uint16_t port) {
    for (uint8_t i = TCP_SOCKET_START; i < TCP_SOCKET_START + TCP_SOCKET_COUNT; i++) {
        if (getSn_SR(i) != SOCK_CLOSED) close(i);
        socket(i, Sn_MR_TCP, port, SF_IO_NONBLOCK);
        listen(i);
        tcp_pending_reset(i);
    DBG_TCP_PRINT("TCP 서버 시작 (소켓: %d, 포트: %d)\n", i, port);
    }
}

void tcp_servers_process(void) {
    for (uint8_t i = TCP_SOCKET_START; i < TCP_SOCKET_START + TCP_SOCKET_COUNT; i++) {
        switch (getSn_SR(i)) {
            case SOCK_ESTABLISHED: {
                // 이 소켓에 못 보낸 응답이 남아있으면 이어서 전송 시도.
                // 논블로킹이라 여기서 절대 멈추지 않으며, 다 못 보내면 다음 루프(10ms 후)에
                // 재시도한다 — 새 명령을 먼저 읽지 않아 응답 순서가 꼬이지 않는다.
                tcp_pending_t* pending = tcp_pending_for(i);
                if (pending->sent < pending->len) {
                    tcp_flush_pending(i);
                    break;
                }

                // 최초 연결 시 현재 출력 상태만 전송
                if (!socket_welcome_sent[i]) {
                    socket_welcome_sent[i] = true;

                    // 현재 출력 상태 피드백 전송
                    char output_feedback[128];
                    gpio_rt_mode_t rt_mode = get_gpio_rt_mode();

                    if (rt_mode == GPIO_RT_MODE_CHANNEL) {
                        // CHANNEL 모드: 바이너리 형식
                        char binary[17];
                        for (int j = 0; j < 16; j++) {
                            binary[j] = (gpio_output_data & (1 << j)) ? '1' : '0';
                        }
                        binary[16] = '\0';
                        snprintf(output_feedback, sizeof(output_feedback),
                                "out,%d,%s\r\n", get_gpio_device_id(), binary);
                    } else {
                        // BYTES 모드: 2바이트 형식
                        uint8_t low_byte = (uint8_t)(gpio_output_data & 0xFF);
                        uint8_t high_byte = (uint8_t)((gpio_output_data >> 8) & 0xFF);
                        snprintf(output_feedback, sizeof(output_feedback),
                                "outb,%d,%d,%d\r\n", get_gpio_device_id(), low_byte, high_byte);
                    }

                    tcp_queue_response(i, output_feedback, strlen(output_feedback));
                    tcp_flush_pending(i);
                    DBG_TCP_PRINT("TCP[%d] Output state sent\n", i);
                    break;
                }
                uint16_t rx_size = getSn_RX_RSR(i);
                if (rx_size > 0) {
                    // TCP 데이터 수신 시 LED 깜빡임
                    status_led_activity_blink();

                    uint8_t buf[512];
                    if (rx_size > sizeof(buf)) rx_size = sizeof(buf);
                    int len = recv(i, buf, rx_size);
                    if (len < 0) len = 0;
                    if (len >= (int)sizeof(buf)) len = (int)sizeof(buf) - 1;
                    buf[len] = 0;
                    DBG_TCP_PRINT("TCP[%d] 수신: %s\n", i, buf);

                    // 텍스트 명령어 처리
                    // static: 4KB를 network_task 스택(8KB)에 매번 할당하면 스택 오버플로우로 보드가 정지함
                    static char response[4096];
                    cmd_result_t result;

                    result = process_command((char*)buf, response, sizeof(response));

                    if ((result == CMD_SUCCESS || result == CMD_ERROR_INVALID)) {
                        size_t resp_len = strlen(response);
                        if (resp_len > 0) {
                            // 응답이 줄바꿈으로 끝나지 않으면 추가
                            if (resp_len < 2 || response[resp_len-2] != '\r' || response[resp_len-1] != '\n') {
                                if (resp_len + 2 <= sizeof(response)) {
                                    response[resp_len++] = '\r';
                                    response[resp_len++] = '\n';
                                }
                            }
                            tcp_queue_response(i, response, resp_len);
                            tcp_flush_pending(i);
                        }
                    } else {
                        char error_msg[128];
                        snprintf(error_msg, sizeof(error_msg), "Command error: %d\r\n", result);
                        tcp_queue_response(i, error_msg, strlen(error_msg));
                        tcp_flush_pending(i);
                    }
                }
                break;
            }
            case SOCK_CLOSE_WAIT:
                disconnect(i);
                socket_welcome_sent[i] = false;  // 연결 종료 시 플래그 리셋
                tcp_pending_reset(i);
                break;
            case SOCK_CLOSED:
                socket_welcome_sent[i] = false;  // 소켓 닫힘 시 플래그 리셋
                tcp_pending_reset(i);
                // 네트워크가 연결된 경우에만 재오픈
                if (network_is_connected()) {
                    close(i); // 안전하게 닫기
                    socket(i, Sn_MR_TCP, tcp_port, SF_IO_NONBLOCK);
                    listen(i);
                    DBG_TCP_PRINT("TCP 서버 재오픈 (소켓: %d, 포트: %d)\n", i, tcp_port);
                }
                break;
            default:
                break;
        }
    }
}

