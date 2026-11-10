/* Actual retry policy, synthetic times only; no radio or live network. */
#include <stdint.h>
#include <stdio.h>
#include "../../../lib/libwifi/sysdepend/pico_rp2040/wifi_recovery.h"
static unsigned checks,failures;
static void expect(int ok,const char *name) {
    ++checks;if(!ok)++failures;
    printf("[WiFi recovery] %s %s\n",ok?"PASS":"FAIL",name);
}
int main(void) {
    uint64_t deadline=0;
    expect(!wifi_rejoin_due(0,100,&deadline)&&deadline==5000100,"DOWN arms five-second delay");
    expect(!wifi_rejoin_due(0,5000099,&deadline),"DOWN does not retry early");
    expect(wifi_rejoin_due(0,5000100,&deadline)&&deadline==10000100,"DOWN retries at exact boundary");
    expect(wifi_rejoin_due(0,10000100,&deadline),"unchanged DOWN retries again without a state transition");
    for(int link=-1;link>=-3;--link) {
        deadline=0;
        expect(!wifi_rejoin_due(link,100,&deadline)&&wifi_rejoin_due(link,5000100,&deadline),
            "each failed-link status retries after backoff");
    }
    for(int link=1;link<=3;++link) {
        deadline=123;
        expect(!wifi_rejoin_due(link,99999999,&deadline)&&deadline==0,
            "JOIN, DHCP and UP cancel retry without interrupting progress");
    }
    expect(!wifi_rejoin_due(0,100000000,&deadline)&&deadline==105000000,
        "a new loss after UP gets a new full delay");
    deadline=0;
    expect(!wifi_rejoin_due(0,UINT64_MAX-100,&deadline)&&
        !wifi_rejoin_due(0,4999898,&deadline)&&wifi_rejoin_due(0,4999899,&deadline),
        "deadline comparison survives uint64 clock wrap");
    deadline=0;
    expect(!wifi_rejoin_due(0,UINT64_MAX-4999999,&deadline)&&deadline==1&&
        !wifi_rejoin_due(0,0,&deadline)&&wifi_rejoin_due(0,1,&deadline),
        "wrapped zero deadline cannot collide with unarmed sentinel");
    printf("[WiFi recovery] SUMMARY %u checks, %u failures\n",checks,failures);
    return failures?1:0;
}
