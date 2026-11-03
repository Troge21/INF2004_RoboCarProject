/* S3: ONE optional line input and LEFT-sensor Code 39 acquisition. */
#include "subsystem3.h"
#include "barcode_capture.h"

static ID s_line_sem, s_barcode_mbf;
static S3_LINE_DATA s_line;

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

EXPORT ER subsystem3_get_barcode(UB *code)
{
    INT size;
    if (code == NULL) return E_PAR;
    *code = 0;
    if (s_barcode_mbf <= 0) return E_OBJ;
    size = tk_rcv_mbf(s_barcode_mbf, code, TMO_POL);
    return size == 1 ? E_OK : (size < E_OK ? size : E_IO);
}

static void barcode_result(int status, UB code, UW *rejected, UW *dropped)
{
    ER er;
    if (status == S3_BC_OK) {
        er = tk_snd_mbf(s_barcode_mbf, &code, 1, TMO_POL);
        if (er != E_OK) ++*dropped;
        if (er != E_OK && S3_IR_LOG_ENABLED)
            tm_printf((UB *)"[BARCODE] result queue full: new result dropped\n");
    } else if (status == S3_BC_INVALID || status == S3_BC_TIMEOUT) ++*rejected;
}

static void ir_task(INT stacd, void *exinf)
{
    const UINT pins[] = {
        S3_BARCODE_PIN,
#if S3_LINE_PIN >= 0
        S3_LINE_PIN,
#endif
    };
    S3_BARCODE_DECODER decoder;
    UW stamp, rejected = 0, dropped = 0, lost = 0;
    UW last_report_ms = 0;
    BOOL black;
    UINT i;
    ER er;
    (void)stacd; (void)exinf;
    for (i = 0; i < sizeof(pins)/sizeof(pins[0]); ++i) {
        er = gpio_set_pin(pins[i], GPIO_MODE_IN);
        if (er != E_OK) { CHECK(er, "IR gpio_set_pin"); tk_ext_tsk(); return; }
    }
    er = s3_capture_start(&stamp, &black);
    if (er != E_OK) { CHECK(er, "barcode timer setup"); tk_ext_tsk(); return; }
    s3_barcode_reset(&decoder, black, stamp);
    if (S3_IR_LOG_ENABLED) {
        tm_printf((UB *)"[IR] barcode=GP%u; barcode sample=%u us; black level=%u\n",
                  S3_BARCODE_PIN, S3_BC_SAMPLE_US, S3_BARCODE_BLACK_LEVEL);
#if S3_LINE_PIN >= 0
        tm_printf((UB *)"[IR] single line sensor=GP%u; 1=black; automatic driving OFF\n",
                  S3_LINE_PIN);
#else
        tm_printf((UB *)"[IR] line sensor DISABLED: wiring pin unconfirmed\n");
#endif
    }
    for (;;) {
        UB code;
        UINT pattern = 0;
        SYSTIM now;
        /* A noisy input cannot cause an unbounded drain loop. */
        for (i = 0; i < S3_BC_EDGE_CAPACITY; ++i) {
            int status;
            er = s3_capture_read(&stamp, &black);
            if (er == E_IO) {
                s3_barcode_reset(&decoder, black, stamp);
                ++lost;
                break;
            }
            if (er == E_TMOUT) {
                status = s3_barcode_poll(&decoder, stamp, &code);
                barcode_result(status, code, &rejected, &dropped);
                break;
            }
            if (er != E_OK) break;
            status = s3_barcode_edge(&decoder, black, stamp, &code);
            barcode_result(status, code, &rejected, &dropped);
        }
#if S3_LINE_PIN >= 0
        pattern = gpio_get_val(S3_LINE_PIN) == S3_IR_BLACK_LEVEL ? 1U : 0U;
#endif
        (void)tk_get_tim(&now);
        if (tk_wai_sem(s_line_sem, 1, TMO_POL) == E_OK) {
            s_line.pattern = pattern; /* LEFT/BARCODE NEVER participates. */
            s_line.timestamp_ms = (UW)now.lo;
            s_line.valid = (S3_LINE_PIN >= 0);
            (void)tk_sig_sem(s_line_sem, 1);
        }
        if (S3_IR_LOG_ENABLED && (UW)((UW)now.lo - last_report_ms) >= 5000U) {
            tm_printf((UB *)"[IR] line_valid=%u line_black=%u; barcode rejected=%u lost=%u dropped=%u\n",
                      (UINT)(S3_LINE_PIN >= 0), pattern, rejected, lost, dropped);
            last_report_ms = (UW)now.lo;
        }
        (void)tk_dly_tsk(S3_IR_PERIOD_MS);
    }
}

EXPORT ER subsystem3_init(void)
{
    T_CSEM sem = {0};
    T_CMBF mbf = {0};
    sem.sematr = TA_TFIFO | TA_FIRST;
    sem.isemcnt = sem.maxsem = 1;
    s_line.valid = FALSE;
    s_line_sem = tk_cre_sem(&sem);
    if (s_line_sem < E_OK) return s_line_sem;
    mbf.mbfatr = TA_TFIFO;
    mbf.bufsz = 64;
    mbf.maxmsz = 1;
    s_barcode_mbf = tk_cre_mbf(&mbf);
    return s_barcode_mbf < E_OK ? s_barcode_mbf : E_OK;
}

EXPORT ER subsystem3_start(void)
{
    ID tid = app_start_task((FP)ir_task, S3_PRI_IR, TP_PRC1, "ir_task");
    return tid < E_OK ? tid : E_OK;
}
