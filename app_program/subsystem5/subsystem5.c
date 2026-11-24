/* HC-SR04: GP4 TRIG, GP5 ECHO through a 5 V -> 3.3 V divider.
 * GPIO IRQ13 timestamps echo edges; the task sleeps while waiting.
 * Only this core-0 task owns sensor/servo I/O. It publishes navigation
 * intent; the shared controller alone requests wheels after its STOP latch.
 */
#include "subsystem5.h"
#include "../gpio_irq.h"
#include "../telemetry_sources.h"
#include "../subsystem2/subsystem2.h"
#include "../controller.h"
#include <sys/sysdef.h>
#include <tk/syslib.h>


#define ECHO_FALL (4U << (4 * S5_ECHO_PIN))
#define ECHO_RISE (8U << (4 * S5_ECHO_PIN))
#define ECHO_EDGES (ECHO_FALL | ECHO_RISE)
#define ECHO_TIMEOUT_MS 40
#define SAMPLE_PAUSE_MS 80 /* trigger spacing remains greater than 60 ms */
#define S5_NAV_REFRESH_MS 50U
#ifdef APP_OBSTACLE_BYPASS
/* A scan can finish during startup. Leave time for the full 90-degree
 * encoder move and stopped-speed confirmation after the controller starts. */
#define S5_TURN_WAIT_MS (APP_START_DELAY_MS + 6000U)
#endif
#define S5_STATIONARY_TURN_MS 500U
#define S5_NAV_PHASE_MASK 7U
#define S5_NAV_TURN_ENDED 8U
#define S5_PWM_SLICE 6U
#define S5_PWM_TOP 39062U
#define S5_PWM_REG(offset) (PWM_BASE + S5_PWM_SLICE * 0x14U + (offset))
#define SCAN_FIRST_DEG 0U
#define SCAN_LAST_DEG 180U
#define SCAN_STEP_DEG 5U
#define SCAN_POINTS ((SCAN_LAST_DEG - SCAN_FIRST_DEG) / SCAN_STEP_DEG + 1U)
#define SCAN_MIDDLE (SCAN_POINTS / 2U)
#define SCAN_POINT_OFFSET 10U
#define SCAN_BYTES (SCAN_POINT_OFFSET + (SCAN_POINTS * 12U + 7U) / 8U)

static UINT scan_bearing(UINT index)
{
    const UINT command=SCAN_FIRST_DEG+index*SCAN_STEP_DEG;
    /* Bearings estimated from overhead photos at commands 0, 45, 90,
     * 135 and 180. The installed horn travels about 82 degrees. */
    static const UB measured[]={48U,70U,90U,110U,130U};
    const UINT segment=command/45U;
    if(segment==4U)return measured[4];
    const UINT fraction=command%45U;
    return (measured[segment]*(45U-fraction)+measured[segment+1U]*fraction+22U)/45U;
}

enum { WAIT_RISE, WAIT_FALL, COMPLETE, BAD_EDGES };
static ID echo_done;
static ID range_sem;
typedef enum { RANGE_OK, RANGE_NO_RETURN, RANGE_ERROR } RANGE_RESULT;
/* seq32 + publication32 + distance12/no-return1/reserved2/present1. */
static UB range_publication[10];
static void range_write(UW seq,UW mm,RANGE_RESULT result)
{
    SYSTIM now={0};UINT i;
    UW word=0x8000U|(result==RANGE_OK?mm:result==RANGE_NO_RETURN?0x1000U:0U);
    (void)tk_get_tim(&now);
    if(tk_wai_sem(range_sem,1,TMO_POL)!=E_OK)return;
    for(i=0;i<4;++i) {
        range_publication[i]=(UB)(seq>>(8*i));
        range_publication[4+i]=(UB)(now.lo>>(8*i));
    }
    range_publication[8]=(UB)word;range_publication[9]=(UB)(word>>8);
    (void)tk_sig_sem(range_sem,1);
}
EXPORT ER subsystem5_get_range(S5_RANGE_DATA *out)
{
    UINT i;UW seq=0,t=0,word;ER er;
    if(!out)return E_PAR;
    if(range_sem<=0)return E_OBJ;
    er=tk_wai_sem(range_sem,1,TMO_POL);if(er!=E_OK)return er;
    for(i=0;i<4;++i){seq|=(UW)range_publication[i]<<(8*i);t|=(UW)range_publication[4+i]<<(8*i);}
    word=range_publication[8]|((UW)range_publication[9]<<8);
    (void)tk_sig_sem(range_sem,1);
    if(!(word&0x8000U))return E_TMOUT;
    out->sequence=seq;out->timestamp_ms=t;out->distance_mm=word&4095U;
    out->valid=out->distance_mm!=0;out->no_return=(word&0x1000U)!=0U;return E_OK;
}
/* Shared under range_sem. A completed scan stores time:32, ID:32,
 * width:10, reserved:5, present:1 and one 12-bit distance per fixed angle. */
static UB navigation_publication[9],scan_publication[SCAN_BYTES];
static void write32(UB *out,UW value)
{
    UINT i;for(i=0;i<4;++i)out[i]=(UB)(value>>(8*i));
}
static UW read32(const UB *in)
{
    UW value=0;UINT i;for(i=0;i<4;++i)value|=(UW)in[i]<<(8*i);return value;
}
static BOOL navigation_write(S5_NAV_PHASE phase,UW turn_id)
{
    SYSTIM now={0};(void)tk_get_tim(&now);
    if(tk_wai_sem(range_sem,1,TMO_POL)!=E_OK)return FALSE;
    BOOL ended=turn_id && read32(navigation_publication+4)==turn_id &&
        (navigation_publication[8]&S5_NAV_TURN_ENDED);
    write32(navigation_publication,now.lo);
    write32(navigation_publication+4,ended?0U:turn_id);
    navigation_publication[8]=(UB)(ended?S5_NAV_SCANNING:phase);
    (void)tk_sig_sem(range_sem,1);
    return ended;
}
EXPORT ER subsystem5_get_navigation(S5_NAV_DATA *out)
{
    UB copy[sizeof(navigation_publication)];UINT i;ER er;
    if(!out)return E_PAR;
    if(range_sem<=0)return E_OBJ;
    er=tk_wai_sem(range_sem,1,TMO_POL);if(er!=E_OK)return er;
    for(i=0;i<sizeof(copy);++i)copy[i]=navigation_publication[i];
    (void)tk_sig_sem(range_sem,1);
    UINT phase=copy[8]&S5_NAV_PHASE_MASK;
    if(phase==S5_NAV_UNAVAILABLE)return E_TMOUT;
    out->timestamp_ms=read32(copy);out->turn_id=read32(copy+4);out->phase=phase;
    return E_OK;
}
EXPORT ER subsystem5_end_turn(UW turn_id)
{
    if(!turn_id)return E_PAR;
    if(range_sem<=0)return E_OBJ;
    ER er=tk_wai_sem(range_sem,1,TMO_POL);
    if(er!=E_OK)return er;
    SYSTIM now={0};
    UINT phase=navigation_publication[8]&S5_NAV_PHASE_MASK;
    (void)tk_get_tim(&now);
    if(read32(navigation_publication+4)==turn_id &&
       (phase==S5_NAV_LEFT||phase==S5_NAV_RIGHT) &&
       (UW)(now.lo-read32(navigation_publication))<=S5_NAV_MAX_AGE_MS) {
        navigation_publication[8]|=S5_NAV_TURN_ENDED;
        er=E_OK;
    } else er=E_OBJ;
    (void)tk_sig_sem(range_sem,1);
    return er;
}
static BOOL scan_write(UW sequence,const uint16_t distances[SCAN_POINTS],UINT width_mm)
{
    SYSTIM now={0};UB copy[SCAN_BYTES]={0};
    (void)tk_get_tim(&now);write32(copy,now.lo);write32(copy+4,sequence);
    copy[8]=(UB)width_mm;copy[9]=(UB)((width_mm>>8)|0x80U);
    for(UINT i=0;i<SCAN_POINTS;++i){
        UINT offset=SCAN_POINT_OFFSET+i*12U/8U;
        UINT shift=(i&1U)*4U, bits=(UINT)distances[i]<<shift;
        copy[offset]|=(UB)bits;copy[offset+1]=(UB)(bits>>8);
    }
    if(tk_wai_sem(range_sem,1,TMO_POL)!=E_OK)return FALSE;
    for(UINT i=0;i<sizeof(copy);++i)scan_publication[i]=copy[i];
    (void)tk_sig_sem(range_sem,1);
    return TRUE;
}
int telemetry_get_scan(TELEMETRY_SCAN *out)
{
    UB copy[SCAN_BYTES];ER er;
    if(!out)return E_PAR;
    if(range_sem<=0)return E_OBJ;
    er=tk_wai_sem(range_sem,1,TMO_POL);if(er!=E_OK)return er;
    for(UINT i=0;i<sizeof(copy);++i)copy[i]=scan_publication[i];
    (void)tk_sig_sem(range_sem,1);
    if(!(copy[9]&0x80U))return E_TMOUT;
    out->timestamp_ms=read32(copy);out->sequence=read32(copy+4);out->count=SCAN_POINTS;
    out->width_mm=(uint16_t)(copy[8]|((UINT)(copy[9]&3U)<<8));
    for(UINT i=0;i<SCAN_POINTS;++i){
        UINT offset=SCAN_POINT_OFFSET+i*12U/8U;
        UINT packed=copy[offset]|((UINT)copy[offset+1]<<8);
        out->points[i].angle=scan_bearing(i);
        out->points[i].distance_mm=(uint16_t)((packed>>((i&1U)*4U))&4095U);
    }
    return E_OK;
}
EXPORT S5_MOTION s5_choose_motion(BOOL centre_valid,UW centre_mm,
                                 BOOL left_valid,UW left_mm,
                                 BOOL right_valid,UW right_mm)
{
    BOOL left_clear=left_valid&&left_mm>S5_OBSTACLE_MM;
    BOOL right_clear=right_valid&&right_mm>S5_OBSTACLE_MM;
    if(!centre_valid)return S5_MOTION_STOP;
    if(centre_mm>S5_OBSTACLE_MM)return S5_MOTION_FORWARD;
    if(!left_clear&&!right_clear)return S5_MOTION_STOP;
    if(!right_clear)return S5_MOTION_LEFT;
    if(!left_clear)return S5_MOTION_RIGHT;
    return left_mm+S5_CLEARANCE_MARGIN_MM>=right_mm?S5_MOTION_LEFT:S5_MOTION_RIGHT;
}
static void servo_point_to(UINT bearing)
{
    UW pulse_us=1000U+(1000U*bearing)/180U;
    UW compare=(pulse_us*(S5_PWM_TOP+1U)+10000U)/20000U;
    (void)pwm_set_cc(S5_SERVO_PIN,compare);
}
static void phase_delay(UW delay_ms,S5_NAV_PHASE phase)
{
    while(delay_ms) {
        UW step=delay_ms>S5_NAV_REFRESH_MS?S5_NAV_REFRESH_MS:delay_ms;
        (void)navigation_write(phase,0U);
        (void)tk_dly_tsk(step);delay_ms-=step;
    }
    (void)navigation_write(phase,0U);
}
#ifdef APP_OBSTACLE_BYPASS
static void wait_for_turn(S5_NAV_PHASE phase,UW turn_id)
{
    const UW limit=(APP_TEST||!APP_LINE_FOLLOW_ENABLED)?S5_STATIONARY_TURN_MS:S5_TURN_WAIT_MS;
    SYSTIM started={0},now={0};
    (void)tk_get_otm(&started);
    do {
        if(navigation_write(phase,turn_id))return;
        (void)tk_dly_tsk(S5_NAV_REFRESH_MS);
        (void)tk_get_otm(&now);
    } while((UW)(now.lo-started.lo)<limit);
    (void)navigation_write(S5_NAV_SCANNING,0U);
}
#endif
static volatile UINT echo_state;
static volatile UW rise_us, width_us;

EXPORT ER s5_echo_to_mm(UW pulse_us, UW *mm)
{
    if (mm == NULL) return E_PAR;
    *mm = 0;
    /* Datasheet formula: centimetres = microseconds / 58 (x10 for mm). */
    if (pulse_us < 116 || pulse_us > 23200) return E_PAR;
    *mm = (pulse_us * 10U + 29U) / 58U;
    return E_OK;
}

static void echo_isr(UINT pin, UINT events, UW levels, UW now)
{
    UW pending = events << (4U * S5_ECHO_PIN);
    (void)pin;
    (void)levels;
    if (pending == 0) return;
    if (echo_state == WAIT_RISE && pending == ECHO_RISE) {
        rise_us = now;
        echo_state = WAIT_FALL;
        return;
    }
    if (echo_state == WAIT_FALL && pending == ECHO_FALL) {
        width_us = now - rise_us; /* unsigned subtraction handles timer wrap */
        echo_state = COMPLETE;
    } else {
        /* Coalesced edges or unexpected order cannot give a valid width. */
        echo_state = BAD_EDGES;
    }
    (void)app_gpio_irq_enable(S5_ECHO_PIN, FALSE);
    tk_sig_sem(echo_done, 1); /* no printing or waiting in interrupt context */
}

static RANGE_RESULT measure(UW *mm)
{
    ER er;
    *mm = 0;
    if (gpio_get_val(S5_ECHO_PIN)) return RANGE_ERROR;
    while (tk_wai_sem(echo_done, 1, TMO_POL) == E_OK) { /* drain stale signal */ }

    (void)app_gpio_irq_enable(S5_ECHO_PIN, FALSE);
    echo_state = WAIT_RISE;
    width_us = 0;
    (void)app_gpio_irq_enable(S5_ECHO_PIN, TRUE);
    gpio_set_val(S5_TRIG_PIN, 1);
    WaitUsec(12); /* HC-SR04 requires at least 10 us */
    gpio_set_val(S5_TRIG_PIN, 0);
    er = tk_wai_sem(echo_done, 1, ECHO_TIMEOUT_MS);
    (void)app_gpio_irq_enable(S5_ECHO_PIN, FALSE);
    if (er == E_TMOUT) {
        return echo_state == WAIT_RISE ? RANGE_NO_RETURN : RANGE_ERROR;
    }
    if (er != E_OK || echo_state != COMPLETE) return RANGE_ERROR;
    if (width_us > 23200U) return RANGE_NO_RETURN;
    return s5_echo_to_mm(width_us, mm) == E_OK ? RANGE_OK : RANGE_ERROR;
}

#define SCAN_BAD_ECHO UINT16_MAX
#define SCAN_TRIG_SPACING_MS 80U
#define SCAN_MAX_ATTEMPTS 2U
#define AUTO_STOP_MM 240U
#define AUTO_APPROACH_MM 320U
#define AUTO_CONFIRM_DELTA_MM 25U
#define OBSTACLE_RELEASE_MM (S5_OBSTACLE_MM+S5_CLEARANCE_MARGIN_MM)

static UB obstacle_stop;
EXPORT BOOL subsystem5_obstacle_stop_required(void)
{
    return __atomic_load_n(&obstacle_stop,__ATOMIC_ACQUIRE)!=0U;
}
/* Starting estimate from the HC-SR04's nominal 15-degree beam. In units
 * of half degrees, each edge moves inward by 15 (7.5 degrees). */
#define SCAN_BEAM_HALF_ANGLE_X2 15

/* Fixed-point trig at each degree from 0 through 45 degrees off centre.
 * The radar and both geometry calculations share the measured bearing map. */
static const uint16_t scan_cos_q12[]={
    4096,4095,4094,4090,4086,4080,4074,4065,4056,4046,4034,
    4021,4006,3991,3974,3956,3937,3917,3896,3873,3849,3824,
    3798,3770,3742,3712,3681,3650,3617,3582,3547,3511,3474,
    3435,3396,3355,3314,3271,3228,3183,3138,3091,3044,2996,
    2946,2896
};

static UINT cos_from_centre(UINT index)
{
    const UINT bearing=scan_bearing(index);
    const UINT off=bearing<S5_SCAN_CENTRE_DEG ? S5_SCAN_CENTRE_DEG-bearing : bearing-S5_SCAN_CENTRE_DEG;
    return off<sizeof(scan_cos_q12)/sizeof(scan_cos_q12[0])?scan_cos_q12[off]:0U;
}

static int scan_edge(const uint16_t distances[SCAN_POINTS],int direction,UW centre,UW tolerance)
{
    unsigned gap=0;
    BOOL gap_has_echo=FALSE;
    /* Projected depth alone was grouping farther echoes with the face.
     * Keep a wider tolerance for direct readings within the sonar beam. */
    const UW depth_tolerance=centre/12U>12U ? centre/12U : 12U;
    for(unsigned step=1;step<=SCAN_MIDDLE;++step){
        const UINT index=(int)SCAN_MIDDLE+direction*(int)step;
        const UINT mm=distances[index];
        const UINT cosine=cos_from_centre(index);
        const UW depth=(mm*cosine+2048U)/4096U;
        if(!cosine || mm==SCAN_BAD_ECHO || (mm && mm+2U*tolerance<centre))return 0;
        const BOOL same_face=mm && (mm<=centre+tolerance ||
            (depth+depth_tolerance>=centre && depth<=centre+depth_tolerance));
        if(same_face)continue;
        /* A single intervening hit is acceptable only when at least one
         * of the clear rays has a farther echo. Two missed pings with a
         * face hit between them do not establish an edge. */
        if(gap && (step-gap==1U || (step-gap==2U && (gap_has_echo || mm))))return (int)gap;
        if(step==SCAN_MIDDLE && mm>centre+tolerance)return (int)(gap?gap:step);
        gap=step;
        gap_has_echo=mm!=0U;
    }
    return 0;
}

static INT edge_angle_x2(UINT first_outside,UINT last_inside)
{
    return (INT)(scan_bearing(first_outside)+scan_bearing(last_inside))-2*(INT)S5_SCAN_CENTRE_DEG;
}

static INT bearing_tan_q10(INT off_x2)
{
    static const uint16_t tangent[]={
        0,18,36,54,72,90,108,126,144,162,181,
        199,218,236,255,274,294,313,333,353,373,393,
        414,435,456,477,499,522,544,568,591,615,640,
        665,691,717,744,772,800,829,859,890,922,955,
        989,1024
    };
    const UINT magnitude=off_x2<0 ? -off_x2 : off_x2;
    if(magnitude>2U*(sizeof(tangent)/sizeof(tangent[0])-1U))return 0;
    const UINT off=magnitude/2U;
    const INT tan=magnitude&1U ? (tangent[off]+tangent[off+1U]+1U)/2U : tangent[off];
    return off_x2<0 ? -tan : tan;
}

static UINT scan_width(const uint16_t distances[SCAN_POINTS],UW reference)
{
    const UW centre=distances[SCAN_MIDDLE];
    if(centre<20U || centre>OBSTACLE_RELEASE_MM)return 0;
    const UW tolerance=centre/5U>30U?centre/5U:30U;
    if(reference+tolerance<centre || reference>centre+tolerance)return 0;
    const int right=scan_edge(distances,-1,centre,tolerance);
    const int left=scan_edge(distances,1,centre,tolerance);
    if(!left || !right || left+right<3)return 0;
    const INT right_angle=edge_angle_x2(SCAN_MIDDLE-right,SCAN_MIDDLE-right+1U)+SCAN_BEAM_HALF_ANGLE_X2;
    const INT left_angle=edge_angle_x2(SCAN_MIDDLE+left,SCAN_MIDDLE+left-1U)-SCAN_BEAM_HALF_ANGLE_X2;
    if(left_angle<=right_angle)return 0;
    const INT span=bearing_tan_q10(left_angle)-bearing_tan_q10(right_angle);
    if(span<=0)return 0;
    const UW width=(centre*(UINT)span+512U)/1024U;
    return width<=TELEMETRY_SCAN_WIDTH_MAX_MM ? width : 0;
}

static BOOL wheels_settled(UW hold_since,S2_MOTION_DATA *motion)
{
    SYSTIM now={0};
    if(subsystem2_get_motion(motion)!=E_OK || tk_get_tim(&now)!=E_OK)return FALSE;
    return motion->encoder_valid && motion->speed_valid && !motion->faults &&
        motion->mode==S2_MODE_STOP && !motion->command_active &&
        !motion->duty_left_x100 && !motion->duty_right_x100 &&
        !motion->actual_left_rpm_x10 && !motion->actual_right_rpm_x10 &&
        now.lo-motion->timestamp_ms<=S2_FEEDBACK_MAX_AGE_MS &&
        now.lo-motion->speed_timestamp_ms<=S2_FEEDBACK_MAX_AGE_MS &&
        now.lo-motion->speed_timestamp_ms+motion->speed_period_ms<=now.lo-hold_since;
}

typedef enum {
    SWEEP_NOT_STARTED, SWEEP_DISCARDED, SWEEP_UNCLEAR, SWEEP_MEASURED
} SWEEP_RESULT;

static BOOL scan_allowed(void)
{
    return APP_TEST || app_controller_scan_stop_ready();
}

/* The outside three rays have to agree there is room on that side. A drop
 * out with a bad pulse cannot establish clearance. */
static UW side_clearance(const uint16_t *distances)
{
    UW nearest=4000U;
    for(UINT i=0;i<3U;++i){
        if(distances[i]==SCAN_BAD_ECHO)return 0U;
        if(distances[i] && distances[i]<nearest)nearest=distances[i];
    }
    return nearest;
}

static SWEEP_RESULT sweep_obstacle(UW profile,UW hold_since,UW reference,UW *sequence,
                                  S5_MOTION *escape)
{
    uint16_t distances[SCAN_POINTS];
    S2_MOTION_DATA motion;
    if(!scan_allowed() || !wheels_settled(hold_since,&motion))return SWEEP_NOT_STARTED;
    const int64_t start_left=motion.count_left,start_right=motion.count_right;

    for(UINT i=0;i<SCAN_POINTS;++i){
        UW mm=0;
        servo_point_to(SCAN_FIRST_DEG+i*SCAN_STEP_DEG);
        phase_delay(i==0U?S5_SERVO_SETTLE_MS:SCAN_TRIG_SPACING_MS,S5_NAV_SCANNING);
        if(!scan_allowed()){
            servo_point_to(S5_SCAN_CENTRE_DEG);
            phase_delay(S5_SERVO_SETTLE_MS,S5_NAV_SCANNING);
            return SWEEP_DISCARDED;
        }
        const RANGE_RESULT result=measure(&mm);
        distances[i]=(uint16_t)(result==RANGE_ERROR?SCAN_BAD_ECHO:mm);
        if(i==SCAN_MIDDLE)range_write((*sequence)++,mm,result);
    }
    servo_point_to(S5_SCAN_CENTRE_DEG);
    phase_delay(S5_SERVO_SETTLE_MS,S5_NAV_SCANNING);
    if(!scan_allowed() || !wheels_settled(hold_since,&motion) ||
       motion.count_left!=start_left || motion.count_right!=start_right)
        return SWEEP_DISCARDED;
    const UINT width=scan_width(distances,reference);
    const BOOL centre_valid=distances[SCAN_MIDDLE]>=20U && distances[SCAN_MIDDLE]<=S5_OBSTACLE_MM;
    if(escape && centre_valid){
        const UW left=side_clearance(distances+SCAN_POINTS-3U);
        const UW right=side_clearance(distances);
        *escape=s5_choose_motion(TRUE,distances[SCAN_MIDDLE],left!=0U,left,right!=0U,right);
    }
    for(UINT i=0;i<SCAN_POINTS;++i)if(distances[i]==SCAN_BAD_ECHO)distances[i]=0;
    if(!scan_write(profile,distances,width))return SWEEP_DISCARDED;
    return width ? SWEEP_MEASURED : SWEEP_UNCLEAR;
}

static void obstacle_width_loop(void)
{
    UW sequence=0,profile=1,hold_since=0,previous_near=0;
    unsigned clear_samples=0,attempts=0;
    BOOL encounter=FALSE;
    S5_NAV_PHASE phase=S5_NAV_SCANNING;
    for(;;){
        UW centre=0;
        /* Startup and both sweep exits already centre and settle the head.
         * Centred samples need only the inter-ping delay below. */
        const RANGE_RESULT result=measure(&centre);
        range_write(sequence++,centre,result);
        const BOOL may_scan=scan_allowed();
        BOOL automatic=subsystem5_obstacle_stop_required();
        /* Two consistent forward pings are required to request a stop. */
        const BOOL approaching=!APP_TEST && !may_scan && !automatic && result==RANGE_OK && centre<=AUTO_APPROACH_MM;
        if(approaching && centre<=AUTO_STOP_MM){
            if(previous_near && centre<=previous_near+AUTO_CONFIRM_DELTA_MM &&
               previous_near<=centre+AUTO_CONFIRM_DELTA_MM){
                __atomic_store_n(&obstacle_stop,1U,__ATOMIC_RELEASE);
                automatic=TRUE;
            }
            previous_near=centre;
        } else previous_near=0;
        const UW threshold=encounter ? OBSTACLE_RELEASE_MM : S5_OBSTACLE_MM;
        if(!may_scan && !automatic){
            encounter=FALSE;
            clear_samples=0;
        } else if(result==RANGE_OK && centre<=threshold){
            clear_samples=0;
            if(!encounter){
                SYSTIM now={0};(void)tk_get_tim(&now);hold_since=now.lo;
                encounter=TRUE;
                attempts=0;
            }
            if(may_scan && attempts<SCAN_MAX_ATTEMPTS){
#ifdef APP_OBSTACLE_BYPASS
                S5_MOTION escape=S5_MOTION_STOP;
                const UW completed_profile=profile;
                S5_MOTION *escape_out=automatic?&escape:NULL;
#else
                S5_MOTION *escape_out=NULL;
#endif
                const SWEEP_RESULT sweep=sweep_obstacle(profile,hold_since,centre,&sequence,escape_out);
                if(sweep==SWEEP_UNCLEAR || sweep==SWEEP_MEASURED){
                    if(++profile==0U)++profile;
                }
                if(sweep!=SWEEP_NOT_STARTED)++attempts;
                if(sweep==SWEEP_MEASURED)attempts=SCAN_MAX_ATTEMPTS;
#ifdef APP_OBSTACLE_BYPASS
                if((sweep==SWEEP_MEASURED || (sweep==SWEEP_UNCLEAR && attempts==SCAN_MAX_ATTEMPTS)) &&
                   (escape==S5_MOTION_LEFT || escape==S5_MOTION_RIGHT)){
                    wait_for_turn(escape==S5_MOTION_LEFT?S5_NAV_LEFT:S5_NAV_RIGHT,completed_profile);
                }
#endif
            }
        } else if(encounter && ((result==RANGE_OK && centre>OBSTACLE_RELEASE_MM) ||
                                (automatic && result==RANGE_NO_RETURN))){
            if(++clear_samples==2U){
                encounter=FALSE;
                __atomic_store_n(&obstacle_stop,0U,__ATOMIC_RELEASE);
            }
        } else clear_samples=0;

        if(!scan_allowed() && !subsystem5_obstacle_stop_required())encounter=FALSE;
        if(encounter && attempts<SCAN_MAX_ATTEMPTS)phase=S5_NAV_SCANNING;
        else if(!encounter && result==RANGE_OK && centre>S5_OBSTACLE_MM)phase=S5_NAV_CLEAR;
#ifdef APP_OBSTACLE_BYPASS
        else if(!encounter && result==RANGE_NO_RETURN)phase=S5_NAV_CLEAR;
#endif
        else phase=S5_NAV_BLOCKED;
        phase_delay(SAMPLE_PAUSE_MS,phase);
    }
}

static void ultrasonic_task(INT stacd, void *exinf)
{
    ER er;
    (void)stacd;
    (void)exinf;
    /* GPIO interrupt routing and NVIC setup are both on physical core 0. */
    er = app_gpio_irq_register(S5_ECHO_PIN, APP_GPIO_RISE | APP_GPIO_FALL, echo_isr);
    if (er < E_OK) {
        CHECK(er, "ultrasonic IRQ setup");
        tk_exd_tsk();
        return;
    }
    (void)tk_dly_tsk(100);
    servo_point_to(S5_SCAN_CENTRE_DEG);
    phase_delay(S5_SERVO_SETTLE_MS,S5_NAV_SCANNING);
    obstacle_width_loop();
}

static ER servo_init(void)
{
    /* Release only PWM reset; S2 owns slices 4 and 5. */
    UINT timeout=10000U;
    clr_w(RESETS_RESET,RESETS_RESET_PWM);
    while(!(in_w(RESETS_RESET_DONE)&RESETS_RESET_PWM)) {
        if(--timeout==0U)return E_IO;
        WaitUsec(1);
    }
    out_w(S5_PWM_REG(PWM_CHx_CSR),0U);
    out_w(S5_PWM_REG(PWM_CHx_DIV),0x400U);
    out_w(S5_PWM_REG(PWM_CHx_CTR),0U);
    out_w(S5_PWM_REG(PWM_CHx_CC),0U);
    out_w(S5_PWM_REG(PWM_CHx_TOP),S5_PWM_TOP);
    ER er=pwm_set_pin(S5_SERVO_PIN);
    if(er<E_OK)return er;
    servo_point_to(S5_SCAN_CENTRE_DEG);
    return pwm_set_enabled(S5_SERVO_PIN,TRUE);
}

#if defined(APP_BENCH_SERVO) && APP_BENCH_MODE == APP_BENCH_SERVO
EXPORT void subsystem5_calibrate_servo(void)
{
    const ER er=servo_init();
    if(er!=E_OK){
        CHECK(er,"servo calibration setup");
        for(;;)(void)tk_dly_tsk(1000);
    }

    for(;;){
        servo_point_to(S5_SCAN_CENTRE_DEG);
        tm_printf((UB*)"[SERVO] Setup position. First photo pose starts in 10 seconds.\n");
        (void)tk_dly_tsk(10000);
        for(UINT command=0;command<=180U;command+=45U){
            servo_point_to(command);
            tm_printf((UB*)"[SERVO] Photo %u/5, command %u degrees. Holding for 8 seconds.\n",
                      command/45U+1U,command);
            (void)tk_dly_tsk(8000);
        }
    }
}
#endif

EXPORT ER subsystem5_init(void)
{
#if APP_TEST && S2_ENCODER_CALIBRATION
    return E_OK;
#else
    T_CSEM csem = {0};
    ER er;
    gpio_set_val(S5_TRIG_PIN, 0);
    er = gpio_set_pin(S5_TRIG_PIN, GPIO_MODE_OUT);
    if (er < E_OK) return er;
    er = gpio_set_pin(S5_ECHO_PIN, GPIO_MODE_IN);
    if (er < E_OK) return er;
    clr_w(GPIO(S5_ECHO_PIN), GPIO_PUE);
    set_w(GPIO(S5_ECHO_PIN), GPIO_PDE); /* idle low when unplugged */
    er=servo_init();
    if(er!=E_OK)return er;
    csem.sematr = TA_TFIFO;
    csem.maxsem = 1;
    echo_done = tk_cre_sem(&csem);
    if(echo_done<E_OK)return echo_done;
    csem.isemcnt=1;
    range_sem=tk_cre_sem(&csem);
    if(range_sem<E_OK){(void)tk_del_sem(echo_done);echo_done=0;}
    return range_sem<E_OK?range_sem:E_OK;
#endif
}

EXPORT ER subsystem5_start(void)
{
#if APP_TEST && S2_ENCODER_CALIBRATION
    if (S5_ULTRA_LOG_ENABLED)
        tm_printf((UB *)"[ULTRA] SKIP: subsystem2 encoder calibration owns GPIO IRQ13.\n");
    return E_OK;
#else
    ID tid;
    if (echo_done <= 0 || range_sem <= 0) return E_OBJ;
    tid = app_start_task((FP)ultrasonic_task, S5_PRI_MAIN, TP_PRC1,
                         "ultrasonic_task");
    return tid < E_OK ? tid : E_OK;
#endif
}
