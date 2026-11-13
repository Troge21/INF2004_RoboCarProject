"""Link the real weak defaults, then an owner override in a separate object."""
from pathlib import Path
import subprocess,tempfile,os,sys
root=Path(__file__).resolve().parents[2]
# Weak override semantics are ELF target semantics; Windows COFF cannot model them.
if os.name=='nt':
    path=Path(__file__).resolve().as_posix()
    unix='/mnt/'+path[0].lower()+path[2:]
    subprocess.run(['wsl','-d','Ubuntu','-u','root','--exec','python3',unix],check=True)
    raise SystemExit(0)

stub=r'''
#include <stdint.h>
#define APP_H
#define EXPORT
#define IMPORT extern
#define E_OK 0
#define E_PAR (-17)
#define E_NOSPT (-9)
#define TK_SUPPORT_MEMLIB 0
typedef unsigned int UW;
typedef struct {UW lo,hi;} SYSTIM;
int tk_get_tim(SYSTIM *out);
'''
main=r'''
#include <assert.h>
#include <stdio.h>
#include "telemetry_sources.h"
int tk_get_tim(SYSTIM *s){s->lo=1500;s->hi=0;return 0;}
uint32_t s1_resource_stack_capacity(void){return 2048;}
int main(void){
    TELEMETRY_BARCODE b={0};TELEMETRY_SCAN scan;TELEMETRY_RESOURCES r;
#ifdef WITH_OWNER
    assert(telemetry_get_barcode(&b)==0&&b.code==138&&b.navigation==1&&b.event_sequence==42);
#else
    b.code=201;assert(telemetry_get_barcode(&b)==E_NOSPT&&b.code==201);
#endif
    assert(telemetry_get_barcode(NULL)==E_PAR);
    assert(telemetry_get_scan(&scan)==E_NOSPT);
    assert(telemetry_get_resources(&r)==0&&r.free_heap==UINT32_MAX&&
        r.allocated_stacks==2048&&r.stack_high_water==UINT32_MAX&&r.timestamp_ms==1500);
    puts("PASS: optional producer absence is explicit; owner override enables data without changing S1.");
}
'''
owner=r'''
#include "telemetry_sources.h"
int telemetry_get_barcode(TELEMETRY_BARCODE *b){
    if(!b)return -17;
    b->timestamp_ms=1500;b->code=138;b->navigation=1;b->event_sequence=42;return 0;
}
'''
with tempfile.TemporaryDirectory(prefix='robocar-producer-hooks-') as folder:
    p=Path(folder)
    for name,text in [('stub.h',stub),('main.c',main),('owner.c',owner)]:(p/name).write_text(text,encoding='utf-8')
    base=['gcc','-std=c11','-Wall','-Wextra','-Werror','-include',str(p/'stub.h'),
          '-I',str(root/'testing/subsystem1/host/include'),'-I',str(root/'app_program'),
          str(root/'app_program/subsystem1/s1_sources.c'),str(p/'main.c')]
    for overridden in [False,True]:
        exe=p/('owner.exe' if overridden else 'default.exe')
        args=base+(['-DWITH_OWNER',str(p/'owner.c')] if overridden else [])+['-o',str(exe)]
        subprocess.run(args,check=True);subprocess.run([str(exe)],check=True)
