"""Native qualification executes the real SDK services with host boundary doubles."""
import os
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
            if name in ('sample_callbacks', 'benchmark'):
                probe = replace(fixture('probesample'), id='probe', identity=dict(engine=46, page=7))
                if name == 'sample_callbacks':
                    generate_sample_callbacks(folder, [probe])
                else:
                    from tooling.benchmark import generate
                    generate(folder, [replace(fixture('probesynth'), id='unfamiliar', identity=dict(engine=46, page=91))])
                # Exercise the real tempo readers and assignment bridge with
                # host addresses standing in for stock functions and globals.
            if name in ('sample_callbacks', 'benchmark', 'sample_routing', 'sample_display_stock'):
                stock = (ROOT / 'sdk/platform/opxy-1.2.1/stock.hpp').read_text()
                stock = stock.replace('typedef __UINTPTR_TYPE__ Address;',
                                      'typedef __UINTPTR_TYPE__ Address;\nAddress host_stock_address(Address);')
                for pointer in ('T*', 'const volatile u32*', 'const volatile unsigned char*'):
                    stock = stock.replace('reinterpret_cast<' + pointer + '>(address)',
                                          'reinterpret_cast<' + pointer + '>(host_stock_address(address))')
                (Path(folder) / 'stock.hpp').write_text(stock)
            if name == 'sample_routing':
                # The production bridge, with only stock address resolution replaced.
                for unit in ('sample_controls_stock.cpp', 'sample_machines.cpp'):
                    (Path(folder) / unit).write_text((ROOT / 'sdk/platform/opxy-1.2.1' / unit).read_text())
            if name == 'sample_display_stock':
                unit = 'sample_display_stock.cpp'
                (Path(folder) / unit).write_text((ROOT / 'sdk/platform/opxy-1.2.1' / unit).read_text())
            if name == 'screen_order':
                generate_screen_order(folder)
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

    def test_sample_display_copies_peaks_and_rejects_stale_or_pending_data(self):
        self.run_fixture('sample_display')

    def test_sample_display_uses_owned_gui_region_and_reports_pending_activity(self):
        self.run_fixture('sample_display_stock')

    def test_progress_never_takes_over_another_notification(self):
        self.run_fixture('sample_progress')

    def test_saved_settings_tuning_and_trim_bounds(self):
        self.run_fixture("stock_sample")

    def test_callbacks_work_for_another_engine_and_preserve_stock_fields(self):
        self.run_fixture('sample_callbacks', ('sdk/platform/opxy-1.2.1/sample_machines.cpp',))

    def test_sample_page_and_assignment_routes_preserve_unowned_stock_paths(self):
        self.run_fixture('sample_routing')

    def test_owned_primary_screen_precedes_stock_overlays(self):
        self.run_fixture('screen_order')

    def test_registration_never_overwrites_and_rechecks_stock_state(self):
        self.run_fixture('registration')

    def test_startup_registers_in_order_and_gates_the_catalog(self):
        self.run_fixture('startup')

    def test_benchmark_forwards_stock_and_only_owned_arbitrary_synths(self):
        self.run_fixture('benchmark')
