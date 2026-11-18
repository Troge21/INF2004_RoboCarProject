#include <assert.h>
#include <stdio.h>
#include "odometry.h"

int main(void)
{
    S2_ODOMETER s;
    unsigned i;
    assert(sizeof(s)==6);
    s2_odometer_init(&s);
    assert(s2_odometer_mm(&s)==0);
    s2_odometer_step(&s,0,0,1000,1000,65);
    assert(s2_odometer_mm(&s)==0);
    s2_odometer_step(&s,100,100,1000,1000,65);
    assert(s2_odometer_mm(&s)==204);
    s2_odometer_step(&s,-100,-100,1000,1000,65);
    assert(s2_odometer_mm(&s)==408); /* reverse adds path, net is now zero */
    s2_odometer_step(&s,100,-100,1000,1000,65);
    assert(s2_odometer_mm(&s)==408); /* ideal pivot has no centre translation */
    s2_odometer_init(&s);
    s2_odometer_step(&s,100,0,1000,1000,65);
    assert(s2_odometer_mm(&s)==102); /* one wheel: half circumference */
    s2_odometer_init(&s);
    for(i=0;i<100;i++)s2_odometer_step(&s,1,1,1000,1000,65);
    assert(s2_odometer_mm(&s)==204); /* keep fractions across slow samples */
    s2_odometer_init(&s);
    s2_odometer_step(&s,6337,5409,6337,5409,65);
    assert(s2_odometer_mm(&s)==2042); /* ten rotations, unequal calibrated CPR */
    s2_odometer_invalidate(&s);
    s2_odometer_step(&s,1,1,1000,1000,65);
    assert(s2_odometer_mm(&s)==UINT32_MAX);
    for(i=0;i<4;i++){
        s2_odometer_init(&s);
        s2_odometer_step(&s,1,1,i==0?0:1000,i==1?0:1000,i==2?0:1001);
        assert(s2_odometer_mm(&s)==UINT32_MAX);
    }
    s2_odometer_init(&s);
    s2_odometer_step(&s,INT32_MIN,INT32_MAX,1,1,1000);
    assert(s2_odometer_mm(&s)==15707); /* extreme mixed signs, safe int64 arithmetic */
    s2_odometer_init(&s);
    s2_odometer_step(&s,INT32_MIN,INT32_MIN,1,1,1000);
    assert(s2_odometer_mm(&s)==UINT32_MAX); /* overflow never wraps */
    s2_odometer_init(&s);
    assert(s2_odometer_mm(&s)==0);
    puts("PASS: 17 odometer checks: zero, forward/reverse, pivot, one wheel, fractions, calibration, faults and overflow.");
}
