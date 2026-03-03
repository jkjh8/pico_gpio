#ifndef OTA_BOOT_H
#define OTA_BOOT_H

// =============================================================================
// AB 듀얼뱅크 OTA 부트 관리
//
// 호출 순서:
//   1) main() 최초에 ota_boot_check() → 플래그 있으면 B→A 복사 후 재부팅
//   2) OTA 수신 완료 시 ota_write_boot_flag() → 재부팅 → 1)에서 적용
// =============================================================================

// 부팅 초기 (FreeRTOS 시작 전) Bank B 적용 여부 확인
// 플래그가 있으면 Bank B → Bank A 복사 후 watchdog 재부팅 (리턴 안 함)
// 플래그 없으면 즉시 리턴
void ota_boot_check(void);

// OTA 수신 완료 시 호출 — "다음 부팅에 Bank B 적용" 플래그를 플래시에 기록
// __no_inline_not_in_flash_func (RAM 실행): flash_range_erase/program 사용
void ota_write_boot_flag(void);

#endif // OTA_BOOT_H
