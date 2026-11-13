/* Host register/IRQ model. test_barcode.py inserts the actual capture source
 * at the marker below. This does not simulate RP2040 interrupt latency. */
#include <assert.h>
#include <stdint.h>
#include <stddef.h>
#define APP_H
#define TK_SUPPORT_SMP 1
typedef unsigned UINT;
typedef uint32_t UW;
typedef unsigned char UB;
typedef unsigned BOOL;
typedef int ER;
typedef int ID;
typedef void (*FP)(void);
typedef struct { unsigned intatr; FP inthdr; } T_DINT;
#define EXPORT
#define TRUE 1
#define FALSE 0
#define E_OK 0
#define E_PAR (-17)
#define E_OBJ (-41)
#define E_CTX (-25)
#define E_IO (-57)
#define E_TMOUT (-50)
#define TA_HLNG 1
#include "subsystem3.h"
enum { TIMER_TIMERAWL, TIMER_INTR, TIMER_ALARM2, TIMER_ARMED, TIMER_INTE };
static UW registers[5];
static UINT level, core = 1, irq_enabled;
static void (*irq_handler)(UINT);
static UW in_w(UINT reg) { return registers[reg]; }
static void out_w(UINT reg, UW value) {
    if (reg == TIMER_INTR) registers[reg] &= ~value;
    else registers[reg] = value;
    if (reg == TIMER_ALARM2) registers[TIMER_ARMED] |= 4U;
}
static void set_w(UINT reg, UW value) { registers[reg] |= value; }
static UINT gpio_get_val(UINT pin) { assert(pin == S3_BARCODE_PIN); return level; }
static void DisableInt(UINT irq) { assert(irq == 2); irq_enabled = 0; }
static void EnableInt(UINT irq, int priority) {
    assert(irq == 2 && priority == 3); irq_enabled = 1;
}
static void ClearInt(UINT irq) { assert(irq == 2); }
static UINT tk_get_prc(void) { return core; }
static ER tk_def_int(UINT irq, const T_DINT *dint) {
    assert(irq == 2);
    irq_handler = (void (*)(UINT))dint->inthdr;
    return E_OK;
}

/* CAPTURE_SOURCE */

static void sample(UINT black, UW gap) {
    assert(irq_enabled);
    registers[TIMER_TIMERAWL] += gap;
    registers[TIMER_INTR] |= 4U;
    level = black;
    irq_handler(2);
    assert(!(registers[TIMER_INTR] & 4U));
    assert(registers[TIMER_ALARM2] == registers[TIMER_TIMERAWL] + S3_BC_SAMPLE_US);
}

int main(void) {
    UW now;
    BOOL black;
    assert(s3_capture_read(&now, &black) == E_OBJ);
    assert(s3_capture_start(NULL, &black) == E_PAR);
    core = 2;
    assert(s3_capture_start(&now, &black) == E_CTX);
    core = 1;
    registers[TIMER_ARMED] = 4U;
    assert(s3_capture_start(&now, &black) == E_OBJ);
    registers[TIMER_ARMED] = 0;
    registers[TIMER_INTE] = 1U; /* someone else's alarm must survive */
    registers[TIMER_INTR] = 1U;
    assert(s3_capture_start(&now, &black) == E_OK);
    assert(registers[TIMER_INTE] == 5U && registers[TIMER_INTR] == 1U);
    assert(s3_capture_start(&now, &black) == E_OBJ);
    assert(s3_capture_read(&now, &black) == E_TMOUT && now == 0 && black == 0);
    sample(1, 500);
    assert(s3_capture_read(&now, &black) == E_OK && now == 500 && black == 1);
    sample(1, 500);
    assert(s3_capture_read(&now, &black) == E_TMOUT && now == 1000 && black == 1);
    sample(0, 500);
    assert(s3_capture_read(&now, &black) == E_OK && now == 1500 && black == 0);
    /* Overflow discards EVERY queued edge and resynchronises actual input. */
    for (unsigned i = 0; i < 70; ++i) sample((i+1U)&1U, 500);
    assert(s3_capture_read(&now, &black) == E_IO && black == level);
    assert(s3_capture_read(&now, &black) == E_TMOUT);
    sample(!level, 500);
    assert(s3_capture_read(&now, &black) == E_OK);
    /* A scheduling gap rejects the capture even if the level did not change. */
    sample(level, 2001);
    assert(s3_capture_read(&now, &black) == E_IO);
    assert(s3_capture_read(&now, &black) == E_TMOUT);
    core = 2;
    assert(s3_capture_read(&now, &black) == E_CTX);
    core = 1;
    registers[TIMER_TIMERAWL] = UINT32_MAX - 200;
    s_sample_us = registers[TIMER_TIMERAWL];
    sample(!level, 500);
    assert(s3_capture_read(&now, &black) == E_OK && now == 299);
    assert(registers[TIMER_INTE] == 5U && registers[TIMER_INTR] == 1U);
    return 0;
}
