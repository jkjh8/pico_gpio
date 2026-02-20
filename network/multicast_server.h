#ifndef MULTICAST_SERVER_H
#define MULTICAST_SERVER_H

#include <stdint.h>
#include <stdbool.h>

// 멀티캐스트 설정
#define MCAST_SOCKET        5
#define MCAST_GROUP_IP      "239.224.0.1"
#define MCAST_PORT          9000  // 송수신 공통 포트

// 멀티캐스트 서버 함수
bool multicast_server_init(void);
void multicast_server_process(void);
void multicast_send_feedback(const char* message);

#endif // MULTICAST_SERVER_H
