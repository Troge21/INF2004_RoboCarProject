"""Real S2 measurement loop in TEST=1; no PWM stub exists or may be linked."""
from pathlib import Path
import os, shutil, subprocess, tempfile
root=Path(__file__).resolve().parents[2]
source=(root/'app_program/subsystem2/subsystem2.c').read_text(encoding='utf-8')
api=source[source.index('static ID s_command_sem'):source.index('EXPORT ER subsystem2_init')]
prefix=r'''
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stddef.h>
#include <setjmp.h>
#define APP_H
#define APP_TEST 1
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
#define S2_DRIVE_ENABLED 0
#define S2_FEEDBACK_ENABLED 1
typedef int ID,INT,BOOL,ER;
typedef unsigned UINT;
typedef uint32_t UW;
typedef struct {UW lo,hi;} SYSTIM;
#include "subsystem2.h"
#include "motion_control.h"
#include "odometry.h"
#include "telemetry_sources.h"
static UW now;
static unsigned ticks,reads,failed,busy,checks;
static S2_ENCODER_COUNTS counts;
static jmp_buf done;
static ER tk_wai_sem(ID id,INT count,INT timeout){
    assert((id==1||id==2)&&count==1&&timeout==TMO_POL);
    return id==2&&busy?E_TMOUT:E_OK;
}
static ER tk_sig_sem(ID id,INT n){assert((id==1||id==2)&&n==1);return E_OK;}
static ER tk_get_tim(SYSTIM *t){t->lo=now;t->hi=0;return E_OK;}
ER s2_encoder_start(void){return E_OK;}
ER s2_encoder_read(S2_ENCODER_COUNTS *out){
    ++reads;if(failed)return E_IO;
    /* Start near counter wrap, then move forward, reverse and one wheel. */
    if(ticks>10&&ticks<=20){counts.left+=7;counts.right+=6;}
    if(ticks>20&&ticks<=30){counts.left-=7;counts.right-=6;}
    if(ticks>30&&ticks<=40)counts.right+=6;
    *out=counts;return E_OK;
}
static ER tk_dly_tsk(INT ms);
#include "motion_control.c"
#include "odometry.c"
'''
tests=r'''
static ER tk_dly_tsk(INT ms){
    S2_MOTION_DATA m;TELEMETRY_TRAVEL t;
    static uint64_t previous;
    assert(ms==20);
    assert(subsystem2_get_motion(&m)==E_OK);
    assert(!s_motor_ready&&subsystem2_set_duty(20,20)==E_OBJ);
    assert(subsystem2_set_rpm_x10(350,350)==E_OBJ);
    assert(m.duty_left_x100==0&&m.duty_right_x100==0&&!m.command_active);
    if(ticks<=40){
        assert(telemetry_get_travel(&t)==E_OK&&t.timestamp_ms==now);
        assert(t.distance_mm>=previous);previous=t.distance_mm;
    }
    if(ticks==10){assert(m.encoder_valid&&m.speed_valid&&m.actual_left_rpm_x10==0&&t.distance_mm==0);checks++;}
    if(ticks==20){assert(m.actual_left_rpm_x10>0&&m.actual_right_rpm_x10>0&&m.count_left==70&&m.count_right==60&&t.distance_mm>20);checks++;}
    if(ticks==30){assert(m.actual_left_rpm_x10<0&&m.actual_right_rpm_x10<0&&m.count_left==0&&m.count_right==0&&t.distance_mm>40);checks++;}
    if(ticks==40){assert(m.actual_left_rpm_x10==0&&m.actual_right_rpm_x10>0&&t.distance_mm>50);checks++;failed=1;}
    if(ticks==41){
        assert(!m.encoder_valid&&!m.speed_valid&&(m.faults&S2_FAULT_ENCODER));
        t.distance_mm=123;t.timestamp_ms=456;
        assert(telemetry_get_travel(&t)==E_OBJ&&t.distance_mm==123&&t.timestamp_ms==456);checks++;
        busy=1;m.sequence=123;
        assert(telemetry_get_travel(&t)==E_TMOUT&&t.distance_mm==123);
        assert(subsystem2_get_motion(&m)==E_TMOUT&&m.sequence==123);checks++;
        busy=0;failed=0;
    }
    if(ticks==42){
        assert(telemetry_get_travel(&t)==E_OBJ&&!m.encoder_valid);checks++;
        assert(telemetry_get_travel(NULL)==E_PAR);checks++;
        assert(s_started==FALSE); /* direct entry; no startup/PWM functions extracted */
        printf("PASS: %u actual passive S2 checks; forward/reverse/wrap, path, faults, contention; no PWM linked.\n",checks);
        longjmp(done,1);
    }
    ++ticks;now+=(UW)ms;return E_OK;
}
int main(void){
    TELEMETRY_TRAVEL t={123,456};
    assert(telemetry_get_travel(&t)==E_OBJ&&t.distance_mm==456);checks++;
    s_command_sem=1;s_feedback_sem=2;
    assert(telemetry_get_travel(&t)==E_OBJ&&t.distance_mm==456);checks++;
    counts=(S2_ENCODER_COUNTS){UINT32_MAX-30,UINT32_MAX-25};
    now=0;
    if(setjmp(done)==0)encoder_feedback_task(0,NULL);
    return 0;
}
'''
gcc=shutil.which('gcc') or 'C:/msys64/ucrt64/bin/gcc.exe'
env=os.environ.copy();env['PATH']=str(Path(gcc).parent)+os.pathsep+env.get('PATH','')
with tempfile.TemporaryDirectory(prefix='robocar-passive-') as folder:
    p=Path(folder);c=p/'passive.c';exe=p/'passive.exe'
    c.write_text(prefix+api+tests,encoding='utf-8')
    subprocess.run([gcc,'-std=c11','-O2','-Wall','-Wextra','-Werror',
        '-I',str(root/'app_program'),'-I',str(root/'app_program/subsystem2'),str(c),'-o',str(exe)],check=True)
    subprocess.run([str(exe)],check=True,env=env)
