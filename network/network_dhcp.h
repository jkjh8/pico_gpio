#ifndef NETWORK_DHCP_H
#define NETWORK_DHCP_H

#include <stdbool.h>
#include <stdint.h>

// Start/stop/process non-blocking DHCP
void network_dhcp_start(void);
int network_dhcp_process(void); // returns 1=leased, 0=running, -1=failed
void network_dhcp_stop(void);

#endif // NETWORK_DHCP_H
