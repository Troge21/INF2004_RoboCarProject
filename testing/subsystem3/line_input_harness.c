/* Host check of the actual S3 task and snapshot API. No real hardware. */
#include <assert.h>
#include <stdint.h>
#include <stddef.h>
#include <setjmp.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#define APP_H
#define TK_SUPPORT_SMP 1
#define EXPORT
#define TRUE 1
#define FALSE 0
#define E_OK 0
#define E_PAR (-17)
#define E_OBJ (-41)
#define E_IO (-57)
#define E_TMOUT (-50)
#define TMO_POL 0
#define TA_TFIFO 0
#define TA_FIRST 0
#define TP_PRC1 1
#define GPIO_MODE_IN 1
#define CHECK(er, name) ((void)(er))
typedef unsigned UINT;
typedef int INT;
typedef uint32_t UW;
typedef unsigned char UB;
typedef unsigned BOOL;
typedef int ER;
typedef int ID;
typedef int PRI;
typedef void (*FP)(void);
typedef struct { UW lo; } SYSTIM;
typedef struct { unsigned sematr; unsigned isemcnt; unsigned maxsem; } T_CSEM;
typedef struct { unsigned mbfatr; unsigned bufsz; unsigned maxmsz; } T_CMBF;
static jmp_buf exit_task;
static UW configured, reads;
static UINT line_level, barcode_level;
static UINT queued;
static UB published;
static ER gpio_set_pin(UINT pin, UINT mode) {
    assert(mode == GPIO_MODE_IN);
    configured |= 1U << pin;
    return E_OK;
}
UINT gpio_get_val(UINT pin) { reads |= 1U << pin; return pin == 6 ? ((line_level >> 1) & 1U) :
    (pin == 16 ? (line_level & 1U) : barcode_level); }
static ER tk_wai_sem(ID id, unsigned count, int timeout) {
    assert(id == 1 && count == 1 && timeout == TMO_POL); return E_OK;
}
static ER tk_sig_sem(ID id, unsigned count) { assert(id == 1 && count == 1); return E_OK; }
static ID tk_cre_sem(const T_CSEM *s) { assert(s->isemcnt == 1); return 1; }
static ID tk_cre_mbf(const T_CMBF *m) { assert(m->bufsz == 64 && m->maxmsz == 1); return 2; }
static int tk_rcv_mbf(ID id, void *data, int timeout) {
    (void)data; assert(id == 2 && timeout == TMO_POL); return E_TMOUT;
}
static ER tk_snd_mbf(ID id, const void *data, int size, int timeout) {
    assert(id == 2 && size == 1 && timeout == TMO_POL);
    ++queued; published = *(const UB *)data; return E_OK;
}
static ER tk_get_tim(SYSTIM *now) { now->lo = 20; return E_OK; }
static ER tk_dly_tsk(int ms) { assert(ms == 20); longjmp(exit_task, 1); return E_OK; }
static void tk_ext_tsk(void) { assert(0 && "Task startup failed"); }
static char trace[8192];
static size_t trace_size;
static void tm_printf(UB *format, ...) {
    va_list args; int length;
    va_start(args, format);
    length = vsnprintf(trace + trace_size, sizeof(trace) - trace_size,
                       (const char *)format, args);
    va_end(args);
    assert(length >= 0 && (size_t)length < sizeof(trace) - trace_size);
    trace_size += (size_t)length;
}
static ID app_start_task(FP entry, PRI priority, UW core, const char *name) {
    (void)entry; (void)name; assert(priority == 17 && core == TP_PRC1); return 3;
}
ER s3_capture_start(UW *time, BOOL *black) { *time=0; *black=barcode_level; return E_OK; }
ER s3_capture_read(UW *time, BOOL *black) { *time=1000; *black=barcode_level; return E_TMOUT; }

/* SUBSYSTEM_SOURCE */

static void run_one_iteration(void) {
    configured = reads = 0;
    trace_size = 0; trace[0] = '\0';
    if (setjmp(exit_task) == 0) ir_task(0, NULL);
    assert(strstr(trace, "trace=BC-TRACE-1") != NULL);
}

static UW trace_frame(S3_BARCODE_DECODER *decoder, UW *rejected, UW *dropped,
                       BOOL valid) {
    const unsigned patterns[3] = {0x094U, 0x109U, 0x094U}; /* *A* */
    UW now = 1000000U;
    trace_size = 0; trace[0] = '\0';
    s3_barcode_reset(decoder, 0, 0);
    barcode_process(decoder, now, 1, TRUE, rejected, dropped);
    for (unsigned i=0; i<S3_BC_RUNS; ++i) {
        unsigned offset=i%10U;
        now += valid && offset<9U && (patterns[i/10U] & (1U << (8U-offset)))
               ? 15000U : 5000U;
        barcode_process(decoder, now, i%2U, TRUE, rejected, dropped);
    }
    return now;
}

static void check_barcode_trace(void) {
    S3_BARCODE_DECODER decoder;
    UW rejected=0, dropped=0, now;
    now=trace_frame(&decoder,&rejected,&dropped,FALSE);
    assert(rejected==1 && queued==0);
    assert(strstr(trace,"REJECT INVALID_FRAME; runs=29/29; leading_white_us=1000000") != NULL);
    assert(strstr(trace,"runs_us=5000,5000,") != NULL);

    now=trace_frame(&decoder,&rejected,&dropped,TRUE);
    assert(decoder.pending && rejected==1);
    barcode_process(&decoder,now+20000U,1,TRUE,&rejected,&dropped);
    assert(rejected==2 && queued==0);
    assert(strstr(trace,"REJECT TRAILING_WHITE_INTERRUPTED; runs=29/29") != NULL);
    assert(strstr(trace,"trailing_needed_us=50000") != NULL);

    trace_size=0; trace[0]='\0';
    s3_barcode_reset(&decoder,0,0);
    barcode_process(&decoder,1000000U,1,TRUE,&rejected,&dropped);
    barcode_process(&decoder,1005000U,0,TRUE,&rejected,&dropped);
    barcode_process(&decoder,1005000U+S3_BC_MAX_RUN_US+1U,0,FALSE,&rejected,&dropped);
    assert(rejected==3 && decoder.count==0);
    assert(strstr(trace,"REJECT RUN_TIMEOUT; runs=1/29") != NULL);
    {
        char expected[48];
        snprintf(expected,sizeof(expected),"run_us=%u",S3_BC_MAX_RUN_US+1U);
        assert(strstr(trace,expected) != NULL);
    }
    assert(strstr(trace,"runs_us=5000\n") != NULL);

    /* A timeout at an edge can simultaneously start a fresh candidate.
     * The diagnostic must describe the discarded frame, not that new one. */
    {
        UW edge_rejected=0, edge_dropped=0;
        trace_size=0; trace[0]='\0';
        s3_barcode_reset(&decoder,0,0);
        barcode_process(&decoder,1000000U,1,TRUE,&edge_rejected,&edge_dropped);
        barcode_process(&decoder,1005000U,0,TRUE,&edge_rejected,&edge_dropped);
        barcode_process(&decoder,1005000U+S3_BC_MAX_RUN_US+1U,1,TRUE,&edge_rejected,&edge_dropped);
        assert(edge_rejected==1 && decoder.capturing &&
               decoder.leading_white_us==S3_BC_MAX_RUN_US+1U);
        assert(strstr(trace,"runs=1/29; leading_white_us=1000000") != NULL);
    }

    trace_size=0; trace[0]='\0';
    s3_barcode_reset(&decoder,0,0);
    now=1000000U;
    barcode_process(&decoder,now,1,TRUE,&rejected,&dropped);
    for (unsigned i=0;i<26U;++i) {
        now+=300000U;
        barcode_process(&decoder,now,i%2U,TRUE,&rejected,&dropped);
    }
    barcode_process(&decoder,now+300000U,1,FALSE,&rejected,&dropped);
    assert(rejected==4 && queued==0);
    assert(strstr(trace,"REJECT FRAME_TIMEOUT; runs=26/29") != NULL);

    now=trace_frame(&decoder,&rejected,&dropped,TRUE);
    barcode_process(&decoder,now+50000U,0,FALSE,&rejected,&dropped);
    barcode_process(&decoder,now+100000U,0,FALSE,&rejected,&dropped);
    assert(queued==1 && published==(S3_BC_VALID | 10U));
    assert(rejected==4 && dropped==0 && strstr(trace,"REJECT")==NULL);
}
int main(void) {
    S3_LINE_DATA line = {0};
    assert(subsystem3_init() == E_OK);
    assert(subsystem3_start() == E_OK);
    assert(subsystem3_get_line(NULL) == E_PAR);
    for (barcode_level = 0; barcode_level <= 1; ++barcode_level) {
        for (line_level = 0; line_level <= 3; ++line_level) {
            run_one_iteration();
            assert(configured == ((1U << 26) | (1U << 6) | (1U << 16)));
            assert(reads == ((1U << 6) | (1U << 16)));
            assert(subsystem3_get_line(&line) == E_OK);
            assert(line.valid && line.pattern == line_level && line.timestamp_ms == 20);
            assert(queued==0);
        }
    }
    check_barcode_trace();
    return 0;
}
