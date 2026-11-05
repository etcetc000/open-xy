"""Shared helpers for SDK kernel tests: a host program run, and the device form's build."""
import os
from pathlib import Path
import subprocess
import tempfile

from tooling.definitions import ROOT
from tooling.native import CXX_FLAGS
from test_divide import blackfin_compiler
from test_sample_services import HOST_CXX

KERNELS = ROOT / 'sdk/platform/blackfin/kernels'


def run_host(test, program):
    """Build and run a host C++ program against the SDK headers; it must exit 0."""
    with tempfile.TemporaryDirectory() as directory:
        source, binary = Path(directory) / 'check.cpp', Path(directory) / 'check'
        source.write_text(program)
        subprocess.run([HOST_CXX, '-std=c++11', '-O2', '-Wall', '-Werror', '-I', str(ROOT / 'sdk/include'),
                        str(source), '-o', str(binary)], check=True)
        test.assertEqual(subprocess.run([str(binary)]).returncode, 0)


def device_build(test, name, caller):
    """The kernel's unit assembles to its one global symbol, and a caller of the
    header's entry point calls it (the reference stays as the range fallback)."""
    compiler = blackfin_compiler()
    assembler = compiler.with_name(compiler.name.replace('g++', 'as'))
    symbol = '_opxy_' + name
    with tempfile.TemporaryDirectory() as directory:
        obj, source, listing = Path(directory) / 'k.o', Path(directory) / 'c.cpp', Path(directory) / 'c.s'
        subprocess.run([str(assembler), '-mcpu=bf707', '-o', str(obj), str(KERNELS / (name + '.s'))], check=True)
        objdump = compiler.with_name(compiler.name.replace('g++', 'objdump'))
        table = subprocess.run([str(objdump), '-t', str(obj)], capture_output=True, text=True, check=True).stdout
        globals_ = [line.split()[-1] for line in table.splitlines() if ' g ' in line]
        test.assertEqual(globals_, [symbol])
        source.write_text(caller)
        subprocess.run([str(compiler), *CXX_FLAGS, '-I', str(ROOT / 'sdk/include'), '-S', str(source),
                        '-o', str(listing)], check=True)
        test.assertRegex(listing.read_text(), r'(call|jump\.[ls]) ' + symbol + ';')
