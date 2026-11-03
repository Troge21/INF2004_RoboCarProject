/* Host check of the actual S3 task and snapshot API. No real hardware. */
#include <assert.h>
#include <stdint.h>
#include <stddef.h>
#include <setjmp.h>
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
static UINT line_level;
static ER gpio_set_pin(UINT pin, UINT mode) {
    assert(mode == GPIO_MODE_IN);
    configured |= 1U << pin;
    return E_OK;
}
UINT gpio_get_val(UINT pin) { reads |= 1U << pin; return line_level; }
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
    (void)id; (void)data; (void)size; (void)timeout;
    assert(0 && "No synthetic barcode should be published by this input test"); return E_IO;
}
static ER tk_get_tim(SYSTIM *now) { now->lo = 20; return E_OK; }
static ER tk_dly_tsk(int ms) { assert(ms == 20); longjmp(exit_task, 1); return E_OK; }
static void tk_ext_tsk(void) { assert(0 && "Task startup failed"); }
static void tm_printf(UB *format, ...) { (void)format; }
static ID app_start_task(FP entry, PRI priority, UW core, const char *name) {
    (void)entry; (void)name; assert(priority == 17 && core == TP_PRC1); return 3;
}
ER s3_capture_start(UW *time, BOOL *black) { *time=0; *black=0; return E_OK; }
ER s3_capture_read(UW *time, BOOL *black) { *time=1000; *black=0; return E_TMOUT; }

/* SUBSYSTEM_SOURCE */

static void run_one_iteration(void) {
    configured = reads = 0;
    if (setjmp(exit_task) == 0) ir_task(0, NULL);
}
int main(void) {
    S3_LINE_DATA line = {0};
    assert(subsystem3_init() == E_OK);
    assert(subsystem3_start() == E_OK);
    assert(subsystem3_get_line(NULL) == E_PAR);
    for (line_level = 0; line_level <= 1; ++line_level) {
        run_one_iteration();
#if S3_LINE_PIN >= 0
        assert(configured == ((1U << 26) | (1U << S3_LINE_PIN)));
        assert(reads == (1U << S3_LINE_PIN));
        assert(subsystem3_get_line(&line) == E_OK);
        assert(line.valid && line.pattern == line_level && line.timestamp_ms == 20);
#else
        assert(configured == (1U << 26));
        assert(reads == 0);
        assert(subsystem3_get_line(&line) == E_TMOUT);
        assert(!line.valid && line.pattern == 0);
#endif
    }
    return 0;
}
