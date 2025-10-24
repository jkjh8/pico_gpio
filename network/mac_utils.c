// 파일 전체 삭제
// 파일 전체 삭제
#include <stdio.h>
#include "mac_utils.h"
#include "pico/unique_id.h"
#include "debug/debug.h"

// MAC 주소 출력 함수
void print_mac_address(uint8_t *mac, const char *description) {
    DBG_NET_PRINT("%s: ", description);
    for(int i = 0; i < 6; i++) {
        DBG_NET_PRINT("%02X", mac[i]);
        if(i < 5) DBG_NET_PRINT(":");
    }
    DBG_NET_PRINT("\n");
}

// 보드 고유 ID 기반 MAC 주소 생성 및 wiz_NetInfo 업데이트
// 보드 고유 ID 기반 MAC 주소 생성 및 반환 (WIZnet OUI: 00:08:DC)
void generate_mac_from_board_id(uint8_t *mac) {
    pico_unique_board_id_t board_id;
    pico_get_unique_board_id(&board_id);

    mac[0] = 0x00;
    mac[1] = 0x08;
    mac[2] = 0xDC;
    mac[3] = board_id.id[PICO_UNIQUE_BOARD_ID_SIZE_BYTES - 3];
    mac[4] = board_id.id[PICO_UNIQUE_BOARD_ID_SIZE_BYTES - 2];
    mac[5] = board_id.id[PICO_UNIQUE_BOARD_ID_SIZE_BYTES - 1];
    DBG_NET_PRINT("Generated MAC Address: %02X:%02X:%02X:%02X:%02X:%02X\n", 
        mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
}

// WIZnet 네트워크 정보 설정 함수 (MAC 주소만 설정)
void setup_wiznet_network(wiz_NetInfo *net_info) {
    DBG_WIZNET_PRINT("\n=== MAC 주소를 WIZnet에 설정 ===\n");
    
    // MAC 주소를 WIZnet 칩에 직접 설정 (SHAR 레지스터)
    setSHAR(net_info->mac);
    
    DBG_WIZNET_PRINT("MAC 주소가 WIZnet 칩에 설정되었습니다.\n");
}
