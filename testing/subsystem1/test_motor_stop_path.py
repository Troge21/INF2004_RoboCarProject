"""Actual integrated controller/latch + S2 API, persistent PID and motor loop.
Deterministic RTOS/encoder/PWM stubs; no board, network or simulated live feed.
"""
from pathlib import Path
import subprocess
import tempfile
root = Path(__file__).resolve().parents[2]
app = (root/'app_program/app_main.c').read_text(encoding='utf-8')
motor = (root/'app_program/subsystem2/subsystem2.c').read_text(encoding='utf-8')
controller = app[app.index('static void app_line_targets'):app.index('static void vehicle_controller_task')]
api = motor[motor.index('static ID s_command_sem'):motor.index('#if S2_FEEDBACK_ENABLED')]
loop = motor[motor.index('#if S2_FEEDBACK_ENABLED'):motor.index('EXPORT ER subsystem2_init')]
assert 'app_controller_step(started.lo);' in app
prefix = r'''
#include <assert.h>
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <setjmp.h>
#define APP_H
#define APP_TEST 0
#define APP_BENCH_MODE 0
#define EXPORT
#define IMPORT extern
#define TRUE 1
#define FALSE 0
#define E_OK 0
#define E_PAR (-17)
#define E_OBJ (-41)
#define E_TMOUT (-50)
#define E_IO (-57)
#define E_BUSY (-65)
#define TMO_POL 0
#define TMO_FEVR (-1)
#define TA_TFIFO 0
#define APP_START_DELAY_MS 3000U
#define APP_CONTROL_PERIOD_MS 20
#define APP_IR_MAX_AGE_MS 100U
#define APP_FORWARD_DUTY 20
#define APP_INNER_DUTY 15
#define S2_PWM_TOP 6249U
#define S2_DRIVE_ENABLED 1
#define S2_FEEDBACK_ENABLED 1
typedef int ID,INT,BOOL,ER;
typedef unsigned UINT;
typedef uint32_t UW;
typedef uint8_t UB;
typedef struct {UW lo,hi;} SYSTIM;
typedef struct {unsigned sematr,isemcnt,maxsem;} T_CSEM;
typedef struct {UINT pattern;UW timestamp_ms;BOOL valid;} S3_LINE_DATA;
#define SUBSYSTEM3_H
#include "subsystem2.h"
#include "encoder_move.c"
#include "motion_control.h"
#include "odometry.h"
#include "telemetry_sources.h"
#include "subsystem5/subsystem5.h"
static UW operating_ms;
static unsigned scenario,phase,ticks,checks,pattern,stale,invalid,compare[4];
static unsigned motor_busy,controller_busy,encoder_fail;
static unsigned nav_phase=S5_NAV_CLEAR,nav_stale,nav_busy;
static UW nav_turn_id,ended_turn_id;
#ifdef APP_OBSTACLE_BYPASS
static UW range_hold_ms;
static BOOL front_blocked,front_missing;
#endif
static int created,held[4];
static S2_ENCODER_COUNTS counts;
static jmp_buf done;
static ID tk_cre_sem(T_CSEM *s){assert(s->isemcnt==1&&s->maxsem==1);return ++created;}
static ER tk_wai_sem(ID id,INT n,INT timeout){
    assert(id>=1&&id<=3&&n==1&&(timeout==TMO_POL||timeout==TMO_FEVR));
    if(held[id]||(id==1&&controller_busy)||(id==2&&motor_busy))return E_TMOUT;
    held[id]=1;return E_OK;
}
static ER tk_sig_sem(ID id,INT n){assert(n==1&&held[id]);held[id]=0;return E_OK;}
static ER tk_get_otm(SYSTIM *t){t->lo=operating_ms;t->hi=0;return E_OK;}
static ER tk_get_tim(SYSTIM *t){t->lo=operating_ms+1000000U;t->hi=0;return E_OK;}
static ER subsystem3_get_line(S3_LINE_DATA *s){
    s->pattern=pattern;s->valid=!invalid;
    s->timestamp_ms=operating_ms+1000000U-(stale?101U:0U);return E_OK;
}
ER subsystem5_get_navigation(S5_NAV_DATA *out){
    if(nav_busy)return E_TMOUT;
    out->phase=nav_phase;out->turn_id=nav_turn_id;
    out->timestamp_ms=operating_ms+1000000U-(nav_stale?201U:0U);
    return E_OK;
}
ER subsystem5_end_turn(UW id){
    assert(id&&id==nav_turn_id&&!nav_stale&&!nav_busy);
    for(unsigned i=0;i<4;++i)assert(!compare[i]);
    ended_turn_id=id;
    return E_OK;
}
#ifdef APP_OBSTACLE_BYPASS
ER subsystem5_get_range(S5_RANGE_DATA *out){
    if(nav_busy)return E_TMOUT;
    out->timestamp_ms=range_hold_ms?range_hold_ms:operating_ms+1000000U;
    out->distance_mm=front_blocked?100U:700U;
    out->valid=!front_missing;
    return E_OK;
}
#include "obstacle_bypass.c"
#endif
ER s2_encoder_start(void){return E_OK;}
ER s2_encoder_read(S2_ENCODER_COUNTS *out){
    if(encoder_fail)return E_IO;
    if(compare[0])counts.left+=7;
    if(compare[1])counts.left-=7;
    if(compare[2])counts.right+=6;
    if(compare[3])counts.right-=6;
    *out=counts;return E_OK;
}
static ER pwm_set_cc(UINT pin,UW value){
    assert(pin>=8&&pin<=11);
    if(value)assert(compare[(pin-8)^1U]==0);
    compare[pin-8]=value;return E_OK;
}
static ER tk_dly_tsk(INT ms);
#include "controller.c"
'''
tests = r'''
static void pass(const char *name){++checks;printf("PASS: %s\n",name);}
static void stopped(void){for(unsigned i=0;i<4;++i)assert(compare[i]==0);}
static BOOL moving(void){return compare[0]>0&&compare[2]>0;}
#ifdef APP_OBSTACLE_BYPASS
static void run_bypass_cycle(void){
    static unsigned seen, release_at;
    static BOOL checked_opening_gap,checked_range_gap;
    uint8_t snapshot[5],reply[10];
    const int side=scenario==8?-1:1;
    if(scenario==13){
        if(phase==0&&moving()){
            assert(app_controller_submit_stop(509,99,operating_ms+1000U)==E_OK);phase=1;
        }else if(phase==1){
            stopped();assert(app_controller_take_reply(reply)==E_OK);
            nav_turn_id=1;nav_phase=S5_NAV_LEFT;phase=2;
        }else if(phase==2&&ended_turn_id==1){
            stopped();assert(bypass.phase==BYPASS_IDLE&&(app_controller_poll()&APP_CTRL_LATCH));
            pass("a settled STOP releases the scan request so front clearance can update");
            longjmp(done,1);
        }
        return;
    }
    if(phase==0&&moving()){
        seen=0;checked_opening_gap=checked_range_gap=FALSE;nav_phase=S5_NAV_SCANNING;phase=1;
    }else if(phase==1){
        stopped();pattern=0;nav_turn_id=1;
        nav_phase=side>0?S5_NAV_LEFT:S5_NAV_RIGHT;phase=2;
    }else if(phase==2&&scenario==7&&!checked_opening_gap&&
             bypass.phase==BYPASS_TURN_OUT&&bypass.move.result==S2_MOVE_IDLE){
        checked_opening_gap=TRUE;invalid=1;release_at=ticks+5U;phase=9;
    }else if(phase==2&&ended_turn_id){
        stopped();assert(bypass.phase==BYPASS_SHIFT_OUT);
        range_hold_ms=bypass.phase_started_ms;nav_phase=S5_NAV_CLEAR;
        release_at=ticks+5U;phase=3;
    }else if(phase==3){
        stopped();assert(bypass.move.result==S2_MOVE_IDLE);
        if(ticks==release_at){
            pass("bypass waits for new front clearance after the initial turn settles");
            range_hold_ms=0;phase=4;
        }
    }else if(phase==4){
        if(bypass.phase==BYPASS_SHIFT_IN){
            if(scenario==14&&bypass.move.result==S2_MOVE_REACHED){
                stopped();pattern=1;
            }else if(moving()&&scenario!=9&&scenario!=14){
                pattern=(seen&(1U<<BYPASS_SHIFT_IN))?1U:0U;
            }
        }
        if(scenario==14)assert(bypass.phase!=BYPASS_FAILED);
        if((scenario==8||scenario==12)&&!checked_range_gap&&
           bypass.phase==BYPASS_PASS&&moving()){
            checked_range_gap=TRUE;front_missing=TRUE;
            release_at=ticks+(scenario==12?30U:5U);phase=10;
        }else if(scenario==10&&bypass.phase==BYPASS_SHIFT_OUT&&moving()){
            front_blocked=TRUE;phase=5;
        }else if(scenario==11&&bypass.phase==BYPASS_PASS&&moving()){
            assert(app_controller_submit_stop(505,99,operating_ms+1000U)==E_OK);phase=6;
        }else if(scenario==16&&bypass.phase==BYPASS_PASS&&moving()){
            app_controller_report_pickup();phase=11;
        }else if(scenario==9&&bypass.phase==BYPASS_FAILED){
            stopped();assert(bypass.line_state!=LINE_CONFIRMED);
            assert(app_controller_snapshot(snapshot)==E_OK&&(snapshot[4]&15)==APP_CTRL_FAULT);
            release_at=ticks+20U;phase=7;
        }else if(bypass.phase==BYPASS_IDLE){
            const unsigned all=(1U<<(BYPASS_ALIGN+1))-2U;
            assert((seen&all)==all&&bypass.line_state==LINE_CONFIRMED);
            assert(scenario==7||scenario==8||scenario==14);
            phase=8;
        }
    }else if(phase==5){
        stopped();assert(bypass.phase==BYPASS_FAILED);
        pass("a new near echo aborts the powered bypass even before the next S5 phase");
        front_blocked=FALSE;release_at=ticks+20U;phase=7;
    }else if(phase==6){
        stopped();assert(bypass.phase==BYPASS_IDLE);
        assert(app_controller_take_reply(reply)==E_OK&&((reply[8]|((unsigned)reply[9]<<8))>>6)==0);
        assert(app_controller_poll()&APP_CTRL_LATCH);
        pass("STOP cancels a live bypass and leaves both PWM outputs zero");
        longjmp(done,1);
    }else if(phase==7){
        stopped();assert(bypass.phase==BYPASS_FAILED);
        if(ticks==release_at){
            pass(scenario==9?"missing tape leaves the car stopped instead of aligning or resuming line following":
                "returning clearance cannot restart an aborted bypass");
            longjmp(done,1);
        }
    }else if(phase==8&&moving()){
        assert(app_controller_snapshot(snapshot)==E_OK&&(snapshot[4]&15)==APP_CTRL_FOLLOWING);
        if(scenario==14)
            pass("fresh tape at the return cutoff is confirmed while the wheels settle");
        else
            pass(side>0?"left-side bypass executes all seven legs and rejoins on fresh IR evidence":
                        "right-side bypass mirrors all turns and rejoins on fresh IR evidence");
        longjmp(done,1);
    }else if(phase==9){
        stopped();assert(bypass.phase==BYPASS_TURN_OUT&&bypass.move.result==S2_MOVE_IDLE);
        if(ticks==release_at){
            pass("the opening turn waits at zero for a temporarily missing IR sample");
            invalid=0;phase=2;
        }
    }else if(phase==10){
        stopped();
        if(bypass.phase==BYPASS_FAILED){
            assert(scenario==12&&ticks+6U>=release_at);
            pass("a sustained loss of the front echo faults the route with both wheels off");
            front_missing=FALSE;release_at=ticks+20U;phase=7;
        }else if(ticks==release_at){
            assert(scenario==8&&bypass.phase==BYPASS_PASS);
            pass("a brief missing echo holds both wheels stopped until fresh clearance");
            front_missing=FALSE;phase=4;
        }
    }else if(phase==11){
        uint32_t ticket=0;
        stopped();assert(bypass.phase==BYPASS_IDLE);
        assert(app_controller_poll()&APP_CTRL_LATCH);
        assert(app_controller_arm_resume(operating_ms+1000U,&ticket)==APP_RESUME_NOT_READY);
        pass("pickup cancels a powered bypass and holds both PWM outputs at zero");
        longjmp(done,1);
    }

    const unsigned leg=bypass.phase;
    if(leg>=BYPASS_TURN_OUT&&leg<=BYPASS_ALIGN){
        if(leg==BYPASS_SHIFT_OUT||leg==BYPASS_PASS||leg==BYPASS_SHIFT_IN){
            if(moving())seen|=1U<<leg;
            assert(!compare[1]&&!compare[3]);
        }else{
            int direction=(leg==BYPASS_TURN_OUT||leg==BYPASS_ALIGN)?side:-side;
            BOOL left_turn=compare[1]&&compare[2],right_turn=compare[0]&&compare[3];
            if(left_turn||right_turn){
                assert(direction>0?left_turn:right_turn);
                seen|=1U<<leg;
            }
        }
    }
}
#endif
static ER tk_dly_tsk(INT ms){
    uint8_t reply[10],snapshot[5];
    S2_MOTION_DATA motion;
    static uint32_t resume_ticket_id;
    static UW reverse_at;
#ifndef APP_OBSTACLE_BYPASS
    static unsigned replay_until;
#endif
    assert(ms==20);++ticks;
    assert(subsystem2_get_motion(&motion)==E_OK);
    if(scenario==6){
        if(phase==0){
            assert(subsystem2_set_rpm_x10(-350,350)==E_PAR);
            assert(subsystem2_set_signed_rpm_x10(-601,350)==E_PAR);
            if(moving()&&motion.actual_left_rpm_x10>0&&motion.actual_right_rpm_x10>0){
                reverse_at=operating_ms;
                phase=1;
            }
        }else if(phase==1){
            if(compare[1]||compare[3]){
                assert(compare[1]&&compare[3]&&!compare[0]&&!compare[2]);
                assert(operating_ms-reverse_at>=S2_SPEED_SAMPLE_MS);
                assert(!motion.actual_left_rpm_x10&&!motion.actual_right_rpm_x10);
                pass("signed PWM waits for a complete stopped encoder sample before reversing either wheel");
                phase=2;
            }else stopped();
        }else if(phase==2&&motion.actual_left_rpm_x10<0&&motion.actual_right_rpm_x10<0){
            assert(motion.duty_left_x100<0&&motion.duty_right_x100<0);
            reverse_at=operating_ms;
            phase=3;
        }else if(phase==3&&operating_ms-reverse_at>=S2_COMMAND_MAX_AGE_MS+S2_PERIOD_MS){
            stopped();
            assert(motion.stop_reason==S2_STOP_COMMAND_TIMEOUT&&!motion.faults);
            pass("reverse PWM uses the same command expiry and cuts both outputs");
            longjmp(done,1);
        }
        if(phase<3){
            INT rpm=phase==0?350:-350;
            assert(subsystem2_set_signed_rpm_x10(rpm,rpm)==E_OK);
        }
        operating_ms+=(UW)ms;
        assert(ticks<120U);
        return E_OK;
    }
    if(scenario==15){
        if(phase==0&&moving()){
            controller_busy=1;app_controller_report_pickup();phase=1;
        }else if(phase==1){
            stopped();assert(app_controller_poll()&APP_CTRL_LATCH);
            controller_busy=0;phase=2;
        }else if(phase==2){
            stopped();assert(app_controller_snapshot(snapshot)==E_OK&&
                (snapshot[4]&APP_CTRL_STATE_MASK)==APP_CTRL_STOP_LATCHED);
            if(ticks>225U){
                pass("local pickup cuts actual line-following PWM even with the command mailbox locked");
                longjmp(done,1);
            }
        }
    }else if(scenario==0){
        if(operating_ms<APP_START_DELAY_MS)stopped();
        else if(phase==0&&moving()){
            const S2_COMMAND command=read_command();
            assert(command.mode==S2_MODE_RPM&&command.left==350&&command.right==350);
            pass("normal controller requests RPM and actual PID/motor loop produces nonzero output after startup");
            assert(app_controller_submit_stop(101,99,operating_ms)==E_OK);phase=1;
        }else if(phase==1){
            assert(moving()&&app_controller_take_reply(reply)==E_OK&&((reply[8]|((unsigned)reply[9]<<8))>>6)==APP_STOP_EXPIRED);
            pass("expired STOP cannot inhibit the running controller");
            controller_busy=1;phase=2;
        }else if(phase==2){
            stopped();pass("controller contention submits zero through actual RPM motor channel");
            controller_busy=0;phase=3;
        }else if(phase==3&&moving()){
            assert(app_controller_submit_stop(102,99,operating_ms+1000U)==E_OK);
            motor_busy=1;phase=4;
        }else if(phase==4){
            stopped();assert(app_controller_take_reply(reply)==E_TMOUT);
            assert(app_controller_poll()&APP_CTRL_LATCH);
            pass("busy motor channel cannot acknowledge a STOP before accepting zero");
            motor_busy=0;phase=5;
        }else if(phase==5){
            stopped();assert(app_controller_take_reply(reply)==E_OK&&((reply[8]|((unsigned)reply[9]<<8))>>6)==0);
            assert((reply[8]&APP_CTRL_STATE_MASK)==APP_CTRL_STOP_LATCHED&&read_command().mode==S2_MODE_STOP);
            assert(motion.duty_left_x100==0&&motion.duty_right_x100==0);
            pass("normal STOP result follows accepted request and zero output in the actual PID/motor loop");
            phase=6;
        }else if(phase==6){
            stopped();assert(app_controller_snapshot(snapshot)==E_OK&&(snapshot[4]&APP_CTRL_STATE_MASK)==APP_CTRL_STOP_LATCHED);
            pattern=ticks%4U;stale=ticks%3U==0U;invalid=ticks%5U==0U;
            nav_phase=ticks%6U;nav_stale=ticks%7U==0U;nav_busy=ticks%11U==0U;
            if(ticks>230U){pass("latched STOP persists through all line and S5 scan/turn phases, stale and busy snapshots");longjmp(done,1);}
        }
    }else if(scenario==1){
        stopped();
        if(ticks==2)assert(app_controller_submit_stop(103,99,1000U)==E_OK);
        if(ticks==3){assert(app_controller_take_reply(reply)==E_OK&&((reply[8]|((unsigned)reply[9]<<8))>>6)==0);pass("STOP is serviced during startup");}
        if(operating_ms>3500U){pass("startup STOP prevents later white/white forward motion");longjmp(done,1);}
    }else if(scenario==2){
        if(phase==0&&moving()){encoder_fail=1;phase=1;}
        else if(phase==1){
            stopped();assert(motion.faults!=0);
            assert(app_controller_submit_stop(104,99,operating_ms+1000U)==E_OK);phase=2;
        }else if(phase==2){
            stopped();assert(app_controller_take_reply(reply)==E_OK&&((reply[8]|((unsigned)reply[9]<<8))>>6)==APP_STOP_FAULT);
            assert((reply[8]&APP_CTRL_STATE_MASK)==APP_CTRL_FAULT&&(reply[8]&APP_CTRL_LATCH));
            pass("S2 encoder fault propagates to controller and never becomes a fabricated successful STOP");
            longjmp(done,1);
        }
#ifndef APP_OBSTACLE_BYPASS
    }else if(scenario==4){
        if(phase==0&&moving()){nav_phase=S5_NAV_SCANNING;phase=1;}
        else if(phase==1){
            stopped();assert(app_controller_snapshot(snapshot)==E_OK&&(snapshot[4]&15)==APP_CTRL_SCANNING);
            pass("controller stops the actual motor loop during S5 scans");nav_phase=S5_NAV_LEFT;nav_turn_id=1;phase=2;
        }else if(phase==2&&compare[1]&&compare[2]){
            const S2_COMMAND command=read_command();
            assert(command.left<0&&command.right>0&&!compare[0]&&!compare[3]);
            assert(app_controller_snapshot(snapshot)==E_OK&&(snapshot[4]&15)==APP_CTRL_TURNING_LEFT);
            pass("left intent starts an encoder turn through signed PWM arbitration");phase=3;
        }else if(phase==3&&ended_turn_id==1){
            stopped();assert(avoidance_turn.result==S2_MOVE_REACHED);
            assert(!motion.actual_left_rpm_x10&&!motion.actual_right_rpm_x10);
            pass("both wheel targets are reached and settled before the scan is released");
            replay_until=ticks+20;phase=4;
        }else if(phase==4){
            stopped();
            if(ticks==replay_until){
                pass("a refreshed request with the same ID cannot start another turn");
                nav_phase=S5_NAV_RIGHT;nav_turn_id=2;phase=5;
            }
        }else if(phase==5&&compare[0]&&compare[3]){
            const S2_COMMAND command=read_command();
            assert(command.left>0&&command.right<0&&!compare[1]&&!compare[2]);
            pass("a new right intent turns the opposite way");nav_stale=1;phase=6;
        }else if(phase==6){
            stopped();pass("stale S5 navigation cancels a running turn");nav_stale=0;phase=7;
        }else if(phase==7){
            stopped();assert(avoidance_turn.result==S2_MOVE_CANCELLED);
            if(ended_turn_id==2){
                pass("freshness recovery does not restart a cancelled turn");
                nav_busy=1;phase=8;
            }
        }else if(phase==8){
            stopped();pass("busy S5 snapshot cannot permit motion");nav_busy=0;nav_phase=S5_NAV_BLOCKED;nav_turn_id=0;phase=9;
        }else if(phase==9){
            stopped();pass("blocked or invalid range intent keeps the motor loop stopped");nav_phase=S5_NAV_CLEAR;phase=10;
        }else if(phase==10&&moving()){
            const S2_COMMAND command=read_command();
            assert(command.left==350&&command.right==350);
            pass("clear front returns to the unchanged line-following targets");nav_phase=S5_NAV_LEFT;nav_turn_id=3;phase=11;
        }else if(phase==11&&compare[1]&&compare[2]){
            assert(app_controller_submit_stop(105,99,operating_ms+1000U)==E_OK);phase=12;
        }else if(phase==12){
            stopped();assert(app_controller_take_reply(reply)==E_OK&&((reply[8]|((unsigned)reply[9]<<8))>>6)==0);
            pass("STOP during avoidance is acknowledged after controller-owned zero request");
            replay_until=ticks+30;phase=13;
        }else if(phase==13){
            stopped();nav_phase=ticks%6U;nav_turn_id=ticks+10U;pattern=ticks%4U;
            if(ticks==replay_until){pass("STOP cannot be overwritten by later scans, turn intents or line readings");longjmp(done,1);}
        }
#else
    }else if(scenario>=7){
        run_bypass_cycle();
#endif
    }else if(scenario==5){
        if(phase==0&&moving()){
            assert(app_controller_submit_stop(301,99,operating_ms+1000U)==E_OK);phase=1;
        }else if(phase==1){
            stopped();assert(app_controller_take_reply(reply)==E_OK);
            assert(app_controller_arm_resume(operating_ms+1000U,&resume_ticket_id)==0);
            assert(app_controller_submit_resume(resume_ticket_id,99,operating_ms+1000U)==0);
            stale=1;phase=2;
        }else if(phase==2){
            stopped();assert(app_controller_take_reply(reply)==E_OK&&(reply[9]&2U)&&
                (((reply[8]|((unsigned)reply[9]<<8))>>6)&7U)==APP_RESUME_NOT_READY);
            assert(app_controller_poll()&APP_CTRL_LATCH);
            pass("actual controller refuses Resume with stale line input and keeps motor outputs zero");
            stale=0;phase=3;
        }else if(phase==3&&motion.actual_left_rpm_x10==0&&motion.actual_right_rpm_x10==0){
            assert(app_controller_arm_resume(operating_ms+1000U,&resume_ticket_id)==0);
            assert(app_controller_submit_resume(resume_ticket_id,99,operating_ms+1000U)==0);phase=4;
        }else if(phase==4){
            assert(app_controller_take_reply(reply)==E_OK&&(reply[9]&2U)&&
                (((reply[8]|((unsigned)reply[9]<<8))>>6)&7U)==0&&!(reply[8]&APP_CTRL_LATCH));
            pass("actual normal controller releases the latch only after fresh stopped feedback and accepted motor handoff");phase=5;
        }else if(phase==5&&moving()){
            const S2_COMMAND command=read_command();
            assert(command.left==350&&command.right==350);
            pass("explicit Resume returns to the existing line targets and actual PID wheel outputs without a reboot");
            assert(app_controller_submit_stop(302,99,operating_ms+1000U)==0);phase=6;
        }else if(phase==6){
            stopped();assert(app_controller_take_reply(reply)==E_OK);
            assert(app_controller_arm_resume(operating_ms+1000U,&resume_ticket_id)==0);
            assert(app_controller_submit_resume(resume_ticket_id,99,operating_ms+1000U)==0);
            assert(app_controller_submit_stop(303,99,operating_ms+1000U)==0);phase=7;
        }else if(phase==7){
            stopped();assert(app_controller_take_reply(reply)==E_OK&&read32(reply)==303&&!(reply[9]&2U));
            pass("STOP supersedes queued Resume before it can reach actual PWM");phase=8;
        }else if(phase==8){
            stopped();nav_phase=ticks%6U;pattern=ticks%4U;
            if(ticks>360U){pass("second STOP remains latched through later line and scan intents after a Resume cycle");longjmp(done,1);}
        }
    }else{
        if(phase==0&&moving()){stale=1;phase=1;}
        else if(phase==1){
            stopped();assert(app_controller_snapshot(snapshot)==E_OK&&(snapshot[4]&APP_CTRL_STATE_MASK)==APP_CTRL_WAITING_LINE);
            stale=0;phase=2;pass("stale ADC snapshot stops actual motor output");
        }else if(phase==2&&moving()){invalid=1;phase=3;}
        else if(phase==3){
            stopped();invalid=0;pattern=7;phase=4;pass("invalid ADC snapshot stops actual motor output");
        }else if(phase==4){
            stopped();pass("unsupported line pattern cannot drive motors");longjmp(done,1);
        }
    }
    app_controller_step(0);
    operating_ms+=(UW)ms;
    const unsigned limit=scenario>=7?1800U:500U;
#ifndef APP_OBSTACLE_BYPASS
    if(ticks>=limit)
        fprintf(stderr,"Scenario %u phase %u timed out (turn %u, result %u, ack %u)\n",
                scenario,phase,(unsigned)avoidance_turn_id,avoidance_turn.result,(unsigned)ended_turn_id);
#endif
    assert(ticks<limit);
    return E_OK;
}
static void run(unsigned which){
    scenario=which;phase=ticks=operating_ms=created=stale=invalid=encoder_fail=0;pattern=0;
    motor_busy=controller_busy=nav_stale=nav_busy=0;nav_phase=S5_NAV_CLEAR;counts=(S2_ENCODER_COUNTS){0,0};
    nav_turn_id=ended_turn_id=last_drive_ms=0;
#ifdef APP_OBSTACLE_BYPASS
    bypass=(OBSTACLE_BYPASS){0};range_hold_ms=0;front_blocked=front_missing=FALSE;
#else
    avoidance_turn_id=0;
    avoidance_turn=(S2_ENCODER_MOVE){0};avoidance_settling=FALSE;
#endif
    for(unsigned i=0;i<4;++i){held[i]=0;compare[i]=0;}
    controller_sem=0;assert(app_controller_init()==E_OK);
    s_command_sem=2;s_feedback_sem=3;s_motor_ready=s_started=FALSE;
    s_command[6]=0;s_feedback.controls=0;
    if(setjmp(done)==0)motor_task(0,NULL);
}
static void test_command_mailbox(void){
    S2_CONTROL_STATE motor;
    const S2_ENCODER_COUNTS edges={0,0};
    const UW timestamp=UINT32_MAX-20U;
    S2_COMMAND command;
    s_command_sem=2;
    assert(sizeof(s_command)==7&&!read_command().valid);
    assert(subsystem2_set_duty(10,20)==E_OBJ&&!read_command().valid);
    s_motor_ready=TRUE;
    operating_ms=timestamp-1000000U;
    for(INT rpm=-S2_MAX_RPM_X10;rpm<=S2_MAX_RPM_X10;++rpm){
        assert(subsystem2_set_signed_rpm_x10(rpm,-rpm)==E_OK);
        command=read_command();
        assert(command.valid&&command.timestamp_ms==timestamp);
        assert(command.mode==(rpm==0?S2_MODE_STOP:S2_MODE_RPM));
        assert(command.left==rpm&&command.right==-rpm);
    }
    for(INT duty=0;duty<=100;++duty){
        assert(subsystem2_set_duty(duty,100-duty)==E_OK);
        command=read_command();
        assert(command.valid&&command.mode==S2_MODE_DUTY&&command.timestamp_ms==timestamp);
        assert(command.left==duty&&command.right==100-duty);
    }
    assert(subsystem2_set_signed_rpm_x10(S2_MAX_RPM_X10+1,0)==E_PAR);
    assert(subsystem2_set_duty(0,101)==E_PAR);
    command=read_command();
    assert(command.valid&&command.mode==S2_MODE_DUTY&&command.left==100&&command.right==0);
    pass("seven-byte mailbox preserves all signed RPM and duty values, STOP, empty and rejected input");

    s2_control_init(&motor,timestamp,edges);
    s2_control_step(&motor,timestamp+S2_COMMAND_MAX_AGE_MS,edges,&command,TRUE,TRUE);
    assert(motor.data.command_active&&motor.data.duty_left_x100>0);
    s2_control_step(&motor,timestamp+S2_COMMAND_MAX_AGE_MS+1U,edges,&command,TRUE,TRUE);
    assert(motor.data.stop_reason==S2_STOP_COMMAND_TIMEOUT);
    assert(motor.data.duty_left_x100==0&&motor.data.duty_right_x100==0);
    pass("packed timestamp wraps with the original watchdog boundary");
}
static void test_pid_ramp(UW start){
    S2_CONTROL_STATE motor;
    S2_ENCODER_COUNTS edges={0,0};
    S2_COMMAND cmd={.mode=S2_MODE_RPM,.left=350,.right=350,.valid=TRUE};
    INT before_change=0, after_change=0;
    const INT one_tick_step=S2_PID_SLEW_X100_PER_S*S2_PERIOD_MS/1000;
    s2_control_init(&motor,start,edges);
    for(UW tick=S2_PERIOD_MS;tick<=2000;tick+=S2_PERIOD_MS){
        if(tick>200){edges.left+=7;edges.right+=6;}
        cmd.timestamp_ms=start+tick;
        if(tick==1980)cmd.left=260;
        s2_control_step(&motor,start+tick,edges,&cmd,TRUE,TRUE);
        if(tick==1960)before_change=motor.data.duty_left_x100;
        if(tick==1980)after_change=motor.data.duty_left_x100;
    }
    INT sample_step=after_change-motor.data.duty_left_x100;
    assert(before_change>after_change);
    assert(sample_step>0&&sample_step<=one_tick_step);
    assert(motor.data.speed_timestamp_ms==start+2000);
    assert(motor.data.speed_period_ms==S2_SPEED_SAMPLE_MS);
    assert(motor.data.faults==0);

    cmd.left=0;cmd.timestamp_ms=start+2020;
    s2_control_step(&motor,start+2020,edges,&cmd,TRUE,TRUE);
    assert(motor.data.duty_left_x100==0&&motor.data.duty_right_x100>0);
    s2_control_step(&motor,start+2140,edges,&cmd,TRUE,TRUE);
    assert(motor.data.stop_reason==S2_STOP_COMMAND_TIMEOUT);
    assert(motor.data.duty_left_x100==0&&motor.data.duty_right_x100==0);
}
int main(void){
    INT l,r;
    for(unsigned p=0;p<4;++p){
        app_line_targets(p,&l,&r);
        assert(l==(p==2?260:350)&&r==(p==1?260:350));
    }
    pass("actual analog policy: white/white and black/black forward; individual corrections");
    test_command_mailbox();
    test_pid_ramp(0);
    test_pid_ramp(UINT32_MAX-1990U);
    pass("steering update before a speed sample respects the PID ramp across timer wrap; zero/expiry bypass it");
    run(0);run(1);run(2);run(3);run(5);run(6);run(15);
#ifdef APP_OBSTACLE_BYPASS
    run(7);run(8);run(9);run(10);run(11);run(12);run(13);run(14);run(16);
#else
    run(4);
#endif
    printf("SUMMARY: %u actual controller/PID/motor checks passed; no hardware or sockets.\n",checks);return 0;
}
'''
with tempfile.TemporaryDirectory(prefix='robocar-ao-stop-') as directory:
    c=Path(directory)/'stop_path.c';exe=Path(directory)/'stop_path.exe'
    c.write_text(prefix+'\n#include "motion_control.c"\n#include "odometry.c"\n'+api+loop+controller+tests,encoding='utf-8')
    for flags in ([],['-DAPP_OBSTACLE_BYPASS=1']):
        subprocess.run(['gcc','-std=c11','-Wall','-Wextra','-Werror',*flags,
                        '-I',str(root/'app_program'),'-I',str(root/'app_program/subsystem2'),
                        str(c),'-o',str(exe)],check=True)
        subprocess.run([str(exe)],check=True)
