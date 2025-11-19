#include "multicast.h"
#include "system/system_config.h"
#include "network_config.h"
#include "gpio/gpio.h"
#include "tcp/tcp_server.h"
#include "debug/debug.h"
#include <string.h>
#include <stdio.h>

static bool multicast_initialized = false;
static broadcast_mode_t broadcast_mode = BROADCAST_MODE_BROADCAST;  // 기본값: 브로드캐스트

// 멀티캐스트 초기화
void multicast_init(void) {
    // 시스템 설정에서 멀티캐스트 활성화 확인
    if (!system_config_get_multicast_enabled()) {
        DBG_NET_PRINT("Multicast disabled in config\n");
        return;
    }
    
    // 소켓 7을 UDP 멀티캐스트로 설정
    uint8_t multicast_ip[] = MULTICAST_GROUP_IP;
    
    // 기존 소켓 닫기
    uint8_t status = getSn_SR(MULTICAST_SOCKET);
    if (status != SOCK_CLOSED) {
        DBG_NET_PRINT("Closing existing multicast socket (status: 0x%02X)\n", status);
        close(MULTICAST_SOCKET);
        sleep_ms(100);
    }
    
    // UDP 소켓 열기 (브로드캐스트 전용)
    int8_t ret = socket(MULTICAST_SOCKET, Sn_MR_UDP, MULTICAST_PORT, SF_IO_NONBLOCK);
    if (ret != MULTICAST_SOCKET) {
        DBG_NET_PRINT("Multicast socket open failed: %d\n", ret);
        return;
    }
    
    // 브로드캐스트 전용 (멀티캐스트 설정 불필요)
    
    // 소켓 상태 확인
    status = getSn_SR(MULTICAST_SOCKET);
    DBG_NET_PRINT("UDP Broadcast socket status after open: 0x%02X\n", status);
    DBG_NET_PRINT("Broadcast port: %d\n", MULTICAST_PORT);
    
    multicast_initialized = true;
    DBG_NET_PRINT("UDP Broadcast initialized on socket %d, port %d (enabled: %d)\n", 
                  MULTICAST_SOCKET, MULTICAST_PORT, system_config_get_multicast_enabled());
}

// 멀티캐스트 메시지 수신 처리
void multicast_process(void) {
    if (!multicast_initialized || !system_config_get_multicast_enabled()) {
        return;
    }
    
    // 소켓 상태 확인
    uint8_t status = getSn_SR(MULTICAST_SOCKET);
    
    if (status != SOCK_UDP) {
        DBG_NET_PRINT("[Multicast] Socket not UDP (0x%02X), reinitializing...\n", status);
        // 소켓이 닫혔으면 재초기화
        multicast_initialized = false;
        multicast_init();
        return;
    }
    
    // 수신 데이터 확인 (요청에 대한 응답만 처리)
    uint16_t len = getSn_RX_RSR(MULTICAST_SOCKET);
    if (len > 0) {
        uint8_t buf[512];
        uint8_t remote_ip[4];
        uint16_t remote_port;
        
        DBG_NET_PRINT("[UDP] Data available: %d bytes\n", len);
        
        int32_t ret = recvfrom(MULTICAST_SOCKET, buf, sizeof(buf), remote_ip, &remote_port);
        
        if (ret > 0) {
            DBG_NET_PRINT("[UDP] Received %d bytes from %d.%d.%d.%d:%d, first byte: 0x%02X\n", 
                         ret, remote_ip[0], remote_ip[1], remote_ip[2], remote_ip[3], remote_port, buf[0]);
            
            // 요청 메시지인지 확인 (첫 바이트가 DEVICE_INFO_REQUEST)
            if (ret >= 1 && buf[0] == DEVICE_INFO_REQUEST) {
                DBG_NET_PRINT("[UDP] Device info request received, preparing response...\n");
                
                // 응답 패킷 생성
                uint8_t response[sizeof(device_info_t) + 1];
                response[0] = DEVICE_INFO_RESPONSE;
                
                device_info_t* dev_info = (device_info_t*)(response + 1);
                dev_info->device_id = get_gpio_device_id();
                
                // IP 주소 가져오기
                wiz_NetInfo net_info;
                wizchip_getnetinfo(&net_info);
                memcpy(dev_info->ip, net_info.ip, 4);
                memcpy(dev_info->mac, net_info.mac, 6);
                dev_info->tcp_port = tcp_port;
                dev_info->uart_baud = system_config_get_uart_baud();
                
                DBG_NET_PRINT("[UDP] Response: device_id=%d, ip=%d.%d.%d.%d, tcp_port=%d, uart_baud=%lu\n",
                             dev_info->device_id, dev_info->ip[0], dev_info->ip[1], 
                             dev_info->ip[2], dev_info->ip[3], dev_info->tcp_port, dev_info->uart_baud);
                
                // 요청한 클라이언트로 응답 전송 (36721 포트)
                int32_t send_ret = sendto(MULTICAST_SOCKET, response, sizeof(response),
                                         remote_ip, MULTICAST_PORT);
                
                if (send_ret > 0) {
                    DBG_NET_PRINT("[UDP] Response sent: %d bytes to %d.%d.%d.%d:%d\n",
                                 send_ret, remote_ip[0], remote_ip[1], remote_ip[2], remote_ip[3], MULTICAST_PORT);
                } else {
                    DBG_NET_PRINT("[UDP] Response send failed: %d\n", send_ret);
                }
            } else {
                DBG_NET_PRINT("[UDP] Unknown message type: 0x%02X\n", buf[0]);
            }
        } else {
            DBG_NET_PRINT("[UDP] recvfrom failed: %d\n", ret);
        }
    }
}

// 멀티캐스트 활성화 상태 확인
bool multicast_is_enabled(void) {
    return system_config_get_multicast_enabled();
}

// 멀티캐스트 활성화/비활성화 설정
void multicast_set_enabled(bool enabled) {
    system_config_set_multicast_enabled(enabled);
    system_config_save_to_flash();
    
    if (enabled && !multicast_initialized) {
        multicast_init();
    } else if (!enabled && multicast_initialized) {
        multicast_close();
    }
    DBG_NET_PRINT("Multicast %s\n", enabled ? "enabled" : "disabled");
}

// 멀티캐스트 소켓 닫기
void multicast_close(void) {
    if (multicast_initialized) {
        close(MULTICAST_SOCKET);
        multicast_initialized = false;
        DBG_NET_PRINT("Multicast closed\n");
    }
}

// 브로드캐스트 모드 설정
void multicast_set_broadcast_mode(broadcast_mode_t mode) {
    broadcast_mode = mode;
    DBG_NET_PRINT("Broadcast mode set to: %s\n", 
                  mode == BROADCAST_MODE_BROADCAST ? "Broadcast" : "Multicast");
}

// 브로드캐스트 모드 조회
broadcast_mode_t multicast_get_broadcast_mode(void) {
    return broadcast_mode;
}
