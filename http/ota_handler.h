#ifndef OTA_HANDLER_H
#define OTA_HANDLER_H

#include <stdint.h>

// =============================================================================
// AB 듀얼뱅크 OTA 메모리 레이아웃 (2MB 플래시 기준)
// ┌─ 0x000000 Bank A (512KB) ─ 현재 실행 중인 펌웨어
// ├─ 0x080000 Bank B (512KB) ─ OTA 수신 스테이징 영역
// ├─ 0x100000 Boot Flag (4KB) ─ "Bank B 적용" 플래그
// └─ 0x1FF000 Config (4KB) ─ 시스템 설정 (변경 없음)
// =============================================================================
#define OTA_BANK_A_OFFSET   0x000000UL   // Bank A 플래시 오프셋 (현재 실행)
#define OTA_BANK_B_OFFSET   0x080000UL   // Bank B 플래시 오프셋 (OTA 대상)
#define OTA_BANK_SIZE       0x080000UL   // 각 뱅크 크기: 512KB
#define OTA_FLAG_OFFSET     0x100000UL   // 부트 플래그 섹터 오프셋

// 플래그 매직 (little-endian uint32_t 읽기 기준)
// bytes: 4F 54 41 46 = "OTAF"
#define OTA_FLAG_MAGIC_0    0x4641544FUL
// bytes: 41 50 50 4F = "APPO"
#define OTA_FLAG_MAGIC_1    0x4F505041UL

// RP2350 BIN 펌웨어 검증 상수
#define OTA_MIN_FW_SIZE   (16 * 1024)    // 최소 16KB
#define OTA_SRAM_BASE     0x20000000UL   // RP2350 SRAM 시작
#define OTA_SRAM_END      0x20082000UL   // RP2350 SRAM 끝 (520KB)
#define OTA_FLASH_BASE    0x10000000UL   // Bank A XIP 주소 (펌웨어 실행 기준)
#define OTA_FLASH_END     (OTA_FLASH_BASE + OTA_BANK_SIZE)  // Bank A 끝 (512KB)

// POST /api/update 핸들러 (raw .bin 스트리밍, 레거시)
void http_handle_post_update(uint8_t sock,
                              const uint8_t *initial_body,
                              int initial_len,
                              int content_len);

// =============================================================================
// POST /api/ota/chunk — multipart/form-data 청크 분할 업로드
//
// 필드:
//   offset   : 이 청크의 펌웨어 시작 오프셋 (0부터)
//   total    : 펌웨어 전체 크기 (offset=0 청크에만 있음)
//   firmware : 바이너리 데이터 (256 배수 바이트)
//
// 완료 시 Bank B에 전체 펌웨어 기록 → 부트 플래그 기록 → 재부팅
// 재부팅 후 ota_boot_check()가 Bank B → Bank A 복사 (롤백 안전)
// =============================================================================
#define OTA_CHUNK_PAGES   12             // 청크당 페이지 수 (12 × 256B = 3072B)
#define OTA_CHUNK_SIZE    (OTA_CHUNK_PAGES * 256)

void http_handle_post_ota_chunk(uint8_t        sock,
                                const char    *request,
                                const uint8_t *body,
                                int            body_len);

#endif // OTA_HANDLER_H
