/* Read-only producer contract. These are transient API views, not wire structs.
 * Time is tk_get_tim().lo, taken at publication, never at getter time.
 * Getters must be nonblocking protected snapshots; never consume event queues.
 * Optional weak defaults return E_NOSPT. An owner's strong implementation
 * automatically enables the existing S1 codec/dashboard without S1 edits.
 */
#ifndef TELEMETRY_SOURCES_H
#define TELEMETRY_SOURCES_H
#include <stdint.h>
#define TELEMETRY_SCAN_POINTS 181U
#define TELEMETRY_SCAN_WIDTH_MAX_MM 1023U
#define TELEMETRY_UNKNOWN_U32 UINT32_MAX
typedef struct {
    uint32_t timestamp_ms;
    uint16_t event_sequence;
    uint8_t code; /* S3 Code39 byte: valid:1/reversed:1/alphabet index:6 */
    uint8_t navigation; /* 0 unmapped, 1 left, 2 right, 3 straight, 4 U-turn */
} TELEMETRY_BARCODE;
typedef struct {
    uint32_t timestamp_ms;
    uint64_t distance_mm; /* encoder-estimated centre path length, not signed net travel */
} TELEMETRY_TRAVEL;
typedef struct {
    uint32_t timestamp_ms;
    uint16_t known, active; /* only external bits 8..10; active is a subset of known */
} TELEMETRY_FAULTS;
typedef struct { uint16_t distance_mm; uint8_t angle; } TELEMETRY_SCAN_POINT;
typedef struct {
    uint32_t timestamp_ms, sequence;
    uint16_t width_mm; /* 0 when the sweep did not resolve both edges */
    uint8_t count;
    TELEMETRY_SCAN_POINT points[TELEMETRY_SCAN_POINTS];
} TELEMETRY_SCAN;
/* Unknown counters are UINT32_MAX. Never invent stack/heap high-water values.
 * free/largest refer to the same allocator; high_water is owner-measured.
 * S1 separately records the minimum free value it actually samples. */
typedef struct {
    uint32_t timestamp_ms, free_heap, largest_free_block;
    uint32_t allocated_stacks, stack_high_water;
} TELEMETRY_RESOURCES;
int telemetry_get_barcode(TELEMETRY_BARCODE *out);
int telemetry_get_travel(TELEMETRY_TRAVEL *out);
int telemetry_get_faults(TELEMETRY_FAULTS *out);
int telemetry_get_scan(TELEMETRY_SCAN *out);
int telemetry_get_resources(TELEMETRY_RESOURCES *out);
#endif
