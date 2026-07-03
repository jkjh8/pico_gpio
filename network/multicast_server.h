#ifndef MULTICAST_SERVER_H
#define MULTICAST_SERVER_H

#include <stdint.h>
#include <stdbool.h>

// 멀티캐스트 설정
// 239.224.0.1처럼 단순한 주소는 다른 벤더/DIY 장비가 흔히 고르는 기본값이라 충돌 위험이 있어
// RFC 2365 organization-local 범위(239.192.0.0/14) 내의 덜 흔한 주소로 지정함
#define MCAST_SOCKET        5
#define MCAST_GROUP_IP      "239.195.42.17"
#define MCAST_PORT          9000  // 송수신 공통 포트

// 멀티캐스트 서버 함수
bool multicast_server_init(void);
void multicast_server_process(void);
void multicast_server_close(void);
void multicast_send_feedback(const char* message);

#endif // MULTICAST_SERVER_H
