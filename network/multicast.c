#include "multicast.h"
#include "system/system_config.h"
#include "network_config.h"
#include "gpio/gpio.h"
#include "tcp/tcp_server.h"
#include "debug/debug.h"
#include <string.h>
#include <stdio.h>

static bool multicast_initialized = false;

// 멀티캐스트 초기화
void multicast_init(void) {
    // 소켓 7을 UDP 멀티캐스트로 설정
    uint8_t multicast_ip[] = MULTICAST_GROUP_IP;
    
    // 기존 소켓 닫기
    if (getSn_SR(MULTICAST_SOCKET) != SOCK_CLOSED) {
        close(MULTICAST_SOCKET);
    }
    
    // UDP 소켓 열기
    int8_t ret = socket(MULTICAST_SOCKET, Sn_MR_UDP, MULTICAST_PORT, 0);
    if (ret != MULTICAST_SOCKET) {
        DBG_NET_PRINT("Multicast socket open failed\n");
        return;
    }
    
    // 멀티캐스트 그룹 가입
    // W5500는 하드웨어 멀티캐스트 필터링을 지원하지 않으므로
    // 소프트웨어에서 멀티캐스트 주소로 전송만 수행
    
    multicast_initialized = true;
    DBG_NET_PRINT("Multicast initialized on socket %d, port %d\n", 
                  MULTICAST_SOCKET, MULTICAST_PORT);
}

// 멀티캐스트 메시지 수신 처리
void multicast_process(void) {
    if (!multicast_initialized || !system_config_get_multicast_enabled()) {
        return;
    }
    
    // 소켓 상태 확인
    uint8_t status = getSn_SR(MULTICAST_SOCKET);
    if (status != SOCK_UDP) {
        // 소켓이 닫혔으면 재초기화
        multicast_initialized = false;
        multicast_init();
        return;
    }
    
    // 수신 데이터 확인
    uint16_t len = getSn_RX_RSR(MULTICAST_SOCKET);
    if (len > 0) {
        uint8_t buf[512];
        uint8_t remote_ip[4];
        uint16_t remote_port;
        
        int32_t ret = recvfrom(MULTICAST_SOCKET, buf, sizeof(buf), remote_ip, &remote_port);
        
        if (ret > 0) {
            // 요청 메시지인지 확인 (첫 바이트가 DEVICE_INFO_REQUEST)
            if (ret >= 1 && buf[0] == DEVICE_INFO_REQUEST) {
                DBG_NET_PRINT("Device info request from %d.%d.%d.%d:%d\n",
                             remote_ip[0], remote_ip[1], remote_ip[2], remote_ip[3], remote_port);
                
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
                
                // 요청한 클라이언트로 유니캐스트 응답 전송
                int32_t send_ret = sendto(MULTICAST_SOCKET, response, sizeof(response),
                                         remote_ip, remote_port);
                
                if (send_ret > 0) {
                    DBG_NET_PRINT("Device info response sent (%d bytes) to %d.%d.%d.%d:%d\n",
                                 send_ret, remote_ip[0], remote_ip[1], remote_ip[2], remote_ip[3], remote_port);
                } else {
                    DBG_NET_PRINT("Response send failed: %d\n", send_ret);
                }
            }
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
