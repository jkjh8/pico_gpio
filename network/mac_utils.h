#ifndef MAC_UTILS_H
#define MAC_UTILS_H

#include <stdint.h>
#include "wizchip_conf.h"
#include "pico/unique_id.h"

// MAC 주소 관련 함수들
void print_mac_address(uint8_t *mac, const char *description);
void generate_mac_from_board_id(uint8_t *mac);
void setup_wiznet_network(wiz_NetInfo *net_info);

#endif // MAC_UTILS_H
