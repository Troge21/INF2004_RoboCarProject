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
        tm_printf((UB *)"[IR] barcode=GP%u; barcode sample=%u us; black level=%u\n",
                  S3_BARCODE_PIN, S3_BC_SAMPLE_US, S3_BARCODE_BLACK_LEVEL);
        tm_printf((UB *)"[IR] line centre=GP%u right=GP%u; 1=black; follow=%u; recovery=%u ms\n",
                  S3_IR_CENTRE_PIN, S3_IR_RIGHT_PIN,
                  (UINT)(APP_LINE_FOLLOW_ENABLED && !APP_TEST), APP_RECOVERY_TIMEOUT_MS);
    }
    for (;;) {
        UB code;
        S3_LINE_DATA line;
        /* Barcode scanning: process a bounded batch of captured edges,
         * handle capture loss, and pass decoder results to the queue. */
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
