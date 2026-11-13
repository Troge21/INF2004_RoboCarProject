"""Actual ARM collector/startup-test stack frames; not a measured runtime peak."""
from pathlib import Path
import subprocess,shlex,tempfile,sys,re
root=Path(__file__).resolve().parents[2]
log=Path(sys.argv[1]) if len(sys.argv)>1 else root/'testing/subsystem1/logs/2026-11-13-s1-batch-stationary-build.txt'
raw=log.read_bytes();text=raw.decode('utf-16') if raw[:2] in [bytes([255,254]),bytes([254,255])] else raw.decode('utf-8-sig')
sources=['../app_program/subsystem1/subsystem1.c','../app_program/subsystem1/subsystem1_test.c',
         '../app_program/app_main.c','../lib/libtm/libtm_printf.c',
         '../app_program/subsystem5/subsystem5.c','../app_program/subsystem5/subsystem5_test.c']
frames={}
with tempfile.TemporaryDirectory(prefix='robocar-s1-stack-') as folder:
    for source in sources:
        line=next(x for x in text.splitlines() if x.startswith('arm-none-eabi-gcc ') and source in x)
        out=Path(folder)/(Path(source).stem+'.o');args=shlex.split(line)
        args[args.index('-o')+1]=str(out)
        args=[('-MF'+str(out.with_suffix('.d'))) if a.startswith('-MF') else '-MT'+str(out) if a.startswith('-MT') else a for a in args]
        args.insert(1,'-fstack-usage')
        subprocess.run(args,cwd=root/'build_make',check=True,capture_output=True,text=True)
        for row in out.with_suffix('.su').read_text().splitlines():
            fields=row.split('\t')
            if len(fields)>=2:frames[fields[0].rsplit(':',1)[-1]]=int(fields[1])
collector=frames['telemetry_task']+max((frames.get(name,0) for name in ['gather_sources','gather_scan','gather_resources']),default=0)
print('Collector static chain:',collector,'/ 2048 bytes')
assert collector<=768,'Collector must reserve at least 1280 bytes for external/kernel/IRQ calls'
initial_header=(root/'include/sys/inittask.h').read_text(encoding='utf-8')
match=re.search(r'INITTASK_STKSZ\s+\(\s*(\d+)\s*\*\s*1024\s*\)',initial_header)
assert match,'Review changed initial-task stack definition'
initial=int(match.group(1))*1024
chain=['usermain','subsystem1_test','tm_printf','tm_vsprintf','out_cons']
known=sum(frames[name] for name in chain)
for name in chain:print(name+':',frames[name])
print('Known startup-test/console chain:',known,'/',initial,'bytes')
assert frames['subsystem1_test']<=256,'On-board vectors must not consume the small startup stack'
assert known<=initial-512,'Startup tests must leave 512 bytes for outer/kernel/IRQ frames'
s5=frames['ultrasonic_task']+max(frames.get('measure',0),
    frames.get('phase_delay',0)+frames.get('navigation_write',0),frames.get('scan_write',0))
print('S5 sensing/scan static chain:',s5,'/ 2048 bytes')
assert s5<=768,'S5 must leave substantial space for kernel/interrupt calls'
s5_startup=known-frames['subsystem1_test']+frames['subsystem5_test']
print('Known S5 startup-test/console chain:',s5_startup,'/',initial,'bytes')
assert s5_startup<=initial-512,'S5 on-board tests must preserve the startup stack margin'
print('PASS: collector, S5 and startup-test stack budgets. Actual runtime high-water still requires measurement.')
