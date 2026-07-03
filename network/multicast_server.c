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

// 멀티캐스트 그룹 IP (multicast_server.h의 MCAST_GROUP_IP와 반드시 일치해야 함)
static uint8_t mcast_group_ip[4] = {239, 195, 42, 17};

bool multicast_server_init(void) {
    DBG_NET_PRINT("[MCAST] Initializing multicast server on socket %d\n", MCAST_SOCKET);
    
    // 소켓 닫기 (이전 상태 정리)
    uint8_t status = getSn_SR(MCAST_SOCKET);
    if (status != SOCK_CLOSED) {
        close(MCAST_SOCKET);
        sleep_ms(10);
    }
    
    // 멀티캐스트 MAC 주소 설정 (소켓 열기 전)
    // IP 239.195.42.17 -> MAC 01:00:5E:43:2A:11
    // 멀티캐스트 MAC = 01:00:5E + (IP 하위 23비트) = 01:00:5E:(2nd옥텟&0x7F):(3rd옥텟):(4th옥텟)
    uint8_t mcast_mac[6] = {0x01, 0x00, 0x5E, 0x43, 0x2A, 0x11};
    setSn_DHAR(MCAST_SOCKET, mcast_mac);
    DBG_NET_PRINT("[MCAST] MAC: %02X:%02X:%02X:%02X:%02X:%02X\n",
        mcast_mac[0], mcast_mac[1], mcast_mac[2], mcast_mac[3], mcast_mac[4], mcast_mac[5]);

    // 멀티캐스트 그룹 IP 설정 (패킷의 "목적지 IP" 필터)
    // 239.195.42.17로 온 패킷만 수신 (송신자가 아니라 dst IP 체크!)
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
    
    // 멀티캐스트 명령 수신 처리 — 버퍼 완전 소진 (AES67/Dante 패킷 폭주 대응)
    static const char* const allowed_cmds[] = {
        "getip", "getin", "getins", "getout", "getouts", "set", "sets", "setb"
    };

    int drain_count = 0;
    uint16_t rx_size;
    while ((rx_size = getSn_RX_RSR(MCAST_SOCKET)) > 0 && drain_count < 16) {
        drain_count++;

        // getSn_RX_RSR이 비정상값(>1472)이면 SPI/소켓 오염 → 소켓 리셋
        if (rx_size > 1472) {
            DBG_NET_PRINT("[MCAST] RX size garbage (%u), reinitializing socket\n", rx_size);
            close(MCAST_SOCKET);
            multicast_server_init();
            return;
        }

        DBG_NET_PRINT("[MCAST] Data available: %d bytes\n", rx_size);
        uint8_t buf[512];
        uint8_t sender_ip[4];
        uint16_t sender_port;

        int32_t len = recvfrom(MCAST_SOCKET, buf, sizeof(buf), sender_ip, &sender_port);

        if (len <= 0) {
            DBG_NET_PRINT("[MCAST] recvfrom error: %d\n", len);
            break;
        }

        if (len >= (int32_t)sizeof(buf)) len = (int32_t)sizeof(buf) - 1;
        buf[len] = '\0';

        // 허용 명령어 화이트리스트 확인 (명령 이름만 추출하여 비교)
        char cmd_name[16] = {0};
        const char* comma = strchr((char*)buf, ',');
        size_t cmd_len = comma ? (size_t)(comma - (char*)buf) : strlen((char*)buf);
        if (cmd_len >= sizeof(cmd_name)) cmd_len = sizeof(cmd_name) - 1;
        memcpy(cmd_name, buf, cmd_len);

        bool whitelisted = false;
        for (int w = 0; w < (int)(sizeof(allowed_cmds) / sizeof(allowed_cmds[0])); w++) {
            if (strcmp(cmd_name, allowed_cmds[w]) == 0) { whitelisted = true; break; }
        }

        if (!whitelisted) {
            continue;
        }

        // 명령어 처리
        // static: 4KB를 network_task 스택(8KB)에 매번 할당하면 스택 오버플로우로 보드가 정지함
        static char response[4096];
        cmd_result_t result = process_mcast_command((char*)buf, response, sizeof(response));

        if (result == CMD_SUCCESS) {
            size_t resp_len = strlen(response);
            if (resp_len > 0) {
                int32_t sent = sendto(MCAST_SOCKET, (uint8_t*)response, resp_len,
                                     sender_ip, sender_port);
                if (sent > 0) {
                    DBG_NET_PRINT("[MCAST] Response sent (%d bytes) to %d.%d.%d.%d:%d\n",
                                 sent,
                                 sender_ip[0], sender_ip[1], sender_ip[2], sender_ip[3],
                                 sender_port);
                } else {
                    DBG_NET_PRINT("[MCAST] Response send failed: %d\n", sent);
                }
            }
        } else {
            // CMD_ERROR_WRONG_ID, CMD_ERROR_INVALID 등 — 무응답
            DBG_NET_PRINT("[MCAST] No response (result=%d)\n", result);
        }
    }
}

void multicast_server_close(void) {
    uint8_t status = getSn_SR(MCAST_SOCKET);
    if (status != SOCK_CLOSED) {
        close(MCAST_SOCKET);
        DBG_NET_PRINT("[MCAST] Closed\n");
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
