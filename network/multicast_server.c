#include "multicast_server.h"
#include "network_config.h"
#include "../debug/debug.h"
#include "../main.h"
#include "../handlers/command_handler.h"
#include "wizchip_conf.h"
#include "w5500.h"
#include "socket.h"
#include "pico/stdlib.h"
#include <string.h>
#include <ctype.h>

// 멀티캐스트 그룹 IP
static uint8_t mcast_group_ip[4] = {239, 224, 0, 1};

bool multicast_server_init(void) {
    DBG_NET_PRINT("[MCAST] Initializing multicast server on socket %d\n", MCAST_SOCKET);
    
    // 소켓 닫기 (이전 상태 정리)
    uint8_t status = getSn_SR(MCAST_SOCKET);
    if (status != SOCK_CLOSED) {
        close(MCAST_SOCKET);
        sleep_ms(10);
    }
    
    // 멀티캐스트 MAC 주소 설정 (소켓 열기 전)
    // IP 239.224.0.1 -> MAC 01:00:5E:60:00:01
    // 멀티캐스트 MAC = 01:00:5E + (IP 하위 23비트)
    uint8_t mcast_mac[6] = {0x01, 0x00, 0x5E, 0x60, 0x00, 0x01};
    setSn_DHAR(MCAST_SOCKET, mcast_mac);
    DBG_NET_PRINT("[MCAST] MAC: %02X:%02X:%02X:%02X:%02X:%02X\n",
        mcast_mac[0], mcast_mac[1], mcast_mac[2], mcast_mac[3], mcast_mac[4], mcast_mac[5]);
    
    // 멀티캐스트 그룹 IP 설정 (패킷의 "목적지 IP" 필터)
    // 239.224.0.1로 온 패킷만 수신 (송신자가 아니라 dst IP 체크!)
    setSn_DIPR(MCAST_SOCKET, mcast_group_ip);
    DBG_NET_PRINT("[MCAST] Group IP: %d.%d.%d.%d\n", 
        mcast_group_ip[0], mcast_group_ip[1], mcast_group_ip[2], mcast_group_ip[3]);
    
    // 멀티캐스트 포트 설정 (송수신 공통 포트)
    setSn_DPORT(MCAST_SOCKET, MCAST_PORT);
    DBG_NET_PRINT("[MCAST] Port: %d (TX/RX shared)\n", MCAST_PORT);
    
    // UDP 멀티캐스트 소켓 생성 (Sn_MR_MULTI 플래그 필수!)
    int8_t ret = socket(MCAST_SOCKET, Sn_MR_UDP | Sn_MR_MULTI, MCAST_PORT, 0);
    if (ret != MCAST_SOCKET) {
        DBG_NET_PRINT("[MCAST] ERROR: Failed to create socket: %d\n", ret);
        return false;
    }
    
    // 소켓 상태 확인
    status = getSn_SR(MCAST_SOCKET);
    DBG_NET_PRINT("[MCAST] Socket status: 0x%02X (expected 0x22 for UDP)\n", status);
    
    DBG_NET_PRINT("[MCAST] Initialized successfully:\n");
    DBG_NET_PRINT("[MCAST]   Port: %d (commands & responses)\n", MCAST_PORT);
    DBG_NET_PRINT("[MCAST]   Group: %d.%d.%d.%d\n",
                 mcast_group_ip[0], mcast_group_ip[1], mcast_group_ip[2], mcast_group_ip[3]);
    
    return true;
}

void multicast_server_process(void) {
    // 소켓 상태 확인
    uint8_t status = getSn_SR(MCAST_SOCKET);
    
    if (status != SOCK_UDP) {
        // 소켓이 닫혀있으면 재초기화 (5초 간격)
        static uint32_t last_reinit = 0;
        uint32_t now = to_ms_since_boot(get_absolute_time());
        
        if (now - last_reinit > 5000) {
            DBG_NET_PRINT("[MCAST] Socket closed (status: 0x%02X), reinitializing...\n", status);
            if (multicast_server_init()) {
                last_reinit = now;
            }
        }
        return;
    }
    
    // 주기적인 상태 체크 (10초마다)
    static uint32_t last_debug = 0;
    uint32_t now = to_ms_since_boot(get_absolute_time());
    if (now - last_debug > 10000) {
        uint16_t rx_check = getSn_RX_RSR(MCAST_SOCKET);
        DBG_NET_PRINT("[MCAST] Status check - Socket: 0x%02X, RX buffer: %d bytes\n", status, rx_check);
        last_debug = now;
    }
    
    // 멀티캐스트 명령 수신 처리
    uint16_t rx_size = getSn_RX_RSR(MCAST_SOCKET);
    if (rx_size > 0) {
        DBG_NET_PRINT("[MCAST] Data available: %d bytes\n", rx_size);
        uint8_t buf[512];
        uint8_t sender_ip[4];
        uint16_t sender_port;
        
        if (rx_size > sizeof(buf)) {
            rx_size = sizeof(buf);
        }
        
        // 멀티캐스트 데이터 수신 (송신자 정보 포함)
        int32_t len = recvfrom(MCAST_SOCKET, buf, rx_size, sender_ip, &sender_port);
        
        if (len > 0) {
            buf[len] = '\0';
            DBG_NET_PRINT("[MCAST] Received from %d.%d.%d.%d:%d (%d bytes): %s\n",
                         sender_ip[0], sender_ip[1], sender_ip[2], sender_ip[3],
                         sender_port, len, buf);
            
            // 명령어 처리
            char response[4096];
            cmd_result_t result = process_command((char*)buf, response, sizeof(response));
            
            // 정상 처리되었거나 유효하지 않은 명령일 경우 응답 전송
            if ((result == CMD_SUCCESS || result == CMD_ERROR_INVALID)) {
                size_t resp_len = strlen(response);
                if (resp_len > 0) {
                    // 옵션 1: 송신자에게 유니캐스트로 응답 (더 안정적)
                    // int32_t sent = sendto(MCAST_SOCKET, (uint8_t*)response, resp_len,
                    //                      sender_ip, sender_port);
                    
                    // 옵션 2: 멀티캐스트 그룹으로 응답 (모든 클라이언트가 받음)
                    int32_t sent = sendto(MCAST_SOCKET, (uint8_t*)response, resp_len,
                                         mcast_group_ip, MCAST_PORT);
                    
                    if (sent > 0) {
                        DBG_NET_PRINT("[MCAST] Response sent (%d bytes) to multicast\n", sent);
                    } else {
                        DBG_NET_PRINT("[MCAST] Response send failed: %d\n", sent);
                    }
                    
                    // 응답이 줄바꿈으로 끝나지 않으면 추가
                    if (resp_len < 2 || response[resp_len-2] != '\r' || response[resp_len-1] != '\n') {
                        const char* newline = "\r\n";
                        sendto(MCAST_SOCKET, (uint8_t*)newline, 2,
                              mcast_group_ip, MCAST_PORT);
                    }
                }
            } else if (result == CMD_ERROR_WRONG_ID) {
                // 디바이스 ID 불일치 - 응답하지 않음 (다른 장치용 명령)
                DBG_NET_PRINT("[MCAST] Command for different device ID, ignoring\n");
            } else {
                DBG_NET_PRINT("[MCAST] Command processing error: %d\n", result);
            }
        } else if (len < 0) {
            DBG_NET_PRINT("[MCAST] recvfrom error: %d\n", len);
        }
    }
}

void multicast_send_feedback(const char* message) {
    if (message == NULL || strlen(message) == 0) {
        return;
    }
    
    // 소켓 상태 확인
    uint8_t status = getSn_SR(MCAST_SOCKET);
    if (status != SOCK_UDP) {
        DBG_NET_PRINT("[MCAST] Cannot send, socket not ready (status: 0x%02X)\n", status);
        return;
    }
    
    // 멀티캐스트 그룹으로 피드백 전송
    int32_t sent = sendto(MCAST_SOCKET, (uint8_t*)message, strlen(message), 
                          mcast_group_ip, MCAST_PORT);
    
    if (sent > 0) {
        DBG_NET_PRINT("[MCAST] Sent feedback (%d bytes): %s\n", sent, message);
    } else {
        DBG_NET_PRINT("[MCAST] Send failed: %d\n", sent);
    }
}
