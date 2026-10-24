"""Check browser-preview prerequisites by compiling and running a WASM probe."""
from importlib.metadata import PackageNotFoundError, version
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile

from .formats import require
from .pipeline import NO_WINDOW


def run(command):
    try:
        result = subprocess.run([str(value) for value in command], capture_output=True,
                                text=True, timeout=30,
                                creationflags=NO_WINDOW)
    except subprocess.TimeoutExpired as error:
        raise ValueError('Prerequisite probe timed out: ' + str(command[0])) from error
    require(result.returncode == 0, result.stdout + result.stderr)
    return result.stdout.strip()


def check(compiler='clang++'):
    """Report every missing prerequisite, not only the first."""
    problems = []
    if sys.version_info < (3, 11):
        problems.append('Use Python 3.11 or newer.')
    print('Python: ' + sys.version.split()[0])
    try:
        numpy = version('numpy')
    except PackageNotFoundError:
        numpy = None
    if numpy != '2.3.5':
        problems.append('Install the pinned preview dependency in a virtual environment: '
                        'python -m pip install numpy==2.3.5 (found ' + str(numpy) + ')')
    else:
        print('NumPy: ' + numpy)
    node = shutil.which('node')
    if not node:
        problems.append('Install Node 22 or newer and make node available on PATH.')
    else:
        node_version = run([node, '--version'])
        if int(node_version.lstrip('v').split('.')[0]) < 22:
            problems.append('Use Node 22 or newer.')
        print('Node: ' + node_version)
    try:
        if node:
            probe(compiler, node)
    except ValueError as error:
        problems.append(str(error))
    require(not problems, '\n'.join(problems))
    print('PASS: WebAssembly compilation, linking and execution. No firmware or device required.')
    return 0


def probe(compiler, node):
    """Build and run a WebAssembly module with this compiler."""
    advice = 'Choose LLVM with wasm32 and wasm-ld using --cxx.'
    if sys.platform == 'darwin':
        advice += (" Apple's Xcode clang lacks wasm-ld; install Homebrew llvm and lld and pass"
                   ' --cxx "$(brew --prefix llvm)/bin/clang++".')
    executable = shutil.which(str(compiler))
    require(executable, f'{compiler} was not found. {advice}')
    executable = Path(executable).resolve()
    try:
        with tempfile.TemporaryDirectory(prefix='opxy-preview-doctor-') as temporary:
            directory = Path(temporary).resolve()
            source = directory / 'probe.cpp'
            module = directory / 'probe.wasm'
            source.write_text('extern "C" int opxy_probe() { return 42; }\n', encoding='utf-8')
            run([executable, '--target=wasm32', '-nostdlib', source, '-o', module,
                 '-Wl,--no-entry', '-Wl,--export=opxy_probe'])
            run([node, '--input-type=module', '-e',
                 "import{readFileSync}from'node:fs';"
                 "const{instance}=await WebAssembly.instantiate(readFileSync(process.argv[1]));"
                 "if(instance.exports.opxy_probe()!==42)throw Error('WASM probe failed');", module])
    except (ValueError, OSError, subprocess.SubprocessError) as error:
        raise ValueError(f'{executable} could not build and run WebAssembly. {advice}\n{error}') from error
    print('Clang: ' + str(executable))
