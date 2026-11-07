/* Line detection: read GP6/GP16 and share a protected, timestamped snapshot.
 * The controller in app_main.c uses it for steering and line-loss recovery. */
#include "line_detection.h"

static ID s_line_sem;
static S3_LINE_DATA s_line;

/* Public getter: copy the latest snapshot without consuming it.
 * Callers check timestamp_ms separately to decide whether it is fresh. */
EXPORT ER subsystem3_get_line(S3_LINE_DATA *sample)
{
    ER er;
    if (sample == NULL) return E_PAR;
    if (s_line_sem <= 0) return E_OBJ;
    er = tk_wai_sem(s_line_sem, 1, TMO_POL);
    if (er != E_OK) return er;
    *sample = s_line;
    (void)tk_sig_sem(s_line_sem, 1);
    return sample->valid ? E_OK : E_TMOUT;
}

/* Create the snapshot lock before any subsystem tasks start. */
ER s3_line_init(void)
{
    T_CSEM sem = {0};
    sem.sematr = TA_TFIFO | TA_FIRST;
    sem.isemcnt = sem.maxsem = 1;
    s_line.valid = FALSE;
    s_line_sem = tk_cre_sem(&sem);
    return s_line_sem < E_OK ? s_line_sem : E_OK;
}

/* GP26 remains the barcode input and is configured by subsystem3.c. */
ER s3_line_configure(void)
{
    const UINT pins[] = { S3_IR_CENTRE_PIN, S3_IR_RIGHT_PIN };
    UINT i;
    ER er;
    for (i = 0; i < sizeof(pins)/sizeof(pins[0]); ++i) {
        er = gpio_set_pin(pins[i], GPIO_MODE_IN);
        if (er != E_OK) return er;
    }
    return E_OK;
}

void s3_line_sample(S3_LINE_DATA *sample)
{
    SYSTIM now;
    /* Centre/right bits: 0=both white, 1=right black, 2=centre black,
     * 3=both black. The barcode input never contributes. */
    sample->pattern =
        (gpio_get_val(S3_IR_CENTRE_PIN) == S3_IR_BLACK_LEVEL ? 2U : 0U)
      | (gpio_get_val(S3_IR_RIGHT_PIN) == S3_IR_BLACK_LEVEL ? 1U : 0U);
    (void)tk_get_tim(&now);
    sample->timestamp_ms = (UW)now.lo;
    sample->valid = TRUE;

    /* Publish all fields together; keep the previous snapshot if busy. */
    if (tk_wai_sem(s_line_sem, 1, TMO_POL) == E_OK) {
        s_line = *sample;
        (void)tk_sig_sem(s_line_sem, 1);
    }
}
