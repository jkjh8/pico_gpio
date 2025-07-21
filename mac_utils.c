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
    pico_unique_board_id_t board_id;
    pico_get_unique_board_id(&board_id);

    // MAC 주소 뒤 3바이트만 보드 ID에서 가져옴 (WIZnet OUI 유지)
    for (int i = 0; i < 3; i++) {
        net_info->mac[3 + i] = board_id.id[PICO_UNIQUE_BOARD_ID_SIZE_BYTES - 3 + i];
    }

    print_mac_address(net_info->mac, "Final MAC Address");
}

// WIZnet 네트워크 정보 설정 함수 (MAC 주소만 설정)
void setup_wiznet_network(wiz_NetInfo *net_info) {
    printf("\n=== MAC 주소를 WIZnet에 설정 ===\n");
    
    // MAC 주소를 WIZnet 칩에 직접 설정 (SHAR 레지스터)
    setSHAR(net_info->mac);
    
    printf("MAC 주소가 WIZnet 칩에 설정되었습니다.\n");
}
