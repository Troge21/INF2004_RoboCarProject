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
api = motor[motor.index('static ID s_command_sem'):motor.index('#if S2_DRIVE_ENABLED')]
loop_start = motor.index('static void motor_duty')
loop = motor[loop_start:motor.index('#endif',loop_start)]
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
#define TMO_POL 0
#define TMO_FEVR (-1)
#define TA_TFIFO 0
#define APP_START_DELAY_MS 3000U
#define APP_CONTROL_PERIOD_MS 20
#define APP_IR_MAX_AGE_MS 100U
#define APP_FORWARD_DUTY 20
#define APP_INNER_DUTY 15
#define S2_PWM_TOP 6249U
typedef int ID,INT,BOOL,ER;
typedef unsigned UINT;
typedef uint32_t UW;
typedef uint8_t UB;
typedef struct {UW lo,hi;} SYSTIM;
typedef struct {unsigned sematr,isemcnt,maxsem;} T_CSEM;
typedef struct {UINT pattern;UW timestamp_ms;BOOL valid;} S3_LINE_DATA;
#include "subsystem2.h"
#include "motion_control.h"
#include "subsystem5/subsystem5.h"
static UW operating_ms;
static unsigned scenario,phase,ticks,checks,pattern,stale,invalid,compare[4];
static unsigned motor_busy,controller_busy,encoder_fail;
static unsigned nav_phase=S5_NAV_CLEAR,nav_stale,nav_busy;
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
    out->phase=nav_phase;out->timestamp_ms=operating_ms+1000000U-(nav_stale?201U:0U);
    return E_OK;
}
ER s2_encoder_start(void){return E_OK;}
ER s2_encoder_read(S2_ENCODER_COUNTS *out){
    if(encoder_fail)return E_IO;
    if(compare[0])counts.left+=7;
    if(compare[2])counts.right+=6;
    *out=counts;return E_OK;
}
static ER pwm_set_cc(UINT pin,UW value){assert(pin>=8&&pin<=11);compare[pin-8]=value;return E_OK;}
static ER tk_dly_tsk(INT ms);
#include "controller.c"
'''
tests = r'''
static void pass(const char *name){++checks;printf("PASS: %s\n",name);}
static void stopped(void){for(unsigned i=0;i<4;++i)assert(compare[i]==0);}
static BOOL moving(void){return compare[0]>0&&compare[2]>0;}
static ER tk_dly_tsk(INT ms){
    uint8_t reply[10],snapshot[5];
    assert(ms==20);++ticks;
    if(scenario==0){
        if(operating_ms<APP_START_DELAY_MS)stopped();
        else if(phase==0&&moving()){
            assert(s_command.mode==S2_MODE_RPM&&s_command.left==350&&s_command.right==350);
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
            assert((reply[8]&APP_CTRL_STATE_MASK)==APP_CTRL_STOP_LATCHED&&s_command.mode==S2_MODE_STOP);
            assert(s_feedback.duty_left_x100==0&&s_feedback.duty_right_x100==0);
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
            stopped();assert(s_feedback.faults!=0);
            assert(app_controller_submit_stop(104,99,operating_ms+1000U)==E_OK);phase=2;
        }else if(phase==2){
            stopped();assert(app_controller_take_reply(reply)==E_OK&&((reply[8]|((unsigned)reply[9]<<8))>>6)==APP_STOP_FAULT);
            assert((reply[8]&APP_CTRL_STATE_MASK)==APP_CTRL_FAULT&&(reply[8]&APP_CTRL_LATCH));
            pass("S2 encoder fault propagates to controller and never becomes a fabricated successful STOP");
            longjmp(done,1);
        }
    }else if(scenario==4){
        if(phase==0&&moving()){nav_phase=S5_NAV_SCANNING;phase=1;}
        else if(phase==1){
            stopped();assert(app_controller_snapshot(snapshot)==E_OK&&(snapshot[4]&15)==APP_CTRL_SCANNING);
            pass("controller stops the actual motor loop during S5 scans");nav_phase=S5_NAV_LEFT;phase=2;
        }else if(phase==2){
            assert(s_command.left==0&&s_command.right==260);
            assert(app_controller_snapshot(snapshot)==E_OK&&(snapshot[4]&15)==APP_CTRL_TURNING_LEFT);
            pass("left avoidance intent reaches S2 only through controller RPM arbitration");nav_phase=S5_NAV_RIGHT;phase=3;
        }else if(phase==3){
            assert(s_command.left==260&&s_command.right==0);
            pass("right avoidance intent reaches the shared controller");nav_stale=1;phase=4;
        }else if(phase==4){
            stopped();pass("stale S5 navigation cannot continue an avoidance turn");nav_stale=0;nav_busy=1;phase=5;
        }else if(phase==5){
            stopped();pass("busy S5 snapshot cannot permit motion");nav_busy=0;nav_phase=S5_NAV_BLOCKED;phase=6;
        }else if(phase==6){
            stopped();pass("blocked or invalid range intent keeps the motor loop stopped");nav_phase=S5_NAV_CLEAR;phase=7;
        }else if(phase==7&&moving()){
            assert(s_command.left==350&&s_command.right==350);
            pass("clear front returns to the unchanged line-following targets");nav_phase=S5_NAV_LEFT;phase=8;
        }else if(phase==8){
            assert(app_controller_submit_stop(105,99,operating_ms+1000U)==E_OK);phase=9;
        }else if(phase==9){
            stopped();assert(app_controller_take_reply(reply)==E_OK&&((reply[8]|((unsigned)reply[9]<<8))>>6)==0);
            pass("STOP during avoidance is acknowledged only after controller-owned zero request");phase=10;
        }else if(phase==10){
            stopped();nav_phase=ticks%6U;pattern=ticks%4U;
            if(ticks>230U){pass("STOP cannot be overwritten by later scans, turn intents or line readings");longjmp(done,1);}
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
    assert(ticks<500U);
    return E_OK;
}
static void run(unsigned which){
    scenario=which;phase=ticks=operating_ms=created=stale=invalid=encoder_fail=0;pattern=0;
    motor_busy=controller_busy=nav_stale=nav_busy=0;nav_phase=S5_NAV_CLEAR;counts=(S2_ENCODER_COUNTS){0,0};
    for(unsigned i=0;i<4;++i){held[i]=0;compare[i]=0;}
    controller_sem=0;assert(app_controller_init()==E_OK);
    s_command_sem=2;s_feedback_sem=3;s_feedback_ready=s_motor_ready=s_started=FALSE;
    s_command=(S2_COMMAND){0};s_feedback=(S2_MOTION_DATA){0};
    if(setjmp(done)==0)motor_task(0,NULL);
}
int main(void){
    INT l,r;
    for(unsigned p=0;p<4;++p){
        app_line_targets(p,&l,&r);
        assert(l==(p==2?260:350)&&r==(p==1?260:350));
    }
    pass("actual analog policy: white/white and black/black forward; individual corrections");
    run(0);run(1);run(2);run(3);run(4);
    printf("SUMMARY: %u actual controller/PID/motor checks passed; no hardware or sockets.\n",checks);return 0;
}
'''
with tempfile.TemporaryDirectory(prefix='robocar-ao-stop-') as directory:
    c=Path(directory)/'stop_path.c';exe=Path(directory)/'stop_path.exe'
    c.write_text(prefix+'\n#include "motion_control.c"\n'+api+loop+controller+tests,encoding='utf-8')
    subprocess.run(['gcc','-std=c11','-Wall','-Wextra','-Werror',
                    '-I',str(root/'app_program'),'-I',str(root/'app_program/subsystem2'),
                    str(c),'-o',str(exe)],check=True)
    subprocess.run([str(exe)],check=True)
