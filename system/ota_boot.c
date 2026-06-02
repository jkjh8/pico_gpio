#include "ota_boot.h"
#include "../http/ota_handler.h"
#include "hardware/flash.h"
#include "hardware/sync.h"
#include "pico/stdlib.h"
#include <string.h>

// =============================================================================
// 섹터 버퍼 (FLASH_SECTOR_SIZE = 4096 bytes, BSS → SRAM)
// Bank B 섹터를 SRAM에 읽은 뒤 Bank A에 기록 — XIP 정지 중에도 안전
// =============================================================================
static uint8_t s_sector_buf[FLASH_SECTOR_SIZE] __attribute__((aligned(4)));

// =============================================================================
// RAM 함수 — flash_range_* 는 XIP를 일시 정지하므로 호출자도 RAM에 있어야 함
// =============================================================================

// Bank A의 한 섹터(4KB)를 SRAM 버퍼로 소거+프로그램
// flash_range_program을 FLASH_PAGE_SIZE(256B) 단위로 호출:
// RP2350 ROM에 4096B를 한번에 넘기면 첫 페이지만 기록되는 경우가 있음
static void __no_inline_not_in_flash_func(ota_apply_sector)(
        uint32_t a_offset, const uint8_t *buf) {
    uint32_t ints = save_and_disable_interrupts();
    flash_range_erase(a_offset, FLASH_SECTOR_SIZE);
    for (uint32_t p = 0; p < FLASH_SECTOR_SIZE; p += FLASH_PAGE_SIZE) {
        flash_range_program(a_offset + p, buf + p, FLASH_PAGE_SIZE);
    }
    restore_interrupts(ints);
}

// Bank A 복사 완료 후 재부팅 — watchdog_reboot() 대신 사용
// watchdog_reboot()는 flash(Bank A)에 있어서 복사 후 호출하면 새 펌웨어 바이트를 실행함
// AIRCR은 ARM Cortex-M33 하드웨어 레지스터 → flash 접근 없이 안전
static void __no_inline_not_in_flash_func(ota_trigger_reboot)(void) {
    volatile uint32_t *aircr = (volatile uint32_t *)0xE000ED0CU;
    *aircr = (0x5FAu << 16) | (1u << 2);  // VECTKEY=0x5FA, SYSRESETREQ=1
    while (1) {}
}

// 부트 플래그 섹터 소거 (클리어)
static void __no_inline_not_in_flash_func(ota_clear_flag_sector)(void) {
    uint32_t ints = save_and_disable_interrupts();
    flash_range_erase(OTA_FLAG_OFFSET, FLASH_SECTOR_SIZE);
    restore_interrupts(ints);
}

// 부트 플래그 기록: "다음 부팅에 Bank B 적용"
// 바이트 순: 0x4F 0x54 0x41 0x46 | 0x41 0x50 0x50 0x4F
// little-endian uint32_t: flag[0]=0x4641544F  flag[1]=0x4F505041
// flag_page 는 SRAM에 위치해야 함 (flash_range_program 중 XIP 정지되므로
// .rodata/flash에 있으면 접근 불가 → hang)
static uint8_t s_flag_page[FLASH_PAGE_SIZE];  // BSS → SRAM 보장

void __no_inline_not_in_flash_func(ota_write_boot_flag)(void) {
    // SRAM 버퍼를 0으로 초기화 후 매직 바이트 기록
    memset(s_flag_page, 0, sizeof(s_flag_page));
    s_flag_page[0] = 0x4F; s_flag_page[1] = 0x54;
    s_flag_page[2] = 0x41; s_flag_page[3] = 0x46;  // "OTAF" → MAGIC_0
    s_flag_page[4] = 0x41; s_flag_page[5] = 0x50;
    s_flag_page[6] = 0x50; s_flag_page[7] = 0x4F;  // "APPO" → MAGIC_1

    uint32_t ints = save_and_disable_interrupts();
    flash_range_erase(OTA_FLAG_OFFSET, FLASH_SECTOR_SIZE);
    flash_range_program(OTA_FLAG_OFFSET, s_flag_page, FLASH_PAGE_SIZE);
    restore_interrupts(ints);
}

// =============================================================================
// ota_boot_check — main() 최초에 호출
//
// 플래그 확인 → Bank B → Bank A 섹터 복사 → 플래그 클리어 → watchdog 재부팅
// 복사 중 전원 차단 시: 다음 부팅에 자동 재시도 (플래그 + Bank B 무결)
// =============================================================================
void __no_inline_not_in_flash_func(ota_boot_check)(void) {
    const uint32_t *flag = (const uint32_t *)(XIP_BASE + OTA_FLAG_OFFSET);

    if (flag[0] != OTA_FLAG_MAGIC_0 || flag[1] != OTA_FLAG_MAGIC_1) {
        return;  // 플래그 없음 → 정상 부팅
    }

    // ── Bank B (스테이징) → Bank A (실행 영역) 섹터 단위 복사 ──────────────
    // 총 OTA_BANK_SIZE / FLASH_SECTOR_SIZE = 512KB / 4KB = 128 섹터
    for (uint32_t offset = 0; offset < OTA_BANK_SIZE; offset += FLASH_SECTOR_SIZE) {

        // 1) XIP로 Bank B 섹터 읽기 (flash_range_erase 전에 반드시 수행)
        // memcpy는 flash에 위치할 수 있어 self-overwrite 도중 호출 시 hang.
        // 인라인 word 복사로 RAM 내에서 완결되게 함.
        const uint32_t *src = (const uint32_t *)(XIP_BASE + OTA_BANK_B_OFFSET + offset);
        uint32_t *dst = (uint32_t *)s_sector_buf;
        for (uint32_t i = 0; i < FLASH_SECTOR_SIZE / 4; i++) {
            dst[i] = src[i];
        }

        // 2) Bank A 해당 섹터에 기록 (RAM 함수 — XIP 정지 중 실행)
        ota_apply_sector(OTA_BANK_A_OFFSET + offset, s_sector_buf);
    }

    // ── 플래그 클리어 (다음 부팅에 재시도 방지) ───────────────────────────
    ota_clear_flag_sector();

    // ── 새 펌웨어로 재부팅 ────────────────────────────────────────────────
    // watchdog_reboot() 는 flash(Bank A)에 있어 복사 후 호출 불가 → AIRCR 사용
    ota_trigger_reboot();
}
