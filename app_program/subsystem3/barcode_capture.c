/* Legacy digital capture: sample S3_BARCODE_PIN using a timer interrupt and
 * queue timestamped transitions. This path is inactive with analog AO wiring. */
#include "barcode_capture.h"
#include "subsystem3.h"
#include <sys/sysdef.h>
#include <tk/syslib.h>

#define BC_IRQ 2U
#define BC_ALARM_BIT (1U << BC_IRQ)
#define BC_RING_MASK (S3_BC_EDGE_CAPACITY - 1U)

/* Producer/consumer are BOTH on core 0. Task accesses only with IRQ2
 * disabled. These volatile objects are NOT a cross-core locking scheme. */
static volatile UW s_edge_us[S3_BC_EDGE_CAPACITY];
static volatile UB s_edge_black[S3_BC_EDGE_CAPACITY];
static volatile UB s_head, s_tail, s_lost;
static volatile BOOL s_black;
static volatile UW s_sample_us;
static BOOL s_started;

static void barcode_sample_isr(UINT intno)
{
    UW now = in_w(TIMER_TIMERAWL);
    BOOL black = gpio_get_val(S3_BARCODE_PIN) == S3_BARCODE_BLACK_LEVEL;
    UB next;
    (void)intno;
    out_w(TIMER_INTR, BC_ALARM_BIT); /* W1C, only our alarm. */
    if ((UW)(now - s_sample_us) > S3_BC_MAX_SAMPLE_GAP_US) s_lost = 1;
    s_sample_us = now;
    if (black != s_black) {
        if (!s_lost) {
            next = (UB)((s_head + 1U) & BC_RING_MASK);
            if (next == s_tail) s_lost = 1;
            else {
                s_edge_us[s_head] = now;
                s_edge_black[s_head] = (UB)black;
                s_head = next;
            }
        }
        s_black = black;
    }
    /* Rearm from a fresh counter AFTER bounded work. No waits, printing,
     * decoding or kernel object calls in this interrupt. */
    out_w(TIMER_ALARM2, in_w(TIMER_TIMERAWL) + S3_BC_SAMPLE_US);
}

/* Start timer alarm 2 sampling on core 0 and return the initial input state. */
ER s3_capture_start(UW *now_us, BOOL *black)
{
    T_DINT dint = {0};
    ER er;
    if (now_us == NULL || black == NULL) return E_PAR;
    if (s_started) return E_OBJ;
#if TK_SUPPORT_SMP
    if (tk_get_prc() != 1) return E_CTX;
#endif
    if ((in_w(TIMER_ARMED) | in_w(TIMER_INTE)) & BC_ALARM_BIT) return E_OBJ;
    DisableInt(BC_IRQ);
    dint.intatr = TA_HLNG;
    dint.inthdr = (FP)barcode_sample_isr;
    er = tk_def_int(BC_IRQ, &dint);
    if (er != E_OK) return er;
    s_head = s_tail = s_lost = 0;
    s_black = gpio_get_val(S3_BARCODE_PIN) == S3_BARCODE_BLACK_LEVEL;
    s_sample_us = in_w(TIMER_TIMERAWL);
    *black = s_black;
    *now_us = s_sample_us;
    out_w(TIMER_INTR, BC_ALARM_BIT);
    ClearInt(BC_IRQ);
    s_started = TRUE;
    set_w(TIMER_INTE, BC_ALARM_BIT);
    out_w(TIMER_ALARM2, in_w(TIMER_TIMERAWL) + S3_BC_SAMPLE_US);
    EnableInt(BC_IRQ, 3);
    return E_OK;
}

/* Read the oldest transition; report empty or lost capture data without waiting. */
ER s3_capture_read(UW *now_us, BOOL *black)
{
    ER er;
    if (now_us == NULL || black == NULL) return E_PAR;
    if (!s_started) return E_OBJ;
#if TK_SUPPORT_SMP
    if (tk_get_prc() != 1) return E_CTX;
#endif
    DisableInt(BC_IRQ);
    if (s_lost) {
        s_tail = s_head;
        s_black = gpio_get_val(S3_BARCODE_PIN) == S3_BARCODE_BLACK_LEVEL;
        s_sample_us = in_w(TIMER_TIMERAWL);
        s_lost = 0;
        *black = s_black;
        *now_us = s_sample_us;
        er = E_IO;
    } else if (s_tail != s_head) {
        *now_us = s_edge_us[s_tail];
        *black = s_edge_black[s_tail];
        s_tail = (UB)((s_tail + 1U) & BC_RING_MASK);
        er = E_OK;
    } else {
        /* A newer wall time could confirm quiet before an unsampled edge;
         * return only the time through which we actually observed input. */
        *now_us = s_sample_us;
        *black = s_black;
        er = E_TMOUT;
    }
    EnableInt(BC_IRQ, 3);
    return er;
}
