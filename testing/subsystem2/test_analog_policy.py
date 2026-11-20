"""Check actual integrated AO policy in RPM and duty modes; no hardware."""
from pathlib import Path
import subprocess
import tempfile
root = Path(__file__).resolve().parents[2]
s = (root/'app_program/app_main.c').read_text(encoding='utf-8')
policy = s[s.index('static INT app_tape_level'):s.index('static ER app_motor_request')]
for rpm in (0, 1):
    prefix = ('#include <assert.h>\n#include <stdint.h>\n'
              'typedef int INT; typedef unsigned UINT; typedef uint32_t UW;\n'
              'typedef struct { UINT pattern; int valid; uint16_t centre_adc, right_adc; } S3_LINE_DATA;\n'
              f'#define S2_LINE_USE_RPM_CONTROL {rpm}\n'
              '#define APP_FORWARD_DUTY 20\n#define APP_INNER_DUTY 15\n')
    forward, partial, transition, bend, inner = (260, 225, 218, 169, 120) if rpm else (20, 18, 17, 16, 15)
    checks = f'''
int main(void) {{
    INT l, r;
    S3_LINE_DATA line = {{.valid = 1, .centre_adc = 145, .right_adc = 165}};
    app_line_targets(&line, &l, &r);
    assert(l == {forward} && r == {forward});
    line.centre_adc = 400;
    app_line_targets(&line, &l, &r);
    assert(l > {partial} && l < {forward} && r == {forward});
    line.centre_adc = 500; line.pattern = 2;
    app_line_targets(&line, &l, &r);
    assert(l == {partial} && r == {forward});
    line.centre_adc = 550;
    app_line_targets(&line, &l, &r);
    assert(l == {transition} && r == {forward});
    line.centre_adc = 650;
    app_line_targets(&line, &l, &r);
    assert(l == {bend} && r == {forward});
    line.centre_adc = 1188;
    app_line_targets(&line, &l, &r);
    assert(l == {inner} && r == {forward});
    line.centre_adc = 145; line.pattern = 0;
    app_line_targets(&line, &l, &r);
    assert(l == {forward} && r == {forward});
    line.right_adc = 1550; line.pattern = 1;
    app_line_targets(&line, &l, &r);
    assert(l == {forward} && r == {partial});
    line.right_adc = 2210;
    app_line_targets(&line, &l, &r);
    assert(l == {forward} && r == {bend});
    line.right_adc = 2680;
    app_line_targets(&line, &l, &r);
    assert(l == {forward} && r == {inner});
    line.centre_adc = 500; line.pattern = 3;
    app_line_targets(&line, &l, &r);
    assert(l == {forward} && r == {forward});
    line.valid = 0;
    app_line_targets(&line, &l, &r);
    assert(l == 0 && r == 0);
    line.valid = 1; line.pattern = 4;
    app_line_targets(&line, &l, &r);
    assert(l == 0 && r == 0);
    return 0;
}}
'''
    with tempfile.TemporaryDirectory() as d:
        c = Path(d)/'policy.c'; exe = Path(d)/'policy.exe'
        c.write_text(prefix+policy+checks,encoding='utf-8')
        subprocess.run(['gcc','-std=c11','-Wall','-Wextra','-Werror',str(c),'-o',str(exe)],check=True)
        subprocess.run([str(exe)],check=True)
print('PASS: actual steering scales each analog sensor, caps the correction and releases it as soon as the tape reading fades')
