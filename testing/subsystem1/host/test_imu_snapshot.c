/* Offline S1 integration checks against actual S4 source. Kernel/driver are
 * stubbed; no claims about I2C, physical pitch accuracy or estimator accuracy. */
#include <stdint.h>
#include <stdio.h>
#include <stdarg.h>
#include <setjmp.h>
#include <string.h>
#include <stdlib.h>
#include <config.h>
#define APP_H
#ifndef APP_TEST
#define APP_TEST 0
#endif
#ifndef APP_S4_MOTOR_TRACE
#define APP_S4_MOTOR_TRACE 0
#endif
#if APP_TEST || APP_S4_MOTOR_TRACE
#define TM_CONSOLE_USB_CDC 1
#endif
#define APP_IR_MAX_AGE_MS 100U
#define EXPORT
#define IMPORT extern
#define TRUE 1
#define FALSE 0
#define E_OK 0
#define E_PAR (-17)
#define E_OBJ (-41)
#define E_IO (-57)
#define E_TMOUT (-50)
#define E_BUSY (-65)
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
#if APP_TEST || APP_S4_MOTOR_TRACE
static UW usb_state, usb_pending;
static unsigned trace_rows;
static char last_trace[180];
static const char *trace_prefix=APP_S4_MOTOR_TRACE&&!APP_TEST?"[S4 MOTOR TRACE] ":"[S4 TRACE] ";
UW tm_usb_state(void) { return usb_state; }
UW tm_usb_pending(void) { return usb_pending; }
#endif
static ER tk_get_tim(SYSTIM *now) { now->lo=clock_ms; now->hi=0; return E_OK; }
static ER tk_get_otm(SYSTIM *now) { now->lo=clock_ms-1000U; now->hi=0; return E_OK; }
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
    if(ms>0) {
        /* Relative timeout adds a tick, then the timer wakes on a tick boundary. */
        clock_ms+=((uint32_t)(ms+CNF_TIMER_PERIOD-1)/CNF_TIMER_PERIOD+1U)*CNF_TIMER_PERIOD;
    }
    if(owner_running && ms<=20 && --loops==0) longjmp(stop_owner,1);
    return E_OK;
}
static ID app_start_task(FP fn, UINT priority, UINT core, const char *name) {
    (void)fn;(void)priority;(void)core;(void)name;return 1;
}
static INT tm_printf(UB *format, ...) {
#if APP_TEST || APP_S4_MOTOR_TRACE
    if(strncmp((char*)format,trace_prefix,strlen(trace_prefix))==0) {
        va_list args;
        va_start(args,format);
        vsnprintf(last_trace,sizeof(last_trace),(char*)format,args);
        va_end(args);
        ++trace_rows;
    }
#else
    (void)format;
#endif
    return 0;
}
#define CHECK(er, label) do { (void)(label); if((er)!=E_OK) ++failures; } while(0)
#include "../../../app_program/subsystem4/subsystem4.h"
#include "../../../app_program/subsystem2/subsystem2.h"
#if APP_TEST || APP_S4_MOTOR_TRACE
#include "../../../app_program/subsystem3/subsystem3.h"
ER subsystem3_get_line(S3_LINE_DATA *line) {
    if(locked) abort();
    *line=(S3_LINE_DATA){.timestamp_ms=clock_ms,.valid=TRUE,.centre_adc=820,.right_adc=1500};
    return E_OK;
}
#endif
static ER accel_result=E_OK;
static int16_t requested_pitch=125;
static unsigned reads, busy_prefix, busy_after, change_at;
static unsigned tilted_samples, skip_at, pickups, pickup_read;
static GY511_ACCEL_DATA_T tilted;
static int motor_moving;
ER drv_gy511_init(void) { if(locked) abort(); return E_OK; }
ER drv_gy511_read_accel(GY511_SAMPLE_T *sample) {
    if(locked) abort();
    ++reads;
    if (reads<=busy_prefix || (busy_after && reads>busy_after) || reads==skip_at) return E_BUSY;
    sample->raw=(GY511_ACCEL_DATA_T){0,(int16_t)-requested_pitch,1000};
    if(change_at&&reads>=change_at) sample->raw.y_mg-=60;
    if(reads>S4_CALIBRATION_SAMPLES && reads<=S4_CALIBRATION_SAMPLES+tilted_samples)
        sample->raw=tilted;
    sample->filtered=sample->raw;
    return accel_result;
}
ER subsystem2_get_motion(S2_MOTION_DATA *motion) {
    if(locked) abort();
    *motion=(S2_MOTION_DATA){.timestamp_ms=clock_ms,.speed_timestamp_ms=clock_ms,
        .encoder_valid=TRUE,.speed_valid=TRUE,.actual_left_rpm_x10=motor_moving?350:0,
        .actual_right_rpm_x10=motor_moving?360:0,
        .command_active=motor_moving,.duty_left_x100=motor_moving?880:0,.duty_right_x100=motor_moving?910:0,
        .count_left=-31,.count_right=47};
    return E_OK;
}
int16_t drv_gy511_calc_pitch_x10(const GY511_ACCEL_DATA_T *accel) {
    return accel->x_mg; /* Controlled scalar stub, not the trigonometry test. */
}
void app_controller_report_pickup(void) {
    if(locked) abort();
    ++pickups;
    pickup_read=reads;
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
static void run_pickup_trace(GY511_ACCEL_DATA_T vector, unsigned samples, unsigned missed_sample) {
    reads=pickups=0;
    tilted=vector;
    tilted_samples=samples;
    skip_at=missed_sample;
    s_state=S4_STATE_INITIALIZING;
    run_owner(1+S4_CALIBRATION_SAMPLES+samples+2);
}
static void test_gentle_pickup(void) {
    const GY511_ACCEL_DATA_T level={0,0,1000};
    const GY511_ACCEL_DATA_T rising={0,0,1100};
    const GY511_ACCEL_DATA_T tilted={0,-438,899};
    S4_PICKUP_PROGRESS progress={0};
    const uint32_t start=UINT32_MAX-40U;
    BOOL confirmed=FALSE;

    for(unsigned ms=0;ms<=60;ms+=20)
        confirmed|=pickup_update(&progress,&level,&rising,start+ms);
    expect(!confirmed&&progress.lift_phase==S4_LIFT_ARMED,
        "a sustained upward pulse arms the gentle route across uptime wrap");
    for(unsigned ms=80;ms<240;ms+=20)
        confirmed|=pickup_update(&progress,&level,&tilted,start+ms);
    expect(!confirmed&&pickup_update(&progress,&level,&tilted,start+240),
        "a moderate tilt needs the full confirmation time after a rise");

    progress=(S4_PICKUP_PROGRESS){0};
    confirmed=FALSE;
    for(unsigned ms=0;ms<=40;ms+=20)
        confirmed|=pickup_update(&progress,&level,&rising,ms);
    for(unsigned ms=60;ms<=300;ms+=20)
        confirmed|=pickup_update(&progress,&level,&tilted,ms);
    expect(!confirmed,"a short upward pulse followed by moderate tilt leaves STOP clear");

    progress=(S4_PICKUP_PROGRESS){0};
    for(unsigned ms=0;ms<=60;ms+=20)
        (void)pickup_update(&progress,&level,&rising,ms);
    for(unsigned ms=80;ms<=980;ms+=20)
        confirmed|=pickup_update(&progress,&level,&level,ms);
    for(unsigned ms=1000;ms<=1240;ms+=20)
        confirmed|=pickup_update(&progress,&level,&tilted,ms);
    expect(!confirmed,"a rise alone and a tilt outside the confirmation window leave STOP clear");
}
static void terrain_samples(S2_MOTION_DATA *motion, uint32_t *now, int pitch, int roll,
                            unsigned count, unsigned step_x10) {
    s_roll_x10=(int16_t)roll;
    for (unsigned i=0;i<count;++i) {
        *now += 20U;
        motion->distance_left_mm_x10 += step_x10;
        motion->distance_right_mm_x10 += step_x10;
        terrain_update(motion,(int16_t)pitch,*now,20);
    }
}
static void test_terrain(void) {
    S2_MOTION_DATA wheel={0};
    uint32_t now=UINT32_MAX-200U;
    s_state=S4_STATE_MONITORING;
    s_flags=S4_FLAG_SENSOR_OK;
    s_current_hump_mm=s_max_hump_mm=0;
    s_hump_event=0;
    s_turn_code=256;
    cancel_hump();
    terrain_samples(&wheel,&now,0,0,5,20);
    terrain_samples(&wheel,&now,115,0,50,20);
    terrain_samples(&wheel,&now,-55,0,15,20);
    terrain_samples(&wheel,&now,0,0,12,20);
    expect(s_current_hump_mm==20 && s_max_hump_mm==20 && s_hump_kind==S4_HUMP_FULL_WIDTH &&
        s_hump_event==1 && (s_flags&S4_FLAG_ENCODER_HEIGHT),
        "pitch plus 100 mm of measured travel records a 20 mm full-width hump across time wrap");
    for (int sign=-1;sign<=1;sign+=2) {
        terrain_samples(&wheel,&now,0,0,5,20);
        terrain_samples(&wheel,&now,0,sign*55,30,20);
        terrain_samples(&wheel,&now,0,0,40,20);
        expect(s_current_hump_mm>=9 && s_current_hump_mm<=11 && s_max_hump_mm==20 &&
            s_hump_kind==(sign<0?S4_HUMP_RIGHT:S4_HUMP_LEFT),
            "side tilt records the correct wheel and preserves the maximum");
    }
    expect(s_hump_event==3,"same-height crossings receive separate event sequences");
    terrain_samples(&wheel,&now,0,0,5,0);
    terrain_samples(&wheel,&now,100,80,30,0);
    terrain_samples(&wheel,&now,-100,0,15,0);
    terrain_samples(&wheel,&now,0,0,20,0);
    expect(s_hump_event==3 && !(s_flags&S4_FLAG_HUMP_ACTIVE),
        "hand tilt at a fixed wheel position cannot produce a measured height");
    terrain_samples(&wheel,&now,0,0,5,20);
    terrain_samples(&wheel,&now,115,0,20,20);
    now+=20;
    terrain_update(NULL,115,now,20);
    terrain_samples(&wheel,&now,-55,0,15,20);
    terrain_samples(&wheel,&now,0,0,12,20);
    expect(s_hump_event==3 && s_max_hump_mm==20,
        "loss of wheel feedback cancels a candidate and preserves accepted heights");
    terrain_samples(&wheel,&now,115,0,20,20);
    HUMP_TELEMETRY_T snapshot;
    publish_snapshot(100,clock_ms,clock_ms,TRUE);
    expect(subsystem4_reset_run()==E_OK && subsystem4_get_telemetry(&snapshot)==E_OK &&
        !snapshot.current_hump_mm && !snapshot.max_hump_mm &&
        !(snapshot.flags&S4_FLAG_HUMP_ACTIVE),"new-run reset immediately masks an active candidate");
    publish_snapshot(100,clock_ms,clock_ms,TRUE);
    terrain_samples(&wheel,&now,115,0,30,20);
    terrain_samples(&wheel,&now,-55,0,15,20);
    terrain_samples(&wheel,&now,0,0,12,20);
    expect(s_current_hump_mm==0 && s_max_hump_mm==0 && s_hump_event==3,
        "the rest of a pre-reset hump cannot populate the new run");
}
static void test_stop_at_ramp_exit(void) {
    S2_MOTION_DATA wheel={0};
    uint32_t now=0;
    cancel_hump();
    s_state=S4_STATE_MONITORING;
    s_flags=S4_FLAG_SENSOR_OK;
    s_current_hump_mm=s_max_hump_mm=0;
    s_hump_event=0;
    s_turn_code=256;
    terrain_samples(&wheel,&now,0,0,5,20);
    terrain_samples(&wheel,&now,0,-55,30,20);
    terrain_samples(&wheel,&now,0,-25,4,20);
    terrain_samples(&wheel,&now,0,-25,8,0);
    expect(s_hump_event==0,"a stop while still on the ramp cannot complete a crossing");
    terrain_samples(&wheel,&now,0,-10,1,20);
    terrain_samples(&wheel,&now,0,0,16,0);
    expect(s_hump_event==1 && s_hump_kind==S4_HUMP_RIGHT && s_current_hump_mm>=9 &&
        s_current_hump_mm<=11,"returning to level completes a right-wheel crossing even if encoder travel stops");

    terrain_samples(&wheel,&now,0,0,5,20);
    terrain_samples(&wheel,&now,0,55,30,20);
    for (unsigned i=0;i<12;++i) {
        terrain_samples(&wheel,&now,32,18,1,20);
        terrain_samples(&wheel,&now,-32,-18,1,20);
    }
    expect(s_hump_event==2 && s_hump_kind==S4_HUMP_LEFT && s_current_hump_mm>=9 &&
        s_current_hump_mm<=11,"alternating vibration on the flat still allows one completed crossing");
}
static void test_slow_right_hump(void) {
    S2_MOTION_DATA wheel={0};
    uint32_t now=0;
    cancel_hump();
    s_state=S4_STATE_MONITORING;
    s_flags=S4_FLAG_SENSOR_OK;
    s_current_hump_mm=s_max_hump_mm=0;
    s_hump_event=0;
    s_turn_code=256;
    terrain_samples(&wheel,&now,0,0,12,6);
    for (unsigned i=1;i<=325;++i) {
        unsigned slope=i<=162?i:325-i;
        terrain_samples(&wheel,&now,22,-(int)(55*slope/162),1,6);
    }
    expect(s_hump_event==1 && s_last_traversal_duration_ms>S4_MAX_TRAVERSAL_MS &&
        s_hump_kind==S4_HUMP_RIGHT && s_current_hump_mm>=9 && s_current_hump_mm<=11,
        "a slow 195 mm right hump can finish despite residual pitch and the old time limit");

    terrain_samples(&wheel,&now,0,0,16,20);
    terrain_samples(&wheel,&now,0,-55,10,20);
    terrain_samples(&wheel,&now,0,0,24,20);
    expect(s_hump_event==1,"a brief right tilt without sustained roll over distance is rejected");
}
static void test_motion_classes(void) {
    s_state=S4_STATE_MONITORING;
    S4_MOTION_PROGRESS motion={0};
    S2_MOTION_DATA wheel={.speed_valid=TRUE,.speed_timestamp_ms=100};
    update_wheel_motion(&motion,&wheel,100);
    expect(motion.wheel_class==S4_MOTION_STATIONARY && s_turn_code==256,
        "valid stopped encoders report stationary and zero turn rate");
    wheel.speed_timestamp_ms=300;wheel.speed_centre_mm_s_x10=1000;
    wheel.actual_left_rpm_x10=wheel.actual_right_rpm_x10=300;
    update_wheel_motion(&motion,&wheel,300);
    expect(motion.wheel_class==S4_MOTION_ACCELERATING,"increasing speed reports acceleration");
    wheel.speed_timestamp_ms=500;wheel.actual_left_rpm_x10=-350;wheel.actual_right_rpm_x10=350;
    update_wheel_motion(&motion,&wheel,500);
    expect(motion.wheel_class==S4_MOTION_LEFT && s_turn_code==369,
        "opposed wheels report a 113 degree/s encoder estimate to the left");
    update_wheel_motion(&motion,&wheel,1001);
    expect(motion.wheel_class==S4_MOTION_UNAVAILABLE && !s_turn_code,"stale speed removes yaw and motion");
    report_motion(&motion,FALSE,1010,2400000,1000000);
    expect(s_motion_class==S4_MOTION_IMPACT,"a brief impact produces a bounded motion event");
    report_motion(&motion,FALSE,2010,1000000,1000000);
    expect(s_motion_class==S4_MOTION_UNAVAILABLE,"the impact event expires");
    report_motion(&motion,TRUE,2030,2400000,1000000);
    expect(s_motion_class==S4_MOTION_PICKUP,"confirmed pickup takes priority in the event report");
}
int main(void) {
    HUMP_TELEMETRY_T sample;
    expect(subsystem4_get_telemetry(NULL)==E_PAR &&
        subsystem4_get_telemetry(&sample)==E_OBJ,"getter rejects null and uninitialized access");
    expect(subsystem4_init()==E_OK && sizeof(s_snapshot)==24 &&
        subsystem4_get_telemetry(&sample)==E_OK && sample.state==0 && sample.flags==0,
        "packed published snapshot starts explicitly initializing without a valid angle");
    s_state=S4_STATE_MONITORING;s_flags=S4_FLAG_SENSOR_OK;s_baseline_pitch_x10=7;
    run_owner(1);
    (void)subsystem4_get_telemetry(&sample);
    expect(sample.pitch_deg_x10==125 && sample.baseline_pitch_x10==7 &&
        (sample.flags&S4_FLAG_PITCH_VALID),"real owner loop publishes the live angle rather than the baseline");
    expect(sample.timestamp_ms==1000 && sample.pitch_timestamp_ms==1000,
        "publication and sensor timestamps are original source times");
    expect(clock_ms==1020,"sample delay compensates for the kernel's extra tick");
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
    s_state=S4_STATE_MONITORING;s_flags=S4_FLAG_SENSOR_OK;
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

    s_state=S4_STATE_INITIALIZING;reads=0;busy_prefix=4;
    run_owner(54);(void)subsystem4_get_telemetry(&sample);
    expect(sample.state==S4_STATE_CALIBRATING&&!(sample.flags&S4_FLAG_PITCH_VALID),
        "data-not-ready polls do not count as calibration samples");
    s_state=S4_STATE_INITIALIZING;reads=0;
    run_owner(55);(void)subsystem4_get_telemetry(&sample);
    expect(sample.state==S4_STATE_MONITORING&&(sample.flags&S4_FLAG_PITCH_VALID),
        "the same transient data-not-ready polls allow calibration once 50 new samples arrive");
    busy_prefix=0;

    s_state=S4_STATE_INITIALIZING;reads=0;change_at=30;
    run_owner(75);(void)subsystem4_get_telemetry(&sample);
    expect(sample.state==S4_STATE_CALIBRATING&&!(sample.flags&S4_FLAG_PITCH_VALID),
        "an acceleration change restarts the still calibration window");
    change_at=0;
    s_state=S4_STATE_INITIALIZING;reads=0;motor_moving=1;
    run_owner(61);(void)subsystem4_get_telemetry(&sample);
    expect(sample.state==S4_STATE_CALIBRATING&&!(sample.flags&S4_FLAG_PITCH_VALID),
        "fresh nonzero encoder speed prevents calibration");
    motor_moving=0;

    s_state=S4_STATE_MONITORING;s_flags=S4_FLAG_SENSOR_OK;reads=0;busy_after=1;
    uint32_t sample_time=clock_ms;
    run_owner(2);(void)subsystem4_get_telemetry(&sample);
    expect(sample.state==S4_STATE_MONITORING&&sample.pitch_timestamp_ms==sample_time&&
        (sample.flags&S4_FLAG_PITCH_VALID),"one missed sample retains its original pitch timestamp");
    s_state=S4_STATE_MONITORING;s_flags=S4_FLAG_SENSOR_OK;reads=0;
    run_owner(6);(void)subsystem4_get_telemetry(&sample);
    expect(sample.state==S4_STATE_FAULT&&(sample.flags&S4_FLAG_FAULT)&&
        !(sample.flags&S4_FLAG_PITCH_VALID),"100 ms without new data invalidates pitch and enters recovery");
    busy_after=0;

    run_pickup_trace((GY511_ACCEL_DATA_T){500,-125,866}, 20, 0);
    expect(pickups==0,"a sustained moderate one-side tilt does not end the run");
    run_pickup_trace((GY511_ACCEL_DATA_T){1500,-125,825}, 20, 0);
    expect(pickups==0,"a large acceleration with an implausible gravity magnitude is not pickup evidence");
    const GY511_ACCEL_DATA_T lift={0,-898,430};
    run_pickup_trace(lift, 8, 0);
    expect(pickups==0,"strong tilt for less than 160 ms does not end the run");
    run_pickup_trace(lift, 17, S4_CALIBRATION_SAMPLES+9);
    expect(pickups==0,"a stale sample cannot bridge two short candidates");
    s_max_hump_mm=25;
    run_pickup_trace(lift, 24, 0);
    (void)subsystem4_get_telemetry(&sample);
    expect(pickups==1&&pickup_read==S4_CALIBRATION_SAMPLES+9,
        "sustained front lift requests the controller latch once at the confirmation boundary");
    expect(sample.state==S4_STATE_MONITORING&&!(sample.flags&S4_FLAG_HUMP_ACTIVE)&&
        sample.max_hump_mm==25,"pickup aborts the active hump and preserves the run maximum");
    run_pickup_trace((GY511_ACCEL_DATA_T){898,-125,430}, 24, 0);
    expect(pickups==1,"a sustained side pickup also requests STOP");
    test_gentle_pickup();
    test_terrain();
    test_stop_at_ramp_exit();
    test_slow_right_hump();
    test_motion_classes();

#if APP_TEST || APP_S4_MOTOR_TRACE
    const GY511_ACCEL_DATA_T raw={10,-65,850};
    const GY511_ACCEL_DATA_T reference={-15,-80,850};
    const S4_PICKUP_PROGRESS trace_pickup={.lift_phase=S4_LIFT_ARMED,.tilt_active=TRUE};
    trace_sample(1,20,&raw,&reference,&trace_pickup,FALSE);
    usb_state=3;
    usb_pending=3000;
    trace_sample(2,40,&raw,&reference,&trace_pickup,FALSE);
    expect(trace_rows==0,"trace leaves the closed or busy USB console alone");
    usb_pending=0;
    motor_moving=1;
    trace_sample(3,60,&raw,&reference,&trace_pickup,TRUE);
    motor_moving=0;
    expect(trace_rows==1&&strcmp(last_trace+strlen(trace_prefix),
        "3,60,10,-65,850,1,-31,47,1,-15,-80,850,2,1,1,1,880,910,1,350,360,1,820,1500\n")==0,
        "USB row preserves IMU, pickup, signed motion and raw IR values");
    trace_rows=0;
    reads=tilted_samples=skip_at=0;
    s_state=S4_STATE_INITIALIZING;
    uint32_t startup_ms=clock_ms-1000U;
    run_owner(1+S4_CALIBRATION_SAMPLES+6);
    unsigned id=0, time=0;
    (void)sscanf(last_trace+strlen(trace_prefix),"%u,%u",&id,&time);
    expect(trace_rows==6&&id==6&&time==startup_ms+(S4_CALIBRATION_SAMPLES+6)*20U,
        "test task emits one timestamped row per new sample after calibration");
#endif
    printf("[S4 SNAPSHOT] SUMMARY %d checks, %d failures\n",checks,failures);
    return failures?1:0;
}
