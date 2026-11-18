#include "odometry.h"

static void store(S2_ODOMETER *s, uint32_t mm, uint16_t fraction)
{
    unsigned i;
    for (i = 0; i < 4; ++i) s->bytes[i] = (uint8_t)(mm >> (8U * i));
    s->bytes[4] = (uint8_t)fraction;
    s->bytes[5] = (uint8_t)(fraction >> 8);
}

uint32_t s2_odometer_mm(const S2_ODOMETER *s)
{
    return (uint32_t)s->bytes[0] | (uint32_t)s->bytes[1] << 8 |
           (uint32_t)s->bytes[2] << 16 | (uint32_t)s->bytes[3] << 24;
}

void s2_odometer_init(S2_ODOMETER *s) { store(s, 0, 0); }
void s2_odometer_invalidate(S2_ODOMETER *s) { store(s, UINT32_MAX, 0); }

void s2_odometer_step(S2_ODOMETER *s, int32_t left, int32_t right,
                      uint32_t left_cpr, uint32_t right_cpr, uint32_t diameter)
{
    uint32_t mm = s2_odometer_mm(s);
    uint64_t fraction, increment, total;
    int64_t left_um, right_um, centre_um;
    if (mm == UINT32_MAX) return;
    /* These bounds also keep the signed intermediate multiplication in range. */
    if (!left_cpr || !right_cpr || !diameter || diameter > 1000U) {
        s2_odometer_invalidate(s);
        return;
    }
    left_um = (int64_t)left * diameter * 355 * 10000 / (113LL * left_cpr);
    right_um = (int64_t)right * diameter * 355 * 10000 / (113LL * right_cpr);
    centre_um = (left_um + right_um) / 2;
    increment = (uint64_t)(centre_um < 0 ? -centre_um : centre_um);
    fraction = s->bytes[4] | (uint16_t)s->bytes[5] << 8;
    total = (uint64_t)mm * 1000 + fraction + increment;
    if (total / 1000 >= UINT32_MAX) s2_odometer_invalidate(s);
    else store(s, (uint32_t)(total / 1000), (uint16_t)(total % 1000));
}
