"""Stock Drumline's second page (drumline.hpp), which a drumline_page extension
supplies: the shared hooks and the service's own against host doubles, with the
service and without (M3's filter and filter envelope words stay stock's in both);
how an extension's declaration turns the service on and becomes its page; and the
stock sites the service relies on."""
import json
import os
from pathlib import Path
import subprocess
import tempfile
import unittest

from tooling import extensions, native, startup
from tooling.firmware import active_services, decode_branch, patch_active
from tooling.formats import ldr, mapped_offset, sha
from tooling.machines import read_collection
from machine_fixtures import FOLDER, fixture_entries
from test_sample_services import HOST_CXX

ROOT = Path(__file__).resolve().parents[1]
PLATFORM = ROOT / 'sdk/platform/opxy-1.2.1'
PROFILE = json.loads((ROOT / 'profiles/opxy-1.2.1.json').read_text(encoding='utf-8'))
PROBE = ROOT / 'tests/fixtures/extension_probe'
# The service's two sites: the drum renderer's load of a voice's method table,
# and where both of the dbox page's branches end.
SITES = {'0x8d51b554': ('sdk_dbox_render_select', '48914130', '0x1527b0'),
         '0x8d531192': ('sdk_drumline_page_hook', 'a0e4b400', '0x1683ee')}
# The profile's user of the service: a collection with a drumline_page extension.
USER = 'drumline_page'
# The services a page turns on: its own and the drum hooks it extends.
SERVICES = {'samples', 'octaves', 'drums', 'drum_sends', 'drum_locks', 'drumline'}
# A page as MD KIT's would declare it, which tests/fixtures/drumline.cpp's table mirrors.
FILTER = ['L%d' % (16 - i) for i in range(16)] + ['OPEN'] + ['H%d' % i for i in range(1, 16)]
SRR = ['OFF'] + ['S%d' % i for i in range(1, 16)]
PAGE = {'parameters': [{'label': 'FILTER', 'choices': FILTER, 'default': 16},
                       {'label': 'SRR', 'choices': SRR},
                       {'label': 'FX I', 'role': 'send1'},
                       {'label': 'FX II', 'role': 'send2'}]}
SOURCE = """#include <opxy/extension.hpp>
#include <opxy/drumline_page.hpp>

namespace opxy { namespace extensions { namespace %s {
namespace {
void start(drumline::Voice&, const drumline::Choices&) {}
void process(drumline::Voice&, const drumline::Choices&, i32*) {}
}
extern constexpr Hooks hooks = Hooks();
extern const drumline::Processor drumline_processor = {start, process};
}}}
"""


def own(label, count):
    """An own parameter of `count` choices."""
    return {'label': label, 'choices': ['C%d' % i for i in range(count)]}


def page_extension(folder, name='pagetwo', page=PAGE):
    """An extension folder in `folder` that declares a Drumline page."""
    root = Path(folder) / name
    root.mkdir()
    (root / 'extension.json').write_text(json.dumps(dict(
        id=name, name='Page two', description='SDK test extension: a second Drumline page.',
        claims=[], actions=[], drumline_page=page)), encoding='utf-8')
    (root / 'extension.cpp').write_text(SOURCE % name, encoding='utf-8')
    return root


def run(command, **kwargs):
    hidden = dict(creationflags=subprocess.CREATE_NO_WINDOW) if os.name == 'nt' else {}
    return subprocess.run(command, capture_output=True, text=True, **kwargs, **hidden)


def words(data):
    return [int.from_bytes(data[i:i + 2], 'little') for i in range(0, len(data), 2)]


class Drumline(unittest.TestCase):
    @unittest.skipUnless(HOST_CXX, 'No working host C++ compiler (set OPXY_TEST_HOST_CXX)')
    def test_the_hooks_with_the_service_and_stock_without_it(self):
        with tempfile.TemporaryDirectory(prefix='drumline-') as folder:
            folder = Path(folder)
            # Stock's addresses reach host doubles, as test_drum_services maps them.
            stock = (PLATFORM / 'stock.hpp').read_text()
            stock = stock.replace('typedef __UINTPTR_TYPE__ Address;',
                                  'typedef __UINTPTR_TYPE__ Address;\nAddress host_stock_address(Address);')
            for pointer in ('T*', 'const volatile u32*', 'const volatile unsigned char*'):
                stock = stock.replace('reinterpret_cast<' + pointer + '>(address)',
                                      'reinterpret_cast<' + pointer + '>(host_stock_address(address))')
            (folder / 'stock.hpp').write_text(stock)
            for header in ('drum_values.hpp', 'drum_lock_store.hpp'):
                (folder / header).write_text((PLATFORM / header).read_text())
            # The dbox objects live at stock's addresses on the device; here, the fixture's.
            unit = (PLATFORM / 'drumline.cpp').read_text()
            index = 'dbox_index(static_cast<u32>(reinterpret_cast<Address>(self)))'
            self.assertEqual(unit.count(index), 1)
            unit = unit.replace(index, 'host_dbox_index(self)').replace(
                '#include "stock.hpp"\n', '#include "stock.hpp"\nopxy::u32 host_dbox_index(const unsigned char*);\n', 1)
            (folder / 'drumline.cpp').write_text(unit)
            sources = [str(folder / 'drumline.cpp')]
            for name in ('drum_locks.cpp', 'drum_machines.cpp', 'synth_controls.cpp', 'drum_synthesis_stock.cpp',
                         'drum_values_stock.cpp'):
                (folder / name).write_text((PLATFORM / name).read_text())
                sources.append(str(folder / name))
            for linked, expected in ((True, 'render ok'), (False, 'stock behaviour ok')):
                with self.subTest(linked=linked):
                    binary = folder / (('on' if linked else 'off') + ('.exe' if os.name == 'nt' else ''))
                    built = run([HOST_CXX, '-std=c++11', '-O2', '-Wall', '-Werror', '-I' + str(folder),
                                 '-I' + str(ROOT / 'sdk/include'), '-I' + str(PLATFORM),
                                 *(['-DDRUMLINE_ON'] if linked else []),
                                 str(ROOT / 'tests/fixtures/drumline.cpp'), *sources, '-o', str(binary)], timeout=180)
                    self.assertEqual(built.returncode, 0, built.stdout + built.stderr)
                    result = run([str(binary)], timeout=60)
                    self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
                    self.assertIn(expected, result.stdout)
                    self.assertIn('packing ok', result.stdout)

    def test_the_profile_reviews_two_sites_of_the_pages_own_service(self):
        services = PROFILE['services']
        self.assertEqual(services['drumline'], [USER])
        for name in SERVICES - {'drumline'}:
            self.assertIn(USER, services[name], name)
        hooks = {p['address']: p for p in PROFILE['patches'] if p.get('service') == 'drumline'}
        self.assertEqual(set(hooks), set(SITES))
        stubs = (PLATFORM / 'drumline_hooks.s').read_text(encoding='utf-8')
        for address, (symbol, before, offset) in SITES.items():
            patch_ = hooks[address]
            self.assertEqual((patch_['kind'], patch_['symbol'], patch_['before'], patch_['call'], patch_['length'],
                              patch_['file_offset']), ('hook', symbol, before, True, 4, offset))
            self.assertIn('.global %s\n' % symbol, stubs)
        # Each stub replays what it replaces: P0 = [P1]; R0 = P1 (0x9148, 0x3041) ...
        self.assertEqual(words(bytes.fromhex(SITES['0x8d51b554'][1])), [0x9148, 0x3041])
        body = stubs[stubs.index('sdk_dbox_render_select:\n'):]
        body = body[:body.index('.size')]
        self.assertIn('    P0 = [P1];\n', body)
        self.assertIn('    R0 = P1;\n    R3 = [SP++];\n    ASTAT = [SP++];\n    RTS;\n', body)
        self.assertIn('R3.H = 0x8404; R3.L = 0xce3c;', body)
        self.assertTrue(body.startswith('sdk_dbox_render_select:\n    [--SP] = ASTAT;\n    [--SP] = R3;\n'))
        # ... and R0 = B[P4 + 0xb4] (Z) after the C++ (a0e4 b400).
        body = stubs[stubs.index('sdk_drumline_page_hook:\n'):]
        body = body[:body.index('.size')]
        self.assertIn('    R0 = P4;\n    CALL sdk_drumline_page;\n    HOOK_RESTORE\n    R0 = B[P4 + 0xb4] (Z);\n    RTS;\n',
                      body)
        self.assertIn('0x11a06d28', (PLATFORM / 'drumline.cpp').read_text(encoding='utf-8'))
        self.assertEqual(native.UNIT_SERVICES['drumline'], 'drumline')
        self.assertEqual(native.UNIT_SERVICES['drumline_hooks'], 'drumline')

    def test_a_drumline_page_extension_turns_the_service_on(self):
        for kinds in (['synth'], ['drum'], ['drum', 'padMix', 'hidePages'], ['sample', 'extension']):
            services = active_services(PROFILE, kinds)
            self.assertNotIn('drumline', services)
            for patch_ in PROFILE['patches']:
                if patch_.get('service') == 'drumline':
                    self.assertFalse(patch_active(patch_, services))
        self.assertEqual(active_services(PROFILE, ['synth', USER]), SERVICES)
        units = lambda machines: [step for _name, m, step in native.plan(machines) if m is None]
        with tempfile.TemporaryDirectory(prefix='drumline-page-') as folder:
            root = page_extension(folder)
            # The extension reaches a collection as any other does (--extension).
            entries = read_collection(None, [FOLDER / 'probesynth'], [root])
            self.assertEqual(entries[-1], {'extension': str(root.resolve())})
            _profile, plain, _definitions = native.configuration(fixture_entries('probesynth'))
            _profile, probed, _definitions = native.configuration(fixture_entries('probesynth') + [{'extension': str(PROBE)}])
            _profile, paged, _definitions = native.configuration(entries)
            self.assertEqual([e.id for e in paged.extensions], ['pagetwo'])
            self.assertEqual([r['label'] for r in paged.extensions[0].drumline_page], ['FILTER', 'SRR', 'FX I', 'FX II'])
            # Only an extension that declares a page uses the service; any other leaves it off.
            for machines in (plain, probed):
                self.assertNotIn(USER, native.service_users(machines))
                self.assertNotIn('drumline', native.active_services(PROFILE, native.service_users(machines)))
                self.assertNotIn('drumline', units(machines))
                self.assertNotIn('drumline_hooks', units(machines))
            self.assertIn(USER, native.service_users(paged))
            self.assertLessEqual(SERVICES, native.active_services(PROFILE, native.service_users(paged)))
            self.assertIn('drumline', units(paged))
            self.assertIn('drumline_hooks', units(paged))
            self.assertIn('extension:pagetwo', units(paged))
            # A subset of the machines keeps the page with its extension.
            self.assertIn(USER, native.service_users(native.subset(paged, [])))
            self.assertIn('extension:pagetwo/extension.json', native.provenance(paged))
            # The extension's own object carries the page's table.
            generated = extensions.generate_extension(Path(folder), paged.extensions[0]).read_text(encoding='utf-8')
            self.assertTrue(generated.endswith(extensions.generate_drumline_page(paged.extensions[0])))
            self.assertIn('return &opxy::extensions::pagetwo::hooks;', generated)
            generated = extensions.generate_extension(Path(folder), probed.extensions[0]).read_text(encoding='utf-8')
            self.assertNotIn('sdk_drumline_page_extension', generated)

    def test_the_shared_hooks_reach_the_service_through_the_startups_accessor(self):
        with_page = startup.render(['probesynth'], drumline=True)
        without = startup.render(['probesynth'])
        for text in (with_page, without):
            self.assertIn('const void* sdk_drumline_get() __asm__("sdk_drumline_get");', text)
        self.assertIn('const void* sdk_drumline_service() __asm__("sdk_drumline_service");', with_page)
        self.assertIn('const void* sdk_drumline_get() { return sdk_drumline_service(); }', with_page)
        self.assertIn('const void* sdk_drumline_get() { return nullptr; }', without)
        self.assertNotIn('sdk_drumline_service', without)
        self.assertNotIn('return nullptr', with_page[with_page.index('sdk_drumline_get() {'):][:80])
        # A browser selection without the extension leaves the service's objects out:
        # startup's import of it resolves to kit_absent, so the accessor answers null.
        self.assertEqual(native.service_fallbacks({'drumline'})['drumline'], {'sdk_drumline_service': 'kit_absent'})
        self.assertNotIn('drumline', native.service_fallbacks({'drums', 'drum_sends', 'drum_locks'}))
        header = (PLATFORM / 'drumline.hpp').read_text(encoding='utf-8')
        self.assertIn('const void* sdk_drumline_get() __asm__("sdk_drumline_get");', header)
        self.assertIn('sdk_drumline_page_extension() __asm__("sdk_drumline_page_extension");', header)
        self.assertNotIn('sdk_drumline_page_extension', (PLATFORM / 'drum_locks.cpp').read_text(encoding='utf-8'))

    def test_a_page_declaration_is_checked_and_packed(self):
        rows = extensions.drumline_page_of('pagetwo', PAGE)
        # Sends take the low bits, FX I first, then the own parameters in declaration order.
        self.assertEqual([(r['label'], r['role'], r['default'], r['shift'], r['bits'], len(r['choices'])) for r in rows],
                         [('FILTER', 0, 16, 6, 5, 32), ('SRR', 0, 0, 11, 4, 16),
                          ('FX I', 1, 0, 0, 3, 8), ('FX II', 2, 0, 3, 3, 8)])
        self.assertEqual(rows[0]['choices'], FILTER)
        self.assertEqual(rows[2]['choices'], rows[3]['choices'])
        self.assertEqual(rows[2]['choices'], ['TRK', '0', '17', '33', '50', '67', '83', '100'])
        # A two-choice parameter takes one bit; the default defaults to the first choice.
        [row] = extensions.drumline_page_of('pagetwo', {'parameters': [{'label': 'MODE', 'choices': ['A', 'B']}]})
        self.assertEqual((row['default'], row['shift'], row['bits'], row['role']), (0, 0, 1, 0))
        # Three 32-choice parameters and a two-choice one fill the 16 bits exactly.
        full = [own('P%d' % i, 32) for i in range(3)] + [own('Q', 2)]
        self.assertEqual(sum(r['bits'] for r in extensions.drumline_page_of('pagetwo', {'parameters': full})), 16)
        send = {'label': 'FX I', 'role': 'send1'}
        bad = {
            'no parameters': {'parameters': []},
            'five parameters': {'parameters': [own('P%d' % i, 2) for i in range(5)]},
            'not a list': {'parameters': own('P', 2)},
            'an extra key': {'parameters': [own('P', 2)], 'title': 'page'},
            'no label': {'parameters': [{'choices': ['A', 'B']}]},
            'an empty label': {'parameters': [own('', 2)]},
            'a long label': {'parameters': [own('LONGEST', 2), own('LONGEST1', 2)]},
            'a non-ASCII label': {'parameters': [own('FILTÉR', 2)]},
            'a numeric label': {'parameters': [own(5, 2)]},
            'one choice': {'parameters': [own('P', 1)]},
            'thirty-three choices': {'parameters': [own('P', 33)]},
            'an empty choice': {'parameters': [{'label': 'P', 'choices': ['A', '']}]},
            'a long choice': {'parameters': [{'label': 'P', 'choices': ['A', 'ABCDEFGH']}]},
            'a numeric choice': {'parameters': [{'label': 'P', 'choices': ['A', 2]}]},
            'a default past the choices': {'parameters': [own('P', 4) | {'default': 4}]},
            'a negative default': {'parameters': [own('P', 4) | {'default': -1}]},
            'a boolean default': {'parameters': [own('P', 4) | {'default': True}]},
            'a text default': {'parameters': [own('P', 4) | {'default': '1'}]},
            'an unknown key': {'parameters': [own('P', 4) | {'step': 1}]},
            'a send with choices': {'parameters': [send | {'choices': ['A', 'B']}]},
            'a send with a default': {'parameters': [send | {'default': 1}]},
            'an unknown role': {'parameters': [{'label': 'FX III', 'role': 'send3'}]},
            'a duplicate send': {'parameters': [send, {'label': 'FX 1', 'role': 'send1'}]},
            'seventeen bits': {'parameters': [own('P%d' % i, 32) for i in range(3)] + [own('Q', 3)]},
            'twenty bits': {'parameters': [own('P%d' % i, 32) for i in range(4)]},
            'a malformed state_id': {'parameters': [own('P', 2)], 'state_id': 'Page-2'},
            'a numeric state_id': {'parameters': [own('P', 2)], 'state_id': 2},
        }
        for case, value in bad.items():
            with self.subTest(case), self.assertRaises(ValueError):
                extensions.drumline_page_of('pagetwo', value)
        # The owner tag follows the ID, or a state_id kept across a rename.
        self.assertEqual({r['state_id'] for r in rows}, {'pagetwo'})
        kept = extensions.drumline_page_of('renamed', {'parameters': [own('P', 2)], 'state_id': 'pagetwo'})
        self.assertEqual(kept[0]['state_id'], 'pagetwo')
        with tempfile.TemporaryDirectory(prefix='drumline-page-') as folder:
            self.assertEqual(extensions.extension_at(page_extension(folder)).drumline_page, rows)
            with self.assertRaises(ValueError):
                extensions.extension_at(page_extension(folder, 'pagebad', bad['seventeen bits']))
        self.assertIsNone(extensions.extension_at(PROBE).drumline_page)

    def test_one_extension_owns_the_page(self):
        with tempfile.TemporaryDirectory(prefix='drumline-page-') as folder:
            two, three = (extensions.extension_at(page_extension(folder, name)) for name in ('pagetwo', 'pagethree'))
            probe = extensions.extension_at(PROBE)
            self.assertEqual(extensions.check_collection([probe, two]), [probe, two])
            with self.assertRaisesRegex(ValueError, 'Drumline page 2 has multiple owners: pagetwo, pagethree'):
                extensions.check_collection([two, probe, three])
            with self.assertRaisesRegex(ValueError, 'multiple owners'):
                native.configuration(read_collection(None, [FOLDER / 'probesynth'], [two.root, three.root]))
            # The kit marks it, as an exclusive use the builder lets one selection hold.
            rows = extensions.metadata([probe, two], {'probe': 'official', 'pagetwo': 'personal'})
            self.assertNotIn('drumline_page', rows[0])
            self.assertIs(rows[1]['drumline_page'], True)
            self.assertEqual((rows[1]['claims'], rows[1]['claim_labels'], rows[1]['origin']), ([], {}, 'personal'))
            self.assertEqual(extensions.DRUMLINE_PAGE_LABEL, 'Drumline page 2')

    def test_the_owner_tag_and_the_generated_table(self):
        def fnv(name):
            h = 0x811c9dc5
            for b in name.encode('ascii'):
                h = ((h ^ b) * 0x01000193) & 0xffffffff
            return ((h >> 16) ^ h) & 0xffff or 1
        # A pad keeps its tag across builds and SDK versions: it is FNV-1a of the ID, folded.
        self.assertEqual(extensions.drumline_tag('pagetwo'), 0x580a)
        for name in ('pagetwo', 'pagethree', 'probe', 'a', 'z' * 32):
            tag = extensions.drumline_tag(name)
            self.assertTrue(0 < tag <= 0xffff, name)
            self.assertEqual(tag, fnv(name))
            self.assertEqual(tag, extensions.drumline_tag(name))
        self.assertNotEqual(extensions.drumline_tag('pagetwo'), extensions.drumline_tag('pagethree'))
        with tempfile.TemporaryDirectory(prefix='drumline-page-') as folder:
            page = extensions.extension_at(page_extension(folder))
            source = extensions.generate_drumline_page(page)
            self.assertIn('#include "' + (ROOT / 'sdk/platform/opxy-1.2.1/drumline_page.hpp').as_posix() + '"', source)
            self.assertIn('const char* const sdk_drumline_choices_0[] = {%s};' % ', '.join('"%s"' % c for c in FILTER), source)
            self.assertIn('const char* const sdk_drumline_choices_1[] = {%s};' % ', '.join('"%s"' % c for c in SRR), source)
            self.assertIn('const char* const sdk_drumline_choices_3[] = {"TRK", "0", "17", "33", "50", "67", "83", "100"};',
                          source)
            self.assertIn('const opxy::platform::DrumlinePageParameter sdk_drumline_parameters[] = {'
                          '{"FILTER", sdk_drumline_choices_0, 32u, 16u, 0u, 6u, 5u}, '
                          '{"SRR", sdk_drumline_choices_1, 16u, 0u, 0u, 11u, 4u}, '
                          '{"FX I", sdk_drumline_choices_2, 8u, 0u, 1u, 0u, 3u}, '
                          '{"FX II", sdk_drumline_choices_3, 8u, 0u, 2u, 3u, 3u}};', source)
            self.assertIn('const opxy::platform::DrumlinePage sdk_drumline_page_table = {%du, '
                          '&opxy::extensions::pagetwo::drumline_processor, sdk_drumline_parameters, 4u};' % 0x580a, source)
            self.assertIn('extern "C" const opxy::platform::DrumlinePage* sdk_drumline_page_extension() '
                          '{ return &sdk_drumline_page_table; }', source)
            # Captions are C strings: quotes and backslashes escaped.
            quoted = extensions.extension_at(page_extension(folder, 'quoted', {'parameters': [
                {'label': 'Q"T', 'choices': ['a"b', 'c\\d']}]}))
            source = extensions.generate_drumline_page(quoted)
            self.assertIn(r'const char* const sdk_drumline_choices_0[] = {"a\"b", "c\\d"};', source)
            self.assertIn(r'{"Q\"T", sdk_drumline_choices_0, 2u, 0u, 0u, 0u, 1u}', source)

    @unittest.skipUnless(os.environ.get('OPXY_TEST_STOCK_OS'), 'Private stock input not supplied')
    def test_stock_sites_hold_what_the_page_relies_on(self):
        stock = Path(os.environ['OPXY_TEST_STOCK_OS']).read_bytes()
        self.assertEqual(sha(stock), PROFILE['stock_sha256'])
        image = ldr(stock)

        def at(address, length):
            offset = mapped_offset(image, address, length)
            return stock[offset:offset + length]

        def load(address):
            # Blackfin+ `reg = imm32`: 0xdaNN (NN 0..7 R0..R7, 8..13 P0..P5), high half, low half.
            w = words(at(address, 8))
            self.assertEqual(w[0] & 0xff00, 0xda00, hex(address))
            return w[0] & 0xff, w[1] << 16 | w[2]

        for address, (symbol, before, offset) in SITES.items():
            self.assertEqual(mapped_offset(image, int(address, 16), 4), int(offset, 16))
            self.assertEqual(at(int(address, 16), 4).hex(), before)

        # The drum renderer's machine call: P1 = [P4 + 0x94] (8d51b53e), R2 = [P4 + 0x10]
        # and R3 = [FP + 0xc] (8d51b542), [SP + 0xc] = R3 (8d51b54a), R1 = 0x11b01948,
        # then the hooked pair, P0 = [P0 + 0xc] and CALL (P0).
        self.assertEqual(at(0x8d51b53e, 4).hex(), '21e52500')
        self.assertEqual(at(0x8d51b542, 8).hex(), '03c80018fba022a1')
        self.assertEqual(at(0x8d51b54a, 2).hex(), 'f3b0')
        self.assertEqual(load(0x8d51b54c), (1, 0x11b01948))
        self.assertEqual(at(0x8d51b558, 4).hex(), 'c0ac6000')
        # The dbox method table: +12 the render 11a06d28, +16 the trigger, +20 note start.
        table = [int.from_bytes(at(0x8404ce3c + 4 * i, 4), 'little') for i in range(6)]
        self.assertEqual(table[3:], [0x11a06d28, 0x8d513bea, 0x8d513a36])
        # Its factory: 0x84c50b9c + index * 0x230 (8d513d1c..8d513d2e); the ctor sets the table (8d513aa6).
        self.assertEqual(at(0x8d513d1c, 4).hex(), '21e13002')
        self.assertEqual(load(0x8d513d26), (1, 0x84c50b9c))
        self.assertEqual(load(0x8d513aa6), (1, 0x8404ce3c))
        # The trigger zeroes the block count +0x30 (R0 = 0 at 8d513bf4, [P1 + 0x30] = R0 at
        # 8d513c0e); the DSP counts each block (11a06fa6..11a06faa).
        self.assertEqual(at(0x8d513bf4, 2).hex(), '0060')
        self.assertEqual(at(0x8d513c0e, 2).hex(), '08b3')
        self.assertEqual(at(0x11a06fa6, 6).hex(), '28a3086428b3')
        # The DSP decodes word 0 as ((w >>> 8) * 66) >>> 23 (11a06d62, 11a06d6a, 11a06d80)
        # and squares word 6 (R7 = [P0 + 0x18], 11a06db8; R6 = R7 * R7, 11a06dce).
        self.assertEqual(at(0x11a06d62, 4).hex(), '82cec307')
        self.assertEqual(at(0x11a06d6a, 4).hex(), '24e14200')
        self.assertEqual(at(0x11a06d80, 4).hex(), '82ce4b01')
        self.assertEqual(at(0x11a06db8, 8).hex(), '31ca242187a10000')
        self.assertEqual(at(0x11a06dce, 4).hex(), '31cabf21')
        # It reads the region only through [FP - 0x10] (11a06d36) and applies level, word 7,
        # at the end (11a073e6, 11a073f8), before the renderer pans the block.
        self.assertEqual(at(0x11a073e6, 2).hex(), 'c9b9')
        self.assertEqual(at(0x11a073f8, 2).hex(), 'c8a1')

        # The dbox page's Shift branch (8d530fb8): its six texts through 8d6011f0, the
        # value nodes from P4 + 0x44 (P2 = 0x44, 8d531040), the first title [P4 + 0x54]
        # (8d531048), the play-mode icons from P4 + 0x6c (8d53101e) shown by B[node + 0x5c]
        # (8d531038), and its strings.
        for call in (0x8d53105a, 0x8d531072, 0x8d531082, 0x8d5310c2, 0x8d531158, 0x8d53118a):
            self.assertEqual(decode_branch(call, at(call, 4)), (0x8d6011f0, True))
        self.assertEqual(decode_branch(0x8d530fb8, at(0x8d530fb8, 4)), (0x8d51f184, True))
        self.assertEqual(at(0x8d531040, 4).hex(), '2ae14400')
        self.assertEqual(at(0x8d531048, 4).hex(), '20e41500')
        self.assertEqual(at(0x8d53101e, 4).hex(), '29e16c00')
        self.assertEqual(at(0x8d531038, 4).hex(), '82e65c00')
        self.assertEqual(at(0x84054890, 0x1b), b'%02d\0\0\0\0random\0\0pan\0volume\0')
        # Its numbers: random (w >>> 8) * 100 >>> 23, pan (p + 100) * 99 / 200 and volume
        # (v + 30) * 99 / 50 through 0x51eb851f, each within 0..99.
        self.assertEqual(at(0x8d531098, 6).hex(), '404de840b84d')
        self.assertEqual(load(0x8d5310cc), (0, 0x51eb851f))
        self.assertEqual(at(0x8d5310e4, 2).hex(), 'f264')        # R2 += 30

        # Both page branches end at the hook: the unshifted one jumps back from 8d5314e2
        # (JUMP.S 0x8d531192), so page 2 can replace the unshifted texts there.
        self.assertEqual(at(0x8d5314e2, 2).hex(), '582e')
        # An unshifted click of encoder 1..4 (codes 10..13, the instrument controller's key
        # table 0x8400e36c entries 4..7) runs 8c0b3314 -> 8c0b2a6c, which on the engine page
        # of a continuous word, Drumline's, only clears a field ([P1] = R6, 8c0b2cca): page 2
        # takes a click stock leaves unused.
        table = [int.from_bytes(at(0x8400e36c + 4 * i, 4), 'little') for i in range(8)]
        self.assertEqual(table[4:], [0x8c0b3314] * 4)
        self.assertEqual(decode_branch(0x8c0b3316, at(0x8c0b3316, 4)), (0x8c0b2a6c, True))
        self.assertEqual(at(0x8c0b2cca, 2).hex(), '0e93')
        # The key the per-key editor edits (stock.hpp drum_edit_key): 0x8400dfc8 if set
        # (8c0b1344, 8c0b1448), else the last played key, 8d4d6cb0, which the first turn
        # latches there (8c0b167a, 8c0b1682); Shift and a pad set it (8c0b29ce); the dbox page
        # reads it (8d530724). The track's "selected" key 8d4d6c98 is another byte.
        self.assertEqual(load(0x8c0b1344), (9, 0x8400dfc8))
        self.assertEqual(at(0x8c0b1448, 8).hex(), '03c800180891eca0')    # R0 = [P1] || R4 = [P5 + 0xc]
        self.assertEqual(at(0x8c0b167a, 6).hex(), '00de4d8db06c')        # CALL.A 0x8d4d6cb0
        self.assertEqual(load(0x8c0b1682), (9, 0x8400dfc8))
        self.assertEqual(at(0x8c0b168a, 2).hex(), '08b0')                # [P1] = R0
        self.assertEqual(at(0x8c0b29ce, 6).hex(), '00d80084c8df')
        self.assertEqual(at(0x8d530724, 6).hex(), '00d80084c8df')
        self.assertEqual(load(0x8d4d6cb0), (1, 0x080379f0))
        self.assertEqual(load(0x8d4d6c98), (1, 0x08037a00))
        # Stock's Shift-press action for engine 4 acts on encoder 1 only (8c0b2752..8c0b275a).
        # R0 = 4; CC = R7 == R0; IF !CC JUMP; CC = R5 == R7; IF CC JUMP 8c0b27f8 (the mode cycle 8d4d6180).
        self.assertEqual(at(0x8c0b2752, 10).hex(), '2060070803103d084f18')
        # The builder publishes with the pad's slot: R2 = B[P5] (8d5ad766), which the
        # region lookup wrote (R2 = P5, 8d5ad680; B[P5] = R1, 8d589308), the record at
        # the track object + 0x394c + slot * 0x80 (8d589308..8d58931e).
        self.assertEqual(at(0x8d5ad766, 8).hex(), '82ce04c2f3bc2a99')
        self.assertEqual(at(0x8d5ad680, 2).hex(), '5530')
        self.assertEqual(decode_branch(0x8d5ad684, at(0x8d5ad684, 4)), (0x8d58926c, True))
        self.assertEqual(at(0x8d589308, 8).hex(), '82ce3982299b0000')
        self.assertEqual(at(0x8d589318, 4).hex(), '22e14c39')
        # The per-key editor (8d4d5f60) finds the pad and never touches +0x00: it edits +0x04
        # (8d4d5fbc) and the rest through its table at 0x84039070.
        self.assertEqual(load(0x8d4d5f94), (10, 0x84039070))
        self.assertEqual(at(0x8d4d5fbc, 2).hex(), '68b0')


if __name__ == '__main__':
    unittest.main()
