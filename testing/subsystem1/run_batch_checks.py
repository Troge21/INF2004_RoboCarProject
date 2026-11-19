"""Consolidated OFFLINE checks. Never accesses the Pico, broker or hotspot."""
from pathlib import Path
import concurrent.futures,datetime,json,os,shutil,subprocess,sys,tempfile
root=Path(__file__).resolve().parents[2]
gcc=shutil.which('gcc') or 'C:/msys64/ucrt64/bin/gcc.exe'
node=shutil.which('node') or 'C:/Program Files/nodejs/node.exe'
env=os.environ.copy();env['PATH']=str(Path(gcc).parent)+os.pathsep+env.get('PATH','')
env['PYTHONUTF8']='1'
stamp=datetime.datetime.now(datetime.timezone.utc).strftime('%Y-%m-%dT%H-%M-%S')
logdir=root/'testing/subsystem1/logs'/('s1-batch-'+stamp)
logdir.mkdir(exist_ok=False)
only=set(sys.argv[1:]);results=[]
def command(args):
    p=subprocess.run([str(x) for x in args],cwd=root,env=env,text=True,encoding='utf-8',errors='replace',capture_output=True)
    return p.returncode,p.stdout+p.stderr
def c_case(name,source,extra=(),wire=False):
    def run():
        with tempfile.TemporaryDirectory(prefix='robocar-batch-') as folder:
            exe=Path(folder)/(name+'.exe')
            args=[gcc,'-std=c11','-O2','-Wall','-Wextra','-Werror','-I',root/'testing/subsystem1/host/include',*extra,root/source]
            if wire:args += [root/'app_program/subsystem1/s1_protocol.c',root/'app_program/subsystem3/barcode_decoder.c']
            code,text=command(args+['-o',exe])
            if code:return code,text
            code,output=command([node,root/'testing/subsystem1/host/check_wire.mjs',exe] if wire else [exe])
            return code,text+output
    return name,run
cases=[
    c_case('odometer','testing/subsystem2/test_odometry.c',[root/'app_program/subsystem2/odometry.c','-I',root/'app_program/subsystem2']),
    c_case('encoder-move','testing/subsystem2/test_encoder_move.c',['-I',root/'app_program/subsystem2']),
    c_case('s4-samples','testing/subsystem4/test_gy511_io.c'),
    c_case('s4-snapshot','testing/subsystem1/host/test_imu_snapshot.c',['-I',root/'config']),
    c_case('s4-trace','testing/subsystem1/host/test_imu_snapshot.c',['-I',root/'config','-DAPP_TEST=1']),
    c_case('s4-motor-trace','testing/subsystem1/host/test_imu_snapshot.c',['-I',root/'config','-DAPP_S4_MOTOR_TRACE=1']),
    c_case('s5-scan-stationary','testing/subsystem1/host/test_s5_scan.c',['-DAPP_TEST=1']),
    c_case('s5-scan-normal','testing/subsystem1/host/test_s5_scan.c',['-DAPP_TEST=0']),
    c_case('s5-scan-bypass','testing/subsystem1/host/test_s5_scan.c',['-DAPP_TEST=0','-DAPP_OBSTACLE_BYPASS=1']),
    c_case('current-layout','testing/subsystem1/host/measure_layout.c'),
    c_case('s1-codec-bridge','testing/subsystem1/host/test_telemetry.c'),
    c_case('s1-bridge-normal','testing/subsystem1/host/test_telemetry.c',['-DAPP_TEST=0']),
    c_case('mqtt-port','testing/subsystem1/host/test_status_mqtt.c'),
    c_case('controller-normal','testing/subsystem1/host/test_controller.c',['-DAPP_TEST=0']),
    c_case('controller-stationary','testing/subsystem1/host/test_controller.c',['-DAPP_TEST=1']),
    c_case('wire-vectors','testing/subsystem1/host/emit_frames.c',wire=True),
]
cases += [('dashboard',lambda:command([node,'--test',*sorted((root/'dashboard/tests').glob('*.test.mjs'))])),
          ('recovery-recorder',lambda:command([node,'--test',root/'testing/subsystem1/host/recovery_probe.test.mjs']))]
for name,path in [
    ('wifi-latency','testing/subsystem1/test_wifi_latency.py'),
    ('passive-feedback','testing/subsystem2/test_passive_feedback.py'),
    ('actual-motor-stop','testing/subsystem1/test_motor_stop_path.py'),
    ('heap-snapshot','testing/subsystem1/test_memory_stats.py'),
    ('producer-linkage','testing/subsystem1/test_producer_hooks.py'),
    ('shared-gpio','testing/subsystem1/test_gpio_sharing.py'),
    ('analog-policy','testing/subsystem2/test_analog_policy.py'),
    ('analog-and-barcode','testing/subsystem3/test_barcode.py')]:
    cases.append((name,lambda path=path:command([sys.executable,root/path])))
def syntax():
    output=''
    for path in ['dashboard/dist/app.js','dashboard/dist/protocol.js','testing/subsystem1/recovery_probe.mjs','testing/subsystem1/observe_controller.mjs']:
        code,text=command([node,'--check',root/path]);output+=text
        if code:return code,output
    return 0,'PASS: current dashboard and recorder JavaScript syntax.\n'
cases.append(('javascript-syntax',syntax))
def execute(item):
    name,run=item
    try:code,output=run()
    except Exception as exc:code,output=1,str(exc)
    (logdir/(name+'.txt')).write_text(output,encoding='utf-8',newline='\n')
    return {'name':name,'passed':code==0,'exit_code':code,'log':(logdir/(name+'.txt')).relative_to(root).as_posix()}
selected=[c for c in cases if not only or c[0] in only]
if only-set(c[0] for c in cases):raise SystemExit('Unknown check name')
with concurrent.futures.ThreadPoolExecutor(max_workers=4) as pool:
    for result in pool.map(execute,selected):
        results.append(result);print(('PASS ' if result['passed'] else 'FAIL ')+result['name'],flush=True)
(logdir/'summary.json').write_text(json.dumps({'offline':True,'checks':results},indent=2)+'\n',encoding='utf-8')
print('Saved '+str(logdir),flush=True)
raise SystemExit(0 if all(r['passed'] for r in results) else 1)
