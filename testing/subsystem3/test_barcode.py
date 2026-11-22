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
import re
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
VECTORS = json.loads(Path(__file__).with_name('barcode_vectors.json').read_text())
MASK = 0xffffffff
MAX_RUN_US = int(re.search(r'^#define\s+S3_BC_MAX_RUN_US\s+(\d+)U',
                         (ROOT/'app_program/subsystem3/barcode_decoder.h').read_text(),
                         re.MULTILINE).group(1))


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
        cls.lib.s3_barcode_decode_analog.argtypes = [c.POINTER(c.c_uint32), c.c_uint,
                                                    c.c_uint32, c.POINTER(c.c_uint32),
                                                    c.POINTER(c.c_uint8)]
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

    def test_analog_decoder_handles_one_short_pulse_across_the_alphabet(self):
        for char in 'ABCDEFGHIJKLMNOPQRSTUVWXYZ':
            for reverse in (False, True):
                runs = widths(char, 10000, 3, reverse)
                middle_wide = next(i for i in range(10, 19) if runs[i] == 30000)
                runs[middle_wide:middle_wide+1] = [14000, 2000, 14000]
                quiet, code = c.c_uint32(), c.c_uint8()
                status = self.lib.s3_barcode_decode_analog(
                    (c.c_uint32*len(runs))(*runs), len(runs), 200000,
                    c.byref(quiet), c.byref(code))
                self.assertEqual(status, 1, (char, reverse))
                self.assertEqual(self.lib.s3_barcode_character(code).decode(), char)
                self.assertEqual(bool(code.value & 64), reverse)
                self.assertEqual(quiet.value, 100000)

    def test_analog_template_distinguishes_close_letters_and_rejects_ties(self):
        for reverse in (False, True):
            for first, third, want in ((22400, 17600, b'A'), (20000, 20000, b'\0'),
                                       (17600, 22400, b'B')):
                with self.subTest(reverse=reverse, expected=want):
                    runs = widths('A', narrow=10000)
                    # A and B differ in the first and third bars. Leave every
                    # other bar and space identical and vary their widths.
                    runs[10], runs[12] = first, third
                    if reverse:
                        runs.reverse()
                    quiet, code = c.c_uint32(), c.c_uint8()
                    status = self.lib.s3_barcode_decode_analog(
                        (c.c_uint32*29)(*runs), 29, 200000, c.byref(quiet), c.byref(code))
                    self.assertEqual(status, 2 if want == b'\0' else 1)
                    self.assertEqual(self.lib.s3_barcode_character(code).decode(), want.decode())

    def test_analog_template_rejects_a_corrupted_driving_frame(self):
        # The last live snapshot before the change contained 30 transitions,
        # but the leading guard was distorted enough to favor a wrong letter.
        runs = [20,268,448,86,236,150,114,230,72,90,212,102,66,218,80,
                78,102,254,270,90,54,82,208,112,222,88,58,232,56]
        quiet, code = c.c_uint32(), c.c_uint8()
        status = self.lib.s3_barcode_decode_analog(
            (c.c_uint32*29)(*(n*1000 for n in runs)), 29, 1500000, c.byref(quiet), c.byref(code))
        self.assertEqual((status, code.value), (2, 0))

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
                  widths('A', 5000, 4.2), widths('*')]
        for runs in cases:
            self.assertEqual(self.decode(runs), (2, 0))
        self.assertEqual(self.decode(base, lead=49999), (2, 0))
        self.assertEqual(self.decode(base, trail=49999), (2, 0))
        corrupt = base.copy()
        for i in range(20, 29): corrupt[i] *= 2
        self.assertEqual(self.decode(corrupt), (2, 0))

    def test_intercharacter_gap_is_symmetric_at_uneven_speed(self):
        runs = widths('A', narrow=10000)
        runs[10:19] = [width * 13 // 10 for width in runs[10:19]]
        runs[9] = 6000
        symbol = 0x80 | VECTORS['alphabet'].index('A')
        self.assertEqual(self.decode(runs), (1, symbol))
        self.assertEqual(self.decode(runs[::-1]), (1, symbol | 0x40))
        runs[9] = 5000
        self.assertEqual(self.decode(runs), (2, 0))
        self.assertEqual(self.decode(runs[::-1]), (2, 0))

    def test_recorded_reverse_a_hand_scan(self):
        # Actual 9 November trace: rejected by the former +/-25% check.
        runs = [74016, 72523, 197040, 61543, 220537, 52507, 72008, 144521,
                43510, 62005, 114018, 57008, 55009, 180019, 40007, 76011,
                66009, 67514, 240036, 76513, 66008, 73506, 210630, 73028,
                151034, 67015, 56010, 154033, 61009]
        expected = 0x80 | 0x40 | VECTORS['alphabet'].index('A')
        self.assertEqual(self.decode(runs), (1, expected))
        decoder, now, events = self.scan(runs)
        self.assertEqual(events, [])
        self.assertEqual(decoder.pending, 1)
        quiet = decoder.quiet
        self.assertEqual(quiet, 660960)
        self.assertEqual(self.poll(decoder, now+quiet-1), (0, 0))
        self.assertEqual(self.poll(decoder, now+quiet), (1, expected))
        self.assertEqual(self.lib.s3_barcode_character(expected), b'A')
        self.assertEqual(self.poll(decoder, now+quiet+1), (0, 0))

    def test_recorded_reverse_f_scan(self):
        # Complete recorded F frame: previous 3.4 ratio limit rejected it.
        runs = [64012, 50010, 156570, 48508, 145019, 48513, 43508, 144058,
                63011, 86516, 49225, 38016, 47505, 170572, 185051, 42508,
                172024, 49010, 48502, 45029, 50020, 41510, 175538, 44506,
                134518, 46509, 42508, 202027, 46006]
        expected = 0x80 | 0x40 | VECTORS['alphabet'].index('F')
        self.assertEqual(self.decode(runs), (1, expected))
        decoder, now, events = self.scan(runs)
        self.assertEqual(events, [])
        self.assertEqual(decoder.pending, 1)
        self.assertEqual(decoder.quiet, 451760)
        self.assertEqual(self.poll(decoder, now+451759), (0, 0))
        self.assertEqual(self.poll(decoder, now+451760), (1, expected))
        self.assertEqual(self.lib.s3_barcode_character(expected), b'F')
        self.assertEqual(self.poll(decoder, now+451761), (0, 0))

    def test_wide_narrow_ratio_boundaries(self):
        for char in VECTORS['alphabet']:
            for reverse in (False, True):
                for ratio, valid in ((1.79, False), (1.8, True),
                                     (4.0, True), (4.01, False)):
                    with self.subTest(char=char, reverse=reverse, ratio=ratio):
                        status, code = self.decode(widths(char, 12000, ratio, reverse))
                        if valid:
                            self.assertEqual(status, 1)
                            self.assertEqual(self.lib.s3_barcode_character(code).decode(), char)
                            self.assertEqual(bool(code & 64), reverse)
                        else:
                            self.assertEqual((status, code), (2, 0))

    def test_cluster_tolerance_boundaries(self):
        # Balance changes so cluster means stay fixed at 12000/36000 us.
        # Exact +/-40% passes; a 1 us excursion beyond either end fails.
        for bit, mean in (('0', 12000), ('1', 36000)):
            indices = [i for i, b in enumerate(VECTORS['patterns']['*']) if b == bit]
            lower, upper = mean*3//5, mean*7//5
            for excursion in ('boundary', 'below', 'above'):
                with self.subTest(cluster=bit, excursion=excursion):
                    runs = widths('A', narrow=12000)
                    runs[indices[0]], runs[indices[1]] = lower, upper
                    if excursion == 'below':
                        runs[indices[0]] -= 1
                        runs[indices[2]] += 1
                    elif excursion == 'above':
                        runs[indices[1]] += 1
                        runs[indices[2]] -= 1
                    status, code = self.decode(runs)
                    if excursion == 'boundary':
                        self.assertEqual(status, 1)
                        self.assertEqual(self.lib.s3_barcode_character(code), b'A')
                    else:
                        self.assertEqual((status, code), (2, 0))

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

    def test_stream_alphabet_with_sample_quantization(self):
        for char in 'ABCDEFGHIJKLMNOPQRSTUVWXYZ':
            for reverse in (False, True):
                for narrow in (3150, 8350, 35400, 90600):
                    with self.subTest(char=char, reverse=reverse, narrow=narrow):
                        ideal_edge = 125
                        sampled_edge = 250
                        runs = []
                        for width in widths(char, narrow, 2.5, reverse):
                            ideal_edge += width
                            next_edge = (ideal_edge + 249) // 250 * 250
                            runs.append(next_edge - sampled_edge)
                            sampled_edge = next_edge
                        decoder, now, events = self.scan(runs)
                        self.assertEqual(events, [])
                        status, code = self.poll(decoder, now + 1000000)
                        self.assertEqual(status, 1)
                        self.assertEqual(self.lib.s3_barcode_character(code).decode(), char)
                        self.assertEqual(bool(code & 64), reverse)
                        self.assertEqual(self.poll(decoder, now + 1000250), (0, 0))

    def test_stream_recovers_after_stray_ink(self):
        for pulse in (500, 5000):
            with self.subTest(pulse=pulse):
                decoder, now, events = self.scan([pulse])
                self.assertEqual(events, [])
                decoder, now, events = self.scan(widths('F', reverse=True),
                    start=now, quiet=150000, decoder=decoder)
                self.assertEqual(events, [(2, 0)])
                status, code = self.poll(decoder, now + 50000)
                self.assertEqual(status, 1)
                self.assertEqual(self.lib.s3_barcode_character(code), b'F')
                self.assertTrue(code & 64)

    def test_stream_restarts_when_white_only_fits_the_smaller_label(self):
        decoder, now, events = self.scan(widths('A', narrow=20000))
        self.assertEqual(events, [])
        self.assertEqual(decoder.quiet, 200000)
        decoder, now, events = self.scan(widths('Z'), start=now,
            quiet=100000, decoder=decoder)
        self.assertEqual(events, [(2, 0)])
        status, code = self.poll(decoder, now + 50000)
        self.assertEqual(status, 1)
        self.assertEqual(self.lib.s3_barcode_character(code), b'Z')

    def test_timer_wrap_and_long_idle(self):
        decoder, now, events = self.scan(widths('Z', reverse=True), start=MASK-1100000)
        self.assertEqual(events, [])
        self.assertEqual(self.poll(decoder, now+50000)[0], 1)
        self.assertEqual(self.poll(decoder, now+2000000), (0, 0))

    def test_partial_and_stuck_signal_timeout(self):
        decoder, now, _ = self.scan(widths('A')[:10])
        self.assertEqual(self.poll(decoder, now+MAX_RUN_US), (0, 0))
        self.assertEqual(self.poll(decoder, now+MAX_RUN_US+1), (3, 0))
        self.assertEqual(self.poll(decoder, now+MAX_RUN_US+2), (0, 0))
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
        actual = '\n'.join(line for line in actual.splitlines()
                           if not line.startswith('#include <'))
        harness = Path(__file__).with_name('analog_input_harness.c').read_text()
        source.write_text(harness.replace('/* SUBSYSTEM_SOURCE */', actual))
        executable = directory/(f'line-{pin}.exe' if os.name == 'nt' else f'line-{pin}')
        command = [COMPILER] + (['cc'] if Path(COMPILER).stem == 'zig' else [])
        command += ['-std=c99', '-Wall', '-Wextra', '-Werror',
                    f'-DAPP_LINE_FOLLOW_ENABLED={pin}', '-DAPP_TEST=0', '-I', str(ROOT/'app_program/subsystem3'),
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

    def test_fast_analog_barcode_detection_actual_task(self):
        command, executable = self.compile_line_harness(1, ['-DAPP_S3_BARCODE_DETECT=1'])
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
