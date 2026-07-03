#include "mdns.h"
#include "network_config.h"
#include "system/system_config.h"
#include "gpio/gpio.h"
#include "debug/debug.h"
#include "lib/wiznet/w5500.h"
#include "lib/wiznet/socket.h"
#include "main.h"
#include "pico/time.h"
#include <string.h>
#include <stdio.h>

// 바이트 순서 변환 매크로
#define htons(x) ((uint16_t)((((x) & 0xFF) << 8) | (((x) >> 8) & 0xFF)))
#define ntohs(x) htons(x)

static bool mdns_initialized = false;
static bool mdns_expired = false;
static char mdns_hostname[32] = {0};  // "pico-gpio-XX.local"
static uint32_t last_announce_time = 0;
static uint32_t mdns_start_time = 0;
#define MDNS_ANNOUNCE_INTERVAL_MS 300000  // 300초마다 자발적 응답
#define MDNS_LIFETIME_MS 300000           // 시작 후 5분간만 동작
// 초기(부팅시) 아나운스 스케줄링 (블로킹 sleep 사용 금지)
static int mdns_initial_announces = 0;
static uint32_t mdns_next_initial_announce = 0;
// 빈/잘못된 질의 로깅 제한
static uint32_t mdns_ignore_last_log_ts = 0;
static int mdns_ignore_log_count = 0;
static int mdns_ignore_log_limit = 5; // 초당 최대 로그 수

// DNS 이름 압축 해제 (라벨 형식 → 문자열)
// packet_len으로 압축 포인터/라벨이 수신 버퍼를 벗어나지 못하도록 막고,
// jump_count로 포인터 루프(A→B→A)에 의한 무한 루프/장시간 점유를 방지한다.
// (조작되거나 손상된 mDNS 질의 패킷이 네트워크 부팅 직후 수신될 수 있음)
static int dns_decode_name(const uint8_t* packet, size_t packet_len, const uint8_t* name_ptr, char* output, int max_len) {
    const uint8_t* packet_end = packet + packet_len;
    int output_pos = 0;
    const uint8_t* ptr = name_ptr;
    bool jumped = false;
    int orig_pos = 0;
    int jump_count = 0;

    while (ptr < packet_end && *ptr != 0 && output_pos < max_len - 1) {
        // 압축 포인터 (상위 2비트가 11)
        if ((*ptr & 0xC0) == 0xC0) {
            if (ptr + 1 >= packet_end) break;      // 두 번째 포인터 바이트가 범위 밖
            if (++jump_count > 8) break;           // 과도한/순환 점프 방지
            if (!jumped) {
                orig_pos = ptr - name_ptr + 2;
            }
            uint16_t offset = ((*ptr & 0x3F) << 8) | *(ptr + 1);
            if (offset >= packet_len) break;       // 패킷 범위를 벗어난 점프 무시
            ptr = packet + offset;
            jumped = true;
            continue;
        }

        // 라벨 길이
        uint8_t len = *ptr++;
        if (len == 0) break;
        if (ptr + len > packet_end) break;         // 라벨이 패킷 끝을 벗어남

        if (output_pos > 0) {
            output[output_pos++] = '.';
        }

        for (int i = 0; i < len && output_pos < max_len - 1; i++) {
            output[output_pos++] = *ptr++;
        }

        if (!jumped) {
            orig_pos = ptr - name_ptr;
        }
    }

    output[output_pos] = '\0';
    return jumped ? orig_pos : (int)(ptr - name_ptr + 1);
}

// DNS 이름 인코딩 (문자열 → 라벨 형식)
static int dns_encode_name(uint8_t* output, const char* name) {
    int pos = 0;
    const char* label_start = name;
    const char* ptr = name;
    
    while (*ptr) {
        if (*ptr == '.') {
            int label_len = ptr - label_start;
            if (label_len > 0) {
                output[pos++] = label_len;
                memcpy(output + pos, label_start, label_len);
                pos += label_len;
            }
            label_start = ptr + 1;
        }
        ptr++;
    }
    
    // 마지막 라벨
    int label_len = ptr - label_start;
    if (label_len > 0) {
        output[pos++] = label_len;
        memcpy(output + pos, label_start, label_len);
        pos += label_len;
    }
    
    output[pos++] = 0;  // 종료
    return pos;
}

// 호스트 이름 비교 (대소문자 무시)
static bool hostname_match(const char* query, const char* hostname) {
    int i = 0;
    while (query[i] && hostname[i]) {
        char q = query[i];
        char h = hostname[i];
        if (q >= 'A' && q <= 'Z') q += 32;
        if (h >= 'A' && h <= 'Z') h += 32;
        if (q != h) return false;
        i++;
    }
    return query[i] == hostname[i];  // 둘 다 '\0'이어야 함
}

// A 레코드 응답 생성
static int build_a_record_response(uint8_t* response, const char* hostname, const uint8_t* ip) {
    int pos = 0;
    
    // 이름
    pos += dns_encode_name(response + pos, hostname);
    
    // TYPE (A)
    response[pos++] = 0;
    response[pos++] = DNS_TYPE_A;
    
    // CLASS (IN with cache flush bit)
    response[pos++] = (DNS_CLASS_IN | DNS_CLASS_FLUSH) >> 8;
    response[pos++] = (DNS_CLASS_IN | DNS_CLASS_FLUSH) & 0xFF;
    
    // TTL
    uint32_t ttl = MDNS_TTL;
    response[pos++] = (ttl >> 24) & 0xFF;
    response[pos++] = (ttl >> 16) & 0xFF;
    response[pos++] = (ttl >> 8) & 0xFF;
    response[pos++] = ttl & 0xFF;
    
    // RDLENGTH (4 bytes for IPv4)
    response[pos++] = 0;
    response[pos++] = 4;
    
    // RDATA (IP address)
    memcpy(response + pos, ip, 4);
    pos += 4;
    
    return pos;
}

// TXT 레코드 생성 (장비 정보)
static int build_txt_record_response(uint8_t* response, const char* hostname) {
    int pos = 0;
    
    // 이름
    pos += dns_encode_name(response + pos, hostname);
    
    // TYPE (TXT)
    response[pos++] = 0;
    response[pos++] = DNS_TYPE_TXT;
    
    // CLASS (IN with cache flush bit)
    response[pos++] = (DNS_CLASS_IN | DNS_CLASS_FLUSH) >> 8;
    response[pos++] = (DNS_CLASS_IN | DNS_CLASS_FLUSH) & 0xFF;
    
    // TTL
    uint32_t ttl = MDNS_TTL;
    response[pos++] = (ttl >> 24) & 0xFF;
    response[pos++] = (ttl >> 16) & 0xFF;
    response[pos++] = (ttl >> 8) & 0xFF;
    response[pos++] = ttl & 0xFF;
    
    // RDLENGTH 자리 예약
    int rdlength_pos = pos;
    pos += 2;
    
    int rdata_start = pos;
    
    // 장비 정보 TXT 레코드 (요청: device id, ip, netmask, gateway, dhcp, mac, tcp_port, uart_baud)
    wiz_NetInfo net_info;
    wizchip_getnetinfo(&net_info);
    
    char txt_buf[128];
    int txt_len;

    // device_id
    txt_len = snprintf(txt_buf, sizeof(txt_buf), "device_id=0x%02X", get_gpio_device_id());
    response[pos++] = txt_len;
    memcpy(response + pos, txt_buf, txt_len);
    pos += txt_len;

    // ip
    txt_len = snprintf(txt_buf, sizeof(txt_buf), "ip=%d.%d.%d.%d",
                       net_info.ip[0], net_info.ip[1], net_info.ip[2], net_info.ip[3]);
    response[pos++] = txt_len;
    memcpy(response + pos, txt_buf, txt_len);
    pos += txt_len;

    // netmask
    txt_len = snprintf(txt_buf, sizeof(txt_buf), "netmask=%d.%d.%d.%d",
                       net_info.sn[0], net_info.sn[1], net_info.sn[2], net_info.sn[3]);
    response[pos++] = txt_len;
    memcpy(response + pos, txt_buf, txt_len);
    pos += txt_len;

    // gateway
    txt_len = snprintf(txt_buf, sizeof(txt_buf), "gateway=%d.%d.%d.%d",
                       net_info.gw[0], net_info.gw[1], net_info.gw[2], net_info.gw[3]);
    response[pos++] = txt_len;
    memcpy(response + pos, txt_buf, txt_len);
    pos += txt_len;

    // dhcp
    txt_len = snprintf(txt_buf, sizeof(txt_buf), "dhcp=%s", net_info.dhcp == NETINFO_DHCP ? "true" : "false");
    response[pos++] = txt_len;
    memcpy(response + pos, txt_buf, txt_len);
    pos += txt_len;

    // mac
    txt_len = snprintf(txt_buf, sizeof(txt_buf), "mac=%02X:%02X:%02X:%02X:%02X:%02X",
                      net_info.mac[0], net_info.mac[1], net_info.mac[2],
                      net_info.mac[3], net_info.mac[4], net_info.mac[5]);
    response[pos++] = txt_len;
    memcpy(response + pos, txt_buf, txt_len);
    pos += txt_len;

    // tcp_port
    txt_len = snprintf(txt_buf, sizeof(txt_buf), "tcp_port=%u", tcp_port);
    response[pos++] = txt_len;
    memcpy(response + pos, txt_buf, txt_len);
    pos += txt_len;

    // uart_baud
    txt_len = snprintf(txt_buf, sizeof(txt_buf), "uart_baud=%lu", system_config_get_uart_baud());
    response[pos++] = txt_len;
    memcpy(response + pos, txt_buf, txt_len);
    pos += txt_len;
    
    // RDLENGTH 설정
    int rdlength = pos - rdata_start;
    response[rdlength_pos] = (rdlength >> 8) & 0xFF;
    response[rdlength_pos + 1] = rdlength & 0xFF;
    
    return pos;
}

// PTR/SRV 관련 서비스 탐색 응답은 더 이상 제공하지 않습니다.
// mDNS 초기화
void mdns_init(void) {
    if (mdns_expired) return;
    // 이미 초기화된 경우 재초기화하지 말고 아나운스만 실행
    if (mdns_initialized) {
        DBG_NET_PRINT("[mDNS] Already initialized, sending mdns_announce only\n");
        mdns_announce();
        return;
    }

    // 호스트 이름 생성: mic-control-XX.local (XX = device_id)
    uint8_t device_id = get_gpio_device_id();
    snprintf(mdns_hostname, sizeof(mdns_hostname), "mic-control-%02x.local", device_id);
    
    DBG_NET_PRINT("[mDNS] Hostname: %s\n", mdns_hostname);
    
    // 기존 소켓 닫기
    uint8_t status = getSn_SR(MDNS_SOCKET);
    if (status != SOCK_CLOSED) {
        close(MDNS_SOCKET);
        sleep_ms(10);  // 소켓이 완전히 닫힐 때까지 대기
    }
    
    // 멀티캐스트 MAC 주소 설정 (소켓 열기 전에 설정)
    uint8_t mdns_mac[6] = {0x01, 0x00, 0x5E, 0x00, 0x00, 0xFB};
    DBG_NET_PRINT("[mDNS] Setting multicast MAC before socket open...\n");
    setSn_DHAR(MDNS_SOCKET, mdns_mac);
    
    // 멀티캐스트 그룹 IP 설정 (소켓 열기 전에 설정)
    uint8_t mdns_ip[] = MDNS_MULTICAST_IP;
    DBG_NET_PRINT("[mDNS] Setting multicast IP before socket open...\n");
    setSn_DIPR(MDNS_SOCKET, mdns_ip);
    
    // 멀티캐스트 포트 설정
    setSn_DPORT(MDNS_SOCKET, MDNS_PORT);

    // UDP 멀티캐스트 소켓 열기
    int8_t ret = socket(MDNS_SOCKET, Sn_MR_UDP | Sn_MR_MULTI, MDNS_PORT, SF_IO_NONBLOCK);
    if (ret != MDNS_SOCKET) {
        DBG_NET_PRINT("[mDNS] Failed to open socket: %d\n", ret);
        return;
    }
    
    // 소켓 상태 확인
    status = getSn_SR(MDNS_SOCKET);
    DBG_NET_PRINT("[mDNS] Socket status after open: 0x%02X (expected 0x22 for UDP)\n", status);

    mdns_initialized = true;
    mdns_start_time = to_ms_since_boot(get_absolute_time());
    last_announce_time = mdns_start_time;
    
    DBG_NET_PRINT("[mDNS] Initialized on socket %d, port %d\n", MDNS_SOCKET, MDNS_PORT);
    DBG_NET_PRINT("[mDNS] Hostname: %s\n", mdns_hostname);
    DBG_NET_PRINT("[mDNS] Multicast group: 224.0.0.251\n");
    
    // 초기 공지: 첫 회는 즉시 전송하고, 나머지는 mdns_process에서 비블로킹으로 스케줄
    mdns_announce();
    mdns_initial_announces = 3; // 남은 전송 횟수 (총 3회)
    mdns_next_initial_announce = last_announce_time + 250; // ms
}

// mDNS 자발적 응답 (Unsolicited Response)
void mdns_announce(void) {
    if (!mdns_initialized) return;
    
    wiz_NetInfo net_info;
    wizchip_getnetinfo(&net_info);

    // static: network_task 스택 절약 (mdns_process 호출 체인과 중첩되지 않아 안전)
    static uint8_t response[512];
    int pos = 0;

    // DNS 헤더
    dns_header_t* header = (dns_header_t*)response;
    memset(header, 0, sizeof(dns_header_t));
    header->flags = htons(DNS_FLAG_RESPONSE | DNS_FLAG_AUTHORITATIVE);
    header->ancount = htons(2);  // A + TXT 레코드
    pos += sizeof(dns_header_t);
    
    // A 레코드
    pos += build_a_record_response(response + pos, mdns_hostname, net_info.ip);
    
    // TXT 레코드 (장비 정보)
    pos += build_txt_record_response(response + pos, mdns_hostname);
    
    // 멀티캐스트 그룹으로 전송
    uint8_t mdns_ip[] = MDNS_MULTICAST_IP;
    int32_t sent = sendto(MDNS_SOCKET, response, pos, mdns_ip, MDNS_PORT);
    
    if (sent > 0) {
        DBG_NET_PRINT("[mDNS] Announced: %s -> %d.%d.%d.%d (%d bytes)\n", 
                     mdns_hostname, net_info.ip[0], net_info.ip[1], 
                     net_info.ip[2], net_info.ip[3], sent);
    }
}

// mDNS 메시지 처리
void mdns_process(void) {
    if (!mdns_initialized) return;

    // 시작 후 5분 경과 시 자동 종료
    uint32_t current_time = to_ms_since_boot(get_absolute_time());
    static uint32_t last_alive_print = 0;
    if (current_time - last_alive_print >= 60000) {
        printf("[mDNS] alive, elapsed=%lus\n", (current_time - mdns_start_time) / 1000);
        last_alive_print = current_time;
    }
    if (current_time - mdns_start_time >= MDNS_LIFETIME_MS) {
        printf("[mDNS] 5 minutes elapsed, stopping mDNS\n");
        mdns_expired = true;
        mdns_close();
        return;
    }

    // 재부팅 요청 시 mDNS 소켓 닫고 처리 중단
    if (is_system_restart_requested()) {
        if (mdns_initialized) {
            DBG_NET_PRINT("[mDNS] Restart requested, closing mDNS\n");
            mdns_close();
        }
        return;
    }
    
    // 소켓 상태 확인
    uint8_t status = getSn_SR(MDNS_SOCKET);
    if (status != SOCK_UDP) {
        DBG_NET_PRINT("[mDNS] Socket not UDP (status: 0x%02X), reinitializing\n", status);
        mdns_initialized = false;
        mdns_init();
        return;
    }
    
    // 주기적 공지 (300초마다)
    if (current_time - last_announce_time >= MDNS_ANNOUNCE_INTERVAL_MS) {
        mdns_announce();
        last_announce_time = current_time;
        // 수신 대기 중임을 알림
        DBG_NET_PRINT("[mDNS] Listening on port %d, waiting for queries...\n", MDNS_PORT);
    }

    // 초기 아나운스(부팅시 다중 전송)를 블로킹 없이 스케줄링
    if (mdns_initial_announces > 0 && current_time >= mdns_next_initial_announce) {
        mdns_announce();
        mdns_initial_announces--;
        mdns_next_initial_announce = current_time + 500;  // 500ms 간격
        last_announce_time = current_time;
        DBG_NET_PRINT("[mDNS] Initial announce sent, %d remaining\n", mdns_initial_announces);
    }
    
    // 수신 데이터 확인 — 버퍼 완전 소진 (AES67/Dante mDNS 패킷 폭주 대응)
    int mdns_drain = 0;
    uint16_t len;
    while ((len = getSn_RX_RSR(MDNS_SOCKET)) > 0 && mdns_drain < 16) {
        mdns_drain++;

        // getSn_RX_RSR이 비정상값이면 SPI/소켓 오염 → 소켓 리셋
        if (len > 1472) {
            DBG_NET_PRINT("[mDNS] RX size garbage (%u), reinitializing\n", len);
            mdns_initialized = false;
            mdns_init();
            return;
        }

        // static: network_task 스택 절약 (mdns_announce 호출 체인과 중첩되지 않아 안전)
        static uint8_t buf[512];
        uint8_t remote_ip[4];
        uint16_t remote_port;

        int32_t ret = recvfrom(MDNS_SOCKET, buf, sizeof(buf), remote_ip, &remote_port);
        if (ret <= (int32_t)sizeof(dns_header_t)) continue;

        dns_header_t* header = (dns_header_t*)buf;
        const uint8_t* buf_end = buf + ret;

        // 질의 패킷만 처리 (응답 패킷 무시)
        if (ntohs(header->flags) & DNS_FLAG_RESPONSE) continue;

        uint16_t qdcount = ntohs(header->qdcount);
        if (qdcount == 0) continue;

        // 질의 파싱
        const uint8_t* ptr = buf + sizeof(dns_header_t);
        char qname[128];

        for (int i = 0; i < qdcount; i++) {
            // qdcount는 패킷 헤더의 값(조작 가능) — 실제 수신 범위를 벗어나면 중단
            if (ptr >= buf_end) break;

            int name_len = dns_decode_name(buf, (size_t)ret, ptr, qname, sizeof(qname));
            ptr += name_len;

            if (ptr + 4 > buf_end) break;  // TYPE+CLASS를 읽기 전 범위 확인
            uint16_t qtype = (ptr[0] << 8) | ptr[1];
            ptr += 4;  // TYPE + CLASS

            // 빈 질의는 로그 없이 무시; qtype==0은 rate-limited 로그
            if (qname[0] == '\0') {
                continue;
            }
            if (qtype == 0) {
                uint32_t now = to_ms_since_boot(get_absolute_time());
                if (now - mdns_ignore_last_log_ts > 1000) {
                    mdns_ignore_last_log_ts = now;
                    mdns_ignore_log_count = 0;
                }
                if (mdns_ignore_log_count < mdns_ignore_log_limit) {
                    mdns_ignore_log_count++;
                }
                continue;
            }

            // A 레코드 질의이고 호스트 이름이 일치하면 응답
            if (qtype == DNS_TYPE_A) {
                bool match = hostname_match(qname, mdns_hostname);
                if (match) {
                    wiz_NetInfo net_info;
                    wizchip_getnetinfo(&net_info);

                    // static: network_task 스택 절약
                    static uint8_t response[512];
                    int pos = 0;

                    // DNS 헤더
                    dns_header_t* resp_header = (dns_header_t*)response;
                    memcpy(resp_header, header, sizeof(dns_header_t));
                    resp_header->flags = htons(DNS_FLAG_RESPONSE | DNS_FLAG_AUTHORITATIVE);
                    resp_header->ancount = htons(2);  // A + TXT
                    resp_header->nscount = 0;
                    resp_header->arcount = 0;
                    pos += sizeof(dns_header_t);

                    // 원본 질의 복사
                    memcpy(response + pos, buf + sizeof(dns_header_t), name_len + 4);
                    pos += name_len + 4;

                    // A 레코드 응답
                    pos += build_a_record_response(response + pos, mdns_hostname, net_info.ip);

                    // TXT 레코드 (장비 정보)
                    pos += build_txt_record_response(response + pos, mdns_hostname);

                    // 유니캐스트 응답 (질의자에게 직접)
                    int32_t sent = sendto(MDNS_SOCKET, response, pos, remote_ip, remote_port);
                    (void)sent;

                    break;
                }
            }
        } // for questions
    } // while drain
}

// mDNS 닫기
void mdns_close(void) {
    if (mdns_initialized) {
        close(MDNS_SOCKET);
        mdns_initialized = false;
        DBG_NET_PRINT("[mDNS] Closed\n");
    }
}

bool mdns_is_initialized(void) {
    return mdns_initialized;
}

bool mdns_is_expired(void) {
    return mdns_expired;
}
