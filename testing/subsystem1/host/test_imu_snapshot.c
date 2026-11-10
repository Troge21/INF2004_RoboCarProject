/* Offline S1 integration checks against actual S4 source. Kernel/driver are
 * stubbed; no claims about I2C, physical pitch accuracy or estimator accuracy. */
#include <stdint.h>
#include <stdio.h>
#include <stdarg.h>
#include <setjmp.h>
#include <string.h>
#include <stdlib.h>
#define APP_H
#define APP_TEST 0
#define EXPORT
#define IMPORT extern
#define TRUE 1
#define FALSE 0
#define E_OK 0
#define E_PAR (-17)
#define E_OBJ (-41)
#define E_IO (-57)
#define E_TMOUT (-50)
#define TMO_POL 0
#define TMO_FEVR (-1)
#define TA_TFIFO 1
#define TA_FIRST 2
#define TP_PRC1 1
typedef uint8_t UB;
typedef int16_t H;
typedef uint16_t UH;
typedef int32_t W, INT, ID, ER, BOOL;
typedef uint32_t UW, UINT;
typedef void (*FP)(INT, void *);
typedef struct { UW lo, hi; } SYSTIM;
typedef struct { UINT sematr; INT isemcnt, maxsem; } T_CSEM;
static uint32_t clock_ms = 1000;
static int locked, failures, checks, owner_running, loops;
static jmp_buf stop_owner;
static ER tk_get_tim(SYSTIM *now) { now->lo=clock_ms; now->hi=0; return E_OK; }
static ID tk_cre_sem(const T_CSEM *config) { (void)config; locked=0; return 1; }
static ER tk_wai_sem(ID id, INT count, INT timeout) {
    (void)timeout;
    if(id!=1 || count!=1) return E_OBJ;
    if(locked) return E_TMOUT;
    locked=1; return E_OK;
}
static ER tk_sig_sem(ID id, INT count) {
    if(id!=1 || count!=1 || !locked) abort();
    locked=0; return E_OK;
}
static ER tk_dly_tsk(INT ms) {
    if(locked) abort();
    clock_ms+=(uint32_t)ms;
    if(owner_running && ms==20 && --loops==0) longjmp(stop_owner,1);
    return E_OK;
}
static ID app_start_task(FP fn, UINT priority, UINT core, const char *name) {
    (void)fn;(void)priority;(void)core;(void)name;return 1;
}
static INT tm_printf(UB *format, ...) { (void)format; return 0; }
#define CHECK(er, label) do { (void)(label); if((er)!=E_OK) ++failures; } while(0)
#include "../../../app_program/subsystem4/subsystem4.h"
static ER accel_result=E_OK;
static int16_t requested_pitch=125;
ER drv_gy511_init(void) { if(locked) abort(); return E_OK; }
ER drv_gy511_read_accel(GY511_ACCEL_DATA_T *accel) {
    if(locked) abort();
    accel->x_mg=0;accel->y_mg=(int16_t)-requested_pitch;accel->z_mg=1000;
    return accel_result;
}
int16_t drv_gy511_calc_pitch_x10(const GY511_ACCEL_DATA_T *accel) {
    return accel->x_mg; /* Controlled scalar stub, not the trigonometry test. */
}
#include "../../../app_program/subsystem4/subsystem4.c"
static void expect(int condition,const char *description) {
    ++checks;printf("[S4 SNAPSHOT] %s %s\n",condition?"PASS":"FAIL",description);
    if(!condition) ++failures;
}
static void run_owner(int count) {
    loops=count;owner_running=1;
    if(setjmp(stop_owner)==0) imu_hump_task(0,NULL);
    owner_running=0;
}
int main(void) {
    HUMP_TELEMETRY_T sample;
    expect(subsystem4_get_telemetry(NULL)==E_PAR &&
        subsystem4_get_telemetry(&sample)==E_OBJ,"getter rejects null and uninitialized access");
    expect(subsystem4_init()==E_OK && sizeof(s_snapshot)==18 &&
        subsystem4_get_telemetry(&sample)==E_OK && sample.state==0 && sample.flags==0,
        "packed published snapshot starts explicitly initializing without a valid angle");
    s_state=S4_STATE_MONITORING;s_flags=S4_FLAG_SENSOR_OK;s_baseline_pitch_x10=7;
    run_owner(1);
    (void)subsystem4_get_telemetry(&sample);
    expect(sample.pitch_deg_x10==125 && sample.baseline_pitch_x10==7 &&
        (sample.flags&S4_FLAG_PITCH_VALID),"real owner loop publishes the live angle rather than the baseline");
    expect(sample.timestamp_ms==1000 && sample.pitch_timestamp_ms==1000 && clock_ms==1020,
        "publication and sensor timestamps are original source times");
    clock_ms=1101;(void)subsystem4_get_telemetry(&sample);
    expect(!(sample.flags&S4_FLAG_PITCH_VALID) && sample.timestamp_ms==1000,
        "getter invalidates an aged pitch without making its timestamp fresh");
    locked=1;sample.timestamp_ms=123;
    expect(subsystem4_get_telemetry(&sample)==E_TMOUT && sample.timestamp_ms==123,
        "busy snapshot returns immediately and leaves caller data unchanged");locked=0;
    s_current_hump_mm=12;s_max_hump_mm=20;s_state=S4_STATE_FAULT;
    publish_snapshot(125,1101,1101,TRUE);(void)subsystem4_get_telemetry(&sample);
    expect(sample.state==6 && (sample.flags&S4_FLAG_FAULT) && !(sample.flags&S4_FLAG_SENSOR_OK) &&
        !(sample.flags&S4_FLAG_PITCH_VALID) && sample.current_hump_mm==12 && sample.max_hump_mm==20,
        "fault status clears live pitch but preserves accepted heights and run maximum");
    expect(subsystem4_reset_run()==E_OK && subsystem4_get_telemetry(&sample)==E_OK &&
        sample.current_hump_mm==0 && sample.max_hump_mm==0 && s_max_hump_mm==20,
        "explicit reset is immediately visible without cross-task estimator writes");
    publish_snapshot(0,0,1101,FALSE);
    expect(s_max_hump_mm==0 && s_accumulated_height_um==0 && !(s_requests & 1U),
        "owner applies the pending run reset under snapshot synchronization");
    subsystem4_set_speed_mm_s(0);
    expect(s_vehicle_speed_mm_s==S4_DEFAULT_SPEED_MM_S,
        "external speed update waits for the owning task");
    s_state=S4_STATE_MONITORING;s_flags=S4_FLAG_SENSOR_OK;
    publish_snapshot(0,1101,1101,TRUE);(void)subsystem4_get_telemetry(&sample);
    expect(s_vehicle_speed_mm_s==0 && (sample.flags&S4_FLAG_SPEED_SUPPLIED),
        "zero measured speed is accepted and distinguished from nominal speed");
    s_baseline_pitch_x10=-900;s_current_hump_mm=s_max_hump_mm=80;
    s_last_traversal_duration_ms=0xffffffffU;clock_ms=0xffffffefU;
    publish_snapshot(900,clock_ms,clock_ms,TRUE);(void)subsystem4_get_telemetry(&sample);
    expect(sample.pitch_deg_x10==900 && sample.baseline_pitch_x10==-900 &&
        sample.current_hump_mm==80 && sample.max_hump_mm==80 &&
        sample.traversal_time_ms==0xffffffffU && sample.timestamp_ms==clock_ms,
        "packed snapshot retains signed boundaries and full-width timestamp/duration values");
    clock_ms=16;(void)subsystem4_get_telemetry(&sample);
    expect(sample.flags&S4_FLAG_PITCH_VALID,"pitch freshness survives 32-bit uptime wrap");
    clock_ms=4000;s_baseline_pitch_x10=0;s_state=S4_STATE_MONITORING;s_flags=S4_FLAG_SENSOR_OK;
    accel_result=E_IO;run_owner(1);(void)subsystem4_get_telemetry(&sample);
    expect(sample.state==6 && (sample.flags&S4_FLAG_FAULT) && !(sample.flags&S4_FLAG_PITCH_VALID),
        "real read failure publishes fault in the same loop");
    accel_result=E_OK;run_owner(1);(void)subsystem4_get_telemetry(&sample);
    expect(sample.state==1 && !(sample.flags&S4_FLAG_PITCH_VALID) && sample.max_hump_mm==80,
        "recovery enters calibration while retaining run maximum");
    run_owner(50);(void)subsystem4_get_telemetry(&sample);
    expect(sample.state==2 && sample.baseline_pitch_x10==125 && sample.pitch_deg_x10==125 &&
        (sample.flags&S4_FLAG_PITCH_VALID),"50 successful calibration samples enable a real live pitch");
    printf("[S4 SNAPSHOT] SUMMARY %d checks, %d failures\n",checks,failures);
    return failures?1:0;
}
