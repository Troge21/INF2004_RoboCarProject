/* S3 coordinates line readings and barcode scanning in one sensor task.
 * Line steering and recovery are handled by the controller in app_main.c. */
#include "subsystem3.h"
#include "barcode_capture.h"
#include "line_detection.h"

static ID s_barcode_mbf;

/* Barcode results: expose decoded events and queue new successful results. */
EXPORT ER subsystem3_get_barcode(UB *code)
{
    INT size;
    if (code == NULL) return E_PAR;
    *code = 0;
    if (s_barcode_mbf <= 0) return E_OBJ;
    size = tk_rcv_mbf(s_barcode_mbf, code, TMO_POL);
    return size == 1 ? E_OK : (size < E_OK ? size : E_IO);
}

/* Save diagnostic timing before the decoder advances/reset its frame.
 * All printing stays in this task; capture and decoding rules are unchanged. */
static void barcode_process(S3_BARCODE_DECODER *decoder, UW stamp, BOOL black,
                            BOOL is_edge, UW *rejected, UW *dropped)
{
    UB code;
    UINT count = decoder->count, i;
    UW run_us = stamp - decoder->last_edge_us;
    UW frame_us = stamp - decoder->frame_start_us;
    UW leading_us = decoder->leading_white_us;
    BOOL pending = decoder->pending;
    UW trailing_us = pending ? decoder->quiet_needed_us : 0;
    int status = is_edge ? s3_barcode_edge(decoder, black, stamp, &code)
                         : s3_barcode_poll(decoder, stamp, &code);
    ER er;
    if (status == S3_BC_OK) {
        er = tk_snd_mbf(s_barcode_mbf, &code, 1, TMO_POL);
        if (er != E_OK) ++*dropped;
        if (er != E_OK && S3_IR_LOG_ENABLED)
            tm_printf((UB *)"[BARCODE] result queue full: new result dropped\n");
    } else if (status == S3_BC_INVALID || status == S3_BC_TIMEOUT) {
        const char *reason;
        ++*rejected;
        if (!S3_IR_LOG_ENABLED) return;
        if (status == S3_BC_TIMEOUT)
            reason = run_us > S3_BC_MAX_RUN_US ? "RUN_TIMEOUT" : "FRAME_TIMEOUT";
        else {
            count = decoder->count;
            reason = pending ? "TRAILING_WHITE_INTERRUPTED" : "INVALID_FRAME";
        }
        tm_printf((UB *)"[BARCODE] REJECT %s; runs=%u/%u; leading_white_us=%u\n",
                  reason, count, S3_BC_RUNS, leading_us);
        tm_printf((UB *)"[BARCODE] timing run_us=%u frame_us=%u trailing_needed_us=%u\n",
                  run_us, frame_us, trailing_us);
        tm_printf((UB *)"[BARCODE] runs_us=");
        for (i = 0; i < count && i < S3_BC_RUNS; ++i)
            tm_printf((UB *)"%s%u", i == 0U ? "" : ",", decoder->widths[i]);
        tm_printf((UB *)"\n");
    }
}

static void ir_task(INT stacd, void *exinf)
{
    S3_BARCODE_DECODER decoder;
    UW stamp, rejected = 0, dropped = 0, lost = 0;
    UW last_report_ms = 0;
    BOOL black;
    UINT i;
    ER er;
    (void)stacd; (void)exinf;
    /* Sensor startup: configure inputs, start capture, reset the decoder. */
    er = gpio_set_pin(S3_BARCODE_PIN, GPIO_MODE_IN);
    if (er != E_OK) { CHECK(er, "IR gpio_set_pin"); tk_ext_tsk(); return; }
    er = s3_line_configure();
    if (er != E_OK) { CHECK(er, "IR gpio_set_pin"); tk_ext_tsk(); return; }
    er = s3_capture_start(&stamp, &black);
    if (er != E_OK) { CHECK(er, "barcode timer setup"); tk_ext_tsk(); return; }
    s3_barcode_reset(&decoder, black, stamp);
    if (S3_IR_LOG_ENABLED) {
        tm_printf((UB *)"[BARCODE] trace=BC-TRACE-1; max_run_us=%u frame_timeout_us=%u\n",
                  S3_BC_MAX_RUN_US, S3_BC_FRAME_TIMEOUT_US);
        tm_printf((UB *)"[IR] barcode=GP%u; barcode sample=%u us; black level=%u\n",
                  S3_BARCODE_PIN, S3_BC_SAMPLE_US, S3_BARCODE_BLACK_LEVEL);
        tm_printf((UB *)"[IR] line centre=GP%u right=GP%u; 1=black; follow=%u; recovery=%u ms\n",
                  S3_IR_CENTRE_PIN, S3_IR_RIGHT_PIN,
                  (UINT)(APP_LINE_FOLLOW_ENABLED && !APP_TEST), APP_RECOVERY_TIMEOUT_MS);
    }
    for (;;) {
        S3_LINE_DATA line;
        /* Barcode scanning: process a bounded batch of captured edges,
         * handle capture loss, and pass decoder results to the queue. */
        for (i = 0; i < S3_BC_EDGE_CAPACITY; ++i) {
            er = s3_capture_read(&stamp, &black);
            if (er == E_IO) {
                s3_barcode_reset(&decoder, black, stamp);
                ++lost;
                if (S3_IR_LOG_ENABLED)
                    tm_printf((UB *)"[BARCODE] CAPTURE_LOST; frame discarded\n");
                break;
            }
            if (er == E_TMOUT) {
                barcode_process(&decoder, stamp, black, FALSE, &rejected, &dropped);
                break;
            }
            if (er != E_OK) break;
            barcode_process(&decoder, stamp, black, TRUE, &rejected, &dropped);
        }
        /* Line detection: read and publish the centre/right sensor pattern. */
        s3_line_sample(&line);
        if (S3_IR_LOG_ENABLED && (UW)(line.timestamp_ms - last_report_ms) >= 5000U) {
            tm_printf((UB *)"[IR] line C=%u R=%u pattern=%u; barcode rejected=%u lost=%u dropped=%u\n",
                      (line.pattern >> 1) & 1U, line.pattern & 1U, line.pattern,
                      rejected, lost, dropped);
            last_report_ms = line.timestamp_ms;
        }
        (void)tk_dly_tsk(S3_IR_PERIOD_MS);
    }
}

/* Lifecycle: prepare the line lock and barcode queue before tasks start. */
EXPORT ER subsystem3_init(void)
{
    T_CMBF mbf = {0};
    ER er = s3_line_init();
    if (er != E_OK) return er;
    mbf.mbfatr = TA_TFIFO;
    mbf.bufsz = 64;
    mbf.maxmsz = 1;
    s_barcode_mbf = tk_cre_mbf(&mbf);
    return s_barcode_mbf < E_OK ? s_barcode_mbf : E_OK;
}

/* Start the existing S3 sensor task on core 0. */
EXPORT ER subsystem3_start(void)
{
    ID tid = app_start_task((FP)ir_task, S3_PRI_IR, TP_PRC1, "ir_task");
    return tid < E_OK ? tid : E_OK;
}
