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
typedef int16_t H;
typedef uint16_t UH;
typedef int32_t W;
typedef int32_t INT, ID, ER, BOOL;
typedef void (*FP)(INT, void *);
typedef struct { UW lo, hi; } SYSTIM;
typedef struct { UW mbfatr; INT maxmsz, bufsz; } T_CMBF;
typedef struct { UW tskatr; FP task; INT itskpri, stksz; } T_CTSK;

static int failures, checks;
static UW clock_ms;
static UW operating_ms;
static jmp_buf idle;
static int running;
static struct { unsigned head, count, size, capacity; unsigned char data[4][256]; INT lengths[4]; } queues[3];
static int queue_count;
static ER tk_get_tim(SYSTIM *now) { now->lo = clock_ms; now->hi = 0; return E_OK; }
static ER tk_get_otm(SYSTIM *now) { now->lo = operating_ms; now->hi = 0; return E_OK; }
static ID tk_cre_mbf(const T_CMBF *config) {
    if (++queue_count > 2 || config->maxmsz > 256) abort();
    queues[queue_count].size = (unsigned)config->maxmsz;
    queues[queue_count].capacity = (unsigned)config->bufsz /
        (sizeof(INT) + (((unsigned)config->maxmsz + sizeof(INT) - 1U) / sizeof(INT)) * sizeof(INT));
    if (queues[queue_count].capacity > 4) abort();
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
    if (queues[id].count == queues[id].capacity) return E_TMOUT;
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
#include "../../../app_program/subsystem4/subsystem4.h"
static HUMP_TELEMETRY_T source_imu;
static ER imu_result = E_OBJ;
ER subsystem4_get_telemetry(HUMP_TELEMETRY_T *sample) {
    if (imu_result != E_OK) return imu_result;
    *sample = source_imu; return E_OK;
}
#include "../../../app_program/subsystem1/s1_protocol.c"
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
    operating_ms = 25; /* Separate clock domains; never mistake calendar time for uptime. */
    source_line.pattern = pattern; source_line.timestamp_ms = now;
    source_line.valid = TRUE; source_result = E_OK;
    memset(&source_imu, 0, sizeof(source_imu)); imu_result = E_OBJ;
    if (subsystem1_init() != E_OK) abort();
}
static int packet_is(const UB *bytes, INT length, UW sequence, UINT mm, UINT line)
{
    S1_TELEMETRY actual;
    if (length != (INT)S1_FRAME_BYTES) return 0;
    memcpy(actual.bytes, bytes, sizeof(actual.bytes));
    return s1_validate_telemetry(&actual) == S1_CODEC_OK &&
           s1_sequence(&actual) == sequence && s1_distance(&actual) == mm &&
           s1_line_bits(&actual) == line;
}
int main(void) {
    S1_TELEMETRY sample = {{0}};
    UB payload[S1_PAYLOAD_CAPACITY];
    int i, full, length;
    puts("Development host validation; GPIO, radio and MQTT hardware are not exercised.");
    subsystem1_test();
    (void)s1_make_telemetry(&sample, 42, 451, 1);

    reset_at(1000, 3);
    (void)s1_submit_telemetry(&sample); pump();
    length = s1_network_poll(payload, sizeof(payload));
    expect("S1-U34", length > 0 && packet_is(payload, length, 42, 451, 7),
           "real S1 queue pipeline maps both-black snapshot");

    reset_at(1000, 2);
    (void)s1_submit_telemetry(&sample); pump(); clock_ms = 1101;
    length = s1_network_poll(payload, sizeof(payload));
    expect("S1-U35", length > 0 && packet_is(payload, length, 42, 451, 0),
           "IR expires in output queue while distance remains usable");

    reset_at(1000, 3); source_result = E_TMOUT;
    (void)s1_submit_telemetry(&sample); pump();
    length = s1_network_poll(payload, sizeof(payload));
    expect("S1-U36", length > 0 && packet_is(payload, length, 42, 451, 0),
           "busy or unavailable S3 snapshot cannot reuse old bits");

    reset_at(1000, 4);
    (void)s1_submit_telemetry(&sample); pump();
    length = s1_network_poll(payload, sizeof(payload));
    expect("S1-U37", length > 0 && packet_is(payload, length, 42, 451, 0),
           "unsupported S3 pattern remains unavailable");

    reset_at(1000, 2); source_line.timestamp_ms = 899;
    (void)s1_submit_telemetry(&sample); pump();
    length = s1_network_poll(payload, sizeof(payload));
    expect("S1-U38", length > 0 && packet_is(payload, length, 42, 451, 0),
           "stale source snapshot rejected before outbound queue");

    reset_at(1000, 0);
    for (i = 0; i < 4; ++i) { (void)s1_make_telemetry(&sample, (UW)i, 451, 1); (void)s1_submit_telemetry(&sample); }
    full = s1_submit_telemetry(&sample); pump();
    length = s1_network_poll(payload, sizeof(payload));
    expect("S1-U39", full == E_TMOUT && length > 0 && packet_is(payload, length, 3, 451, 4),
           "full input is nonblocking; newest fresh all-background snapshot selected");

    reset_at(1000, 2);
    for (i = 0; i < 5; ++i) { (void)s1_make_telemetry(&sample, (UW)i, 451, 1); (void)s1_submit_telemetry(&sample); pump(); }
    length = s1_network_poll(payload, sizeof(payload));
    expect("S1-U40", length > 0 && packet_is(payload, length, 3, 451, 6),
           "full outbound queue drops new sample without blocking sensing");

    reset_at(1000, 1);
    (void)s1_submit_telemetry(&sample); clock_ms = 3001; pump();
    payload[0] = 0xa5;
    expect("S1-U41", s1_network_poll(payload, sizeof(payload)) == E_TMOUT && payload[0] == 0xa5,
           "distance expired in input queue is not published");

    reset_at(1000, 1);
    (void)s1_submit_telemetry(&sample); pump(); clock_ms = 3001;
    payload[0] = 0xa5;
    expect("S1-U42", s1_network_poll(payload, sizeof(payload)) == E_TMOUT && payload[0] == 0xa5,
           "distance expired in outbound queue is not published");

    reset_at(1000, 2); imu_result = E_OK;
    source_imu.pitch_deg_x10 = 125; source_imu.current_hump_mm = 12;
    source_imu.max_hump_mm = 20; source_imu.state = 2; source_imu.flags = 0x11;
    source_imu.timestamp_ms = source_imu.pitch_timestamp_ms = 1000;
    (void)s1_submit_telemetry(&sample); pump();
    length = s1_network_poll(payload, sizeof(payload));
    expect("S1-U56", length == 11 && (s1_read_u32(payload + 6) & 2047U) == 1026U &&
        ((s1_read_u32(payload + 6) >> 18) & 127U) == 20,
        "actual S1 pipeline forwards S4 pitch and retained maximum");

    reset_at(1000, 2); imu_result = E_OK;
    source_imu.pitch_deg_x10 = 125; source_imu.max_hump_mm = 20;
    source_imu.state = 2; source_imu.flags = 0x11;
    source_imu.timestamp_ms = source_imu.pitch_timestamp_ms = 1000;
    (void)s1_submit_telemetry(&sample); pump(); clock_ms = 1101;
    length = s1_network_poll(payload, sizeof(payload));
    expect("S1-U57", length == 11 && (s1_read_u32(payload + 6) & 2047U) == 0U &&
        ((s1_read_u32(payload + 6) >> 18) & 127U) == 20,
        "pitch expires in queue without discarding real retained height");

    reset_at(1000, 2); imu_result = E_TMOUT;
    (void)s1_submit_telemetry(&sample); pump();
    length = s1_network_poll(payload, sizeof(payload));
    expect("S1-U58", length == 11 && s1_read_u32(payload + 6) == (7U << 25),
        "busy S4 snapshot yields explicit unavailable IMU, not fabricated zeros");

    reset_at(1001, 2); imu_result = E_OK;
    source_imu.state = 2; source_imu.flags = 1;
    (void)s1_submit_telemetry(&sample); pump();
    length = s1_network_poll(payload, sizeof(payload));
    expect("S1-U59", length == 11 && s1_read_u32(payload + 6) == (7U << 25),
        "stale S4 publication cannot be refreshed by reading its getter");

    reset_at(1000, 2); imu_result = E_OK;
    source_imu.current_hump_mm = 12; source_imu.max_hump_mm = 20;
    source_imu.state = 6; source_imu.flags = 0x80; source_imu.timestamp_ms = 1000;
    (void)s1_submit_telemetry(&sample); pump();
    length = s1_network_poll(payload, sizeof(payload));
    expect("S1-U60", length == 11 && (s1_read_u32(payload + 6) & 2047U) == 0U &&
        ((s1_read_u32(payload + 6) >> 25) & 7U) == 6 && (payload[10] & 2U),
        "S4 fault travels independently from retained height and valid range/IR");

    reset_at(1000, 2); imu_result = E_OK;
    source_imu.current_hump_mm = source_imu.max_hump_mm = 99;
    source_imu.state = 2; source_imu.flags = 1; source_imu.timestamp_ms = 1000;
    (void)s1_submit_telemetry(&sample); pump();
    length = s1_network_poll(payload, sizeof(payload));
    expect("S1-U61", length == 11 && s1_read_u32(payload + 6) == (7U << 25),
        "invalid S4 values cannot leak or truncate into plausible telemetry");

    {
        UB request[5] = {0xa0,12,0,0,0}, reply[S1_STATUS_MAX_BYTES];
        reset_at(1000,2);
        length = s1_network_status(request,5,reply,sizeof(reply));
        expect("S1-U74",length==11 && s1_read_u32(reply+1)==12 && s1_read_u32(reply+5)==25 &&
            reply[9]==255 && reply[10]==15,"STATUS works before any telemetry sample exists");
        (void)s1_make_telemetry(&sample,42,451,1);
        (void)s1_submit_telemetry(&sample);pump();(void)s1_network_poll(payload,sizeof(payload));
        clock_ms=1010;length=s1_network_status(request,5,reply,sizeof(reply));
        expect("S1-U75",length==22 && reply[9]==10 && reply[10]==8 &&
            packet_is(reply+11,11,42,451,6),"STATUS returns cached real sample with source age");
        clock_ms=1101;length=s1_network_status(request,5,reply,sizeof(reply));
        full = length==22 && packet_is(reply+11,11,42,451,0);
        clock_ms=3001;length=s1_network_status(request,5,reply,sizeof(reply));
        expect("S1-U76",full && length==11 && reply[9]==255 && reply[10]==15,
            "STATUS independently expires queued IR then whole cached telemetry");
        (void)s1_submit_telemetry(&sample);full=(int)queues[1].count;reply[0]=0xa5;
        expect("S1-U77",s1_network_status((const UB *)"STOP",4,reply,sizeof(reply))==E_PAR &&
            reply[0]==0xa5 && queues[1].count==(unsigned)full,
            "unsupported commands leave queues and output untouched");
        reset_at(0xfffffff0U,2);(void)s1_submit_telemetry(&sample);pump();
        (void)s1_network_poll(payload,sizeof(payload));clock_ms=16;
        length=s1_network_status(request,5,reply,sizeof(reply));
        full=length==22 && reply[9]==32;
        reset_at(16,2);length=s1_network_status(request,5,reply,sizeof(reply));
        expect("S1-U78",full && length==11,"STATUS age survives uptime wrap and reinitialization clears cache");
    }
    printf("[HOST] queue SUMMARY %d checks, %d total failures\n", checks, failures);
    printf("Queue storage: input=%u bytes; output=%u bytes (four messages plus length headers each)\n",
           (unsigned)S1_MBF_STORAGE(sizeof(S1_INPUT)),
           (unsigned)S1_MBF_STORAGE(sizeof(S1_OUTPUT)));
    return failures ? 1 : 0;
}
