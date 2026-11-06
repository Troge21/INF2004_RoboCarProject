/* Host-only checks of the actual S1 sources. No sockets or live publications.
 * Substitute the kernel queues, clock, console and S3 snapshot interface.
 * Windows: gcc -std=c11 -Wall -Wextra -Werror -I testing/subsystem1/host/include
 *          testing/subsystem1/host/test_telemetry.c -o <temporary-executable>
 */
#include <stdint.h>
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <setjmp.h>
#include <stdlib.h>

/* Skip platform app.h, but compile the real S1/S3 public headers below. */
#define APP_H
#define APP_TEST 1
#define TM_MQTT 0
#define TM_WIFI_CYW43 0
#define IMPORT extern
#define EXPORT
#define TRUE 1
#define FALSE 0
#define E_OK 0
#define E_PAR (-17)
#define E_OBJ (-41)
#define E_TMOUT (-50)
#define TMO_POL 0
#define TA_TFIFO 1
#define TA_HLNG 2
#define TA_RNG0 4
#define TASK_STACK_SIZE 2048
#define APP_IR_MAX_AGE_MS 100U
typedef uint32_t UW, UINT;
typedef uint8_t UB;
typedef int32_t INT, ID, ER, BOOL;
typedef void (*FP)(INT, void *);
typedef struct { UW lo, hi; } SYSTIM;
typedef struct { UW mbfatr; INT maxmsz, bufsz; } T_CMBF;
typedef struct { UW tskatr; FP task; INT itskpri, stksz; } T_CTSK;

static int failures, checks;
static UW clock_ms;
static jmp_buf idle;
static int running;
static struct { unsigned head, count, size; unsigned char data[4][256]; INT lengths[4]; } queues[3];
static int queue_count;
static ER tk_get_tim(SYSTIM *now) { now->lo = clock_ms; now->hi = 0; return E_OK; }
static ID tk_cre_mbf(const T_CMBF *config) {
    if (++queue_count > 2 || config->maxmsz > 256) abort();
    queues[queue_count].size = (unsigned)config->maxmsz;
    return queue_count;
}
static ER tk_del_mbf(ID id) { (void)id; return E_OK; }
static ID tk_cre_tsk(const T_CTSK *config) { (void)config; return 1; }
static ER tk_sta_tsk(ID id, INT code) { (void)id; (void)code; return E_OK; }
static ER tk_dly_tsk(INT ms) { (void)ms; return E_OK; }
static ER tk_snd_mbf(ID id, const void *msg, INT length, INT timeout) {
    unsigned tail;
    (void)timeout;
    if (id < 1 || id > queue_count) return E_OBJ;
    if (length < 0 || (unsigned)length > queues[id].size) return E_PAR;
    if (queues[id].count == 4) return E_TMOUT;
    tail = (queues[id].head + queues[id].count) % 4;
    memcpy(queues[id].data[tail], msg, (size_t)length);
    queues[id].lengths[tail] = length;
    ++queues[id].count;
    return E_OK;
}
static INT tk_rcv_mbf(ID id, void *msg, INT timeout) {
    INT length;
    (void)timeout;
    if (id < 1 || id > queue_count) return E_OBJ;
    if (queues[id].count == 0) {
        if (running && id == 1) longjmp(idle, 1);
        return E_TMOUT;
    }
    length = queues[id].lengths[queues[id].head];
    memcpy(msg, queues[id].data[queues[id].head], (size_t)length);
    queues[id].head = (queues[id].head + 1) % 4;
    --queues[id].count;
    return length;
}
static INT tm_sprintf(UB *out, const UB *format, ...) {
    int result; va_list args; va_start(args, format);
    result = vsprintf((char *)out, (const char *)format, args);
    va_end(args); return result;
}
static INT tm_printf(UB *format, ...) {
    char text[1024]; int result; va_list args; va_start(args, format);
    result = vsnprintf(text, sizeof(text), (const char *)format, args);
    va_end(args); fputs(text, stdout);
    if (strstr(text, " FAIL ")) ++failures;
    return result;
}
#define CHECK(er, label) do { if ((er) != E_OK) { fprintf(stderr, "%s failed\n", label); ++failures; } } while (0)

#include "../../../app_program/subsystem3/subsystem3.h"
static S3_LINE_DATA source_line;
static ER source_result = E_OK;
ER subsystem3_get_line(S3_LINE_DATA *sample) { *sample = source_line; return source_result; }
#include "../../../app_program/subsystem1/subsystem1.c"
#include "../../../app_program/subsystem1/subsystem1_test.c"

static void expect(const char *id, int condition, const char *description) {
    ++checks;
    printf("[HOST] %s %s %s\n", id, condition ? "PASS" : "FAIL", description);
    if (!condition) ++failures;
}
static void pump(void) {
    running = 1;
    if (setjmp(idle) == 0) wifi_status_task(0, NULL);
    running = 0;
}
static void reset_at(UW now, UINT pattern) {
    memset(queues, 0, sizeof(queues)); queue_count = 0; clock_ms = now;
    source_line.pattern = pattern; source_line.timestamp_ms = now;
    source_line.valid = TRUE; source_result = E_OK;
    if (subsystem1_init() != E_OK) abort();
}
int main(void) {
    S1_TELEMETRY sample = {42, 451, 1};
    char payload[S1_PAYLOAD_CAPACITY];
    int i, full, length;
    puts("Development host validation; GPIO, radio and MQTT hardware are not exercised.");
    subsystem1_test();

    reset_at(1000, 3);
    (void)s1_submit_telemetry(&sample); pump();
    length = s1_network_poll(payload, sizeof(payload));
    expect("S1-U34", length > 0 && strstr(payload, "\"line_bits\":7}"),
           "real S1 queue pipeline maps both-black snapshot");

    reset_at(1000, 2);
    (void)s1_submit_telemetry(&sample); pump(); clock_ms = 1101;
    length = s1_network_poll(payload, sizeof(payload));
    expect("S1-U35", length > 0 && strstr(payload, "\"obstacle_mm\":451") && strstr(payload, "\"line_bits\":0}"),
           "IR expires in output queue while distance remains usable");

    reset_at(1000, 3); source_result = E_TMOUT;
    (void)s1_submit_telemetry(&sample); pump();
    length = s1_network_poll(payload, sizeof(payload));
    expect("S1-U36", length > 0 && strstr(payload, "\"line_bits\":0}"),
           "busy or unavailable S3 snapshot cannot reuse old bits");

    reset_at(1000, 4);
    (void)s1_submit_telemetry(&sample); pump();
    length = s1_network_poll(payload, sizeof(payload));
    expect("S1-U37", length > 0 && strstr(payload, "\"line_bits\":0}"),
           "unsupported S3 pattern remains unavailable");

    reset_at(1000, 2); source_line.timestamp_ms = 899;
    (void)s1_submit_telemetry(&sample); pump();
    length = s1_network_poll(payload, sizeof(payload));
    expect("S1-U38", length > 0 && strstr(payload, "\"line_bits\":0}"),
           "stale source snapshot rejected before outbound queue");

    reset_at(1000, 0);
    for (i = 0; i < 4; ++i) { sample.sequence = (UW)i; (void)s1_submit_telemetry(&sample); }
    full = s1_submit_telemetry(&sample); pump();
    length = s1_network_poll(payload, sizeof(payload));
    expect("S1-U39", full == E_TMOUT && length > 0 && strstr(payload, "\"sequence\":3,") && strstr(payload, "\"line_bits\":4}"),
           "full input is nonblocking; newest fresh all-background snapshot selected");

    reset_at(1000, 2);
    for (i = 0; i < 5; ++i) { sample.sequence = (UW)i; (void)s1_submit_telemetry(&sample); pump(); }
    length = s1_network_poll(payload, sizeof(payload));
    expect("S1-U40", length > 0 && strstr(payload, "\"sequence\":3,"),
           "full outbound queue drops new sample without blocking sensing");

    reset_at(1000, 1);
    (void)s1_submit_telemetry(&sample); clock_ms = 3001; pump();
    expect("S1-U41", s1_network_poll(payload, sizeof(payload)) == E_TMOUT && payload[0] == '\0',
           "distance expired in input queue is not published");

    reset_at(1000, 1);
    (void)s1_submit_telemetry(&sample); pump(); clock_ms = 3001;
    expect("S1-U42", s1_network_poll(payload, sizeof(payload)) == E_TMOUT && payload[0] == '\0',
           "distance expired in outbound queue is not published");

    printf("[HOST] queue SUMMARY %d checks, %d total failures\n", checks, failures);
    printf("Queue storage: input=%u bytes; output=%u bytes (four messages plus length headers each)\n",
           (unsigned)(4 * (sizeof(S1_INPUT) + sizeof(INT))),
           (unsigned)(4 * (sizeof(S1_OUTPUT) + sizeof(INT))));
    return failures ? 1 : 0;
}
