"""Computer-only auditions; source snapshots never enter the firmware builder."""
import base64
import shutil
import subprocess

from . import preview
from .formats import require, sha
from .machines import machine_at
from .native import check_sources
from .pipeline import ROOT, NO_WINDOW

EFFECT_EXPORTS = ('opxy_effect_abi', 'opxy_effect_left', 'opxy_effect_right', 'opxy_effect_parameters',
                  'opxy_effect_reset', 'opxy_effect_render')


def compile_effect(compiler, machine, out):
    resources = machine.definition['resources']
    size = resources.get('bufferBytes', 0)
    values = dict(NAMESPACE=machine.id, STATE_LIMIT='opxy::effect_buffered_state_bytes' if size else 'opxy::effect_state_bytes',
                  BUFFER_STORAGE=f'static opxy::i32 scratch[{size // 4}];' if size else '',
                  BUFFER=f'opxy::Buffer{{scratch, {size // 4}u}}, ' if size else '',
                  TRANSPORT='const opxy::Transport transport = {1200, ticks, true};' if resources.get('transport') else '(void)ticks;',
                  TRANSPORT_ARGUMENT='transport, ' if resources.get('transport') else '')
    source = (ROOT / 'sdk/host/effect_preview.cpp.in').read_text()
    for key, value in values.items():
        source = source.replace('@' + key + '@', str(value))
    unit = out / 'effect_preview.cpp'
    unit.write_text(source, encoding='utf-8')
    paths = [ROOT / 'sdk/include', ROOT / 'sdk/platform/blackfin/freestanding', machine.root,
             *(machine.root / p for p in machine.includes)]
    command = [compiler, *preview.FLAGS, *('-I' + str(p) for p in paths), unit,
               *(machine.root / p for p in machine.sources), ROOT / 'sdk/platform/blackfin/memory.cpp',
               '-o', out / 'engine.wasm', '-Wl,--no-entry', '-Wl,-z,stack-size=16384',
               f'-Wl,--initial-memory={preview.MEMORY}', f'-Wl,--max-memory={preview.MEMORY}',
               *('-Wl,--export=' + name for name in EFFECT_EXPORTS)]
    result = subprocess.run([str(x) for x in command], capture_output=True, text=True, timeout=120, creationflags=NO_WINDOW)
    require(result.returncode == 0, 'Effect audition compilation failed:\n' + result.stderr +
            '\nCheck the preview compiler with python sdk.py doctor --preview, or configure preview_cxx.')
    result = subprocess.run(['node', str(ROOT / 'tools/check_effect_preview.mjs'), str(out / 'engine.wasm')],
                            capture_output=True, text=True, timeout=120, creationflags=NO_WINDOW)
    require(result.returncode == 0, 'Effect audition contract failed:\n' + result.stdout + result.stderr)


def build(root, out, compiler='clang++'):
    m = machine_at(root)
    require(m.kind in ('synth', 'drum', 'effect'), 'Audition supports synths, drums and effects')
    executable = shutil.which(str(compiler))
    require(executable is not None, 'Audition needs Clang++ with wasm32/wasm-ld; run python sdk.py doctor --preview')
    check_sources([m])
    def hashes():
        return dict(preview.source_hashes(m), **{name: sha((ROOT / name).read_bytes()) for name in
                    ('tooling/manager_audition.py', 'sdk/host/effect_preview.cpp.in', 'tools/check_effect_preview.mjs')})
    before = hashes()
    if m.kind == 'effect':
        compile_effect(executable, m, out)
    else:
        preview.compile_preview(executable, m.definition, out, m.root, m.sources, m.includes, kind=m.kind)
    require(before == hashes(), 'Source changed during audition build')
    wasm = (out / 'engine.wasm').read_bytes()
    require(0 < len(wasm) <= 1048576 and wasm[:8] == b'\0asm\x01\0\0\0', 'Invalid audition module')
    return dict(id=m.id, name=m.name, kind=m.kind, definition=m.definition, dry_parameter=m.dry_parameter,
                wasm=base64.b64encode(wasm).decode('ascii'), wasm_sha256=sha(wasm))
