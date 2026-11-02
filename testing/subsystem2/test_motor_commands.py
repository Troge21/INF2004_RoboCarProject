"""Exercise actual S2 command API and motor loop with mocked RTOS/PWM."""
from pathlib import Path
import subprocess
import tempfile
root = Path(__file__).resolve().parents[2]
s = (root/'app_program/subsystem2/subsystem2.c').read_text()
state_api = s[s.index('/* Shared latest-command'):s.index('#if !APP_TEST')]
loop = s[s.index('static void motor_task'):s.index('#endif', s.index('static void motor_task'))]
h = (root/'app_program/subsystem2/subsystem2.h').read_text()
defines = '\n'.join(l for l in h.splitlines() if l.startswith('#define S2_'))
prelude = '''
#include <assert.h>
#include <stdint.h>
#include <setjmp.h>
#define EXPORT
#define TRUE 1
#define FALSE 0
#define E_OK 0
#define E_PAR -17
#define E_OBJ -41
#define E_TMOUT -50
#define TMO_POL 0
typedef int ID, BOOL, INT, ER;
typedef unsigned UINT;
typedef uint32_t UW;
typedef struct { UW lo; } SYSTIM;
static UW clock_ms;
static int sem_result, outputs[2];
static jmp_buf done;
static ER tk_wai_sem(ID id, INT count, INT timeout) { (void)id;(void)count;(void)timeout;return sem_result; }
static ER tk_sig_sem(ID id, INT count) { (void)id;(void)count;return 0; }
static ER tk_get_tim(SYSTIM *t) { t->lo=clock_ms;return 0; }
static void motor_duty(UINT a, UINT b, INT duty, BOOL rev, UINT scale) {
    (void)b;(void)rev;(void)scale; outputs[a==8?0:1]=duty;
}
static void tk_dly_tsk(INT delay) { (void)delay;longjmp(done,1); }
'''
tests = '''
static void tick(void) { if (setjmp(done)==0) motor_task(0,0); }
int main(void) {
    assert(subsystem2_set_duty(25,25)==E_OBJ);
    s_motor_ready=TRUE;
    assert(subsystem2_set_duty(-1,25)==E_PAR);
    assert(subsystem2_set_duty(25,101)==E_PAR);
    tick(); assert(outputs[0]==0 && outputs[1]==0);
    clock_ms=10; assert(subsystem2_set_duty(25,15)==E_OK);
    tick(); assert(outputs[0]==25 && outputs[1]==15);
    clock_ms=10+S2_COMMAND_MAX_AGE_MS;
    tick(); assert(outputs[0]==25 && outputs[1]==15);
    ++clock_ms; tick(); assert(outputs[0]==0 && outputs[1]==0);
    clock_ms=200; assert(subsystem2_set_duty(0,0)==E_OK);
    tick(); assert(outputs[0]==0 && outputs[1]==0);
    sem_result=E_TMOUT;
    assert(subsystem2_set_duty(25,25)==E_TMOUT);
    tick(); assert(outputs[0]==0 && outputs[1]==0);
    sem_result=E_OK;
    clock_ms=UINT32_MAX-10; assert(subsystem2_set_duty(20,0)==E_OK);
    clock_ms=10; tick(); assert(outputs[0]==20 && outputs[1]==0);
    clock_ms=S2_COMMAND_MAX_AGE_MS; tick(); assert(outputs[0]==0 && outputs[1]==0);
    return 0;
}
'''
with tempfile.TemporaryDirectory() as d:
    c=Path(d)/'motor.c'; exe=Path(d)/'motor.exe'
    c.write_text(prelude+defines+'\n'+state_api+loop+tests)
    subprocess.run(['gcc','-Wall','-Wextra','-Werror',str(c),'-o',str(exe)],check=True)
    subprocess.run([str(exe)],check=True)
print('PASS: motor command bounds, readiness, fresh/expired commands, stop, contention, clock wrap')
