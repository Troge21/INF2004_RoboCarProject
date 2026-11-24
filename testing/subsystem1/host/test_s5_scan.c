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
#define APP_BENCH_MODE 0
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
enum { NEAR_THRESHOLD=6, RETRY_SUCCEEDS, DRIVING_NOISY, RESUME_DURING_SCAN,
       AUTO_SCAN, AUTO_RIGHT, AUTO_BLOCKED, AUTO_UNCLEAR };
static UINT created,counts[4],busy,measured,scenario,seen[6],servo_calls,checks;
static UW servo_compare;
static UW first_hold_motion_ms,last_driving_ping_ms,last_post_scan_ping_ms;
static unsigned sweep_echoes,post_scan_echoes,driving_echoes;
static BOOL automatic_case(void){return scenario>=AUTO_SCAN;}
#ifdef APP_OBSTACLE_BYPASS
static UW turn_started_ms;
static BOOL turn_acked;
#endif
#define APP_START_DELAY_MS 3000U
static jmp_buf done;
static FP started;
static ER tk_get_tim(SYSTIM *s){s->lo=source_ms;s->hi=0;return E_OK;}
#ifdef APP_OBSTACLE_BYPASS
static ER tk_get_otm(SYSTIM *s){return tk_get_tim(s);}
#endif
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
int app_controller_scan_stop_ready(void){
    if(automatic_case())return subsystem5_obstacle_stop_required();
    return scenario!=DRIVING_NOISY && (scenario!=RESUME_DURING_SCAN || sweep_echoes<SCAN_MIDDLE);
}
ER subsystem2_get_motion(S2_MOTION_DATA *out){
    if(!first_hold_motion_ms)first_hold_motion_ms=source_ms;
    *out=(S2_MOTION_DATA){.timestamp_ms=source_ms,.speed_timestamp_ms=source_ms,.speed_period_ms=200,
        .encoder_valid=TRUE,.speed_valid=TRUE,.mode=S2_MODE_STOP,
        .actual_left_rpm_x10=source_ms-first_hold_motion_ms<300U?100:0,
        .count_left=scenario==3&&sweep_echoes==SCAN_POINTS?1:0};
    return E_OK;
}
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
    UW mm;
    if(scenario==1){
        assert(servo_compare==2930U);
        mm=0;
    }else if(!APP_TEST && !app_controller_scan_stop_ready()){
        assert(servo_compare==2930U);
        if(scenario==DRIVING_NOISY){
            static const UW pings[]={235,0,235,195,240,241};
            assert(!subsystem5_obstacle_stop_required() && driving_echoes<sizeof(pings)/sizeof(pings[0]));
            mm=pings[driving_echoes];
        }else if(automatic_case()){
            static const UW pings[]={310,250,239,234};
            assert(driving_echoes<sizeof(pings)/sizeof(pings[0]));
            if(driving_echoes)assert(source_ms-last_driving_ping_ms>=80U && source_ms-last_driving_ping_ms<90U);
            mm=pings[driving_echoes];
        }else mm=200;
        last_driving_ping_ms=source_ms;
        ++driving_echoes;
    }else if(sweep_echoes%SCAN_POINTS || servo_compare==1953U){
        S5_NAV_DATA nav;
        const BOOL first_attempt=sweep_echoes<SCAN_POINTS;
        const unsigned index=sweep_echoes++%SCAN_POINTS;
        const unsigned angle=SCAN_FIRST_DEG+index*SCAN_STEP_DEG;
        UW pulse=1000U+1000U*angle/180U;
        assert(servo_compare==(pulse*(S5_PWM_TOP+1U)+10000U)/20000U);
        assert(source_ms-first_hold_motion_ms>=300U);
        assert(subsystem5_get_navigation(&nav)==E_OK && nav.phase==S5_NAV_SCANNING);
        const unsigned off=index>SCAN_MIDDLE?index-SCAN_MIDDLE:SCAN_MIDDLE-index;
        const unsigned face=scenario==NEAR_THRESHOLD?252U:automatic_case()?234U:200U;
        const unsigned cosine=cos_from_centre(index);
        const BOOL unclear=scenario==2 || (scenario==RETRY_SUCCEEDS&&first_attempt);
        mm=off<=(unclear?0U:11U)?(face*4096U+cosine/2U)/cosine:600U;
        if((scenario==AUTO_RIGHT && index>=SCAN_POINTS-3U) ||
           (scenario==AUTO_BLOCKED && off>=SCAN_MIDDLE-2U) ||
           (scenario==AUTO_UNCLEAR && index>=SCAN_MIDDLE))mm=face;
    }else if(sweep_echoes>=SCAN_POINTS*((scenario==2||scenario==RETRY_SUCCEEDS||scenario==AUTO_UNCLEAR)?2U:1U)){
        assert(servo_compare==2930U);
        if(post_scan_echoes&&automatic_case()){
            UW interval=source_ms-last_post_scan_ping_ms;
            assert(interval>=SAMPLE_PAUSE_MS&&interval<=SAMPLE_PAUSE_MS+ECHO_TIMEOUT_MS);
        }
        last_post_scan_ping_ms=source_ms;
        mm=post_scan_echoes++ && !automatic_case()?600U:0U;
    }else{
        assert(servo_compare==2930U);
        mm=scenario==NEAR_THRESHOLD?(measured?252U:248U):automatic_case()?234U:200U;
    }
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
#ifdef APP_OBSTACLE_BYPASS
        if(nav.phase==S5_NAV_LEFT||nav.phase==S5_NAV_RIGHT){
            static TELEMETRY_SCAN scan;
            const BOOL unclear=scenario==AUTO_UNCLEAR;
            assert(automatic_case() && scenario!=AUTO_BLOCKED);
            assert(nav.phase==(scenario==AUTO_SCAN?S5_NAV_LEFT:S5_NAV_RIGHT));
            assert(nav.turn_id==(unclear?2U:1U));
            assert(subsystem5_obstacle_stop_required());
            assert(telemetry_get_scan(&scan)==E_OK&&scan.count==37&&scan.sequence==nav.turn_id);
            assert(unclear?scan.width_mm==0U:scan.width_mm>0U);
            assert(subsystem5_end_turn(nav.turn_id+1U)==E_OBJ);
            if(!turn_started_ms)turn_started_ms=nav.timestamp_ms;
            if(!turn_acked && (scenario!=AUTO_RIGHT || source_ms-turn_started_ms>=5500U)){
                assert(subsystem5_end_turn(nav.turn_id)==E_OK);
                turn_acked=TRUE;
            }
        }else assert(nav.turn_id==0U);
#else
        assert(nav.turn_id==0U && nav.phase!=S5_NAV_LEFT && nav.phase!=S5_NAV_RIGHT);
#endif
        if(post_scan_echoes==1U || post_scan_echoes==2U)
#ifdef APP_OBSTACLE_BYPASS
            if(post_scan_echoes==1U || !automatic_case())
#endif
            assert(nav.phase!=S5_NAV_CLEAR);
        if(automatic_case() && post_scan_echoes==1U)
            assert(subsystem5_obstacle_stop_required());
        if((scenario==1&&measured==1&&(nav.phase==S5_NAV_BLOCKED||nav.phase==S5_NAV_CLEAR))||
           (scenario==DRIVING_NOISY&&driving_echoes==6U&&nav.phase==S5_NAV_BLOCKED)||
           (scenario==RESUME_DURING_SCAN&&driving_echoes==1U&&nav.phase==S5_NAV_BLOCKED)||
           (automatic_case()&&post_scan_echoes==2U&&!subsystem5_obstacle_stop_required())||
           (post_scan_echoes==3U&&nav.phase==S5_NAV_CLEAR))longjmp(done,1);
    }
    source_ms+=(UW)ms;raw_us+=(UW)ms*1000U;
    assert(source_ms<20000);
    return E_OK;
}
static void pass(const char *name){++checks;printf("PASS: %s\n",name);}
static void reset(unsigned which){
    created=busy=measured=servo_calls=0;scenario=which;source_ms=1000;raw_us=1000000;
#ifdef APP_OBSTACLE_BYPASS
    turn_started_ms=0;turn_acked=FALSE;
#endif
    first_hold_motion_ms=last_driving_ping_ms=sweep_echoes=post_scan_echoes=driving_echoes=0;
    last_post_scan_ping_ms=0;
    __atomic_store_n(&obstacle_stop,0U,__ATOMIC_RELEASE);
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
    assert(seen[S5_NAV_SCANNING]&&seen[S5_NAV_CLEAR]&&!seen[S5_NAV_LEFT]);
    assert(servo_calls>=6&&subsystem5_get_range(&range)==E_OK&&range.distance_mm==600);
    assert(telemetry_get_scan(&scan)==E_OK&&scan.sequence==1);
    pass("actual task publishes a completed scan without using a side echo as the centre range");
    assert(sweep_echoes==37&&scan.count==37&&scan.width_mm==132);
    assert(scan.points[0].angle==48&&scan.points[9].angle==70&&scan.points[18].angle==90&&
           scan.points[27].angle==110&&scan.points[36].angle==130);
    for(unsigned i=1;i<scan.count;++i)assert(scan.points[i-1].angle<scan.points[i].angle);
    assert(scan.points[0].distance_mm==600&&scan.points[18].distance_mm==200);
    assert(subsystem5_get_navigation(&nav)==E_OK&&nav.phase==S5_NAV_CLEAR&&post_scan_echoes==3);
    pass("the 37-angle sweep waits for a full stop and two far echoes rearm scanning");
    {
        UB packed[S1_SCAN_PACKED_BYTES]={0},wire[S1_SCAN_PACKET_BYTES];
        for(UINT i=0;i<scan.count;++i){
            s1_set_bits(packed,20*i,8,scan.points[i].angle);
            s1_set_bits(packed,20*i+8,12,scan.points[i].distance_mm);
        }
        const int length=s1_pack_scan(17,scan.sequence,7000,packed,scan.count,0,scan.width_mm,wire,sizeof(wire));
        assert(length==58&&wire[13]==37&&wire[15]==(16|S1_SCAN_WIDTH_FLAG));
        assert(wire[56]==132&&wire[57]==0&&s1_get_bits(wire+16,0,8)==48);
        assert(s1_pack_scan(17,scan.sequence,7000,packed,37,16,scan.width_mm,wire,sizeof(wire))==56&&wire[15]==16);
        assert(s1_pack_scan(17,scan.sequence,7000,packed,37,32,scan.width_mm,wire,sizeof(wire))==29&&wire[15]==5);
        assert(s1_pack_scan(17,scan.sequence,7000,packed,37,0,1024,wire,sizeof(wire))<0);
    }
    pass("real scan getter feeds the packed scan frame and any width trailer");
    before=scan;busy=1;
    assert(telemetry_get_scan(&scan)==E_TMOUT&&!memcmp(&scan,&before,sizeof(scan)));
    assert(subsystem5_get_navigation(&nav)==E_TMOUT);busy=0;
    pass("snapshot contention returns immediately with caller output intact");
    uint16_t packed_ranges[SCAN_POINTS]={0};
    packed_ranges[SCAN_MIDDLE]=4000;
    assert(scan_write(UINT32_MAX,packed_ranges,0)&&telemetry_get_scan(&scan)==E_OK&&scan.sequence==UINT32_MAX);
    assert(scan_write(0,packed_ranges,0)&&telemetry_get_scan(&scan)==E_OK&&scan.sequence==0&&scan.points[SCAN_MIDDLE].distance_mm==4000);
    pass("scan ID wrap and invalid-echo sentinel survive packed publication");
    reset(1);if(setjmp(done)==0)started(0,NULL);
    assert(subsystem5_get_range(&range)==E_OK&&!range.valid&&range.no_return&&range.distance_mm==0);
    assert(telemetry_get_scan(&scan)==E_TMOUT);
    pass("no centre echo publishes an invalid range without a fabricated scan");
    reset(2);if(setjmp(done)==0)started(0,NULL);
    assert(telemetry_get_scan(&scan)==E_OK&&scan.width_mm==0&&scan.sequence==2);
    assert(sweep_echoes==2U*SCAN_POINTS);
    pass("centre-only returns finish after two sweeps without producing a width");
    reset(RETRY_SUCCEEDS);if(setjmp(done)==0)started(0,NULL);
    assert(telemetry_get_scan(&scan)==E_OK&&scan.width_mm==132&&scan.sequence==2);
    assert(sweep_echoes==2U*SCAN_POINTS);
    pass("one automatic retry can recover a usable estimate while stopped");
    reset(3);if(setjmp(done)==0)started(0,NULL);
    assert(telemetry_get_scan(&scan)==E_TMOUT);
    pass("encoder motion during the sweep discards the profile");
    reset(NEAR_THRESHOLD);if(setjmp(done)==0)started(0,NULL);
    assert(telemetry_get_scan(&scan)==E_OK&&scan.width_mm==166&&scan.points[SCAN_MIDDLE].distance_mm==252);
    pass("a 248 mm detection still scans and estimates width when the settled range is 252 mm");
    if(!APP_TEST){
        reset(DRIVING_NOISY);if(setjmp(done)==0)started(0,NULL);
        assert(!subsystem5_obstacle_stop_required() && sweep_echoes==0 && !first_hold_motion_ms);
        assert(telemetry_get_scan(&scan)==E_TMOUT);
        assert(subsystem5_get_range(&range)==E_OK && range.valid && range.distance_mm==241);
        pass("a lone close echo, missing echo, inconsistent close pair and 241 mm do not trigger an automatic stop");
        reset(AUTO_SCAN);if(setjmp(done)==0)started(0,NULL);
        assert(driving_echoes==4 && sweep_echoes==SCAN_POINTS);
        assert(telemetry_get_scan(&scan)==E_OK && scan.width_mm>0 && scan.points[SCAN_MIDDLE].distance_mm==234);
        assert(!subsystem5_obstacle_stop_required() && post_scan_echoes==2);
        pass("239 and 234 mm confirm a stop and width sweep; two absent echoes allow Resume after removal");
#ifdef APP_OBSTACLE_BYPASS
        assert(turn_acked&&seen[S5_NAV_LEFT]&&!seen[S5_NAV_RIGHT]);
        reset(AUTO_RIGHT);if(setjmp(done)==0)started(0,NULL);
        assert(turn_acked&&seen[S5_NAV_RIGHT]&&!seen[S5_NAV_LEFT]&&source_ms-turn_started_ms>=5500U);
        pass("measured right clearance is selected and the scan request waits for the settled turn");
        reset(AUTO_BLOCKED);if(setjmp(done)==0)started(0,NULL);
        assert(!turn_acked&&!seen[S5_NAV_RIGHT]&&!seen[S5_NAV_LEFT]);
        pass("blocked sides leave the vehicle at its scan hold without a turn request");
        reset(AUTO_UNCLEAR);if(setjmp(done)==0)started(0,NULL);
        assert(turn_acked&&seen[S5_NAV_RIGHT]&&sweep_echoes==2U*SCAN_POINTS);
        pass("an unclear width can still furnish safe side evidence for the bypass");
#endif
        reset(RESUME_DURING_SCAN);if(setjmp(done)==0)started(0,NULL);
        assert(sweep_echoes==SCAN_MIDDLE && telemetry_get_scan(&scan)==E_TMOUT);
        assert(subsystem5_get_range(&range)==E_OK && range.valid && range.distance_mm==200);
        pass("Resume cancels an in-progress sweep and restores centred ranging");
    }
    for(unsigned i=0;i<SCAN_POINTS;++i){
        const unsigned cosine=cos_from_centre(i);
        packed_ranges[i]=(uint16_t)((200U*4096U+cosine/2U)/cosine);
    }
    assert(scan_width(packed_ranges,200)==0);
    for(unsigned i=0;i<SCAN_POINTS;++i)if(i<SCAN_MIDDLE-9||i>SCAN_MIDDLE+11)packed_ranges[i]=0;
    assert(scan_width(packed_ranges,200)==113);
    packed_ranges[SCAN_MIDDLE+1]=0;
    packed_ranges[SCAN_MIDDLE+3]=0;
    assert(scan_width(packed_ranges,200)==113);
    packed_ranges[SCAN_MIDDLE+1]=SCAN_BAD_ECHO;
    assert(scan_width(packed_ranges,200)==0);
    pass("an isolated miss is tolerated, while a wall or bad echo yields no estimate");
    for(unsigned i=0;i<SCAN_POINTS;++i)
        packed_ranges[i]=i>=SCAN_MIDDLE-13&&i<=SCAN_MIDDLE+3?200U:0U;
    assert(scan_width(packed_ranges,200)==89);
    packed_ranges[SCAN_MIDDLE+2]=packed_ranges[SCAN_MIDDLE+3]=0;
    assert(scan_width(packed_ranges,200)==71); /* corrected edges can lie on the same side */
    for(unsigned i=0;i<SCAN_MIDDLE;++i)packed_ranges[i]=200U;
    assert(scan_width(packed_ranges,200)==0);
    pass("flat off-axis ranges can resolve a wider edge; a side with no bracket still yields no estimate");
    /* Actual tissue-box captures before and after the angle calibration. */
    const uint16_t tissue_box[SCAN_POINTS]={
        0,0,0,0,0,1389,1376,227,214,227,201,227,217,217,204,217,217,217,217,
        238,238,230,229,362,362,362,362,367,0,572,571,0,0,0,1965,1935,1931
    };
    const uint16_t unclear_box[SCAN_POINTS]={
        374,416,304,312,312,312,304,253,235,235,235,227,227,226,226,244,244,238,238,
        238,238,247,247,226,226,227,227,235,235,425,429,0,0,0,0,0,1947
    };
    const uint16_t live_box[SCAN_POINTS]={
        0,299,304,313,312,367,367,256,238,238,238,238,250,241,232,232,232,232,232,
        232,241,241,250,241,250,259,276,247,247,247,265,0,0,0,1959,1949,1953
    };
    const uint16_t intermittent_edge[SCAN_POINTS]={
        294,763,312,238,312,244,273,256,229,229,229,238,232,232,223,224,224,224,224,
        232,232,241,241,247,247,238,229,238,247,264,273,0,0,0,0,1940,1931
    };
    assert(scan_width(tissue_box,217)==82&&scan_width(unclear_box,238)==146);
    assert(scan_width(live_box,232)==164);
    assert(scan_width(intermittent_edge,224)==148);
    pass("a 155 mm box capture with an intermittent edge resolves to 148 mm in replay");
    /* Angled box face slightly closer than centre - tolerance, plus boundary far echo. */
    for(unsigned i=0;i<SCAN_POINTS;++i)
        packed_ranges[i]=(i<=SCAN_MIDDLE-8U||i>=SCAN_MIDDLE+8U)?600U:200U;
    packed_ranges[SCAN_MIDDLE-4]=145U;
    packed_ranges[SCAN_MIDDLE+4]=145U;
    assert(scan_width(packed_ranges,200)>0);
    packed_ranges[SCAN_MIDDLE+4]=110U; /* < centre - 2*tolerance still rejected */
    assert(scan_width(packed_ranges,200)==0);
    for(unsigned i=0;i<SCAN_POINTS;++i)packed_ranges[i]=200U;
    packed_ranges[0]=600U;packed_ranges[SCAN_POINTS-1]=600U;
    assert(scan_width(packed_ranges,200)>0);
    pass("angled face within 2x tolerance and boundary-step far echoes resolve obstacle width");
    printf("SUMMARY: %u actual S5 scan/publication checks passed, APP_TEST=%d; no motor API linked.\n",checks,APP_TEST);
    return 0;
}
