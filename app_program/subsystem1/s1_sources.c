#include "../app.h"
#include "../telemetry_sources.h"
#include <tk/syslib.h>
#include "subsystem1.h"
/* These defaults explicitly publish nothing. Owners replace individual getters
 * with strong functions using exactly the declared contract. */
#define EMPTY_SOURCE(name,type) \
    __attribute__((weak)) int name(type *out) { return out ? E_NOSPT : E_PAR; }
EMPTY_SOURCE(telemetry_get_barcode, TELEMETRY_BARCODE)
EMPTY_SOURCE(telemetry_get_travel, TELEMETRY_TRAVEL)
EMPTY_SOURCE(telemetry_get_faults, TELEMETRY_FAULTS)
EMPTY_SOURCE(telemetry_get_scan, TELEMETRY_SCAN)
__attribute__((weak)) int telemetry_get_resources(TELEMETRY_RESOURCES *out)
{
    SYSTIM now={0};UW free_bytes,largest;
    if(!out)return E_PAR;
    out->free_heap=out->largest_free_block=out->allocated_stacks=
        out->stack_high_water=UINT32_MAX;
#if TK_SUPPORT_MEMLIB
    if(Kmeminfo(&free_bytes,&largest)==E_OK) {
        out->free_heap=free_bytes;out->largest_free_block=largest;
    }
#else
    (void)free_bytes;(void)largest;
#endif
    out->allocated_stacks=s1_resource_stack_capacity();
    (void)tk_get_tim(&now);out->timestamp_ms=now.lo;
    return E_OK;
}
