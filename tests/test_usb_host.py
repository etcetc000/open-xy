"""The USB host glue of the 1.2.1 adapter: the usb_host_ready event, driver registration
and the probe trampolines, with stock doubles and no device."""
from pathlib import Path
import tempfile
import unittest
from test_extension_runtime import PLATFORM, ROOT, build_and_run
from test_sample_services import HOST_CXX


@unittest.skipUnless(HOST_CXX, 'No working host C++ compiler (set OPXY_TEST_HOST_CXX)')
class UsbHost(unittest.TestCase):
    def test_event_registration_probe_and_log(self):
        with tempfile.TemporaryDirectory(prefix='usb-host-test-') as name:
            folder = Path(name)
            stock = (PLATFORM / 'stock.hpp').read_text()
            stock = stock.replace('typedef __UINTPTR_TYPE__ Address;',
                                  'typedef __UINTPTR_TYPE__ Address;\nAddress host_stock_address(Address);')
            for pointer in ('T*', 'const volatile u32*', 'const volatile unsigned char*'):
                stock = stock.replace('reinterpret_cast<' + pointer + '>(address)',
                                      'reinterpret_cast<' + pointer + '>(host_stock_address(address))')
            (folder / 'stock.hpp').write_text(stock)
            (folder / 'usb_host.cpp').write_text((PLATFORM / 'usb_host.cpp').read_text().replace(
                '#include "../extension_runtime.hpp"', '#include <extension_runtime.hpp>'))
            build_and_run(self, ROOT / 'tests/fixtures/usb_host.cpp', folder)

    def test_hook_site_is_two_whole_stock_calls_and_the_event_is_listed(self):
        from tooling.extensions import events
        from tooling.pipeline import config
        patch = next(p for p in config()['patches'] if p.get('symbol') == 'sdk_extensions_usb_host_init')
        # CALL 0x8c071a50 and CALL 0x8c070350, which the hook replaces and the adapter repeats.
        self.assertEqual((patch['address'], patch['length'], patch['before'], patch['events']),
                         ('0x8c073c34', 8, 'ffe30eefffe38ce3', ['usb_host_ready']))
        self.assertIn('usb_host_ready', events())


if __name__ == '__main__':
    unittest.main()
