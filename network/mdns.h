#ifndef MDNS_H
#define MDNS_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C"
{
#endif

// mDNS 설정
#define MDNS_SOCKET 1              // 소켓 1 사용 (DHCP는 0, TCP는 2-5)
#define MDNS_PORT 5353             // mDNS 표준 포트
#define MDNS_MULTICAST_IP {224, 0, 0, 251}  // 224.0.0.251
#define MDNS_TTL 300               // TTL 300초 (5분)

// DNS 헤더 플래그
#define DNS_FLAG_RESPONSE   0x8000
#define DNS_FLAG_AUTHORITATIVE 0x0400

// DNS 레코드 타입
#define DNS_TYPE_A      1          // IPv4 주소
#define DNS_TYPE_PTR    12         // 포인터
#define DNS_TYPE_TXT    16         // 텍스트
#define DNS_TYPE_AAAA   28         // IPv6 주소
#define DNS_TYPE_SRV    33         // 서비스

// DNS 클래스
#define DNS_CLASS_IN    1          // 인터넷
#define DNS_CLASS_FLUSH 0x8000     // 캐시 플러시 비트

// DNS 패킷 구조체
typedef struct __attribute__((packed)) {
    uint16_t id;
    uint16_t flags;
    uint16_t qdcount;    // 질의 개수
    uint16_t ancount;    // 응답 개수
    uint16_t nscount;    // 권한 개수
    uint16_t arcount;    // 추가 레코드 개수
} dns_header_t;

// mDNS 함수
void mdns_init(void);
void mdns_process(void);
void mdns_close(void);
bool mdns_is_initialized(void);
bool mdns_is_expired(void);
void mdns_announce(void);  // 자발적 응답 (Unsolicited Response)

#ifdef __cplusplus
}
#endif

#endif // MDNS_H
