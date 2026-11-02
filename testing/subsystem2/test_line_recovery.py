"""Host checks for the actual app_main controller policy functions; requires gcc on PATH."""
from pathlib import Path
import subprocess
import tempfile

root = Path(__file__).resolve().parents[2]
source = (root / 'app_program/app_main.c').read_text()
policy = source[source.index('typedef struct {'):source.index('static void vehicle_controller_task')]
header = (root / 'app_program/app.h').read_text()
defines = '\n'.join(line for line in header.splitlines() if line.startswith('#define APP_') and any(k in line for k in ('FORWARD_DUTY', 'INNER_DUTY', 'RECOVERY_')))
types = '''
#include <stdint.h>
#include <stddef.h>
#include <assert.h>
#define EXPORT
#define TRUE 1
#define FALSE 0
typedef unsigned UINT;
typedef int INT;
typedef int BOOL;
typedef uint32_t UW;
'''
tests = '''
int main(void) {
    APP_LINE_TRACKER t = {0};
    int l, r;
    /* Unknown line position must not cause blind motion. */
    app_track_line(&t,0,TRUE,0,&l,&r); assert(l==0 && r==0);
    app_track_line(&t,2,TRUE,10,&l,&r);
    assert(l==APP_FORWARD_DUTY && r==APP_FORWARD_DUTY);
    app_track_line(&t,0,TRUE,20,&l,&r); assert(l==0 && r==0);
    app_track_line(&t,7,TRUE,25,&l,&r);
    assert(l==APP_FORWARD_DUTY && r==APP_FORWARD_DUTY);
    /* Left history survives centre; recover by running right wheel. */
    app_track_line(&t,6,TRUE,30,&l,&r);
    app_track_line(&t,2,TRUE,40,&l,&r);
    app_track_line(&t,0,TRUE,50,&l,&r); assert(l==0 && r==APP_RECOVERY_DUTY);
    app_track_line(&t,0,TRUE,50+APP_RECOVERY_TIMEOUT_MS-1,&l,&r); assert(r==APP_RECOVERY_DUTY);
    app_track_line(&t,0,TRUE,50+APP_RECOVERY_TIMEOUT_MS,&l,&r); assert(l==0 && r==0);
    app_track_line(&t,0,TRUE,5000,&l,&r); assert(l==0 && r==0);
    /* Reacquire and recover in the other direction. */
    app_track_line(&t,1,TRUE,5010,&l,&r); assert(l==APP_FORWARD_DUTY && r==0);
    app_track_line(&t,0,TRUE,5020,&l,&r); assert(l==APP_RECOVERY_DUTY && r==0);
    /* Stale data clears history; no later blind recovery. */
    app_track_line(&t,1,FALSE,5030,&l,&r); assert(l==0 && r==0 && t.last_side==0);
    app_track_line(&t,0,TRUE,5040,&l,&r); assert(l==0 && r==0);
    for (unsigned p=5;p<=8;++p) {
        if (p==6 || p==7) continue;
        app_track_line(&t,4,TRUE,6000,&l,&r);
        app_track_line(&t,p,TRUE,6010,&l,&r); assert(l==0 && r==0 && t.last_side==0);
    }
    /* Unsigned elapsed-time subtraction must survive the uptime wrap. */
    app_track_line(&t,3,TRUE,UINT32_MAX-50,&l,&r);
    app_track_line(&t,0,TRUE,UINT32_MAX-10,&l,&r);
    app_track_line(&t,0,TRUE,10,&l,&r); assert(l==APP_RECOVERY_DUTY && r==0);
    app_track_line(&t,0,TRUE,APP_RECOVERY_TIMEOUT_MS,&l,&r); assert(l==0 && r==0);
    return 0;
}
'''
with tempfile.TemporaryDirectory() as directory:
    c = Path(directory)/'policy.c'
    exe = Path(directory)/'policy.exe'
    c.write_text(types + defines + '\n' + policy + tests)
    subprocess.run(['gcc','-Wall','-Wextra','-Werror',str(c),'-o',str(exe)],check=True)
    subprocess.run([str(exe)],check=True)
print('PASS: recovery direction, reacquisition, timeout, stale/ambiguous stops, uptime wrap')
