"""Check actual integrated AO policy in RPM and duty modes; no hardware."""
from pathlib import Path
import subprocess
import tempfile
root = Path(__file__).resolve().parents[2]
s = (root/'app_program/app_main.c').read_text(encoding='utf-8')
policy = s[s.index('static void app_line_targets'):s.index('static ER app_motor_request')]
for rpm in (0, 1):
    prefix = ('#include <assert.h>\ntypedef int INT; typedef unsigned UINT;\n'
              f'#define S2_LINE_USE_RPM_CONTROL {rpm}\n'
              '#define S2_LINE_FORWARD_RPM_X10 350\n#define S2_LINE_INNER_RPM_X10 260\n'
              '#define APP_FORWARD_DUTY 20\n#define APP_INNER_DUTY 15\n')
    forward, inner = (350, 260) if rpm else (20, 15)
    expected = [(forward, forward), (forward, inner), (inner, forward), (forward, forward), (0, 0)]
    checks = ''.join(f'app_line_targets({i},&l,&r);assert(l=={l} && r=={r});' for i,(l,r) in enumerate(expected))
    with tempfile.TemporaryDirectory() as d:
        c = Path(d)/'policy.c'; exe = Path(d)/'policy.exe'
        c.write_text(prefix+policy+'\nint main(void){int l,r;'+checks+'return 0;}\n',encoding='utf-8')
        subprocess.run(['gcc','-std=c11','-Wall','-Wextra','-Werror',str(c),'-o',str(exe)],check=True)
        subprocess.run([str(exe)],check=True)
print('PASS: actual AO policy in RPM/duty modes; both-white/both-black forward, corrections, invalid zero')
