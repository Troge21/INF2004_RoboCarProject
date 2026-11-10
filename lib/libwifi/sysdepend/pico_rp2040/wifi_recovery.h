/* Radio-owner recovery policy, independent of SDK/RTOS types for host tests. */
#ifndef WIFI_RECOVERY_H
#define WIFI_RECOVERY_H
#include <stdint.h>
#define WIFI_REJOIN_DELAY_US UINT64_C(5000000)

/* SDK link states: 0=down, negatives=failed, positives=join/DHCP/up.
 * Never disturb a join or DHCP in progress. Keep retrying even if another
 * join request fails without causing a link-state transition. */
static inline int wifi_rejoin_due(int32_t link, uint64_t now, uint64_t *deadline)
{
    if (link > 0) { *deadline = 0; return 0; }
    if (*deadline == 0) {
        *deadline = now + WIFI_REJOIN_DELAY_US;
        if (*deadline == 0) *deadline = 1;
        return 0;
    }
    if ((uint64_t)(now - *deadline) >= (UINT64_C(1) << 63)) return 0;
    *deadline = now + WIFI_REJOIN_DELAY_US;
    if (*deadline == 0) *deadline = 1;
    return 1;
}
#endif
