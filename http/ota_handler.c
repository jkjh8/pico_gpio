#include "ota_handler.h"
#include "http_server.h"
#include "debug/debug.h"
#include "../main.h"
#include "lib/wiznet/socket.h"
#include "lib/wiznet/w5500.h"
#include "hardware/flash.h"
#include "hardware/sync.h"
#include "pico/stdlib.h"
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
// .bin 바이너리에서 offset 0x100 (2nd stage bootloader 직후 앱 시작):
//   [0x000]: Initial Stack Pointer → RP2350 SRAM 범위
//   [0x004]: Reset Handler 주소  → 플래시 범위, LSB=1 (Thumb)
// =============================================================================

static bool validate_vector_table(const uint8_t *page1_data) {
    uint32_t sp  = (uint32_t)page1_data[0]
                 | ((uint32_t)page1_data[1] << 8)
                 | ((uint32_t)page1_data[2] << 16)
                 | ((uint32_t)page1_data[3] << 24);
    uint32_t rst = (uint32_t)page1_data[4]
                 | ((uint32_t)page1_data[5] << 8)
                 | ((uint32_t)page1_data[6] << 16)
                 | ((uint32_t)page1_data[7] << 24);

    DBG_HTTP_PRINT("[OTA] SP=0x%08X  Reset=0x%08X\n", (unsigned)sp, (unsigned)rst);

    // SP: SRAM 범위 확인
    if (sp < OTA_SRAM_BASE || sp > OTA_SRAM_END) {
        DBG_HTTP_PRINT("[OTA] ERROR: SP out of SRAM range\n");
        return false;
    }

    // Reset vector: Thumb 모드 확인 (LSB=1), 플래시 범위 확인
    if ((rst & 1) == 0) {
        DBG_HTTP_PRINT("[OTA] ERROR: Reset vector not Thumb (LSB=0)\n");
        return false;
    }
    uint32_t rst_addr = rst & ~1u;
    if (rst_addr < OTA_FLASH_BASE + OTA_APP_OFFSET || rst_addr >= OTA_FLASH_END) {
        DBG_HTTP_PRINT("[OTA] ERROR: Reset vector out of flash range\n");
        return false;
    }

    return true;
}

// =============================================================================
// RAM 실행 플래시 기록 함수
// SMP FreeRTOS: vTaskSuspendAll + save_and_disable_interrupts 조합
// (system_config_save_to_flash와 동일한 패턴)
// =============================================================================

static void __no_inline_not_in_flash_func(ota_erase_sector)(uint32_t offset) {
    flash_range_erase(offset, FLASH_SECTOR_SIZE);
}

static void __no_inline_not_in_flash_func(ota_program_page)(uint32_t offset,
                                                              const uint8_t *data) {
    flash_range_program(offset, data, FLASH_PAGE_SIZE);
}

// 섹터 경계에서 erase, 256B 페이지 단위 program
static bool ota_flash_write_page(uint32_t flash_offset, const uint8_t *data) {
    if (flash_offset >= OTA_FLASH_MAX_OFFSET) {
        DBG_HTTP_PRINT("[OTA] SKIP: config sector (offset=0x%05X)\n", (unsigned)flash_offset);
        return true;
    }

    vTaskSuspendAll();
    uint32_t ints = save_and_disable_interrupts();

    if ((flash_offset % FLASH_SECTOR_SIZE) == 0) {
        ota_erase_sector(flash_offset);
    }
    ota_program_page(flash_offset, data);

    restore_interrupts(ints);
    xTaskResumeAll();

    return true;
}

// =============================================================================
// 소켓 + initial_body로부터 순서대로 데이터 수신
// dest에 need 바이트를 채운 후 실제 복사한 바이트 수 반환 (< need면 오류)
// =============================================================================

typedef struct {
    const uint8_t *ibuf;
    int            ilen;
    int            ipos;   // ibuf 내 현재 위치
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
                DBG_HTTP_PRINT("[OTA] ERROR: recv timeout\n");
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
//
// 동작:
//   1. 파일 크기 검증 (최소/최대/페이지 정렬)
//   2. 처음 두 페이지(512B) 수신 → ARM 벡터 테이블 검증
//      - SP:    SRAM 범위 (0x20000000-0x20082000)
//      - Reset: 플래시 범위, Thumb 모드 (LSB=1)
//   3. 검증 통과 시 처음 두 페이지부터 순서대로 플래시 기록
//   4. 이후 256B 페이지 단위로 스트리밍 기록
//   5. 완료 → 200 OK → reboot
// =============================================================================

void http_handle_post_update(uint8_t sock,
                              const uint8_t *initial_body,
                              int initial_len,
                              int content_len) {
    DBG_HTTP_PRINT("[OTA] BIN update start, size=%d\n", content_len);

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

    // --- Phase 1: 첫 두 페이지 수신 및 검증 ---
    static uint8_t pre_buf[FLASH_PAGE_SIZE * 2];  // 512B (page0 + page1)

    if (ota_stream_read(&stream, pre_buf, sizeof(pre_buf)) != sizeof(pre_buf)) {
        http_send_response(sock, "409 Conflict", "application/json",
                           "{\"error\":\"Failed to receive firmware header\"}");
        return;
    }

    // 벡터 테이블은 page1 (offset 0x100) 시작
    if (!validate_vector_table(pre_buf + FLASH_PAGE_SIZE)) {
        DBG_HTTP_PRINT("[OTA] ERROR: vector table validation failed\n");
        http_send_response(sock, "400 Bad Request", "application/json",
                           "{\"error\":\"Invalid firmware: vector table check failed\"}");
        return;
    }
    DBG_HTTP_PRINT("[OTA] Vector table OK, starting flash write...\n");

    // --- Phase 2: 첫 두 페이지 기록 ---
    if (!ota_flash_write_page(0, pre_buf) ||
        !ota_flash_write_page(FLASH_PAGE_SIZE, pre_buf + FLASH_PAGE_SIZE)) {
        http_send_response(sock, "500 Internal Server Error", "application/json",
                           "{\"error\":\"Flash write failed\"}");
        return;
    }

    // --- Phase 3: 나머지 페이지 스트리밍 기록 ---
    static uint8_t page_buf[FLASH_PAGE_SIZE];
    uint32_t flash_offset = FLASH_PAGE_SIZE * 2;
    int total_recv = (int)sizeof(pre_buf);
    uint32_t pages_written = 2;
    bool error = false;

    while (total_recv < content_len) {
        int n = ota_stream_read(&stream, page_buf, FLASH_PAGE_SIZE);
        if (n != FLASH_PAGE_SIZE) {
            DBG_HTTP_PRINT("[OTA] ERROR: short read at offset 0x%05X\n", (unsigned)flash_offset);
            error = true;
            break;
        }

        if (!ota_flash_write_page(flash_offset, page_buf)) {
            DBG_HTTP_PRINT("[OTA] ERROR: flash write failed at 0x%05X\n", (unsigned)flash_offset);
            error = true;
            break;
        }

        flash_offset += FLASH_PAGE_SIZE;
        total_recv   += FLASH_PAGE_SIZE;
        pages_written++;

        if ((pages_written % 64) == 0) {
            DBG_HTTP_PRINT("[OTA] Progress: %d/%d bytes\n", total_recv, content_len);
        }
    }

    if (error) {
        DBG_HTTP_PRINT("[OTA] FAILED at page %u\n", (unsigned)pages_written);
        http_send_response(sock, "500 Internal Server Error", "application/json",
                           "{\"error\":\"Flash write error\"}");
        return;
    }

    DBG_HTTP_PRINT("[OTA] SUCCESS: %u pages written (%d bytes)\n",
                   (unsigned)pages_written, total_recv);

    char resp[96];
    snprintf(resp, sizeof(resp),
             "{\"status\":\"ok\",\"pages\":%u,\"bytes\":%d}",
             (unsigned)pages_written, total_recv);
    http_send_response(sock, "200 OK", "application/json", resp);

    vTaskDelay(pdMS_TO_TICKS(300));
    disconnect(sock);
    vTaskDelay(pdMS_TO_TICKS(200));
    system_restart_request();
}
