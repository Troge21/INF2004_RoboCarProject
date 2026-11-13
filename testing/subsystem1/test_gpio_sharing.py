"""Actual GPIO dispatcher: encoder and ultrasonic callbacks coexist."""
from pathlib import Path
import subprocess
import tempfile
root=Path(__file__).resolve().parents[2]
source=(root/'app_program/gpio_irq.c').read_text(encoding='utf-8')
source='\n'.join(s for s in source.splitlines() if not s.startswith('#include <'))
prefix=r'''
#include <assert.h>
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#define APP_H
#define TK_SUPPORT_SMP 1
#define TRUE 1
#define FALSE 0
#define E_OK 0
#define E_PAR (-17)
#define E_CTX (-25)
#define E_OBJ (-41)
#define TA_HLNG 1
#define IO_BANK0_BASE 0U
#define GPIO_IN 4096U
#define TIMER_TIMERAWL 4100U
#define DI(sts) ((sts)=0)
#define EI(sts) ((void)(sts))
typedef unsigned UINT,BOOL;
typedef uint32_t UW;
typedef int ER;
typedef void (*FP)(UINT);
typedef struct {UINT intatr;FP inthdr;} T_DINT;
static UW raw[4],enabled[4];
static unsigned installs,core=1,seen[30],last_events[30];
static void (*handler)(UINT);
static UINT tk_get_prc(void){return core;}
static ER tk_def_int(UINT irq,T_DINT *d){assert(irq==13);++installs;handler=d->inthdr;return E_OK;}
static void ClearInt(UINT irq){assert(irq==13);}
static void EnableInt(UINT irq,UINT pri){assert(irq==13&&pri==3);}
static UW in_w(UINT r){
    if(r==GPIO_IN)return 0x1234U;
    if(r==TIMER_TIMERAWL)return 999U;
    assert(r>=0x120U&&r<=0x12cU);return raw[(r-0x120U)/4U]&enabled[(r-0x120U)/4U];
}
static void out_w(UINT r,UW value){assert(r>=0xf0U&&r<=0xfcU);raw[(r-0xf0U)/4U]&=~value;}
static void set_w(UINT r,UW value){assert(r>=0x100U&&r<=0x10cU);enabled[(r-0x100U)/4U]|=value;}
static void clr_w(UINT r,UW value){assert(r>=0x100U&&r<=0x10cU);enabled[(r-0x100U)/4U]&=~value;}
'''
tests=r'''
static void event(UINT pin,UINT edges,UW levels,UW stamp){
    assert(levels==0x1234U&&stamp==999U);seen[pin]++;last_events[pin]=edges;
}
int main(void){
    assert(app_gpio_irq_register(16,8,event)==E_OK);
    assert(app_gpio_irq_register(0,8,event)==E_OK);
    assert(app_gpio_irq_register(5,12,event)==E_OK&&installs==1);
    assert(app_gpio_irq_enable(16,TRUE)==E_OK&&app_gpio_irq_enable(0,TRUE)==E_OK);
    assert(app_gpio_irq_enable(5,TRUE)==E_OK);
    raw[0]=8U|(8U<<20);raw[2]=8U;handler(13);
    assert(seen[0]==1&&seen[16]==1&&seen[5]==1&&last_events[5]==8);
    assert(raw[0]==0&&raw[2]==0);
    assert(app_gpio_irq_enable(5,FALSE)==E_OK);
    raw[0]=8U|(4U<<20);raw[2]=8U;handler(13);
    assert(seen[0]==2&&seen[16]==2&&seen[5]==1); /* echo wait cannot mask encoders */
    assert(app_gpio_irq_enable(5,TRUE)==E_OK&&raw[0]==0);
    raw[0]=4U<<20;handler(13);assert(seen[5]==2&&last_events[5]==4);
    assert(app_gpio_irq_register(16,8,event)==E_OBJ);
    core=2;assert(app_gpio_irq_enable(5,FALSE)==E_CTX);core=1;
    assert(app_gpio_irq_unregister(5)==E_OK);
    raw[0]=8U;raw[2]=8U;handler(13);
    assert(seen[0]==3&&seen[16]==3&&installs==1);
    puts("PASS: actual shared IRQ13 dispatch, per-pin echo masking, encoder continuity, edge clearing and core ownership");
    return 0;
}
'''
with tempfile.TemporaryDirectory() as d:
    c=Path(d)/'gpio.c';exe=Path(d)/'gpio.exe'
    c.write_text(prefix+source+tests,encoding='utf-8')
    subprocess.run(['gcc','-std=c11','-Wall','-Wextra','-Werror','-I',str(root/'app_program'),str(c),'-o',str(exe)],check=True)
    subprocess.run([str(exe)],check=True)
