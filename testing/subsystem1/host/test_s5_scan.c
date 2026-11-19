/* Actual S5 task/IRQ/publications and actual v4 scan serializer.
 * Deterministic peripheral/RTOS substitutes; no hardware or sockets.
 * No motor API is supplied: an S5 wheel request would fail to link. */
#include <stdint.h>
#include <stddef.h>
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <setjmp.h>
#define APP_H
#ifndef APP_TEST
#define APP_TEST 1
#endif
#define S2_ENCODER_CALIBRATION 0
#define APP_LINE_FOLLOW_ENABLED 1
#define EXPORT
#define IMPORT extern
#define TRUE 1
#define FALSE 0
#define E_OK 0
#define E_PAR (-17)
#define E_OBJ (-41)
#define E_TMOUT (-50)
#define E_IO (-57)
#define TMO_POL 0
#define TA_TFIFO 1
#define TP_PRC1 1
#define GPIO_MODE_OUT 1
#define GPIO_MODE_IN 0
#define GPIO_PUE 1U
#define GPIO_PDE 2U
#define GPIO(n) (100U+(n))
#define RESETS_RESET 1U
#define RESETS_RESET_DONE 2U
#define RESETS_RESET_PWM 4U
#define PWM_BASE 1000U
#define PWM_CHx_CSR 0U
#define PWM_CHx_DIV 4U
#define PWM_CHx_CTR 8U
#define PWM_CHx_CC 12U
#define PWM_CHx_TOP 16U
#define CHECK(er,name) assert((er)==E_OK)
typedef uint32_t UW,UINT;
typedef uint8_t UB;
typedef int ER,ID,INT,BOOL;
typedef void (*FP)(INT,void *);
typedef struct {UW lo,hi;} SYSTIM;
typedef struct {UW sematr;INT isemcnt,maxsem;} T_CSEM;
static UW source_ms=1000,raw_us=1000000;
enum { LEFT_ESCAPE=4, SLOW_BYPASS_TURN };
static UINT created,counts[4],busy,measured,scenario,seen[6],servo_calls,checks;
static UW turn_started_ms,servo_compare;
static BOOL turn_acknowledged;
#define APP_START_DELAY_MS 3000U
static jmp_buf done;
static FP started;
static ER tk_get_tim(SYSTIM *s){s->lo=source_ms;s->hi=0;return E_OK;}
static ER tk_get_otm(SYSTIM *s){return tk_get_tim(s);}
static ER tk_wai_sem(ID,INT,INT);
static ER tk_sig_sem(ID,INT);
static ER tk_dly_tsk(INT);
static ID tk_cre_sem(const T_CSEM *s){assert(++created<4);counts[created]=s->isemcnt;return (ID)created;}
static ER tk_del_sem(ID id){counts[id]=0;return E_OK;}
static void tk_exd_tsk(void){assert(0);}
static UW in_w(UINT reg){assert(reg==RESETS_RESET_DONE);return RESETS_RESET_PWM;}
static void out_w(UINT reg,UW v){(void)v;assert(reg>=PWM_BASE+6U*20U&&reg<=PWM_BASE+6U*20U+16U);}
static void clr_w(UINT reg,UW v){assert((reg==RESETS_RESET&&v==RESETS_RESET_PWM)||reg==GPIO(5));}
static void set_w(UINT reg,UW v){assert(reg==GPIO(5)&&v==GPIO_PDE);}
static ER gpio_set_pin(UINT pin,UINT mode){(void)mode;assert(pin==4||pin==5);return E_OK;}
static ER gpio_set_val(UINT pin,UINT v){assert(pin==4&&v<=1);return E_OK;}
static UINT gpio_get_val(UINT pin){assert(pin==5);return 0;}
static void WaitUsec(UINT us){raw_us+=us;}
static ER pwm_set_pin(UINT pin){assert(pin==12);return E_OK;}
static ER pwm_set_enabled(UINT pin,BOOL enabled){assert(pin==12&&enabled);return E_OK;}
static ER pwm_set_cc(UINT pin,UW value){
    assert(pin==12&&value>=1953&&value<=3907);
    servo_compare=value;++servo_calls;return E_OK;
}
static ID app_start_task(FP fn,INT priority,UW core,const char *name){
    (void)name;assert(priority==20&&core==1);started=fn;return 10;
}
#include "../../../app_program/subsystem5/subsystem5.c"
#include "../../../app_program/subsystem1/s1_protocol.c"
ER app_gpio_irq_register(UINT pin,UINT events,APP_GPIO_CALLBACK cb){
    assert(pin==5&&events==12&&cb==echo_isr);return E_OK;
}
ER app_gpio_irq_enable(UINT pin,BOOL enabled){(void)enabled;assert(pin==5);return E_OK;}
ER app_gpio_irq_unregister(UINT pin){assert(pin==5);return E_OK;}
static ER tk_sig_sem(ID id,INT n){assert(id>0&&id<4&&n==1);assert(++counts[id]<=1);return E_OK;}
static ER tk_wai_sem(ID id,INT n,INT timeout){
    assert(id>0&&id<4&&n==1);
    if(id==range_sem){
        if(busy)return E_TMOUT;
        assert(timeout==TMO_POL&&counts[id]==1);counts[id]=0;return E_OK;
    }
    assert(id==echo_done);
    if(timeout==TMO_POL){
        if(!counts[id])return E_TMOUT;
        --counts[id];return E_OK;
    }
    assert(timeout==40);
    /* The installed servo points left at the higher pulse width. Check the
     * actual PWM at each echo as well as the published 30/right and 150/left. */
    if(measured==1)assert(servo_compare==3580U);
    else if(measured==2)assert(servo_compare==2277U);
    else assert(servo_compare==2930U);
    UW mm;
    if(scenario==1)mm=0;
    else if(measured==0)mm=100;
    else if(measured==1)mm=scenario==2?0:scenario==LEFT_ESCAPE?400:240;
    else if(measured==2)mm=scenario==2?0:scenario==LEFT_ESCAPE?160:260;
    else mm=600;
    ++measured;
    if(!mm){source_ms+=40;return E_TMOUT;}
    UW pulse=mm*58U/10U;
    echo_isr(5,8,0,raw_us);echo_isr(5,4,0,raw_us+pulse);
    raw_us+=pulse;source_ms+=pulse/1000U;
    assert(counts[id]==1);--counts[id];return E_OK;
}
static ER tk_dly_tsk(INT ms){
    S5_NAV_DATA nav;
    assert(ms>0&&(ms<=50||ms==100));
    if(subsystem5_get_navigation(&nav)==E_OK){
        assert(nav.phase<=S5_NAV_BLOCKED&&source_ms-nav.timestamp_ms<=50);
        ++seen[nav.phase];
        if(nav.phase==S5_NAV_LEFT||nav.phase==S5_NAV_RIGHT){
            S5_RANGE_DATA range;static TELEMETRY_SCAN scan;
            BOOL left_escape=scenario==LEFT_ESCAPE;
            assert(nav.phase==(left_escape?S5_NAV_LEFT:S5_NAV_RIGHT));
            assert(nav.turn_id==1U);
            if(!turn_started_ms)turn_started_ms=nav.timestamp_ms;
            assert(subsystem5_end_turn(nav.turn_id+1U)==E_OBJ);
            if(!APP_TEST&&(scenario==0||left_escape)&&seen[nav.phase]==2U)
                assert(subsystem5_end_turn(nav.turn_id)==E_OK);
            if(scenario==SLOW_BYPASS_TURN&&source_ms-turn_started_ms>=5500U){
                assert(subsystem5_end_turn(nav.turn_id)==E_OK);
                turn_acknowledged=TRUE;
            }
            assert(subsystem5_get_range(&range)==E_OK&&range.valid&&range.distance_mm==100);
            assert(telemetry_get_scan(&scan)==E_OK&&scan.count==3&&scan.sequence==1);
            assert(scan.points[0].angle==30&&scan.points[0].distance_mm==(left_escape?160:260));
            assert(scan.points[1].angle==90&&scan.points[1].distance_mm==100);
            assert(scan.points[2].angle==150&&scan.points[2].distance_mm==(left_escape?400:240));
        }else assert(nav.turn_id==0U);
        if((scenario==1&&measured==1&&nav.phase==S5_NAV_BLOCKED)||
           (scenario==2&&measured==3&&nav.phase==S5_NAV_BLOCKED)||
           (measured==4&&nav.phase==S5_NAV_CLEAR))longjmp(done,1);
    }
    source_ms+=(UW)ms;raw_us+=(UW)ms*1000U;
    assert(source_ms<20000);
    return E_OK;
}
static void pass(const char *name){++checks;printf("PASS: %s\n",name);}
static void reset(unsigned which){
    created=busy=measured=servo_calls=0;scenario=which;source_ms=1000;raw_us=1000000;
    turn_started_ms=0;turn_acknowledged=FALSE;
    echo_done=range_sem=0;memset(counts,0,sizeof(counts));memset(seen,0,sizeof(seen));
    memset(range_publication,0,sizeof(range_publication));memset(navigation_publication,0,sizeof(navigation_publication));
    memset(scan_publication,0,sizeof(scan_publication));
    assert(subsystem5_init()==E_OK&&subsystem5_start()==E_OK&&started);
}
int main(void){
    static TELEMETRY_SCAN scan,before;
    S5_NAV_DATA nav={.timestamp_ms=123,.phase=9};S5_RANGE_DATA range;
    assert(subsystem5_get_navigation(NULL)==E_PAR&&telemetry_get_scan(NULL)==E_PAR);
    assert(subsystem5_get_navigation(&nav)==E_OBJ&&nav.phase==9);
    pass("passive API errors do not manufacture measurements");
    assert(s5_choose_motion(1,251,0,0,0,0)==S5_MOTION_FORWARD);
    assert(s5_choose_motion(0,0,1,500,1,500)==S5_MOTION_STOP);
    assert(s5_choose_motion(1,100,1,250,1,250)==S5_MOTION_STOP);
    assert(s5_choose_motion(1,100,1,240,1,260)==S5_MOTION_RIGHT);
    assert(s5_choose_motion(1,100,1,250,1,251)==S5_MOTION_RIGHT);
    assert(s5_choose_motion(1,100,1,500,1,300)==S5_MOTION_LEFT);
    assert(s5_choose_motion(1,100,0,0,1,500)==S5_MOTION_RIGHT);
    pass("thresholds, missing echoes and clearance margin preserve a clear escape");
    reset(0);assert(telemetry_get_scan(&scan)==E_TMOUT);
    if(setjmp(done)==0)started(0,NULL);
    assert(seen[S5_NAV_SCANNING]&&seen[S5_NAV_RIGHT]&&seen[S5_NAV_CLEAR]&&!seen[S5_NAV_LEFT]);
    assert(servo_calls>=6&&subsystem5_get_range(&range)==E_OK&&range.distance_mm==600);
    assert(telemetry_get_scan(&scan)==E_OK&&scan.sequence==1);
    pass("actual task scans while publishing intent; centre range never becomes a side echo");
    assert(seen[S5_NAV_RIGHT]==(APP_TEST?10U:2U));
    assert(subsystem5_end_turn(1U)==E_OBJ&&subsystem5_end_turn(0U)==E_PAR);
    pass("only the current turn ID can end a request; stationary scan dwell is preserved");
    {
        UB packed[S1_SCAN_PACKED_BYTES]={0},wire[S1_SCAN_PACKET_BYTES];
        for(UINT i=0;i<3;++i){
            s1_set_bits(packed,20*i,8,scan.points[i].angle);
            s1_set_bits(packed,20*i+8,12,scan.points[i].distance_mm);
        }
        assert(s1_pack_scan(17,scan.sequence,7000,packed,scan.count,0,wire,sizeof(wire))==24);
        assert(wire[13]==3&&wire[14]==0&&wire[15]==3);
        assert(s1_get_bits(wire+16,0,8)==30&&s1_get_bits(wire+16,40,8)==150);
    }
    pass("real strong scan getter feeds the existing three-point v4 scan frame");
    before=scan;busy=1;
    assert(telemetry_get_scan(&scan)==E_TMOUT&&!memcmp(&scan,&before,sizeof(scan)));
    assert(subsystem5_get_navigation(&nav)==E_TMOUT);busy=0;
    pass("snapshot contention returns immediately with caller output intact");
    scan_write(UINT32_MAX,4000,20,0);assert(telemetry_get_scan(&scan)==E_OK&&scan.sequence==UINT32_MAX);
    scan_write(0,0,4000,20);assert(telemetry_get_scan(&scan)==E_OK&&scan.sequence==0&&scan.points[1].distance_mm==4000);
    pass("scan ID wrap and invalid-echo sentinel survive packed publication");
    reset(1);if(setjmp(done)==0)started(0,NULL);
    assert(subsystem5_get_range(&range)==E_OK&&!range.valid&&range.distance_mm==0);
    assert(telemetry_get_scan(&scan)==E_TMOUT&&!seen[S5_NAV_CLEAR]);
    pass("no centre echo publishes invalid range and a blocking intent without a fabricated scan");
    reset(2);if(setjmp(done)==0)started(0,NULL);
    assert(telemetry_get_scan(&scan)==E_OK&&scan.points[0].distance_mm==0&&scan.points[2].distance_mm==0);
    assert(!seen[S5_NAV_LEFT]&&!seen[S5_NAV_RIGHT]);
    pass("missing side echoes remain gaps in a completed profile and never authorize a turn");
    reset(LEFT_ESCAPE);if(setjmp(done)==0)started(0,NULL);
    assert(seen[S5_NAV_LEFT]==(APP_TEST?10U:2U)&&!seen[S5_NAV_RIGHT]&&seen[S5_NAV_CLEAR]);
    pass("blocked right and measured clear left publish a left turn through the actual scan task");
    if(!APP_TEST){
        reset(3);if(setjmp(done)==0)started(0,NULL);
        assert(seen[S5_NAV_RIGHT]==S5_TURN_WAIT_MS/S5_NAV_REFRESH_MS);
        assert(source_ms-turn_started_ms>=S5_TURN_WAIT_MS&&seen[S5_NAV_CLEAR]);
        assert(subsystem5_end_turn(1U)==E_OBJ);
        pass("normal sensing resumes after an unanswered turn request expires");
#ifdef APP_OBSTACLE_BYPASS
        reset(SLOW_BYPASS_TURN);if(setjmp(done)==0)started(0,NULL);
        assert(turn_acknowledged&&seen[S5_NAV_CLEAR]);
        pass("the 90-degree bypass still accepts a settled turn acknowledgement at 5500 ms");
#endif
    }
    printf("SUMMARY: %u actual S5 scan/publication checks passed, APP_TEST=%d; no motor API linked.\n",checks,APP_TEST);
    return 0;
}
