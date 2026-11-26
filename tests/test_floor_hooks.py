"""Stock work every block pays, done exactly with less: the LFO evaluator's pass check
without its two DDR calls (sdk_lfo_pass) and the tape track while the tape is silent
(sdk_tape_play, sdk_tape_tail), all in floor_hooks.s."""
import os
from pathlib import Path
import re
import subprocess
import tempfile
import unittest

from tooling import native
from tooling.formats import ldr
from tooling.pipeline import config
from test_divide import blackfin_compiler

ROOT = Path(__file__).resolve().parents[1]
PLATFORM = ROOT / 'sdk/platform/opxy-1.2.1'
SIM = ROOT / 'tests/fixtures/floor_hooks_sim'
CCES = Path(os.environ.get('OPXY_TEST_CCES') or r'C:/Analog Devices/CrossCore Embedded Studio 2.12.1')
HIDDEN = dict(creationflags=subprocess.CREATE_NO_WINDOW) if os.name == 'nt' else {}
MODULE = 0x8d0d0380
# symbol: (address, length, preimage, where it jumps back to, as the source writes it)
HOOKS = {
    'sdk_lfo_pass': (0x11a044e6, 8, '00de508dd44a0000', ('P1 = 0x11a0451c;\n    JUMP (P1);',)),
    'sdk_tape_play': (0x8d503878, 4, 'ffe368fe', ('P1.L = 0x3904; P1.H = 0x8d50;\n    JUMP (P1);',
                                                  'P1.L = 0x387c; P1.H = 0x8d50;\n    JUMP (P1);')),
    'sdk_tape_tail': (0x8d50fac4, 8, '09dab011d51d0000', ('P1.L = 0xfd22; P1.H = 0x8d50;\n    JUMP (P1);',
                                                          'P2.L = 0xfacc; P2.H = 0x8d50;\n    JUMP (P2);')),
}
# Stock's instructions the LFO hook stands for, 11a044e6..11a0451c (both calls and the stores between).
LFO_WINDOW = 0x11a044e6, 0x36


def stock_regions(data):
    return [(b['target'], data[b['offset'] + 16:b['offset'] + 16 + b['count']]) for b in ldr(data) if b['count'] and not b['fill']]


def stock_bytes(regions, address, count):
    for base, data in regions:
        if base <= address and address + count <= base + len(data):
            return data[address - base:address - base + count]
    raise AssertionError(f'No stock bytes at {address:#x}')


class FloorHooks(unittest.TestCase):
    def test_block_trims_jumps_to_the_three_hooks(self):
        source = (PLATFORM / 'floor_hooks.s').read_text()
        for symbol, (address, length, before, backs) in HOOKS.items():
            found = [p for p in config()['patches'] if p.get('symbol') == symbol]
            self.assertEqual(len(found), 1, symbol)
            p = found[0]
            # Jumps, never calls: each hook leaves through stock's own code.
            self.assertEqual((int(p['address'], 16), p['length'], p['call'], p['kind'], p['before']),
                             (address, length, False, 'hook', before))
            self.assertEqual(p['service'], 'block_trims')
            self.assertNotIn('profiling', p)
            body = source.split(f'\n{symbol}:\n')[1].split('.size')[0]
            for back in backs:
                self.assertEqual(body.count(back), 1, (symbol, back))
        self.assertIn('floor_hooks', native.SDK_UNITS)
        self.assertEqual(native.UNIT_SERVICES['floor_hooks'], 'block_trims')
        # The idle playback still makes stock's call whenever the tape may sound.
        self.assertIn('P2.L = 0x3548; P2.H = 0x8d50;\n    CALL (P2);', source)

    @unittest.skipUnless(os.environ.get('OPXY_TEST_STOCK_OS'), 'Stock OS image not supplied')
    def test_the_hooks_replace_stock_instructions(self):
        data = Path(os.environ['OPXY_TEST_STOCK_OS']).read_bytes()
        regions = stock_regions(data)
        for symbol, (address, length, before, _) in HOOKS.items():
            p = next(p for p in config()['patches'] if p.get('symbol') == symbol)
            offset = int(p['file_offset'], 16)
            self.assertEqual(data[offset:offset + length].hex(), before, symbol)
            self.assertEqual(stock_bytes(regions, address, length).hex(), before, symbol)

    @unittest.skipUnless(os.environ.get('OPXY_TEST_STOCK_OS') and blackfin_compiler() and
                         (CCES / 'System/chipfactory.exe').is_file(), 'Needs the stock OS, the Blackfin compiler and CCES')
    def test_stock_and_the_hooks_agree_on_the_bf707_simulator(self):
        # Stock OS 1.2.1 at its own addresses on CCES's Functional-Sim, constructed by its own constructor
        # table: the LFO window against the hook (3,000 cases), the tape track with and without the tape
        # hooks from the same memory (400 cases), and the whole audio loop from start-up (160 blocks,
        # through the tape's start-up clear) with a hash of the audio state after every block.
        r = run_simulator(Path(os.environ['OPXY_TEST_STOCK_OS']))
        self.assertEqual(r['lfo'][:2], [3000, 0])
        self.assertLess(r['lfo'][3], r['lfo'][2])
        self.assertEqual(r['tape'][:2], [400, 0])
        idle = r['tape'][2:5]   # idle with the filter off: n, stock, hooks
        self.assertGreater(idle[0], 0)
        self.assertLess(idle[2], idle[1] * 2 // 3)
        self.assertEqual(r['whole'][:3], [160, 0, 0xffffffff])
        self.assertLess(r['whole'][4], r['whole'][3])


def run_simulator(stock_os, out=None):
    """Build and run the probe (tests/fixtures/floor_hooks_sim); returns its three results."""
    compiler = blackfin_compiler()
    def tool(name):
        return compiler.with_name('bfin-elf-' + name + compiler.suffix)
    flags = ['-mcpu=bf707', '-O2', '-std=gnu++11', '-ffreestanding', '-fno-exceptions', '-fno-rtti',
             '-fno-threadsafe-statics', '-fno-use-cxa-atexit', '-fno-asynchronous-unwind-tables', '-fno-unwind-tables',
             '-fno-common', '-fno-tree-loop-distribute-patterns', '-fomit-frame-pointer', '-nostdinc++',
             '-ffunction-sections', '-fdata-sections']
    with tempfile.TemporaryDirectory(prefix='floor-hooks-sim-') as scratch:
        folder = Path(out or scratch)
        folder.mkdir(parents=True, exist_ok=True)

        def run(command, timeout=600):
            r = subprocess.run([str(c) for c in command], cwd=folder, capture_output=True, timeout=timeout, **HIDDEN)
            if r.returncode:
                raise AssertionError(f'{command[0]} failed:\n' + (r.stdout + r.stderr).decode(errors='replace')[-3000:])
            return r.stdout + r.stderr

        raw = stock_os.read_bytes()
        # Stock's loaded image as contiguous chunks (fills under 1 MiB written as zeros; the large BSS is
        # the simulator's zeroed memory, and the probe's start-up zeroes it again).
        chunks = []
        for target, count, fill, offset in sorted((b['target'], b['count'], b['fill'], b['offset']) for b in ldr(raw) if b['count']):
            if fill and count > 0x100000:
                continue
            data = bytes(count) if fill else raw[offset + 16:offset + 16 + count]
            if chunks and chunks[-1][0] + len(chunks[-1][1]) == target:
                chunks[-1][1] += data
            else:
                chunks.append([target, bytearray(data)])
        for start, data in chunks:
            if start == LFO_WINDOW[0] & ~0xfffff:
                (folder / 'lfo.bin').write_bytes(bytes(data[LFO_WINDOW[0] - start:LFO_WINDOW[0] - start + LFO_WINDOW[1]]))
                # The simulator resets to 0x11a00000 (stock's start-up code, not used here): P0 = 0x080a0000;
                # JUMP (P0), the CCES runtime's start.
                data[0:10] = bytes.fromhex('08e1000048e10a085000')
            while len(data) % 4:
                data += b'\0'
            (folder / f'img_{start:08x}.bin').write_bytes(bytes(data))
        hooks = (PLATFORM / 'floor_hooks.s').read_text()
        hooks = hooks.replace('    P1 = 0x11a0451c;\n    JUMP (P1);', '    JUMP.L lfo_return;')
        (folder / 'floor_hooks.s').write_text(hooks)
        run([compiler, *flags, f'-I{SIM}', f'-I{PLATFORM}', '-S', SIM / 'harness.cpp', '-o', 'harness.gs'])
        for source, obj in (('harness.gs', 'harness.o'), ('floor_hooks.s', 'hooks.o'), (SIM / 'harness.s', 'glue.o')):
            run([tool('as'), '-mcpu=bf707', '-I', ROOT, source, '-o', obj])
        (folder / 'native.ld').write_text(
            f'OUTPUT_ARCH(bfin)\nENTRY(_sim_probe)\nSECTIONS {{ .text {MODULE:#x} : {{ *(.text.sim_probe) *(.text*) }} '
            '.rodata : { *(.rodata*) } .data : { *(.data*) } .bss : { __sim_bss_start = .; *(.bss*) *(COMMON) . = ALIGN(4); '
            '__sim_bss_end = .; } /DISCARD/ : { *(.comment .note* .eh_frame*) } }\n')
        libgcc = next(compiler.parent.parent.glob('lib/gcc/bfin-elf/*/libgcc.a'))
        run([tool('ld'), '--gc-sections', '-T', 'native.ld', '-o', 'native.elf', 'harness.o', 'hooks.o', 'glue.o', libgcc])
        run([tool('objcopy'), '-O', 'binary', 'native.elf', 'native.bin'])
        symbols = run([tool('objdump'), '-t', 'native.elf']).decode().replace('\r', '')
        entry = int(re.search(r'^([0-9a-f]{8}) .*\s_sim_probe$', symbols, re.M)[1], 16)
        blob = (folder / 'native.bin').read_bytes()
        blob += b'\0' * (-len(blob) % 4)
        (folder / 'native_pad.bin').write_bytes(blob)
        asm = ['.section candidate;', '.global _candidate;', '_candidate:', '.INC/BINARY "native_pad.bin";']
        mem = [f'MEM_CANDIDATE {{START({MODULE:#010x}) END(0x8d4d037f) TYPE(RAM) WIDTH(8)}}']
        sec = ['candidate {INPUT_SECTIONS($OBJECTS(candidate))}>MEM_CANDIDATE']
        for start, data in chunks:
            name = f'img_{start:08x}.bin'
            size = (folder / name).stat().st_size
            asm += [f'.section stock_{start:08x};', f'.global _stock_{start:08x};', f'_stock_{start:08x}:', f'.INC/BINARY "{name}";']
            mem.append(f'MEM_STOCK_{start:08x} {{START({start:#010x}) END({start + size - 1:#010x}) TYPE(RAM) WIDTH(8)}}')
            sec.append(f'stock_{start:08x} {{INPUT_SECTIONS($OBJECTS(stock_{start:08x}))}}>MEM_STOCK_{start:08x}')
        (folder / 'code.s').write_text('\n'.join(asm) + '\n')
        # CPLBs off (CCES's table covers only its own memory), then the probe.
        (folder / 'probe.c').write_text(
            '#include <stdio.h>\nstatic unsigned a[1024];\nint main(){*(volatile unsigned*)0x1FC00004&=~2u;asm("ssync;");'
            '*(volatile unsigned*)0x1FC01004&=~2u;asm("csync;");unsigned n=((unsigned(*)(unsigned*))' + hex(entry) +
            ')(a),i;puts("COST_BEGIN");for(i=0;i<n;++i)printf("%08x\\n",a[i]);puts("COST_END");return 0;}\n')
        # The CCES runtime moves out of L1 and stock's DDR (into L2 from 0x080a0000 and the top of DDR).
        ldf = (CCES / 'Blackfin/ldf/ADSP-BF707.ldf').read_text()
        ldf = re.sub(r'^\s*MEM_L1_\w+\s*\{[^}]*\}\s*$', '', ldf, flags=re.M)
        for old, new in (('START(0x08002000)', 'START(0x080a0000)'),
                         ('START(0x84000000) END(0x85FFFFFF)', 'START(0x87000000) END(0x873FFFFF)'),
                         ('START(0x86000000) END(0x87FFFFFF)', 'START(0x87400000) END(0x877FFFFF)'),
                         ('#define START_ADDR 0x11A00000', '#define START_ADDR 0x080A0000'),
                         ('MEMORY {', 'MEMORY {\n' + '\n'.join(mem) + '\n'),
                         ('  SECTIONS {', '  SECTIONS {\n' + '\n'.join(sec) + '\n')):
            assert old in ldf, old
            ldf = ldf.replace(old, new, 1)
        ldf = re.sub(r'>\s*MEM_L1_\w+', '>MEM_L2_SRAM', ldf)
        (folder / 'probe.ldf').write_text(ldf)
        run([CCES / 'ccblkfn.exe', '-proc', 'ADSP-BF707', '-si-revision', '1.0', 'probe.c', 'code.s', '-T', 'probe.ldf',
             '-o', 'probe.dxe'], timeout=900)
        r = subprocess.run([str(CCES / 'System/chipfactory.exe'), '-proc', 'ADSP-BF707', '-sim-type', 'Functional-Sim',
                            '-bound-exe-time', '80000000000', '-exit-value', 'probe.dxe'],
                           cwd=folder, capture_output=True, timeout=5400, **HIDDEN)
        log = r.stdout + r.stderr
        found = re.search(rb'COST_BEGIN\s+(.*?)COST_END', log, re.S)
        if not found:
            raise AssertionError('No probe output:\n' + log.decode(errors='replace')[-2000:])
        words = [int(v, 16) for v in found[1].split()]
    i, j, k = words.index(0xf1000001), words.index(0xf1000002), words.index(0xf1000003)
    return dict(lfo=words[i + 1:i + 5], tape=words[j + 1:j + 15], whole=words[k + 1:k + 6])


if __name__ == '__main__':
    unittest.main()
