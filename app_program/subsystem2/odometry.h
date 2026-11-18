#ifndef S2_ODOMETRY_H
#define S2_ODOMETRY_H
#include <stdint.h>

/* Task-owned path accumulator: mm:32, fractional micrometres:10, spare:6.
 * UINT32_MAX mm means unavailable (encoder failure or range overflow).
 * Centre path integrates abs((left displacement + right displacement)/2).
 * It is distinct from both signed net travel and mean wheel speed magnitude. */
typedef struct { uint8_t bytes[6]; } S2_ODOMETER;
void s2_odometer_init(S2_ODOMETER *state);
void s2_odometer_invalidate(S2_ODOMETER *state);
uint32_t s2_odometer_mm(const S2_ODOMETER *state);
void s2_odometer_step(S2_ODOMETER *state, int32_t left_delta, int32_t right_delta,
                      uint32_t left_cpr_x10, uint32_t right_cpr_x10,
                      uint32_t diameter_mm);
#endif
