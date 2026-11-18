"""ARM-compile actual startup/motor profiles using flags from a retained build log.
Run in WSL; optionally pass a newer stationary build log as the first argument.
Outputs are temporary objects only; no hardware or firmware flashing.
"""
from pathlib import Path
import shlex,subprocess,tempfile,sys
root=Path(__file__).resolve().parents[2]
build_log=Path(sys.argv[1]) if len(sys.argv)>1 else root/'testing/subsystem1/logs/2026-11-17-resume-stationary-build.txt'
b=build_log.read_bytes()
log=b.decode('utf-16') if b.startswith((b'\xff\xfe',b'\xfe\xff')) else b.decode('utf-8-sig')
line=next(s for s in log.splitlines() if s.startswith('arm-none-eabi-gcc ') and '../app_program/app_main.c' in s)
base=shlex.split(line)
def compile_source(source,test,mode,path):
    args=base.copy()
    args.insert(1,'-fstack-usage')
    for i,arg in enumerate(args):
        if arg.startswith('-DAPP_TEST='):args[i]='-DAPP_TEST='+str(test)
        elif arg.startswith('-DAPP_BENCH_MODE='):args[i]='-DAPP_BENCH_MODE='+str(mode)
        elif arg.startswith('-MF'):args[i]='-MF'+str(path.with_suffix('.d'))
        elif arg.startswith('-MT'):args[i]='-MT'+str(path)
    args[args.index('-o')+1]=str(path)
    args[-1]=source
    subprocess.run(args,check=True,stdout=subprocess.PIPE,stderr=subprocess.PIPE,cwd=root/'build_make')
    return subprocess.check_output(['arm-none-eabi-nm',str(path)],text=True)
with tempfile.TemporaryDirectory(prefix='robocar-profile-') as tmp:
    tmp=Path(tmp)
    for test,mode,name in [(0,0,'integrated normal'),(1,0,'integrated stationary'),(0,1,'explicit IR bench'),(1,2,'explicit encoder bench')]:
        app_symbols=compile_source('../app_program/app_main.c',test,mode,tmp/'app.o')
        frames={}
        for row in (tmp/'app.su').read_text().splitlines():
            fields=row.split('\t')
            if len(fields)>1:frames[fields[0].rsplit(':',1)[-1]]=int(fields[1])
        control_frame=frames.get('vehicle_controller_task',0)+frames.get('app_controller_step',0)
        assert control_frame<=768,(name,control_frame)
        motor_symbols=compile_source('../app_program/subsystem2/subsystem2.c',test,mode,tmp/'motor.o')
        compile_source('../app_program/subsystem2/subsystem2_test.c',test,mode,tmp/'motor_test.o')
        ultra_symbols=compile_source('../app_program/subsystem5/subsystem5.c',test,mode,tmp/'ultra.o')
        assert 'subsystem2_set_' not in ultra_symbols,'S5 must never request wheels'
        assert any(s.split()[-2:]==['T','telemetry_get_scan'] for s in ultra_symbols.splitlines())
        motor_present=any(s.split()[-1:] == ['motor_task'] for s in motor_symbols.splitlines())
        assert motor_present == (not test or mode==2),name
        passive=any(s.split()[-1:] == ['encoder_feedback_task'] for s in motor_symbols.splitlines())
        assert passive == (test==1 and mode==0),name
        if test and mode==0:
            assert not any('pwm_set_' in s for s in motor_symbols.splitlines()),'Passive encoder task must have no PWM calls'
        assert any(s.split()[-2:]==['T','telemetry_get_travel'] for s in motor_symbols.splitlines())
        s1_present=any(s.split()[-1:] == ['subsystem1_init'] for s in app_symbols.splitlines())
        assert s1_present == (mode==0),name
        if mode==1:assert any(s.split()[-1:] == ['ir_adc_test'] for s in app_symbols.splitlines())
        if mode==2:assert any(s.split()[-1:] == ['subsystem2_test'] for s in app_symbols.splitlines())
        print('PASS: '+name+' actual ARM compilation and startup/motor symbols')
print('PASS: stationary integrated TEST=1 collects encoders without PWM; hardware benches require explicit APP_MODE.')
