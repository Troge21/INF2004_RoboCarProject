/* HC-SR04: GP4 TRIG, GP5 ECHO through a 5 V -> 3.3 V divider.
 * GPIO IRQ13 timestamps echo edges; the task sleeps while waiting.
 * Only this core-0 task owns sensor/servo I/O. It publishes navigation
 * intent; the shared controller alone requests wheels after its STOP latch.
 */
#include "subsystem5.h"
#include "../gpio_irq.h"
#include "../telemetry_sources.h"
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

enum { WAIT_RISE, WAIT_FALL, COMPLETE, BAD_EDGES };
static ID echo_done;
static ID range_sem;
/* seq32 + publication32 + distance12/present1/reserved3; no native padding. */
static UB range_publication[10];
static void range_write(UW seq,UW mm,BOOL valid)
{
    SYSTIM now={0};UINT i;UW word=0x8000U|(valid?mm:0U);
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
    out->valid=out->distance_mm!=0;return E_OK;
}
/* Shared under range_sem. time32 + turn ID32 + phase3/end1; completed scan is time32 +
 * ID32 + three distance12 values + present1/reserved3. Fixed angles implicit. */
static UB navigation_publication[9],scan_publication[13];
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
static void scan_write(UW sequence,UW right,UW centre,UW left)
{
    SYSTIM now={0};UB copy[13]={0};UINT i;
    (void)tk_get_tim(&now);write32(copy,now.lo);write32(copy+4,sequence);
    copy[8]=(UB)right;copy[9]=(UB)((right>>8)|((centre&15U)<<4));
    copy[10]=(UB)(centre>>4);copy[11]=(UB)left;copy[12]=(UB)((left>>8)|16U);
    if(tk_wai_sem(range_sem,1,TMO_POL)!=E_OK)return;
    for(i=0;i<13;++i)scan_publication[i]=copy[i];
    (void)tk_sig_sem(range_sem,1);
}
int telemetry_get_scan(TELEMETRY_SCAN *out)
{
    UB copy[13];UINT i;ER er;
    if(!out)return E_PAR;
    if(range_sem<=0)return E_OBJ;
    er=tk_wai_sem(range_sem,1,TMO_POL);if(er!=E_OK)return er;
    for(i=0;i<13;++i)copy[i]=scan_publication[i];
    (void)tk_sig_sem(range_sem,1);
    if(!(copy[12]&16U))return E_TMOUT;
    out->timestamp_ms=read32(copy);out->sequence=read32(copy+4);out->count=3;
    out->points[0].angle=S5_SCAN_RIGHT_DEG;
    out->points[0].distance_mm=(uint16_t)(copy[8]|((copy[9]&15U)<<8));
    out->points[1].angle=S5_SCAN_CENTRE_DEG;
    out->points[1].distance_mm=(uint16_t)((copy[9]>>4)|((UINT)copy[10]<<4));
    out->points[2].angle=S5_SCAN_LEFT_DEG;
    out->points[2].distance_mm=(uint16_t)(copy[11]|((copy[12]&15U)<<8));
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
#if defined(APP_OBSTACLE_BYPASS) || APP_TEST
static void wait_for_turn(S5_NAV_PHASE phase,UW turn_id)
{
#ifdef APP_OBSTACLE_BYPASS
    const UW limit=(APP_TEST||!APP_LINE_FOLLOW_ENABLED)?S5_STATIONARY_TURN_MS:S5_TURN_WAIT_MS;
#else
    const UW limit=S5_STATIONARY_TURN_MS;
#endif
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

static const char *measure(UW *mm, UW *pulse)
{
    ER er;
    *mm = 0;
    *pulse = 0;
    if (gpio_get_val(S5_ECHO_PIN)) return "ECHO_HIGH_BEFORE_TRIGGER";
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
        return echo_state == WAIT_RISE ? "NO_ECHO_TIMEOUT" : "ECHO_END_TIMEOUT";
    }
    if (er < E_OK) return "KERNEL_WAIT_ERROR";
    if (echo_state != COMPLETE) return "INVALID_EDGE_SEQUENCE";
    *pulse = width_us;
    return s5_echo_to_mm(*pulse, mm) == E_OK ? NULL : "OUT_OF_RANGE";
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
    {
        UW sequence=0,profile=0;
        S5_NAV_PHASE current=S5_NAV_SCANNING;
        for(;;) {
            UW centre=0,left=0,right=0,pulse;
            const char *centre_error;
            servo_point_to(S5_SCAN_CENTRE_DEG);
            phase_delay(S5_SERVO_SETTLE_MS,current);
            centre_error=measure(&centre,&pulse);
            /* Only actual centre samples reach the front-distance tile. */
            range_write(sequence++,centre,centre_error==NULL);
            if(centre_error!=NULL) {
                current=S5_NAV_BLOCKED;phase_delay(SAMPLE_PAUSE_MS,current);continue;
            }
            if(centre>S5_OBSTACLE_MM) {
                current=S5_NAV_CLEAR;phase_delay(SAMPLE_PAUSE_MS,current);continue;
            }
            phase_delay(SAMPLE_PAUSE_MS,S5_NAV_SCANNING);
            servo_point_to(S5_SCAN_LEFT_DEG);
            phase_delay(S5_SERVO_SETTLE_MS,S5_NAV_SCANNING);
            (void)measure(&left,&pulse);
            phase_delay(SAMPLE_PAUSE_MS,S5_NAV_SCANNING);
            servo_point_to(S5_SCAN_RIGHT_DEG);
            phase_delay(S5_SERVO_SETTLE_MS,S5_NAV_SCANNING);
            (void)measure(&right,&pulse);
            servo_point_to(S5_SCAN_CENTRE_DEG);
            if(++profile==0U)++profile; /* Zero is reserved for no turn request. */
            scan_write(profile,right,centre,left);
#if defined(APP_OBSTACLE_BYPASS) || APP_TEST
            S5_MOTION choice=s5_choose_motion(TRUE,centre,left!=0U,left,right!=0U,right);
            if(choice==S5_MOTION_LEFT||choice==S5_MOTION_RIGHT) {
                wait_for_turn(choice==S5_MOTION_LEFT?S5_NAV_LEFT:S5_NAV_RIGHT,profile);
                current=S5_NAV_SCANNING; /* stop until a fresh centre check */
            } else {
                current=S5_NAV_BLOCKED;phase_delay(SAMPLE_PAUSE_MS,current);
            }
#else
            /* The normal controller follows IR only. Scans still feed telemetry. */
            current=S5_NAV_SCANNING;
#endif
        }
    }
}

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
    /* Release only PWM reset; do not reconfigure S2 slices 4/5.
     * Stationary S2 does not initialize PWM, so S5 owns this startup too. */
    {
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
        er=pwm_set_pin(S5_SERVO_PIN);if(er<E_OK)return er;
        servo_point_to(S5_SCAN_CENTRE_DEG);
        er=pwm_set_enabled(S5_SERVO_PIN,TRUE);if(er<E_OK)return er;
    }
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
