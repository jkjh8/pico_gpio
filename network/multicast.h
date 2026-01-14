#ifndef MULTICAST_H
#define MULTICAST_H

#include <stdint.h>
#include <stdbool.h>
#include "wizchip_conf.h"
#include "socket.h"

#ifdef __cplusplus
extern "C"
{
#endif

// 멀티캐스트/브로드캐스트 설정
#define MULTICAST_SOCKET 1
#define MULTICAST_PORT 36721           // 송수신 포트 통일
#define MULTICAST_GROUP_IP {224, 255, 0, 1}  // 224.255.0.1 (동시 수신)
#define BROADCAST_IP {255, 255, 255, 255}    // 255.255.255.255 (동시 수신)

// 주기적 상태 전송 설정
#define MULTICAST_AUTO_ANNOUNCE_INTERVAL_MS 3000  // 3초마다 자동 전송

// 브로드캐스트 모드 (사용 안 함 - 항상 두 개 동시 수신)
typedef enum {
    BROADCAST_MODE_MULTICAST = 0,  // 멀티캐스트
    BROADCAST_MODE_BROADCAST = 1   // 브로드캐스트 (기본)
} broadcast_mode_t;

// 멀티캐스트 메시지 타입 (224.255.0.1)
#define DEVICE_INFO_REQUEST  0x01  // 디바이스 검색 요청
#define DEVICE_INFO_RESPONSE 0x02  // 디바이스 정보 응답
#define DEVICE_IP_CHANGE_REQUEST 0x03  // IP 변경 요청

// 브로드캐스트 메시지 타입 (255.255.255.255)
#define DEVICE_INFO_REQUEST_BROADCAST  0x11  // 디바이스 검색 요청 (브로드캐스트)
#define DEVICE_INFO_RESPONSE_BROADCAST 0x12  // 디바이스 정보 응답 (브로드캐스트)
#define DEVICE_IP_CHANGE_REQUEST_BROADCAST 0x13  // IP 변경 요청 (브로드캐스트)

// 멀티캐스트 메시지 구조체
typedef struct {
    uint8_t msg_type;       // 메시지 타입
    uint8_t reserved[3];    // 정렬을 위한 예약 바이트
} multicast_request_t;

// 디바이스 정보 구조체 (msg_type 제외 - 별도 전송)
typedef struct __attribute__((packed)) {
    uint8_t device_id;
    uint8_t ip[4];
    uint8_t mac[6];
    uint16_t tcp_port;
    uint32_t uart_baud;
    uint8_t dhcp;          // 0=Static, 1=DHCP
} device_info_t;

// IP 변경 요청 구조체
typedef struct __attribute__((packed)) {
    uint8_t device_id;     // 대상 디바이스 ID
    uint8_t mac[6];        // 대상 디바이스 MAC 주소
    uint8_t is_dhcp;       // 1=DHCP, 0=Static
    uint8_t new_ip[4];     // 새 IP 주소 (DHCP일 경우 무시)
    uint8_t subnet[4];     // 서브넷 마스크 (DHCP일 경우 무시)
    uint8_t gateway[4];    // 게이트웨이 (DHCP일 경우 무시)
} ip_change_request_t;

// 멀티캐스트/브로드캐스트 함수
void multicast_init(void);
void multicast_process(void);
void multicast_close(void);
bool multicast_is_enabled(void);
void multicast_set_enabled(bool enabled);
void multicast_set_broadcast_mode(broadcast_mode_t mode);
broadcast_mode_t multicast_get_broadcast_mode(void);
void multicast_announce_device(void);  // 수동 디바이스 정보 전송

#ifdef __cplusplus
}
#endif

#endif // MULTICAST_H
