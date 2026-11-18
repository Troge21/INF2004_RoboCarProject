"""Exercise actual interface-setup sequence; deterministic driver/lock stubs."""
from pathlib import Path
import os,shutil,subprocess,tempfile
root=Path(__file__).resolve().parents[2]
source=(root/'lib/libwifi/sysdepend/pico_rp2040/cyw43_utk.c').read_text(encoding='utf-8')
start=source.index('    cyw43_thread_enter();\n    cyw43_wifi_set_up')
flow=source[start:source.index('    radio_status.init_result = result;',start)]
prefix=r'''
#include <assert.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#define TM_NET_LWIP 1
#define WIFI_DIAGNOSTIC_SCAN 0
#define CYW43_ITF_STA 0
#define CYW43_COUNTRY_WORLDWIDE 0
#define CYW43_EIO 5
#define CYW43_NONE_PM 0x123U
typedef unsigned char UB;
typedef struct {unsigned itf_state;} cyw43_t;
static cyw43_t cyw43_state;
static struct {UB mac[6];} radio_status;
static unsigned locked,bus_ok,pm_calls,joins,mac_calls,logs;
static int pm_result;
static void cyw43_thread_enter(void){assert(!locked);locked=1;}
static void cyw43_thread_exit(void){assert(locked);locked=0;}
static void cyw43_wifi_set_up(cyw43_t *s,unsigned itf,bool up,unsigned country){
    assert(locked&&itf==0&&up&&country==0);s->itf_state=bus_ok;
}
static int cyw43_wifi_pm(cyw43_t *s,unsigned mode){
    assert(locked&&s->itf_state&&mode==CYW43_NONE_PM);++pm_calls;return pm_result;
}
static int cyw43_wifi_get_mac(cyw43_t *s,unsigned itf,UB *out){
    assert(locked&&s->itf_state&&itf==0&&pm_calls==1&&out==radio_status.mac);
    ++mac_calls;return 0;
}
static int wifi_try_join(void){assert(locked&&pm_calls==1&&mac_calls==1);++joins;return 0;}
static int tm_printf(UB *format,...){(void)format;++logs;return 0;}
static int setup(void){int32_t result;
'''
suffix=r'''
return result;}
int main(void){
    bus_ok=1;assert(setup()==0&&pm_calls==1&&joins==1&&!locked&&logs==0);
    pm_calls=joins=mac_calls=logs=0;pm_result=-5;
    assert(setup()==0&&pm_calls==1&&joins==1&&!locked&&logs==1);
    pm_calls=joins=mac_calls=logs=0;bus_ok=0;
    assert(setup()==-5&&pm_calls==0&&joins==0&&!locked&&logs==1);
    puts("PASS: actual WiFi setup applies no-sleep mode under owner lock before join; mode failure is reported and bus failure skips it.");
}
'''
gcc=shutil.which('gcc') or 'C:/msys64/ucrt64/bin/gcc.exe'
env=os.environ.copy();env['PATH']=str(Path(gcc).parent)+os.pathsep+env.get('PATH','')
with tempfile.TemporaryDirectory(prefix='robocar-wifi-latency-') as folder:
    c=Path(folder)/'wifi.c';exe=c.with_suffix('.exe')
    c.write_text(prefix+flow+suffix,encoding='utf-8')
    subprocess.run([gcc,'-std=c11','-Wall','-Wextra','-Werror',str(c),'-o',str(exe)],check=True)
    subprocess.run([str(exe)],check=True,env=env)
