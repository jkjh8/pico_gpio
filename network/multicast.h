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

// 멀티캐스트 설정
#define MULTICAST_SOCKET 7
#define MULTICAST_PORT 5555
#define MULTICAST_GROUP_IP {239, 255, 0, 1}  // 239.255.0.1

// 멀티캐스트 메시지 타입
#define DEVICE_INFO_REQUEST  0x01  // 디바이스 검색 요청
#define DEVICE_INFO_RESPONSE 0x02  // 디바이스 정보 응답

// 멀티캐스트 메시지 구조체
typedef struct {
    uint8_t msg_type;       // 메시지 타입
    uint8_t reserved[3];    // 정렬을 위한 예약 바이트
} multicast_request_t;

// 디바이스 정보 구조체
typedef struct {
    uint8_t msg_type;       // 메시지 타입 (MCAST_MSG_RESPONSE)
    uint8_t device_id;
    uint8_t ip[4];
    uint8_t mac[6];
    uint16_t tcp_port;
} device_info_t;

// 멀티캐스트 함수
void multicast_init(void);
void multicast_process(void);
void multicast_close(void);
bool multicast_is_enabled(void);
void multicast_set_enabled(bool enabled);

#ifdef __cplusplus
}
#endif

#endif // MULTICAST_H
