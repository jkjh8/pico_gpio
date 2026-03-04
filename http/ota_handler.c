#include "ota_handler.h"
#include "http_server.h"
#include "debug/debug.h"
#include "../main.h"
#include "../system/ota_boot.h"
#include "lib/wiznet/socket.h"
#include "lib/wiznet/w5500.h"
#include "hardware/flash.h"
#include "hardware/sync.h"
#include "hardware/gpio.h"
#include "pico/stdlib.h"
#include "FreeRTOS.h"
#include "task.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include "led/status_led.h"

// LED 디버그 헬퍼 (USB IRQ 꺼진 후에도 사용 가능)
// 🔴=플래시 쓰기 중  🟢=완료  🟢깜박=응답 전송  🔴🟢=에러
#define OTA_LED_FLASH_START()  do { status_led_red_on();   status_led_green_off(); } while(0)
#define OTA_LED_PAGE_DONE()    do { status_led_toggle_red(); } while(0)
#define OTA_LED_FLASH_DONE()   do { status_led_green_on();  status_led_red_off();  } while(0)
#define OTA_LED_RESP_SENT()    do { \
    for (int _b = 0; _b < 3; _b++) { \
        status_led_toggle_green(); sleep_ms(80); \
        status_led_toggle_green(); sleep_ms(80); \
    } \
} while(0)
#define OTA_LED_ERROR()        do { status_led_red_on(); status_led_green_on(); } while(0)

// =============================================================================
// 청크 OTA 상태 (POST /api/ota/chunk 용)
// =============================================================================
typedef struct {
    bool     active;
    uint32_t total_size;
    uint32_t bytes_written;
    uint32_t t_start;
} ota_chunk_state_t;

static ota_chunk_state_t g_ota = {0};

// =============================================================================
// multipart/form-data 파서
// =============================================================================

// Content-Type 헤더에서 "--boundary" delimiter 추출
// delim 버퍼에 "--{boundary}" 형태(null-terminated)로 저장
static bool parse_multipart_boundary(const char *request, char *delim, int dlen) {
    const char *ct = strstr(request, "Content-Type:");
    if (!ct) ct = strstr(request, "content-type:");
    if (!ct) {
        DBG_HTTP_PRINT("[MPB] no Content-Type header\n"); stdio_flush();
        return false;
    }
    const char *b = strstr(ct, "boundary=");
    if (!b) {
        DBG_HTTP_PRINT("[MPB] no boundary= in Content-Type\n"); stdio_flush();
        return false;
    }
    b += 9;
    while (*b == '"') b++;
    const char *end = b;
    while (*end && *end != '\r' && *end != '\n' && *end != ';' &&
           *end != ' '  && *end != '"') end++;
    int blen = (int)(end - b);
    if (blen <= 0 || blen + 3 >= dlen) {
        DBG_HTTP_PRINT("[MPB] boundary len invalid (%d)\n", blen); stdio_flush();
        return false;
    }
    delim[0] = '-'; delim[1] = '-';
    memcpy(delim + 2, b, blen);
    delim[2 + blen] = '\0';
    return true;
}

// memmem 대체 (C 표준에 없음)
static const uint8_t *mem_find(const uint8_t *hay, int hlen,
                                const char *needle, int nlen) {
    if (nlen <= 0 || hlen < nlen) return NULL;
    for (int i = 0; i <= hlen - nlen; i++) {
        if (memcmp(hay + i, needle, nlen) == 0) return hay + i;
    }
    return NULL;
}

// multipart 에서 텍스트 필드 값을 out_buf(null-terminated)에 저장
// 없으면 false 반환
static bool multipart_get_text(const uint8_t *body, int body_len,
                                const char *delim,
                                const char *field_name,
                                char *out_buf, int out_blen) {
    int dlen = (int)strlen(delim);
    const uint8_t *p    = body;
    const uint8_t *bend = body + body_len;

    while (p < bend) {
        const uint8_t *dp = mem_find(p, (int)(bend - p), delim, dlen);
        if (!dp) break;
        p = dp + dlen;
        if (p + 1 < bend && p[0] == '-' && p[1] == '-') break; // final boundary
        if (p + 1 < bend && p[0] == '\r' && p[1] == '\n') p += 2;

        // 파트 헤더 끝 찾기 (\r\n\r\n)
        const uint8_t *hend = NULL;
        for (const uint8_t *q = p; q < bend - 3; q++) {
            if (q[0]=='\r' && q[1]=='\n' && q[2]=='\r' && q[3]=='\n') {
                hend = q + 4; break;
            }
        }
        if (!hend) break;

        // name="field_name" 매칭
        char pat[64];
        snprintf(pat, sizeof(pat), "name=\"%s\"", field_name);
        if (!mem_find(p, (int)(hend - p), pat, (int)strlen(pat))) {
            p = hend; continue;
        }

        // 데이터 범위: hend ~ 다음 delimiter 직전 \r\n 제거
        const uint8_t *ndp  = mem_find(hend, (int)(bend - hend), delim, dlen);
        const uint8_t *dend = ndp ? ndp : bend;
        if (dend > hend && dend[-1] == '\n') dend--;
        if (dend > hend && dend[-1] == '\r') dend--;
        int len = (int)(dend - hend);
        if (len <= 0 || len >= out_blen) return false;
        memcpy(out_buf, hend, len);
        out_buf[len] = '\0';
        return true;
    }
    return false;
}

// multipart 에서 바이너리 필드 포인터와 길이 반환. 없으면 NULL
static const uint8_t *multipart_get_binary(const uint8_t *body, int body_len,
                                            const char *delim,
                                            const char *field_name,
                                            int *out_len) {
    int dlen = (int)strlen(delim);
    const uint8_t *p    = body;
    const uint8_t *bend = body + body_len;

    while (p < bend) {
        const uint8_t *dp = mem_find(p, (int)(bend - p), delim, dlen);
        if (!dp) break;
        p = dp + dlen;
        if (p + 1 < bend && p[0] == '-' && p[1] == '-') break;
        if (p + 1 < bend && p[0] == '\r' && p[1] == '\n') p += 2;

        const uint8_t *hend = NULL;
        for (const uint8_t *q = p; q < bend - 3; q++) {
            if (q[0]=='\r' && q[1]=='\n' && q[2]=='\r' && q[3]=='\n') {
                hend = q + 4; break;
            }
        }
        if (!hend) break;

        char pat[64];
        snprintf(pat, sizeof(pat), "name=\"%s\"", field_name);
        if (!mem_find(p, (int)(hend - p), pat, (int)strlen(pat))) {
            p = hend; continue;
        }

        const uint8_t *ndp  = mem_find(hend, (int)(bend - hend), delim, dlen);
        const uint8_t *dend = ndp ? ndp : bend;
        if (dend > hend && dend[-1] == '\n') dend--;
        if (dend > hend && dend[-1] == '\r') dend--;
        *out_len = (int)(dend - hend);
        return hend;
    }
    return NULL;
}

// =============================================================================
// 상수
// =============================================================================

// 소켓 수신 타임아웃 (ms)
#define OTA_RECV_TIMEOUT_MS   5000

// =============================================================================
// ARM Cortex-M 벡터 테이블 검증
// RP2350 .bin은 offset 0x000부터 바로 벡터 테이블 시작:
//   [0x000]: Initial Stack Pointer → RP2350 SRAM 범위
//   [0x004]: Reset Handler 주소  → 플래시 범위, LSB=1 (Thumb)
// =============================================================================

static bool validate_vector_table(const uint8_t *data) {
    uint32_t sp  = (uint32_t)data[0]
                 | ((uint32_t)data[1] << 8)
                 | ((uint32_t)data[2] << 16)
                 | ((uint32_t)data[3] << 24);
    uint32_t rst = (uint32_t)data[4]
                 | ((uint32_t)data[5] << 8)
                 | ((uint32_t)data[6] << 16)
                 | ((uint32_t)data[7] << 24);

    if (sp < OTA_SRAM_BASE || sp > OTA_SRAM_END) {
        DBG_HTTP_PRINT("[OTA] ERROR: SP out of SRAM range (0x%08X..0x%08X)\n",
                       OTA_SRAM_BASE, OTA_SRAM_END);
        return false;
    }
    if ((rst & 1) == 0) {
        DBG_HTTP_PRINT("[OTA] ERROR: Reset vector not Thumb (LSB=0)\n");
        return false;
    }
    uint32_t rst_addr = rst & ~1u;
    if (rst_addr < OTA_FLASH_BASE || rst_addr >= OTA_FLASH_END) {
        DBG_HTTP_PRINT("[OTA] ERROR: Reset vector out of flash range (0x%08X..0x%08X)\n",
                       OTA_FLASH_BASE, OTA_FLASH_END);
        return false;
    }
    return true;
}

// =============================================================================
// 플래시 기록 (RAM 실행)
// 싱글코어: 인터럽트만 비활성화하면 안전 (Core 1 조율 불필요)
// =============================================================================

static void __no_inline_not_in_flash_func(ota_erase_sector)(uint32_t offset) {
    flash_range_erase(offset, FLASH_SECTOR_SIZE);
}

static void __no_inline_not_in_flash_func(ota_program_page)(uint32_t offset,
                                                              const uint8_t *data) {
    flash_range_program(offset, data, FLASH_PAGE_SIZE);
}

static bool __no_inline_not_in_flash_func(ota_flash_write_page)(uint32_t flash_offset, const uint8_t *data) {
    // Bank B 범위만 허용 (OTA 전용 영역)
    if (flash_offset < OTA_BANK_B_OFFSET ||
        flash_offset >= OTA_BANK_B_OFFSET + OTA_BANK_SIZE) {
        return false;
    }

    uint32_t ints = save_and_disable_interrupts();
    if ((flash_offset % FLASH_SECTOR_SIZE) == 0) {
        ota_erase_sector(flash_offset);
    }
    ota_program_page(flash_offset, data);
    restore_interrupts(ints);

    return true;
}

// =============================================================================
// 소켓 수신
// =============================================================================

typedef struct {
    const uint8_t *ibuf;
    int            ilen;
    int            ipos;
    uint8_t        sock;
    uint32_t       timeout_start;
} ota_stream_t;

static int ota_stream_read(ota_stream_t *s, uint8_t *dest, int need) {
    int filled = 0;

    while (filled < need && s->ipos < s->ilen) {
        dest[filled++] = s->ibuf[s->ipos++];
    }

    while (filled < need) {
        uint8_t status = getSn_SR(s->sock);
        if (status == SOCK_CLOSED || status == SOCK_CLOSE_WAIT) {
            DBG_HTTP_PRINT("[OTA] ERROR: socket closed (filled=%d/%d)\n", filled, need);
            return -1;
        }

        uint16_t avail = getSn_RX_RSR(s->sock);
        if (avail == 0) {
            uint32_t now = to_ms_since_boot(get_absolute_time());
            if ((now - s->timeout_start) > OTA_RECV_TIMEOUT_MS) {
                DBG_HTTP_PRINT("[OTA] ERROR: recv timeout (filled=%d/%d)\n", filled, need);
                return -1;
            }
            vTaskDelay(pdMS_TO_TICKS(5));
            continue;
        }

        int want = need - filled;
        if (want > avail) want = avail;
        int32_t n = recv(s->sock, dest + filled, want);
        if (n <= 0) {
            DBG_HTTP_PRINT("[OTA] ERROR: recv()=%d\n", (int)n);
            return -1;
        }
        filled += n;
        s->timeout_start = to_ms_since_boot(get_absolute_time());
    }

    return filled;
}

// =============================================================================
// POST /api/update 핸들러 — raw .bin 스트리밍
// =============================================================================

void http_handle_post_update(uint8_t sock,
                              const uint8_t *initial_body,
                              int initial_len,
                              int content_len) {
    uint32_t t_start = to_ms_since_boot(get_absolute_time());

    if (content_len < OTA_MIN_FW_SIZE) {
        http_send_response(sock, "400 Bad Request", "application/json",
                           "{\"error\":\"Firmware too small\"}");
        return;
    }
    if (content_len > (int)OTA_BANK_SIZE) {
        http_send_response(sock, "400 Bad Request", "application/json",
                           "{\"error\":\"Firmware exceeds 512KB bank size\"}");
        return;
    }
    if ((content_len % FLASH_PAGE_SIZE) != 0) {
        http_send_response(sock, "400 Bad Request", "application/json",
                           "{\"error\":\"Size not page-aligned (must be multiple of 256)\"}");
        return;
    }

    ota_stream_t stream = {
        .ibuf          = initial_body ? initial_body : NULL,
        .ilen          = initial_body ? initial_len  : 0,
        .ipos          = 0,
        .sock          = sock,
        .timeout_start = to_ms_since_boot(get_absolute_time()),
    };

    // --- Phase 1: 첫 두 페이지 수신 및 벡터 테이블 검증 ---
    static uint8_t pre_buf[FLASH_PAGE_SIZE * 2];

    if (ota_stream_read(&stream, pre_buf, sizeof(pre_buf)) != sizeof(pre_buf)) {
        http_send_response(sock, "409 Conflict", "application/json",
                           "{\"error\":\"Failed to receive firmware header\"}");
        return;
    }
    if (!validate_vector_table(pre_buf)) {
        http_send_response(sock, "400 Bad Request", "application/json",
                           "{\"error\":\"Invalid firmware: vector table check failed\"}");
        return;
    }
    // --- Phase 2: 첫 두 페이지 기록 (Bank B) ---
    // A/B 듀얼뱅크: Bank A 벡터 테이블(0x10000000)은 건드리지 않으므로 VTOR 리다이렉트 불필요
    if (!ota_flash_write_page(OTA_BANK_B_OFFSET + 0, pre_buf) ||
        !ota_flash_write_page(OTA_BANK_B_OFFSET + FLASH_PAGE_SIZE, pre_buf + FLASH_PAGE_SIZE)) {
        http_send_response(sock, "500 Internal Server Error", "application/json",
                           "{\"error\":\"Flash write failed\"}");
        return;
    }
    // --- Phase 3: 나머지 페이지 스트리밍 기록 (Bank B) ---
    static uint8_t page_buf[FLASH_PAGE_SIZE];
    uint32_t flash_offset = OTA_BANK_B_OFFSET + FLASH_PAGE_SIZE * 2;
    int total_recv = (int)sizeof(pre_buf);
    uint32_t pages_written = 2;
    bool error = false;

    while (total_recv < content_len) {
        int n = ota_stream_read(&stream, page_buf, FLASH_PAGE_SIZE);
        if (n != FLASH_PAGE_SIZE) {
            DBG_HTTP_PRINT("[OTA] ERROR: recv short at 0x%05X (got %d)\n",
                           (unsigned)flash_offset, n);
            error = true;
            break;
        }

        if (!ota_flash_write_page(flash_offset, page_buf)) {
            DBG_HTTP_PRINT("[OTA] ERROR: flash write at 0x%05X\n", (unsigned)flash_offset);
            error = true;
            break;
        }

        flash_offset += FLASH_PAGE_SIZE;
        total_recv   += FLASH_PAGE_SIZE;
        pages_written++;
    }

    if (error) {
        http_send_response(sock, "500 Internal Server Error", "application/json",
                           "{\"error\":\"Flash write error\"}");
        return;
    }

    uint32_t elapsed = to_ms_since_boot(get_absolute_time()) - t_start;
    DBG_HTTP_PRINT("[OTA] SUCCESS: %u pages / %d bytes / %ums\n",
                   (unsigned)pages_written, total_recv, (unsigned)elapsed);
    stdio_flush();

    char resp[128];
    snprintf(resp, sizeof(resp),
             "{\"status\":\"ok\",\"pages\":%u,\"bytes\":%d,\"ms\":%u}",
             (unsigned)pages_written, total_recv, (unsigned)elapsed);
    http_send_response(sock, "200 OK", "application/json", resp);

    vTaskDelay(pdMS_TO_TICKS(300));
    disconnect(sock);
    vTaskDelay(pdMS_TO_TICKS(200));
    system_restart_request();
}
// =============================================================================
// POST /api/ota/chunk — 2KB 청크 분할 OTA
//
// 흐름:
//   1) 클라이언트가 offset=0 으로 첫 청크 전송 → 벡터 검증 + VTOR→RAM
//   2) 순서대로 청크 전송, 각 요청은 HTTP 헤더 포함 ~1700B ≤ W5500 2KB RX 버퍼
//   3) bytes_written >= total_size 되면 200 "done" 응답 후 재시작
// =============================================================================
void http_handle_post_ota_chunk(uint8_t        sock,
                                const char    *request,
                                const uint8_t *body,
                                int            body_len) {

    // ── multipart/form-data boundary 추출 ─────────────────────────────────
    char delim[128];
    if (!parse_multipart_boundary(request, delim, sizeof(delim))) {
        http_send_response(sock, "400 Bad Request", "application/json",
                           "{\"error\":\"Missing multipart boundary\"}");
        return;
    }

    // ── 필드 파싱: offset (필수) ───────────────────────────────────────────
    char val_buf[32];
    if (!multipart_get_text(body, body_len, delim, "offset", val_buf, sizeof(val_buf))) {
        DBG_HTTP_PRINT("[OTA-CHK] ERR: offset field not found\n"); stdio_flush();
        http_send_response(sock, "400 Bad Request", "application/json",
                           "{\"error\":\"Missing offset field\"}");
        return;
    }
    int ota_offset = atoi(val_buf);

    // ── 필드 파싱: total (첫 청크에만 존재) ───────────────────────────────
    int ota_total = -1;
    if (multipart_get_text(body, body_len, delim, "total", val_buf, sizeof(val_buf))) {
        ota_total = atoi(val_buf);
    }

    // ── 필드 파싱: firmware 바이너리 ──────────────────────────────────────
    int fw_len = 0;
    const uint8_t *fw_data = multipart_get_binary(body, body_len, delim, "firmware", &fw_len);
    if (!fw_data || fw_len <= 0) {
        DBG_HTTP_PRINT("[OTA-CHK] ERR: firmware field not found (fw_len=%d)\n", fw_len); stdio_flush();
        // body 앞 64바이트 헥스 덤프 (파악용)
        DBG_HTTP_PRINT("[OTA-CHK] body hex: ");
        for (int _i = 0; _i < 64 && _i < body_len; _i++) {
            DBG_HTTP_PRINT("%02X ", body[_i]);
        }
        DBG_HTTP_PRINT("\n"); stdio_flush();
        http_send_response(sock, "400 Bad Request", "application/json",
                           "{\"error\":\"Missing firmware field\"}");
        return;
    }

    if (fw_len % FLASH_PAGE_SIZE != 0) {
        DBG_HTTP_PRINT("[OTA-CHK] ERR: fw_len=%d not multiple of 256\n", fw_len);
        stdio_flush();
        http_send_response(sock, "400 Bad Request", "application/json",
                           "{\"error\":\"Firmware must be multiple of 256 bytes\"}");
        return;
    }

    // ── 첫 번째 청크 (offset == 0): 초기화 + 검증 ─────────────────────────
    if (ota_offset == 0) {
        if (ota_total < 0) {
            http_send_response(sock, "400 Bad Request", "application/json",
                               "{\"error\":\"Missing total field on first chunk\"}");
            return;
        }
        if (ota_total < (int)OTA_MIN_FW_SIZE) {
            http_send_response(sock, "400 Bad Request", "application/json",
                               "{\"error\":\"Firmware too small\"}");
            return;
        }
        if (ota_total > (int)OTA_BANK_SIZE) {
            http_send_response(sock, "400 Bad Request", "application/json",
                               "{\"error\":\"Firmware exceeds 512KB bank size\"}");
            return;
        }
        if ((ota_total % FLASH_PAGE_SIZE) != 0) {
            http_send_response(sock, "400 Bad Request", "application/json",
                               "{\"error\":\"Total size not multiple of 256\"}");
            return;
        }
        if (!validate_vector_table(fw_data)) {
            http_send_response(sock, "400 Bad Request", "application/json",
                               "{\"error\":\"Invalid firmware: vector table check failed\"}");
            return;
        }

        // 상태 초기화
        g_ota.active        = true;
        g_ota.total_size    = (uint32_t)ota_total;
        g_ota.bytes_written = 0;
        g_ota.t_start       = to_ms_since_boot(get_absolute_time());

        // A/B 듀얼뱅크: Bank B에만 쓰므로 Bank A 벡터 테이블 불변 → VTOR 리다이렉트 불필요
        DBG_HTTP_PRINT("[OTA-CHK] start total=%u — Bank B 기록 시작\n",
                       (unsigned)g_ota.total_size);
        stdio_flush();

    } else {
        // ── 후속 청크: 순서 및 상태 검증 ──────────────────────────────────
        if (!g_ota.active) {
            http_send_response(sock, "409 Conflict", "application/json",
                               "{\"error\":\"OTA session not started\"}");
            return;
        }
        if ((uint32_t)ota_offset != g_ota.bytes_written) {
            char err[80];
            snprintf(err, sizeof(err),
                     "{\"error\":\"Offset mismatch: expected %u got %d\"}",
                     (unsigned)g_ota.bytes_written, ota_offset);
            http_send_response(sock, "409 Conflict", "application/json", err);
            return;
        }
    }

    // 범위 검사
    if ((uint32_t)ota_offset + (uint32_t)fw_len > g_ota.total_size) {
        http_send_response(sock, "400 Bad Request", "application/json",
                           "{\"error\":\"Chunk exceeds total size\"}");
        return;
    }

    // ── 플래시 기록 (256B 페이지 단위, Bank B에 기록) ────────────────────────
    // 🔴 RED ON = 플래시 쓰기 시작
    OTA_LED_FLASH_START();
    for (int i = 0; i < fw_len; i += FLASH_PAGE_SIZE) {
        // Bank B 기준 플래시 오프셋 = OTA_BANK_B_OFFSET + ota_offset + i
        uint32_t flash_offset = OTA_BANK_B_OFFSET + (uint32_t)ota_offset + (uint32_t)i;
        if (!ota_flash_write_page(flash_offset, fw_data + i)) {
            OTA_LED_ERROR();   // 🔴🟢 동시 = 에러
            g_ota.active = false;
            sleep_ms(10);
            http_send_response(sock, "500 Internal Server Error", "application/json",
                               "{\"error\":\"Flash write failed\"}");
            return;
        }
        OTA_LED_PAGE_DONE();   // 페이지마다 🔴 토글
    }
    // 🟢 GREEN ON = 플래시 완료
    OTA_LED_FLASH_DONE();

    g_ota.bytes_written += (uint32_t)fw_len;

    // ── 완료 판정 ──────────────────────────────────────────────────────────
    if (g_ota.bytes_written >= g_ota.total_size) {
        uint32_t elapsed = to_ms_since_boot(get_absolute_time()) - g_ota.t_start;

        DBG_HTTP_PRINT("[OTA] Bank B 기록 완료: %u bytes %ums — 부트 플래그 기록 중\n",
                       (unsigned)g_ota.bytes_written, (unsigned)elapsed);
        stdio_flush();
        sleep_ms(20);

        // 부트 플래그 기록 (RAM 함수: flash_range_erase/program)
        ota_write_boot_flag();

        DBG_HTTP_PRINT("[OTA] 플래그 기록 완료 — 재부팅 후 Bank B → A 복사\n");
        stdio_flush();
        sleep_ms(20);

        char resp[128];
        snprintf(resp, sizeof(resp),
                 "{\"status\":\"done\",\"pages\":%u,\"bytes\":%u,\"ms\":%u}",
                 (unsigned)(g_ota.bytes_written / FLASH_PAGE_SIZE),
                 (unsigned)g_ota.bytes_written,
                 (unsigned)elapsed);
        g_ota.active = false;
        OTA_LED_RESP_SENT();   // 🟢 3회 깜박 = 응답 전송 직전
        http_send_response(sock, "200 OK", "application/json", resp);
        vTaskDelay(pdMS_TO_TICKS(300));
        disconnect(sock);
        vTaskDelay(pdMS_TO_TICKS(200));
        system_restart_request();
    } else {
        // 🟢 3회 깜박 = 응답 전송 직전
        OTA_LED_RESP_SENT();
        char resp[48];
        snprintf(resp, sizeof(resp),
                 "{\"status\":\"ok\",\"next\":%u}",
                 (unsigned)g_ota.bytes_written);
        http_send_response(sock, "200 OK", "application/json", resp);
    }
}