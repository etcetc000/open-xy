"""The audio loop's per-track mixing without stock's dead weight: the five bus mixes
that skip silence (sdk_track_mix, track_hooks.s)."""
import os
from pathlib import Path
import re
import struct
import subprocess
import tempfile
import unittest

from tooling import native
from tooling.formats import ldr
from tooling.pipeline import config
from test_divide import blackfin_compiler

ROOT = Path(__file__).resolve().parents[1]
PLATFORM = ROOT / 'sdk/platform/opxy-1.2.1'
SIM = ROOT / 'tests/fixtures/track_hooks_sim'
CCES = Path(os.environ.get('OPXY_TEST_CCES') or r'C:/Analog Devices/CrossCore Embedded Studio 2.12.1')
HIDDEN = dict(creationflags=subprocess.CREATE_NO_WINDOW) if os.name == 'nt' else {}
# The probe's relocated stock windows (harness.s).
MIXES, TAP = 0x08048000, 0x0804c000


def stock_regions(data):
    return [(b['target'], data[b['offset'] + 16:b['offset'] + 16 + b['count']]) for b in ldr(data) if b['count'] and not b['fill']]


def stock_bytes(regions, address, count):
    for base, data in regions:
        if base <= address and address + count <= base + len(data):
            return data[address - base:address - base + count]
    raise AssertionError(f'No stock bytes at {address:#x}')


class TrackHooks(unittest.TestCase):
    def test_block_trims_jumps_to_the_mix_hook(self):
        found = [p for p in config()['patches'] if p.get('symbol') == 'sdk_track_mix' and 'profiling' not in p]
        self.assertEqual(len(found), 1)
        p = found[0]
        # 8d511014 R0 = [P2++] and the aux loop's LSETUP: a jump, never a return.
        self.assertEqual((int(p['address'], 16), p['length'], p['call'], p['kind'], p['before']),
                         (0x8d511014, 6, False, 'hook', '1090c1e00afc'))
        self.assertEqual(p['service'], 'block_trims')
        self.assertNotIn('profiling', p)
        self.assertIn('track_hooks', native.SDK_UNITS)
        self.assertEqual(native.UNIT_SERVICES['track_hooks'], 'block_trims')
        source = (PLATFORM / 'track_hooks.s').read_text()
        # The hook jumps back to 8d511182, stock's code after the five mixes.
        self.assertEqual(source.count('P1.L = 0x1182; P1.H = 0x8d51;\n    JUMP (P1);'), 1)

    def test_the_silence_scan_reads_all_128_words(self):
        # Both halves of the 128-word track buffer (11903600: I0 the first 64 words, I1 the second), every word
        # once: the loop count times the loads a pass makes from each half covers 64 words of each.
        source = (PLATFORM / 'track_hooks.s').read_text()
        head, body = source.split('LSETUP (.Lscan, .Lscan_end) LC0 = P1;\n')
        count = int(re.findall(r'P1 = (\d+);\n\s*$', head)[0])
        body = body.split('.Lscan_end:')[0]
        self.assertIn('I0.L = 0x3600; I0.H = 0x1190;\n    I1.L = 0x3700; I1.H = 0x1190;', head)
        self.assertEqual((count * body.count('[I0++]'), count * body.count('[I1++]')), (64, 64))

    def test_without_profiling_the_tap_timer_end_is_skipped(self):
        # 8d510f04 CLI R0; R2 = CYCLES, the start of the monitor tap's stage
        # timer end (slot 17: 08035228), jumps straight to the mixes in builds
        # without profiling; profiling builds keep stock's timing.
        found = [p for p in config()['patches'] if p.get('symbol') == 'sdk_track_mix' and 'profiling' in p]
        self.assertEqual(len(found), 1)
        p = found[0]
        self.assertEqual((int(p['address'], 16), p['length'], p['call'], p['kind'], p['before'], p['profiling']),
                         (0x8d510f04, 4, False, 'hook', '30009631', False))
        self.assertEqual(p['service'], 'block_trims')

    @unittest.skipUnless(os.environ.get('OPXY_TEST_STOCK_OS'), 'Stock OS image not supplied')
    def test_the_hook_replaces_stock_instructions(self):
        data = Path(os.environ['OPXY_TEST_STOCK_OS']).read_bytes()
        for p in (p for p in config()['patches'] if p.get('symbol') == 'sdk_track_mix'):
            offset, size = int(p['file_offset'], 16), p['length']
            self.assertEqual(data[offset:offset + size].hex(), p['before'])
            self.assertEqual(stock_bytes(stock_regions(data), int(p['address'], 16), size).hex(), p['before'])

    @unittest.skipUnless(os.environ.get('OPXY_TEST_STOCK_OS') and blackfin_compiler() and
                         (CCES / 'System/chipfactory.exe').is_file(), 'Needs the stock OS, the Blackfin compiler and CCES')
    def test_stock_instructions_and_the_hooks_agree_on_the_bf707_simulator(self):
        # Stock's own bytes (the five mixes 8d511014..8d511182) against track_hooks.s
        # from the same memory, on CCES's Functional-Sim.
        result = run_simulator(Path(os.environ['OPXY_TEST_STOCK_OS']))
        self.assertEqual(result['mix_mismatches'], 0)
        self.assertEqual(result['named_same'], [1, 1, 1, 1])
        # Stock's tap timer end then its mixes, against the profiling-off hook at 8d510f04
        # entered with junk in every register stock's timer code writes.
        self.assertEqual(result['tap_mismatches'], 0)
        self.assertEqual(result['tap_counted'], result['tap_scenarios'])
        self.assertLess(result['tap_mean'][1], result['tap_mean'][0])
        # A silent track's mixes cost less than stock's (sim cycles, no DDR model).
        self.assertLess(result['named'][0][1], result['named'][0][0] // 2)


def run_simulator(stock_os, out=None):
    """Build and run the probe; returns the comparison (tests/fixtures/track_hooks_sim)."""
    compiler = blackfin_compiler()
    def tool(name):
        return compiler.with_name('bfin-elf-' + name + compiler.suffix)
    flags = ['-mcpu=bf707', '-O2', '-std=gnu++11', '-ffreestanding', '-fno-exceptions', '-fno-rtti',
             '-fno-threadsafe-statics', '-fno-use-cxa-atexit', '-fno-asynchronous-unwind-tables', '-fno-unwind-tables',
             '-fno-common', '-fno-tree-loop-distribute-patterns', '-fomit-frame-pointer', '-nostdinc++',
             '-ffunction-sections', '-fdata-sections']
    with tempfile.TemporaryDirectory(prefix='track-hooks-sim-') as scratch:
        folder = Path(out or scratch)
        folder.mkdir(parents=True, exist_ok=True)

        def run(command, timeout=600):
            r = subprocess.run([str(c) for c in command], cwd=folder, capture_output=True, timeout=timeout, **HIDDEN)
            if r.returncode:
                raise AssertionError(f'{command[0]} failed:\n' + (r.stdout + r.stderr).decode(errors='replace')[-3000:])
            return r.stdout + r.stderr

        regions = stock_regions(stock_os.read_bytes())
        (folder / 'mix.bin').write_bytes(stock_bytes(regions, 0x8d511014, 0x8d511182 - 0x8d511014))
        (folder / 'tap.bin').write_bytes(stock_bytes(regions, 0x8d510f04, 0x8d511014 - 0x8d510f04))
        hooks = (PLATFORM / 'track_hooks.s').read_text()
        hooks = hooks.replace('    P1.L = 0x1182; P1.H = 0x8d51;\n    JUMP (P1);', '    JUMP.L mix_return;')
        (folder / 'track_hooks.s').write_text(hooks)
        run([compiler, *flags, f'-I{PLATFORM}', '-S', SIM / 'harness.cpp', '-o', 'harness.s'])
        for source, obj in (('harness.s', 'harness.o'), ('track_hooks.s', 'hooks.o'), (SIM / 'harness.s', 'glue.o')):
            run([tool('as'), '-mcpu=bf707', '-I', ROOT, source, '-o', obj])
        (folder / 'native.ld').write_text(
            f'OUTPUT_ARCH(bfin)\nENTRY(_sim_probe)\nSECTIONS {{ .mixwin {MIXES:#x} : {{ KEEP(glue.o(.mixwin)) }} .tapwin {TAP:#x} : {{ KEEP(glue.o(.tapwin)) }} .text 0x08050000 : {{ *(.text*) }} .rodata : {{ *(.rodata*) }} '
            '.data : { *(.data*) } .bss : { *(.bss*) *(COMMON) } /DISCARD/ : { *(.comment .note* .eh_frame*) } }\n')
        libgcc = next(compiler.parent.parent.glob('lib/gcc/bfin-elf/*/libgcc.a'))
        run([tool('ld'), '--gc-sections', '-T', 'native.ld', '-o', 'native.elf', 'glue.o', 'harness.o', 'hooks.o', libgcc])
        run([tool('objcopy'), '-O', 'binary', 'native.elf', 'native.bin'])
        symbols = run([tool('objdump'), '-t', 'native.elf']).decode().replace('\r', '')
        entry = int(re.search(r'^([0-9a-f]{8}) .*\s_sim_probe$', symbols, re.M)[1], 16)
        blob = (folder / 'native.bin').read_bytes()
        blob += b'\0' * (len(blob) % 2)
        (folder / 'code.s').write_text('.section candidate;\n.global _candidate;\n_candidate:\n.byte2 candidate_bytes[]=' +
                                       ','.join(hex(v) for v in struct.unpack('<%dH' % (len(blob) // 2), blob)) + ';\n')
        (folder / 'probe.c').write_text('#include <stdio.h>\nstatic unsigned a[4096];\nint main(){unsigned n=((unsigned(*)(unsigned*))' +
                                        hex(entry) + ')(a),i;puts("COST_BEGIN");for(i=0;i<n;++i)printf("%08x\\n",a[i]);'
                                        'puts("COST_END");return 0;}\n')
        ldf = (CCES / 'Blackfin/ldf/ADSP-BF707.ldf').read_text()
        # The probe's own code and data stay clear of the stock addresses the code under test uses:
        # L2 below 0x080a0000, L1 data B from 0x11903400, L1 data C from 0x11b01400.
        for old, new in (('START(0x08002000)', 'START(0x080a0000)'),
                         # The blob (objcopy -O binary) starts at its lowest section, the mixes' window.
                         ('MEMORY {', f'MEMORY {{\nMEM_CANDIDATE {{START({MIXES:#010x}) END(0x0809ffff) TYPE(RAM) WIDTH(8)}}\n'),
                         ('  SECTIONS {', '  SECTIONS {\ncandidate {INPUT_SECTIONS($OBJECTS(candidate))}>MEM_CANDIDATE\n'),
                         ('START(0x11B00000) END(0x11B01FFF)', 'START(0x11B00000) END(0x11B013FF)'),
                         ('START(0x11900000) END(0x11903FFF)', 'START(0x11900000) END(0x119033FF)'),
                         ('START(0x11900000) END(0x11907FFF)', 'START(0x11900000) END(0x119033FF)')):
            assert old in ldf, old
            ldf = ldf.replace(old, new, 1)
        (folder / 'probe.ldf').write_text(ldf)
        run([CCES / 'ccblkfn.exe', '-proc', 'ADSP-BF707', '-si-revision', '1.0', 'probe.c', 'code.s', '-T', 'probe.ldf',
             '-o', 'probe.dxe'])
        log = run([CCES / 'System/chipfactory.exe', '-proc', 'ADSP-BF707', '-sim-type', 'Functional-Sim', '-bound-exe-time',
                   '40960000000', '-exit-value', 'probe.dxe'], timeout=3000)
        found = re.search(rb'COST_BEGIN\s+(.*?)COST_END', log, re.S)
        if not found:
            raise AssertionError('No probe output:\n' + log.decode(errors='replace')[-2000:])
        words = [int(v, 16) for v in found[1].split()]
    i, k, t = words.index(0xaaaa0001), words.index(0xaaaa0002), words.index(0xaaaa0004)
    return dict(tap_scenarios=words[t + 1], tap_mismatches=words[t + 2], tap_counted=words[t + 3],
                # Mean cycles: stock tap end + stock mixes, the 8d510f04 hook, stock mixes, the 8d511014 hook.
                tap_mean=tuple(words[t + 4:t + 8]),mix_scenarios=words[i + 1], mix_mismatches=words[i + 2], mix_mean=(words[i + 3], words[i + 4]),
                named=[(words[k + 1 + 3 * n], words[k + 2 + 3 * n]) for n in range(4)],
                named_same=[words[k + 3 + 3 * n] for n in range(4)])


if __name__ == '__main__':
    unittest.main()
