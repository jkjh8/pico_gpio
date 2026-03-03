#include "ota_handler.h"
#include "http_server.h"
#include "debug/debug.h"
#include "../main.h"
#include "lib/wiznet/socket.h"
#include "lib/wiznet/w5500.h"
#include "hardware/flash.h"
#include "hardware/sync.h"
#include "pico/stdlib.h"
#include "pico/multicore.h"
#include "FreeRTOS.h"
#include "task.h"
#include <string.h>
#include <stdio.h>

// =============================================================================
// 상수
// =============================================================================

// 마지막 섹터(config 영역) 보호: 그 이전까지만 기록 허용
#define OTA_FLASH_MAX_OFFSET  (PICO_FLASH_SIZE_BYTES - FLASH_SECTOR_SIZE)

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

    DBG_HTTP_PRINT("[OTA] SP=0x%08X  Reset=0x%08X\n", (unsigned)sp, (unsigned)rst);

    // SP: SRAM 범위 확인
    if (sp < OTA_SRAM_BASE || sp > OTA_SRAM_END) {
        DBG_HTTP_PRINT("[OTA] ERROR: SP out of SRAM range (0x%08X..0x%08X)\n",
                       OTA_SRAM_BASE, OTA_SRAM_END);
        return false;
    }

    // Reset vector: Thumb 모드 확인 (LSB=1), 플래시 범위 확인
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
// RAM 실행 플래시 기록 함수
//
// 전략: OTA 시작 전에 multicore_reset_core1()로 Core 1을 ROM 대기 상태로 전환.
// Core 1이 플래시를 접근하지 않으므로 Core 0에서 인터럽트만 비활성화하면 안전.
// flash_safe_execute()의 FreeRTOS SMP 구현은 __wfe() 루프가 Core 1 응답 없이
// 무한 대기할 수 있어 사용하지 않음.
// =============================================================================

static void __no_inline_not_in_flash_func(ota_erase_sector)(uint32_t offset) {
    flash_range_erase(offset, FLASH_SECTOR_SIZE);
}

static void __no_inline_not_in_flash_func(ota_program_page)(uint32_t offset,
                                                              const uint8_t *data) {
    flash_range_program(offset, data, FLASH_PAGE_SIZE);
}

// Core 1이 이미 ROM에 있다고 가정. 인터럽트만 비활성화하고 erase/program.
static bool ota_flash_write_page(uint32_t flash_offset, const uint8_t *data) {
    if (flash_offset >= OTA_FLASH_MAX_OFFSET) {
        DBG_HTTP_PRINT("[OTA] SKIP: config sector (offset=0x%05X)\n", (unsigned)flash_offset);
        return true;
    }

    bool do_erase = (flash_offset % FLASH_SECTOR_SIZE) == 0;

    uint32_t ints = save_and_disable_interrupts();
    if (do_erase) {
        ota_erase_sector(flash_offset);
    }
    ota_program_page(flash_offset, data);
    restore_interrupts(ints);

    return true;
}

// =============================================================================
// 소켓 + initial_body로부터 순서대로 데이터 수신
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

    // 1) initial_body 잔여분 먼저 소비
    while (filled < need && s->ipos < s->ilen) {
        dest[filled++] = s->ibuf[s->ipos++];
    }

    // 2) 소켓에서 나머지 수신
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
    DBG_HTTP_PRINT("[OTA] BIN update start, size=%d, t=%u\n", content_len, (unsigned)t_start);

    // 파일 크기 검증
    if (content_len < OTA_MIN_FW_SIZE) {
        DBG_HTTP_PRINT("[OTA] ERROR: too small (%d < %d)\n", content_len, OTA_MIN_FW_SIZE);
        http_send_response(sock, "400 Bad Request", "application/json",
                           "{\"error\":\"Firmware too small\"}");
        return;
    }
    if (content_len > (int)OTA_FLASH_MAX_OFFSET) {
        DBG_HTTP_PRINT("[OTA] ERROR: too large (%d > %d)\n", content_len, (int)OTA_FLASH_MAX_OFFSET);
        http_send_response(sock, "400 Bad Request", "application/json",
                           "{\"error\":\"Firmware too large\"}");
        return;
    }
    if ((content_len % FLASH_PAGE_SIZE) != 0) {
        DBG_HTTP_PRINT("[OTA] ERROR: not page-aligned (%d)\n", content_len);
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

    DBG_HTTP_PRINT("[OTA] t=%u: Receiving header (512B)...\n",
                   (unsigned)to_ms_since_boot(get_absolute_time()));
    stdio_flush();

    if (ota_stream_read(&stream, pre_buf, sizeof(pre_buf)) != sizeof(pre_buf)) {
        DBG_HTTP_PRINT("[OTA] ERROR: header recv failed\n");
        http_send_response(sock, "409 Conflict", "application/json",
                           "{\"error\":\"Failed to receive firmware header\"}");
        return;
    }
    DBG_HTTP_PRINT("[OTA] t=%u: Header OK. Validating vector table...\n",
                   (unsigned)to_ms_since_boot(get_absolute_time()));

    if (!validate_vector_table(pre_buf)) {
        DBG_HTTP_PRINT("[OTA] ERROR: vector table invalid\n");
        http_send_response(sock, "400 Bad Request", "application/json",
                           "{\"error\":\"Invalid firmware: vector table check failed\"}");
        return;
    }
    DBG_HTTP_PRINT("[OTA] t=%u: Vector table OK\n",
                   (unsigned)to_ms_since_boot(get_absolute_time()));

    // --- Core 1 정지: ROM 대기 상태로 전환 ---
    // flash_safe_execute()의 FreeRTOS SMP __wfe() 방식 대신
    // 하드웨어 리셋으로 Core 1을 플래시 접근 불가 상태로 만듦
    DBG_HTTP_PRINT("[OTA] t=%u: Resetting Core 1 (ROM wait)...\n",
                   (unsigned)to_ms_since_boot(get_absolute_time()));
    stdio_flush();

    multicore_reset_core1();

    DBG_HTTP_PRINT("[OTA] t=%u: Core 1 reset OK. Starting flash write (%d pages, %d sectors)...\n",
                   (unsigned)to_ms_since_boot(get_absolute_time()),
                   content_len / FLASH_PAGE_SIZE,
                   content_len / FLASH_SECTOR_SIZE);
    stdio_flush();

    // --- Phase 2: 첫 두 페이지 기록 ---
    {
        uint32_t t0 = to_ms_since_boot(get_absolute_time());
        DBG_HTTP_PRINT("[OTA] t=%u: Writing page 0 (sector 0 erase + program)...\n", (unsigned)t0);
        stdio_flush();

        if (!ota_flash_write_page(0, pre_buf)) {
            DBG_HTTP_PRINT("[OTA] ERROR: page 0 write failed\n");
            http_send_response(sock, "500 Internal Server Error", "application/json",
                               "{\"error\":\"Flash write failed at page 0\"}");
            return;
        }
        DBG_HTTP_PRINT("[OTA] t=%u: Page 0 done (%ums)\n",
                       (unsigned)to_ms_since_boot(get_absolute_time()),
                       (unsigned)(to_ms_since_boot(get_absolute_time()) - t0));

        t0 = to_ms_since_boot(get_absolute_time());
        DBG_HTTP_PRINT("[OTA] t=%u: Writing page 1...\n", (unsigned)t0);
        stdio_flush();

        if (!ota_flash_write_page(FLASH_PAGE_SIZE, pre_buf + FLASH_PAGE_SIZE)) {
            DBG_HTTP_PRINT("[OTA] ERROR: page 1 write failed\n");
            http_send_response(sock, "500 Internal Server Error", "application/json",
                               "{\"error\":\"Flash write failed at page 1\"}");
            return;
        }
        DBG_HTTP_PRINT("[OTA] t=%u: Page 1 done (%ums)\n",
                       (unsigned)to_ms_since_boot(get_absolute_time()),
                       (unsigned)(to_ms_since_boot(get_absolute_time()) - t0));
        stdio_flush();
    }

    // --- Phase 3: 나머지 페이지 스트리밍 기록 ---
    static uint8_t page_buf[FLASH_PAGE_SIZE];
    uint32_t flash_offset = FLASH_PAGE_SIZE * 2;
    int total_recv = (int)sizeof(pre_buf);
    uint32_t pages_written = 2;
    bool error = false;
    uint32_t sector_t = 0;

    while (total_recv < content_len) {
        // 섹터 경계: 수신 전에 로그 (erase가 언제 일어나는지 파악)
        bool is_sector_start = (flash_offset % FLASH_SECTOR_SIZE) == 0;
        if (is_sector_start) {
            sector_t = to_ms_since_boot(get_absolute_time());
            DBG_HTTP_PRINT("[OTA] t=%u: Sector 0x%05X recv start\n",
                           (unsigned)sector_t, (unsigned)flash_offset);
            stdio_flush();
        }

        int n = ota_stream_read(&stream, page_buf, FLASH_PAGE_SIZE);
        if (n != FLASH_PAGE_SIZE) {
            DBG_HTTP_PRINT("[OTA] ERROR: recv short at 0x%05X (got %d, want %d)\n",
                           (unsigned)flash_offset, n, FLASH_PAGE_SIZE);
            stdio_flush();
            error = true;
            break;
        }

        if (is_sector_start) {
            DBG_HTTP_PRINT("[OTA] t=%u: Sector 0x%05X write start (erase+prog)\n",
                           (unsigned)to_ms_since_boot(get_absolute_time()), (unsigned)flash_offset);
            stdio_flush();
        }

        if (!ota_flash_write_page(flash_offset, page_buf)) {
            DBG_HTTP_PRINT("[OTA] ERROR: flash write failed at 0x%05X\n", (unsigned)flash_offset);
            stdio_flush();
            error = true;
            break;
        }

        if (is_sector_start) {
            DBG_HTTP_PRINT("[OTA] t=%u: Sector 0x%05X done (%ums)\n",
                           (unsigned)to_ms_since_boot(get_absolute_time()),
                           (unsigned)flash_offset,
                           (unsigned)(to_ms_since_boot(get_absolute_time()) - sector_t));
            stdio_flush();
        }

        flash_offset += FLASH_PAGE_SIZE;
        total_recv   += FLASH_PAGE_SIZE;
        pages_written++;
    }

    if (error) {
        DBG_HTTP_PRINT("[OTA] FAILED at page %u (offset=0x%05X)\n",
                       (unsigned)pages_written, (unsigned)flash_offset);
        stdio_flush();
        http_send_response(sock, "500 Internal Server Error", "application/json",
                           "{\"error\":\"Flash write error\"}");
        return;
    }

    uint32_t elapsed = to_ms_since_boot(get_absolute_time()) - t_start;
    DBG_HTTP_PRINT("[OTA] SUCCESS: %u pages / %d bytes in %ums\n",
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
