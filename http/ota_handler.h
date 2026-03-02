#ifndef OTA_HANDLER_H
#define OTA_HANDLER_H

#include <stdint.h>

// RP2350 BIN 펌웨어 검증 상수
#define OTA_MIN_FW_SIZE   (16 * 1024)            // 최소 16KB
#define OTA_SRAM_BASE     0x20000000UL           // RP2350 SRAM 시작
#define OTA_SRAM_END      0x20082000UL           // RP2350 SRAM 끝 (520KB)
#define OTA_FLASH_BASE    0x10000000UL           // XIP 시작
#define OTA_FLASH_END     0x10400000UL           // 4MB 플래시 끝
#define OTA_APP_OFFSET    0x00000100UL           // 2nd stage bootloader 이후 앱 시작 (256B)

// POST /api/update 핸들러 (raw .bin 스트리밍)
// sock        : W5500 소켓 번호
// initial_body: HTTP 버퍼에서 이미 읽힌 body 시작 포인터
// initial_len : 버퍼 내 body 바이트 수
// content_len : Content-Length 헤더 값 (전체 .bin 크기)
void http_handle_post_update(uint8_t sock,
                              const uint8_t *initial_body,
                              int initial_len,
                              int content_len);

#endif // OTA_HANDLER_H
