"""Compile and exercise the REAL C decoder (no board required).

python testing/subsystem3/test_barcode.py [--compiler /path/to/zig-or-gcc]
Reference patterns and measured PDF stripe widths are in barcode_vectors.json.
"""
import argparse
import ctypes as c
import json
import os
from pathlib import Path
import random
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
VECTORS = json.loads(Path(__file__).with_name('barcode_vectors.json').read_text())
MASK = 0xffffffff


class Decoder(c.Structure):
    _fields_ = [('widths', c.c_uint32 * 29), ('last_edge', c.c_uint32),
                ('start', c.c_uint32), ('leading', c.c_uint32),
                ('quiet', c.c_uint32), ('count', c.c_uint8),
                ('black', c.c_uint8), ('capturing', c.c_uint8),
                ('pending', c.c_uint8), ('code', c.c_uint8)]


def widths(character, narrow=5000, ratio=3, reverse=False):
    result = []
    for index, char in enumerate('*' + character + '*'):
        result.extend(round(narrow * ratio) if bit == '1' else narrow
                      for bit in VECTORS['patterns'][char])
        if index < 2:
            result.append(narrow)
    return result[::-1] if reverse else result


class BarcodeTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temp = tempfile.TemporaryDirectory()
        library = Path(cls.temp.name) / ('decoder.dll' if os.name == 'nt' else 'decoder.so')
        command = [COMPILER] + (['cc'] if Path(COMPILER).stem == 'zig' else [])
        flags = ['-Wl,--export-all-symbols'] if os.name == 'nt' else ['-fPIC']
        subprocess.run(command + ['-std=c99', '-Wall', '-Wextra', '-Werror', '-shared',
                       '-O2', str(ROOT/'app_program/subsystem3/barcode_decoder.c'),
                       *flags, '-o', str(library)], check=True)
        cls.lib = c.CDLL(str(library))
        cls.lib.s3_barcode_decode.argtypes = [c.POINTER(c.c_uint32), c.c_uint32,
                                             c.c_uint32, c.POINTER(c.c_uint8)]
        cls.lib.s3_barcode_character.argtypes = [c.c_uint8]
        cls.lib.s3_barcode_character.restype = c.c_char
        cls.lib.s3_barcode_reset.argtypes = [c.POINTER(Decoder), c.c_int, c.c_uint32]
        cls.lib.s3_barcode_edge.argtypes = [c.POINTER(Decoder), c.c_int, c.c_uint32,
                                          c.POINTER(c.c_uint8)]
        cls.lib.s3_barcode_poll.argtypes = [c.POINTER(Decoder), c.c_uint32,
                                          c.POINTER(c.c_uint8)]

    @classmethod
    def tearDownClass(cls):
        if os.name == 'nt':
            # All calls have finished; unload before removing the temp DLL.
            import _ctypes
            handle = cls.lib._handle
            cls.lib = None
            _ctypes.FreeLibrary(handle)
        cls.temp.cleanup()

    def decode(self, runs, lead=1000000, trail=1000000):
        code = c.c_uint8(255)
        status = self.lib.s3_barcode_decode((c.c_uint32*29)(*runs), lead, trail,
                                            c.byref(code))
        return status, code.value

    def scan(self, runs, start=0, quiet=1000000, decoder=None):
        decoder = decoder or Decoder()
        if not decoder.last_edge and not decoder.capturing and not decoder.pending:
            self.lib.s3_barcode_reset(c.byref(decoder), 0, start)
        code = c.c_uint8()
        now = (start + quiet) & MASK
        events = []
        status = self.lib.s3_barcode_edge(c.byref(decoder), 1, now, c.byref(code))
        if status: events.append((status, code.value))
        for i, duration in enumerate(runs):
            now = (now + duration) & MASK
            status = self.lib.s3_barcode_edge(c.byref(decoder), i % 2, now, c.byref(code))
            if status: events.append((status, code.value))
        return decoder, now, events

    def poll(self, decoder, now):
        code = c.c_uint8(255)
        status = self.lib.s3_barcode_poll(c.byref(decoder), now & MASK, c.byref(code))
        return status, code.value

    def test_entire_alphabet_both_directions_speeds_ratios(self):
        for char in VECTORS['alphabet']:
            for reverse in (False, True):
                for narrow in (2000, 5000, 12000, 50000, 100000):
                    for ratio in (2, 2.5, 3):
                        with self.subTest(char=char, reverse=reverse, n=narrow, ratio=ratio):
                            status, code = self.decode(widths(char, narrow, ratio, reverse))
                            self.assertEqual(status, 1)
                            self.assertEqual(self.lib.s3_barcode_character(code).decode(), char)
                            self.assertEqual(bool(code & 64), reverse)

    def test_measured_pdf_stripes_at_multiple_scan_speeds(self):
        for vector in VECTORS['pdf_samples']:
            for reverse in (False, True):
                for narrow in (4000, 12000, 50000):
                    runs = [round(x * narrow / min(vector['pixels'])) for x in vector['pixels']]
                    if reverse: runs.reverse()
                    status, code = self.decode(runs)
                    self.assertEqual(status, 1, vector['page'])
                    self.assertEqual(self.lib.s3_barcode_character(code).decode(), vector['character'])

    def test_jitter(self):
        rng = random.Random(2004)
        for char in VECTORS['alphabet']:
            for reverse in (False, True):
                for _ in range(10):
                    runs = [round(w * rng.uniform(.92, 1.08))
                            for w in widths(char, 12000, 2.5, reverse)]
                    status, code = self.decode(runs)
                    self.assertEqual(status, 1)
                    self.assertEqual(self.lib.s3_barcode_character(code).decode(), char)

    def test_malformed_clusters_guards_gaps_and_quiet(self):
        base = widths('A')
        cases = [[5000]*29, [15000]*29]
        for index in (0, 4, 9, 19, 20, 28):
            corrupt = base.copy()
            corrupt[index] = 0
            cases.append(corrupt)
        corrupt = base.copy(); corrupt[0] = 15000; cases.append(corrupt)
        corrupt = base.copy(); corrupt[28] = 15000; cases.append(corrupt)
        corrupt = base.copy(); corrupt[9] = 0xffffffff; cases.append(corrupt)
        cases += [widths('A', 1000), widths('A', 110000), widths('A', 5000, 1.3),
                  widths('A', 5000, 4), widths('*')]
        for runs in cases:
            self.assertEqual(self.decode(runs), (2, 0))
        self.assertEqual(self.decode(base, lead=49999), (2, 0))
        self.assertEqual(self.decode(base, trail=49999), (2, 0))
        corrupt = base.copy()
        for i in range(20, 29): corrupt[i] *= 2
        self.assertEqual(self.decode(corrupt), (2, 0))

    def test_stream_reports_once_after_trailing_quiet(self):
        for char in ('A', 'Z', '0', '%'):
            for reverse in (False, True):
                decoder, now, events = self.scan(widths(char, reverse=reverse))
                self.assertEqual(events, [])
                self.assertEqual(self.poll(decoder, now+49999), (0, 0))
                status, code = self.poll(decoder, now+50000)
                self.assertEqual(status, 1)
                self.assertEqual(self.lib.s3_barcode_character(code).decode(), char)
                self.assertEqual(self.poll(decoder, now+100000), (0, 0))

    def test_timer_wrap_and_long_idle(self):
        decoder, now, events = self.scan(widths('Z', reverse=True), start=MASK-1100000)
        self.assertEqual(events, [])
        self.assertEqual(self.poll(decoder, now+50000)[0], 1)
        self.assertEqual(self.poll(decoder, now+2000000), (0, 0))

    def test_partial_and_stuck_signal_timeout(self):
        decoder, now, _ = self.scan(widths('A')[:10])
        self.assertEqual(self.poll(decoder, now+350001), (3, 0))
        self.assertEqual(self.poll(decoder, now+700000), (0, 0))
        self.lib.s3_barcode_reset(c.byref(decoder), 1, now)
        self.assertEqual(self.poll(decoder, now+1000000), (0, 0))

    def test_early_ink_invalidates_pending_frame(self):
        decoder, now, _ = self.scan(widths('A'))
        code = c.c_uint8()
        self.assertEqual(self.lib.s3_barcode_edge(c.byref(decoder), 1, now+20000,
                                                c.byref(code)), 2)
        self.assertEqual(self.poll(decoder, now+100000), (0, 0))

    def test_resync_discards_capture(self):
        decoder, now, _ = self.scan(widths('A')[:10])
        self.lib.s3_barcode_reset(c.byref(decoder), 0, now)
        self.assertEqual(self.poll(decoder, now+50000), (0, 0))
        decoder, now, _ = self.scan(widths('Z'), start=now, decoder=decoder)
        status, code = self.poll(decoder, now+50000)
        self.assertEqual(status, 1)
        self.assertEqual(self.lib.s3_barcode_character(code), b'Z')

    def test_all_invalid_packed_codes(self):
        for code in range(256):
            valid = bool(code & 128) and (code & 63) < 43
            self.assertEqual(self.lib.s3_barcode_character(code) != b'\0', valid)

    def test_actual_capture_driver_with_register_model(self):
        directory = Path(self.temp.name)
        capture = (ROOT/'app_program/subsystem3/barcode_capture.c').read_text()
        capture = '\n'.join(line for line in capture.splitlines()
                            if not line.startswith('#include <'))
        harness = Path(__file__).with_name('capture_harness.c').read_text()
        source = directory/'capture.c'
        source.write_text(harness.replace('/* CAPTURE_SOURCE */', capture))
        executable = directory/('capture.exe' if os.name == 'nt' else 'capture')
        command = [COMPILER] + (['cc'] if Path(COMPILER).stem == 'zig' else [])
        subprocess.run(command + ['-std=c99', '-Wall', '-Wextra', '-Werror',
                       '-I', str(ROOT/'app_program/subsystem3'), str(source),
                       '-o', str(executable)], check=True)
        subprocess.run([str(executable)], check=True)


    def compile_line_harness(self, pin, extra=None):
        directory = Path(self.temp.name)
        source = directory/f'line-{pin}.c'
        actual = '\n'.join((ROOT/'app_program/subsystem3'/name).read_text()
                           for name in ('line_detection.c', 'subsystem3.c'))
        harness = Path(__file__).with_name('line_input_harness.c').read_text()
        source.write_text(harness.replace('/* SUBSYSTEM_SOURCE */', actual))
        executable = directory/(f'line-{pin}.exe' if os.name == 'nt' else f'line-{pin}')
        command = [COMPILER] + (['cc'] if Path(COMPILER).stem == 'zig' else [])
        command += ['-std=c99', '-Wall', '-Wextra', '-Werror',
                    f'-DAPP_LINE_FOLLOW_ENABLED={pin}', '-DAPP_TEST=0', '-DAPP_RECOVERY_TIMEOUT_MS=200U', '-I', str(ROOT/'app_program/subsystem3'),
                    str(source), str(ROOT/'app_program/subsystem3/barcode_decoder.c'),
                    '-o', str(executable)]
        command += extra or []
        return command, executable

    def test_centre_right_inputs_independent_of_barcode_actual_task(self):
        for enabled in (0, 1):
            with self.subTest(line_follow=enabled):
                command, executable = self.compile_line_harness(enabled)
                subprocess.run(command, check=True)
                subprocess.run([str(executable)], check=True)


if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    parser.add_argument('--compiler', default=shutil.which('gcc') or shutil.which('cc'))
    args, rest = parser.parse_known_args()
    COMPILER = args.compiler
    if not COMPILER:
        parser.error('Install a host C compiler, or pass --compiler /path/to/zig')
    unittest.main(argv=[__file__, *rest], verbosity=2)
