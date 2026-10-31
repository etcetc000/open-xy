"""Native qualification executes the real SDK services with host boundary doubles."""
import os
import shutil
from pathlib import Path
import subprocess
import tempfile
import unittest
from dataclasses import replace
from tooling.adapters import generate_sample_callbacks, generate_screen_order
from tooling.startup import generate as generate_startup
from machine_fixtures import fixture

ROOT = Path(__file__).resolve().parents[1]


def host_compiler():
    """A C++ compiler that builds and runs host programs, or None."""
    compiler = os.environ.get('OPXY_TEST_HOST_CXX', 'g++')
    with tempfile.TemporaryDirectory(prefix='host-cxx-') as folder:
        source, binary = Path(folder) / 'probe.cpp', Path(folder) / ('probe.exe' if os.name == 'nt' else 'probe')
        source.write_text('int main() { return sizeof(unsigned) == 4 && sizeof(void*) >= 4 ? 0 : 1; }\n')
        hidden = dict(creationflags=subprocess.CREATE_NO_WINDOW) if os.name == 'nt' else {}
        try:
            built = subprocess.run([compiler, str(source), '-o', str(binary)], capture_output=True, timeout=60, **hidden)
            return compiler if built.returncode == 0 and subprocess.run([str(binary)], timeout=30, **hidden).returncode == 0 else None
        except (OSError, subprocess.SubprocessError):
            return None


HOST_CXX = host_compiler()


@unittest.skipUnless(HOST_CXX, 'No working host C++ compiler (set OPXY_TEST_HOST_CXX)')
class SampleServices(unittest.TestCase):
    def run_fixture(self, name, extra=()):
        with tempfile.TemporaryDirectory(prefix='sample-services-') as folder:
            binary = Path(folder) / (name + ('.exe' if os.name == 'nt' else ''))
            hidden = dict(creationflags=subprocess.CREATE_NO_WINDOW) if os.name == 'nt' else {}
            if name in ('sample_callbacks', 'benchmark', 'drum_controls', 'presets', 'sample_slice_clock'):
                probe = replace(fixture('probesample'), id='probe', identity=dict(engine=46, page=7))
                if name == 'sample_callbacks':
                    probe = replace(probe, definition=dict(probe.definition,
                        resources=dict(probe.definition['resources'], sliceTiming=1)))
                    # A page-2 engine with no services beside it: it plays like the sampler.
                    plain = fixture('probesample')
                    sampler = replace(plain, id='sampler', identity=dict(engine=47, page=2),
                                      definition=dict(plain.definition, resources=dict(voiceCapacity=256)))
                    generate_sample_callbacks(folder, [probe, sampler])
                elif name in ('drum_controls', 'sample_slice_clock'):
                    source = ROOT / 'sdk/platform/opxy-1.2.1' / (
                        'drum_controls_stock.cpp' if name == 'drum_controls' else 'sample_slice_clock.cpp')
                    (Path(folder) / source.name).write_text(source.read_text())
                elif name == 'benchmark':
                    from tooling.benchmark import generate
                    generate(folder, [replace(fixture('probesynth'), id='unfamiliar', identity=dict(engine=46, page=91))])
                # Exercise the real tempo readers and assignment bridge with
                # host addresses standing in for stock functions and globals.
                stock = (ROOT / 'sdk/platform/opxy-1.2.1/stock.hpp').read_text()
                stock = stock.replace('typedef __UINTPTR_TYPE__ Address;',
                                      'typedef __UINTPTR_TYPE__ Address;\nAddress host_stock_address(Address);')
                for pointer in ('T*', 'const volatile u32*', 'const volatile unsigned char*'):
                    stock = stock.replace('reinterpret_cast<' + pointer + '>(address)',
                                          'reinterpret_cast<' + pointer + '>(host_stock_address(address))')
                (Path(folder) / 'stock.hpp').write_text(stock)
                if name == 'presets':  # beside the host stock.hpp, so their includes reach it
                    for unit in ('presets.cpp', 'registration.hpp'):
                        shutil.copy(ROOT / 'sdk/platform/opxy-1.2.1' / unit, Path(folder) / unit)
            if name == 'screen_order':
                generate_screen_order(folder)
            if name == 'screen_drawing':
                # Copy the unchanged renderer so its quoted stock-header include
                # can resolve to a host boundary double instead of a 32-bit ABI.
                (Path(folder) / 'screen_drawing.hpp').write_bytes(
                    (ROOT / 'sdk/platform/opxy-1.2.1/screen_drawing.hpp').read_bytes())
                (Path(folder) / 'screen.hpp').write_bytes(
                    (ROOT / 'tests/fixtures/screen_drawing_host.hpp').read_bytes())
            if name == 'startup':
                generate_startup(folder, ['first', 'second', 'sampled'], ['effect'], ['sampled'])
            # A fixture that includes barrier.hpp supplies host_barrier for the Blackfin SSYNC.
            command = [HOST_CXX, '-std=c++11', '-O2', '-Wall', '-Werror', '-D__builtin_bfin_ssync()=host_barrier()',
                       '-I' + str(folder), '-I' + str(ROOT / 'sdk/include'),
                       '-I' + str(ROOT / 'sdk/platform/opxy-1.2.1'), str(ROOT / 'tests/fixtures' / (name + '.cpp')),
                       *(str(ROOT / path) for path in extra), '-o', str(binary)]
            result = subprocess.run(command, capture_output=True, text=True, timeout=120, **hidden)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            result = subprocess.run([str(binary)], capture_output=True, text=True, timeout=120, **hidden)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def test_cache_lifetime_capacity_and_browser_deferral(self):
        self.run_fixture('sample_cache')

    def test_progress_never_takes_over_another_notification(self):
        self.run_fixture('sample_progress')

    def test_saved_settings_tuning_and_trim_bounds(self):
        self.run_fixture("stock_sample")

    def test_preset_scan_hides_factory_presets_of_engines_not_installed(self):
        self.run_fixture('presets')

    def test_callbacks_work_for_another_engine_and_preserve_stock_fields(self):
        self.run_fixture('sample_callbacks', ('sdk/platform/opxy-1.2.1/sample_machines.cpp',))

    def test_pad_shortcuts_clipboard_and_multi_edit(self):
        self.run_fixture('drum_controls', ('sdk/platform/opxy-1.2.1/sample_machines.cpp',))

    def test_slicer_source_preview_matches_timestamp_rate_and_frozen_bounds(self):
        self.run_fixture('sample_preview')

    def test_slicer_clock_uses_frozen_gui_controls_and_machine_rate(self):
        self.run_fixture('sample_slice_clock', ('sdk/platform/opxy-1.2.1/sample_machines.cpp',))

    def test_owned_primary_screen_precedes_stock_overlays(self):
        self.run_fixture('screen_order')

    def test_display_activation_resets_state_and_invalidates_geometry_keys(self):
        self.run_fixture('screen_drawing')

    def test_registration_never_overwrites_and_rechecks_stock_state(self):
        self.run_fixture('registration')

    def test_startup_registers_in_order_and_gates_the_catalog(self):
        self.run_fixture('startup')

    def test_benchmark_forwards_stock_and_only_owned_arbitrary_synths(self):
        self.run_fixture('benchmark')
