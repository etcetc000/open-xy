"""pitch_constants (docs/cpu-capabilities.md): stock's note-to-pitch routine (8d5c09d6), which every synth voice
start runs, converts the float constants 1200.0 and 100.0 to fixed point on every call through 8d5bf5ec (software
floating point). The capability loads the two words those calls return. These tests pin the two
edits (where they are, the stock bytes they replace, the words they load) and, on the BF707 Functional-Sim, run
stock's routine with and without them."""
import json
import os
from pathlib import Path
import re
import struct
import subprocess
import tempfile
import unittest

from tooling import extensions, native
from tooling.extensions import extension_at
from tooling.firmware import active_services, patch_active
from tooling.formats import ldr, mapped_offset, sha
from tooling.pipeline import ROOT, config
from test_divide import blackfin_compiler
import test_floor_hooks

SIM = ROOT / 'tests/fixtures/pitch_constants_sim'
CCES = test_floor_hooks.CCES
# What 8d5bf5ec returns for 1200.0f and 100.0f (the simulator test reads them from stock's own call).
FIRST, SECOND = 0x04b00000, 0x00640000
# Stock 1.2.1 at 8d5c0a1e and 8d5c0a2c: LOAD R0 = 1200.0f / 100.0f (a 32-bit immediate: opcode, high half, low
# half, a zero half), then CALL 8d5bf5ec.
STOCK = {0x8d5c0a1e: '00da964400000000ffe3e3f5', 0x8d5c0a2c: '00dac84200000000ffe3dcf5'}


def load_r0(word):
    """LOAD R0 = word in the encoding stock uses at the site, then two 16-bit NOPs where the CALL was."""
    return (bytes.fromhex('00da') + struct.pack('<HH', word >> 16, word & 0xffff) + bytes(6)).hex()


def stock_fixed(constant):
    """8d5bf5ec as stock's code computes it, in single precision: the integer part (as a 16-bit value) in the high
    half, the hundredths of the rest (plus 0.005, times 100, truncated) in the low half."""
    f32 = lambda x: struct.unpack('<f', struct.pack('<f', x))[0]
    whole = int(constant // 1)
    fraction = f32(f32(f32(constant - f32(whole)) + f32(0.005)) * 100.0)
    return (whole & 0xffff) << 16 | (int(fraction) & 0xffff)


def edits():
    return [p for p in config()['patches'] if p.get('service') == 'pitch_constants']


class PitchConstants(unittest.TestCase):
    def test_registry_and_selection(self):
        registry = json.loads((ROOT / extensions.PROFILE).read_text(encoding='utf-8'))['capabilities']
        self.assertEqual(registry['pitch_constants'], {'build': True})
        self.assertIn('pitch_constants', extensions.build_capabilities())
        profile = config()
        self.assertEqual(profile['services']['pitch_constants'], ['pitch_constants'])
        cpu = json.loads((ROOT / 'extensions/cpu/extension.json').read_text(encoding='utf-8'))
        self.assertIn('pitch_constants', cpu['capabilities'])   # the cpu extension names it, with no setting
        self.assertNotIn('pitch_constants', cpu.get('settings', {}))
        # Off, both sites keep stock's bytes; the cpu extension, or any extension naming it, turns both on.
        class Machines(list):
            def __init__(self, *extensions_):
                super().__init__()
                self.extensions = extensions_
        off = active_services(profile, native.service_users(Machines()))
        self.assertFalse([p for p in edits() if patch_active(p, off, False) or patch_active(p, off, True)])
        with_cpu = active_services(profile, native.service_users(Machines(extension_at(ROOT / 'extensions/cpu'))))
        self.assertEqual(len([p for p in edits() if patch_active(p, with_cpu, False) and patch_active(p, with_cpu, True)]), 2)
        with tempfile.TemporaryDirectory() as folder:
            (Path(folder) / 'extension.json').write_text(json.dumps(
                {'id': 'pitch_test', 'name': 'Pitch test', 'description': 'Turns on pitch_constants.', 'claims': [],
                 'actions': [], 'capabilities': ['pitch_constants']}), encoding='utf-8')
            on = active_services(profile, native.service_users(Machines(extension_at(Path(folder)))))
        self.assertIn('pitch_constants', on)
        self.assertEqual(len([p for p in edits() if patch_active(p, on, False) and patch_active(p, on, True)]), 2)

    def test_two_edits_at_the_constants(self):
        found = edits()
        self.assertEqual([int(p['address'], 16) for p in found], sorted(STOCK))
        for p, word in zip(found, (FIRST, SECOND)):
            with self.subTest(address=p['address']):
                self.assertEqual((p['kind'], p['length'], p.get('build')), ('limit', 12, True))
                self.assertEqual(p['before'], STOCK[int(p['address'], 16)])
                self.assertEqual(p['after'], load_r0(word))
                self.assertNotIn('choices', p)
                # Only the immediate and the call differ: the opcode and the zero half are stock's.
                before, after = bytes.fromhex(p['before']), bytes.fromhex(p['after'])
                self.assertEqual(before[:2] + before[6:8], after[:2] + after[6:8])
                self.assertEqual(after[8:], bytes(4))

    def test_the_words_are_stocks_conversion(self):
        self.assertEqual(stock_fixed(1200.0), FIRST)
        self.assertEqual(stock_fixed(100.0), SECOND)
        self.assertEqual(struct.unpack('<f', bytes.fromhex('00009644'))[0], 1200.0)
        self.assertEqual(struct.unpack('<f', bytes.fromhex('0000c842'))[0], 100.0)

    @unittest.skipUnless(os.environ.get('OPXY_TEST_STOCK_OS'), 'Stock OS image not supplied')
    def test_stock_bytes_and_the_routine(self):
        stock = Path(os.environ['OPXY_TEST_STOCK_OS']).read_bytes()
        self.assertEqual(sha(stock), config()['stock_sha256'])
        blocks = ldr(stock)

        def code(address, length):
            offset = mapped_offset(blocks, address, length)
            return stock[offset:offset + length].hex()
        for p in edits():
            self.assertEqual(code(int(p['address'], 16), 12), p['before'])
            self.assertEqual(int(p['file_offset'], 16), mapped_offset(blocks, int(p['address'], 16), 12))
        # Both CALLs reach 8d5bf5ec (pc-relative, in halfwords).
        for site in (0x8d5c0a26, 0x8d5c0a34):
            hi, lo = struct.unpack('<HH', bytes.fromhex(code(site, 4)))
            offset = ((hi & 0xff) << 16 | lo) - (1 << 24 if hi & 0x80 else 0)
            self.assertEqual(site + 2 * offset, 0x8d5bf5ec)

    @unittest.skipUnless(os.environ.get('OPXY_TEST_STOCK_OS') and blackfin_compiler() and
                         (CCES / 'System/chipfactory.exe').is_file(), 'Needs the stock OS, the Blackfin compiler and CCES')
    def test_stock_and_the_edits_agree_on_the_bf707_simulator(self):
        # Stock OS 1.2.1 at its own addresses on CCES's Functional-Sim, constructed by its own constructor table:
        # 8d5bf5ec's own answers, then stock's note pitch on 24,576 inputs, with booted and with random tables,
        # with stock's bytes at the sites and with the edits.
        r = run_simulator(Path(os.environ['OPXY_TEST_STOCK_OS']))
        self.assertEqual(r['words'], [FIRST, SECOND, FIRST, SECOND])
        for name in ('booted', 'random'):
            calls, mismatches, distinct, stock, edited = r[name]
            with self.subTest(tables=name):
                self.assertEqual((calls, mismatches), (24576, 0))
                # The two conversions, about 600 cycles a call (docs/cpu-capabilities.md quotes 834 and 233).
                self.assertAlmostEqual(stock, 834, delta=10)
                self.assertAlmostEqual(stock - edited, 601, delta=5)
                # The two conversions cost hundreds of cycles; the loads cost none.
                self.assertGreater(stock - edited, 500)
        # The booted tables give every input one pitch; the random ones make the inputs move the result.
        self.assertGreater(r['random'][2], 1000)
        self.assertEqual(r['stock'], 1)      # the sites held the profile's stock bytes, which the edits differ from
        self.assertEqual(r['restored'], 1)


def run_simulator(stock_os, out=None):
    """Build and run the probe (tests/fixtures/pitch_constants_sim); returns its results."""
    compiler = blackfin_compiler()

    def tool(name):
        return compiler.with_name('bfin-elf-' + name + compiler.suffix)
    halves = lambda hex_: ','.join(f'0x{h:04x}' for h in struct.unpack('<6H', bytes.fromhex(hex_)))
    first, second = edits()
    for p in (first, second):
        if p['before'] == p['after']:
            raise AssertionError(p['address'])
    flags = ['-mcpu=bf707', '-O2', '-std=gnu++11', '-ffreestanding', '-fno-exceptions', '-fno-rtti',
             '-fno-threadsafe-statics', '-fno-use-cxa-atexit', '-fno-asynchronous-unwind-tables', '-fno-unwind-tables',
             '-fno-common', '-fno-tree-loop-distribute-patterns', '-fomit-frame-pointer', '-nostdinc++',
             '-ffunction-sections', '-fdata-sections', f'-DEDIT1={halves(first["after"])}', f'-DEDIT2={halves(second["after"])}',
             f'-DSTOCK1={halves(first["before"])}', f'-DSTOCK2={halves(second["before"])}']
    with tempfile.TemporaryDirectory(prefix='pitch-constants-sim-') as scratch:
        folder = Path(out or scratch)
        folder.mkdir(parents=True, exist_ok=True)

        def run(command, timeout=600):
            r = subprocess.run([str(c) for c in command], cwd=folder, capture_output=True, timeout=timeout,
                               **test_floor_hooks.HIDDEN)
            if r.returncode:
                raise AssertionError(f'{command[0]} failed:\n' + (r.stdout + r.stderr).decode(errors='replace')[-3000:])
            return r.stdout + r.stderr

        raw = stock_os.read_bytes()
        # Stock's loaded image as contiguous chunks (fills over 1 MiB are the simulator's zeroed memory).
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
            if start == 0x11a00000:
                # The simulator resets to 0x11a00000 (stock's start-up code, not used here): P0 = 0x080a0000;
                # JUMP (P0), the CCES runtime's start.
                data[0:10] = bytes.fromhex('08e1000048e10a085000')
            while len(data) % 4:
                data += b'\0'
            (folder / f'img_{start:08x}.bin').write_bytes(bytes(data))
        run([compiler, *flags, '-S', SIM / 'harness.cpp', '-o', 'harness.gs'])
        run([tool('as'), '-mcpu=bf707', 'harness.gs', '-o', 'harness.o'])
        module = test_floor_hooks.MODULE
        (folder / 'native.ld').write_text(
            f'OUTPUT_ARCH(bfin)\nENTRY(_sim_probe)\nSECTIONS {{ .text {module:#x} : {{ *(.text.sim_probe) *(.text*) }} '
            '.rodata : { *(.rodata*) } .data : { *(.data*) } .bss : { __sim_bss_start = .; *(.bss*) *(COMMON) . = ALIGN(4); '
            '__sim_bss_end = .; } /DISCARD/ : { *(.comment .note* .eh_frame*) } }\n')
        libgcc = next(compiler.parent.parent.glob('lib/gcc/bfin-elf/*/libgcc.a'))
        run([tool('ld'), '--gc-sections', '-T', 'native.ld', '-o', 'native.elf', 'harness.o', libgcc])
        run([tool('objcopy'), '-O', 'binary', 'native.elf', 'native.bin'])
        symbols = run([tool('objdump'), '-t', 'native.elf']).decode().replace('\r', '')
        entry = int(re.search(r'^([0-9a-f]{8}) .*\s_sim_probe$', symbols, re.M)[1], 16)
        blob = (folder / 'native.bin').read_bytes()
        blob += b'\0' * (-len(blob) % 4)
        (folder / 'native_pad.bin').write_bytes(blob)
        asm = ['.section candidate;', '.global _candidate;', '_candidate:', '.INC/BINARY "native_pad.bin";']
        mem = [f'MEM_CANDIDATE {{START({module:#010x}) END(0x8d4d037f) TYPE(RAM) WIDTH(8)}}']
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
            '#include <stdio.h>\nstatic unsigned a[64];\nint main(){*(volatile unsigned*)0x1FC00004&=~2u;asm("ssync;");'
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
            if old not in ldf:
                raise AssertionError(old)
            ldf = ldf.replace(old, new, 1)
        ldf = re.sub(r'>\s*MEM_L1_\w+', '>MEM_L2_SRAM', ldf)
        (folder / 'probe.ldf').write_text(ldf)
        run([CCES / 'ccblkfn.exe', '-proc', 'ADSP-BF707', '-si-revision', '1.0', 'probe.c', 'code.s', '-T', 'probe.ldf',
             '-o', 'probe.dxe'], timeout=900)
        r = subprocess.run([str(CCES / 'System/chipfactory.exe'), '-proc', 'ADSP-BF707', '-sim-type', 'Functional-Sim',
                            '-bound-exe-time', '80000000000', '-exit-value', 'probe.dxe'],
                           cwd=folder, capture_output=True, timeout=5400, **test_floor_hooks.HIDDEN)
        log = r.stdout + r.stderr
        found = re.search(rb'COST_BEGIN\s+(.*?)COST_END', log, re.S)
        if not found:
            raise AssertionError('No probe output:\n' + log.decode(errors='replace')[-2000:])
        words = [int(v, 16) for v in found[1].split()]
    i, j, k, m = (words.index(t) for t in (0xf2000001, 0xf2000002, 0xf2000003, 0xf2000004))
    return dict(words=words[i + 1:i + 5], booted=words[j + 1:j + 6], random=words[k + 1:k + 6], restored=words[m + 1],
                stock=words[m + 2])


if __name__ == '__main__':
    unittest.main()
