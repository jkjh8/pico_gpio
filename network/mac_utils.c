// 파일 전체 삭제
// 파일 전체 삭제
#include <stdio.h>
#include "mac_utils.h"
#include "pico/unique_id.h"

// MAC 주소 출력 함수
void print_mac_address(uint8_t *mac, const char *description) {
    printf("%s: ", description);
    for(int i = 0; i < 6; i++) {
        printf("%02X", mac[i]);
        if(i < 5) printf(":");
    }
    printf("\n");
}

// 보드 고유 ID 기반 MAC 주소 생성 및 wiz_NetInfo 업데이트
void generate_mac_from_board_id(wiz_NetInfo *net_info) {
    // Pico의 고유 보드 ID 읽기
    pico_unique_board_id_t board_id;
    pico_get_unique_board_id(&board_id);
    
    printf("=== 보드 고유 ID 기반 MAC 주소 생성 ===\n");
    
    printf("Board Unique ID: ");
    for(int i = 0; i < PICO_UNIQUE_BOARD_ID_SIZE_BYTES; i++) {
        printf("%02X", board_id.id[i]);
        if(i < PICO_UNIQUE_BOARD_ID_SIZE_BYTES - 1) printf(":");
    }
    printf("\n");
    
    // wiz_NetInfo 구조체의 MAC 주소 업데이트 (WIZnet OUI는 유지)
    net_info->mac[3] = board_id.id[PICO_UNIQUE_BOARD_ID_SIZE_BYTES - 3];
    net_info->mac[4] = board_id.id[PICO_UNIQUE_BOARD_ID_SIZE_BYTES - 2];
    net_info->mac[5] = board_id.id[PICO_UNIQUE_BOARD_ID_SIZE_BYTES - 1];
    
    printf("Generated MAC suffix: %02X:%02X:%02X\n", 
           net_info->mac[3], net_info->mac[4], net_info->mac[5]);
    
    print_mac_address(net_info->mac, "Final MAC Address");
}

// WIZnet 네트워크 정보 설정 함수 (MAC 주소만 설정)
void setup_wiznet_network(wiz_NetInfo *net_info) {
    printf("\n=== MAC 주소를 WIZnet에 설정 ===\n");
    
    // MAC 주소를 WIZnet 칩에 직접 설정 (SHAR 레지스터)
    setSHAR(net_info->mac);
    
    printf("MAC 주소가 WIZnet 칩에 설정되었습니다.\n");
}
