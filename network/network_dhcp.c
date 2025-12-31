#include "network_dhcp.h"
#include "network_config.h" // for w5500_check_link_status etc.
#include "main.h" // for g_ethernet_buf extern

#include "dhcp.h"

// --- DHCP state machine (non-blocking) ---
// Uses socket 0 for DHCP (UDP port 68)
static int dhcp_socket = -1;
static int dhcp_state = 0; // 0=idle,1=running
static uint32_t dhcp_start_time = 0;
static uint32_t dhcp_last_tick_ms = 0;

extern uint8_t g_ethernet_buf[2048];

void network_dhcp_start(void) {
    if (dhcp_state == 1) return; // already running
    // Only close DHCP socket (0) to avoid interfering with HTTP or other services
    if (getSn_SR(0) != SOCK_CLOSED) {
        close(0);
    }
    sleep_ms(50);
    dhcp_socket = socket(0, Sn_MR_UDP, 68, 0);
    if (dhcp_socket != 0) {
        // if failed to open as 0, try to close any and mark failed (-1)
        dhcp_socket = -1;
        dhcp_state = 0;
        return;
    }
    DHCP_init(0, g_ethernet_buf);
    dhcp_state = 1;
    dhcp_start_time = to_ms_since_boot(get_absolute_time());
    DBG_DHCP_PRINT("Started on socket 0, start_time=%u\n", (unsigned)dhcp_start_time);
}

int network_dhcp_process(void) {
    if (dhcp_state == 0) return 0; // not started
    // check physical link
    uint8_t phy_status = getPHYCFGR();
    if (!(phy_status & PHYCFGR_LNK_ON)) {
        // link down: stop DHCP and report failure
        if (dhcp_socket >= 0) close(0);
        dhcp_socket = -1;
        dhcp_state = 0;
        return -1;
    }

    // Ensure DHCP internal 1s tick handler runs
    uint32_t now = to_ms_since_boot(get_absolute_time());
    if ((now - dhcp_last_tick_ms) >= 1000) {
        DHCP_time_handler();
        dhcp_last_tick_ms = now;
    }

    uint8_t dhcp_status = DHCP_run();
    DBG_DHCP_PRINT("DHCP_run() -> %u\n", dhcp_status);
    switch (dhcp_status) {
        case DHCP_IP_LEASED: {
            wiz_NetInfo info;
            getIPfromDHCP(info.ip);
            getGWfromDHCP(info.gw);
            getSNfromDHCP(info.sn);
            getDNSfromDHCP(info.dns);
            info.dhcp = NETINFO_DHCP;
            // Preserve existing MAC in g_net_info; update network fields only.
            memcpy(g_net_info.ip, info.ip, 4);
            memcpy(g_net_info.gw, info.gw, 4);
            memcpy(g_net_info.sn, info.sn, 4);
            memcpy(g_net_info.dns, info.dns, 4);
            g_net_info.dhcp = NETINFO_DHCP;
         DBG_DHCP_PRINT("Lease obtained; applying to W5500 and preserving MAC %02X:%02X:%02X:%02X:%02X:%02X\n",
                   g_net_info.mac[0], g_net_info.mac[1], g_net_info.mac[2],
                   g_net_info.mac[3], g_net_info.mac[4], g_net_info.mac[5]);
            // Apply to W5500 registers
            wizchip_setnetinfo(&g_net_info);
            if (dhcp_socket >= 0) close(0);
            dhcp_socket = -1;
            dhcp_state = 0;
            return 1;
        }
        case DHCP_FAILED: {
            // attempt a retry by reinitializing DHCP; keep elapsed time
            if (dhcp_socket >= 0) close(0);
            dhcp_socket = socket(0, Sn_MR_UDP, 68, 0);
            if (dhcp_socket == 0) {
                DHCP_init(0, g_ethernet_buf);
            } else {
                // failed to reopen
                dhcp_state = 0;
                dhcp_socket = -1;
                return -1;
            }
            break;
        }
        default:
            break;
    }

    // timeout after 20 seconds (빠른 DHCP를 위해 60초에서 단축)
    if ((now - dhcp_start_time) > 20000) {
        DBG_DHCP_PRINT("DHCP timeout (20s), stopping\n");
        if (dhcp_socket >= 0) close(0);
        dhcp_socket = -1;
        dhcp_state = 0;
        return -1;
    }
    return 0;
}

void network_dhcp_stop(void) {
    if (dhcp_socket >= 0) close(0);
    dhcp_socket = -1;
    dhcp_state = 0;
}
