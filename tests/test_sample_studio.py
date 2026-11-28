"""Sample Studio's capability (docs/sample-studio.md, sdk/include/opxy/sample_studio.hpp).

Pure Python, no host compile. Checks the five library sites and their trampolines, that every
service the header declares is defined by the SDK, that the master-bus receiver hands the
player its channels and stock the rest, and the tooling: the extension field, its exports and
artwork, one studio per build, and the units and asset it brings.
"""
import base64
import hashlib
import json
import re
import sys
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT))

PLATFORM = ROOT / 'sdk/platform/opxy-1.2.1'
API = ROOT / 'sdk/include/opxy/sample_studio.hpp'
HOST = PLATFORM / 'sample_studio_host.cpp'
AUDIO = PLATFORM / 'sample_studio_audio.cpp'
HOOKS = PLATFORM / 'sample_studio_hooks.s'
# The five sites: address, the stock bytes, the entry and its trampoline.
SITES = {
    '0x8c0a330a': ('083200e80000f505', 'sdk_sample_studio_press', 'sdk_sample_studio_stock_press'),
    '0x8c0a2be4': ('00e80000f405a66f', 'sdk_sample_studio_shift_press', 'sdk_sample_studio_stock_shift_press'),
    '0x8c0a2d68': ('00e80700b8617005', 'sdk_sample_studio_release', 'sdk_sample_studio_stock_release'),
    '0x8c0a24d0': ('00e8000060058064', 'sdk_sample_studio_turn', 'sdk_sample_studio_stock_turn'),
    '0x8d55341c': ('00e80e00e305666f', 'sdk_sample_studio_frame', 'sdk_sample_studio_stock_frame'),
}
ARTWORK = ('<svg xmlns="http://www.w3.org/2000/svg" width="480" height="220" viewBox="0 0 480 220">'
           '<path id="ss_a" d="M0 0 L1 1 Z"/></svg>')
EXTENSION = '''#include <opxy/extension.hpp>
#include <opxy/sample_studio.hpp>
namespace opxy { namespace extensions { namespace studio_test {
extern constexpr Hooks hooks = Hooks();
extern const studio::Editor editor = {};
extern const studio::Player player = {};
}}}
'''


def code(path):
    return re.sub(r'/\*.*?\*/|//[^\n]*', '', path.read_text(), flags=re.S)


class Studio:
    sample_studio, master_processor, drumline_page, capabilities, linked = True, None, None, (), True
    id, root = 'studio_test', None


class Built(list):
    def __init__(self, machines, extensions=()):
        super().__init__(machines)
        self.extensions = tuple(extensions)


class Sites(unittest.TestCase):
    def test_profile_service_and_its_five_entry_branches(self):
        profile = json.loads((ROOT / 'profiles/opxy-1.2.1.json').read_text())
        self.assertEqual(profile['services']['sample_studio'], ['sampleStudio'])
        mine = [p for p in profile['patches'] if p.get('service') == 'sample_studio']
        self.assertEqual({p['address']: (p['before'], p['symbol']) for p in mine},
                         {a: (b, s) for a, (b, s, _) in SITES.items()})
        for p in mine:
            self.assertEqual((p['kind'], p['length'], p['call']), ('hook', 8, False))
        self.assertIn('sampleStudio', profile['services']['mixer'])
        self.assertNotIn('sampleStudio', profile['services']['drum_sends'])   # the send buses' sources stay the sends'
        bus = [p for p in profile['patches'] if p.get('service') == 'master_bus']
        self.assertEqual(sorted(p['address'] for p in bus), ['0x8d511b80'])
        # the B3 receiver's call is shared with the device identity: the master bus's receiver without it
        receive = next(p for p in profile['patches'] if p['address'] == '0x8d5b145e')
        self.assertEqual((receive['service'], receive['symbols']['master_bus']), ('parameter_receive', 'sdk_master_receive'))
        self.assertIn('sampleStudio', profile['services']['parameter_receive'])

    def test_each_trampoline_replays_its_bytes_and_returns_after_them(self):
        text = HOOKS.read_text()
        for address, (before, _symbol, stub) in SITES.items():
            comment = text.split(f'.global {stub}\n', 1)[0].rsplit('/*', 1)[1]
            body = text.split(f'{stub}:\n', 1)[1].split('.size', 1)[0]
            self.assertIn(address[2:], comment)
            replayed = ''.join(re.findall(r'[0-9a-f]{4}', comment.rsplit('(', 1)[1].split(')')[0].replace(' ', '')))
            self.assertEqual(replayed, before, stub)
            target = int(address, 16) + 8
            self.assertIn(f'P0.H = 0x{target >> 16:04x}; P0.L = 0x{target & 0xffff:04x};', body)
            self.assertIn('JUMP (P0);', body)

    def test_each_entry_hands_the_editor_its_input_and_falls_back_to_stock(self):
        host = HOST.read_text()
        for _a, (_b, symbol, stub) in SITES.items():
            body = re.search(rf'\n(?:u32|void) {symbol}\([^)]*\) \{{(.*?)\n\}}', host, re.S)
            self.assertIsNotNone(body, symbol)
            self.assertIn(f'__asm__("{stub}")', host)
            fallback = 'studio::stock_frame()' if symbol.endswith('frame') else stub + '('
            self.assertIn(fallback, body.group(1), symbol)              # what the editor doesn't take is stock's
            self.assertIn('const studio::Editor* e = editor();' if symbol.endswith('frame') else
                          'const studio::Editor* e = keys_editor();', body.group(1), symbol)
        self.assertIn('const studio::Editor* editor() { return sdk_sample_studio_editor(); }', host)
        frame = re.search(r'\nvoid sdk_sample_studio_frame\(void\* screen, i32 elapsed\) \{(.*?)\n\}', host, re.S).group(1)
        self.assertIn('e->frame(elapsed)', frame)

    def test_the_editor_hears_every_frame_and_draws_only_the_librarys(self):
        # The frame site is the browser screen stock's other browsers share. The editor hears every
        # frame, so it closes at once when the library leaves (029 handed it only the library's frames,
        # so opening the preset browser left the studio open and its close-time work undone); in a frame
        # the library doesn't own, stock draws whatever the editor returns, and the surface draws nothing.
        host = code(HOST)
        frame = re.search(r'\nvoid sdk_sample_studio_frame\(void\* screen, i32 elapsed\) \{(.*?)\n\}', host, re.S).group(1)
        self.assertIn('const studio::Editor* e = editor();', frame)
        self.assertNotIn('? editor() : nullptr', frame)
        self.assertLess(frame.index('library_frame = studio::library_on_top();'), frame.index('e->frame(elapsed)'))
        self.assertIn('const bool drew = e && e->frame(elapsed) && library_frame;', frame)
        self.assertIn('if (!drew) studio::stock_frame();', frame)
        self.assertTrue(frame.rstrip().endswith('library_frame = false;'))
        for name in ('surface_present(const Surface& frame)', 'surface_blank()'):
            body = re.search(r'\nvoid ' + re.escape(name) + r' \{\n(.*?)\n', host).group(1)
            self.assertRegex(body, r'^\s*if \((?:!bound \|\| )?!library_frame\) return;', name)

    def test_the_library_keys_reach_the_editor_only_while_it_is_on_top(self):
        host = code(HOST)
        self.assertIn('const studio::Editor* keys_editor() { return studio::library_on_top() ? editor() : nullptr; }', host)
        for symbol in ('sdk_sample_studio_press', 'sdk_sample_studio_shift_press', 'sdk_sample_studio_release',
                       'sdk_sample_studio_turn'):
            body = re.search(rf'\n(?:u32|void) {symbol}\([^)]*\) \{{(.*?)\n\}}', host, re.S).group(1)
            self.assertIn('const studio::Editor* e = keys_editor();', body, symbol)
        # every turn position goes to the editor, unmasked: it takes what it owns (all, while open)
        turn = re.search(r'\nvoid sdk_sample_studio_turn\([^)]*\) \{(.*?)\n\}', host, re.S).group(1)
        self.assertNotIn('& 3', turn)
        self.assertIn('if (event && e && e->turn(position, event[1])) return;', turn)
        self.assertIn('sdk_sample_studio_stock_turn(controller, position, event);', turn)

    def test_every_declared_service_is_defined(self):
        header = code(API)
        body = header.split('struct Player', 1)[1].split('};', 1)[1]   # after the extension's two tables
        names = set(re.findall(r'^[A-Za-z_][\w:<>,* ]*?[\s*&]([a-z_][a-z0-9_]*)\(', body, re.M))
        self.assertGreater(len(names), 50)
        defined = code(HOST) + code(AUDIO)
        for name in sorted(names):
            self.assertRegex(defined, r'[\s*&:]' + name + r'\([^;{]*\)\s*(const\s*)?\{', name)

    def test_the_extension_holds_no_firmware_address(self):
        # the header is the whole surface: no stock<>, address or asm reaches the extension
        header = code(API)
        self.assertNotIn('stock<', header)
        self.assertNotIn('__asm__', header)
        self.assertNotRegex(header, r'0x8[0-9a-f]{7}')

    def test_the_receiver_gives_the_player_its_channels_and_stock_the_rest(self):
        audio = AUDIO.read_text()
        self.assertIn('const u32 kFirstId = 146, kChannels = 15;', audio)
        receiver = re.search(r'void sdk_master_receive\(u32 base, u32 id, i32 value\) \{(.*?)\n\}', audio, re.S).group(1)
        self.assertEqual(receiver.count('0x8d50eb8c'), 1)   # stock's setter has no bounds check
        self.assertLess(receiver.index('id >= kFirstId && id < kFirstId + kChannels'), receiver.index('0x8d50eb8c'))
        self.assertIn('p->receive(id - kFirstId, value);', receiver)
        master = re.search(r'void sdk_master_process\(void\* object, i32\* left, i32\* right\) \{(.*?)\n\}', audio, re.S).group(1)
        self.assertLess(master.index('0x11a02c10'), master.index('p->master(left, right)'))   # stock's EQ first

    def test_beside_a_master_processor_one_handler_runs_both(self):
        # A stock site has one hook: the master EQ call and the receiver enter master_bus.cpp, which
        # runs the master processor's stage then the player, and the player's numbers then the
        # master processor's receiver. Each side names its entries apart only under the define.
        shared = code(PLATFORM / 'master_bus.cpp')
        process = re.search(r'\nvoid sdk_master_process\(void\* object, i32\* left, i32\* right\) \{(.*?)\n\}', shared, re.S).group(1)
        self.assertLess(process.index('sdk_processor_process(object, left, right)'), process.index('sdk_studio_master(left, right)'))
        self.assertLess(process.index('kStockEq'), process.index('sdk_studio_master'))   # no processor: stock's EQ
        receive = re.search(r'\nvoid sdk_master_receive\(u32 base, u32 id, i32 value\) \{(.*?)\n\}', shared, re.S).group(1)
        self.assertLess(receive.index('if (sdk_studio_receive(id, value)) return;'), receive.index('sdk_processor_receive(base, id, value)'))
        self.assertIn('kStockSet', receive)
        self.assertIn('0x11a02c10', shared)
        self.assertIn('0x8d50eb8c', shared)
        audio, processor = AUDIO.read_text(), (PLATFORM / 'master_filter_stock.cpp').read_text()
        for text, names in ((audio, ('sdk_studio_master', 'sdk_studio_receive')),
                            (processor, ('sdk_processor_receive', 'sdk_processor_process'))):
            shared_part, alone = re.search(r'#ifdef OPXY_MASTER_SHARED\n(.*?)#else\n(.*?)#endif', text, re.S).groups()
            for name in names:
                self.assertIn('__asm__("%s")' % name, shared_part, name)
            self.assertIn('__asm__("sdk_master_process")', alone)
            self.assertIn('__asm__("sdk_master_receive")', alone)
        # the player's shared receiver answers 1 only for its own numbers; kit_absent answers 0
        studio = re.search(r'u32 sdk_studio_receive\(u32 id, i32 value\) \{(.*?)\n\}', audio, re.S).group(1)
        self.assertIn('id < kFirstId || id >= kFirstId + kChannels) return 0;', studio)
        self.assertIn('p->receive(id - kFirstId, value);\n    return 1;', studio)
        studio_master = re.search(r'void sdk_studio_master\(i32\* left, i32\* right\) \{(.*?)\n\}', audio, re.S).group(1)
        self.assertNotIn('0x11a02c10', studio_master)   # the shared handler ran the EQ (or the processor) already

    def test_the_shared_handler_only_for_a_collection_with_both(self):
        from tooling import native

        class Machine:
            kind, definition = 'sample', {'resources': {}}
            id, prefix, sources, native, display = 'm', 'm', (), (), 'stock'

        class Processor(Studio):
            sample_studio, master_processor, id = False, {'artwork': 'p.svg'}, 'processor'
        profile = json.loads((ROOT / 'profiles/opxy-1.2.1.json').read_text())
        collections = {'studio': [Studio()], 'processor': [Processor()], 'both': [Studio(), Processor()], 'none': []}
        for name, extensions in collections.items():
            built = Built([Machine()], extensions)
            both = name == 'both'
            self.assertEqual(native.master_shared(built), both, name)
            self.assertEqual(native.MASTER_SHARED_DEFINE in native.sdk_unit_defines(profile, built), both, name)
            self.assertEqual('sdk/master_bus.o' in [o for o, _m, _s in native.plan(built, profile)], both, name)
            fallbacks = native.service_fallbacks(native.active_services(profile, native.service_users(built)),
                                                 native.master_shared(built))
            for service, symbols in (('master_filter', native.MASTER_PROCESSOR_BUS), ('sample_studio', native.MASTER_STUDIO_BUS)):
                found = {s for s in fallbacks.get(service, {}) if s in symbols}
                self.assertEqual(found, set(symbols) if both else set(), (name, service))
        self.assertEqual(native.UNIT_SERVICES['master_bus'], 'master_bus')
        self.assertEqual(native.MASTER_SHARED_DEFINE, '-DOPXY_MASTER_SHARED=1')
        for name in native.MASTER_PROCESSOR_BUS + native.MASTER_STUDIO_BUS:
            self.assertIn('__asm__("%s")' % name, (PLATFORM / 'master_bus.cpp').read_text(), name)

    def test_no_two_services_share_a_b3_parameter(self):
        # Stock's audio board receives every B3 parameter through one call (8d5b145e), shared by the
        # services that own some: each range read from its own source, none overlapping, each within
        # a byte. A clash would show only on a device, where one receiver swallows the other's numbers.
        def numbers(path, pattern):
            m = re.search(pattern, path.read_text())
            self.assertIsNotNone(m, path.name)
            return [int(x) for x in m.groups()]
        master = numbers(PLATFORM / 'master_processor.hpp', r'message_id = (\d+), commit_id = (\d+)')
        identity = numbers(PLATFORM / 'device_identity.cpp', r'kBegin = (\d+), kFirstWord = \d+, kCommit = (\d+)')
        first, count = numbers(AUDIO, r'kFirstId = (\d+), kChannels = (\d+)')
        studio = [first, first + count - 1]
        ranges = {'stock': [0, 3], 'master processor': master, 'device identity': identity, 'Sample Studio': studio}
        self.assertEqual(studio, [146, 160])
        names = sorted(ranges, key=lambda n: ranges[n][0])
        for a, b in zip(names, names[1:]):
            self.assertLess(ranges[a][1], ranges[b][0], (a, ranges[a], b, ranges[b]))
        for name, (low, high) in ranges.items():
            self.assertTrue(0 <= low <= high <= 255, name)
        # the editor's channels reach the player at the same base, both ways
        self.assertIn('stock<void(u32, i32)>(0x8d4dc84c)(%d + channel, value);' % first, HOST.read_text())
        self.assertIn('if (channel < %d)' % count, HOST.read_text())

    def test_the_player_reaches_the_mixer_through_its_service(self):
        # mixer.cpp calls the player's four entry points, which this board's unit defines; a build
        # with the mixer and no studio resolves them to kit_absent (no weak symbols: the browser
        # builder links none).
        from tooling import native
        mixer = code(PLATFORM / 'mixer.cpp')
        for name in native.MIXER_STUDIO:
            self.assertRegex(code(AUDIO), r'\nvoid ' + name + r'\([^)]*\) \{', name)
            self.assertEqual(len(re.findall(r'\n    ' + name + r'\(', mixer)), 1, name)
        self.assertEqual(native.service_fallbacks({'mixer'})['sample_studio'], dict.fromkeys(native.MIXER_STUDIO, 'kit_absent'))
        self.assertNotIn('sample_studio', native.service_fallbacks({'drum_sends', 'drums'}))
        for path in (PLATFORM / 'drum_send.cpp', PLATFORM / 'mixer.cpp', AUDIO):
            self.assertNotIn('weak', path.read_text(), path.name)

    def test_the_surface_binds_again_for_other_ids_or_counts(self):
        ready = re.search(r'\nbool surface_ready\([^)]*\) \{(.*?)\n\}', code(HOST), re.S).group(1)
        rebind = ready.index('bound = false;')
        self.assertLess(ready.index('if (bound && (artwork != bound_ids || paths != bound_paths || nodes != bound_nodes'), rebind)
        self.assertLess(rebind, ready.index('bound_ids = artwork;'))
        self.assertLess(ready.index('bound_palette = palette;'), ready.index('init(&object);'))
        for name in ('texts != bound_texts', 'palette != bound_palette'):
            self.assertIn(name, ready)

    def test_a_whole_entry_and_an_empty_highlight(self):
        host = code(HOST)
        whole = re.search(r'\nvoid set_entry_whole\([^)]*\) \{(.*?)\n\}', host, re.S).group(1)
        self.assertIn('w[kFrames] = w[kEnd] = w[kLoopEnd] = frames;', whole)
        self.assertIn('w[kStart] = w[kLoopStart] = 0;', whole)
        highlighted = re.search(r'\nbool highlighted\(char\* out, u32 size\) \{(.*?)\n\}', host, re.S).group(1)
        self.assertTrue(highlighted.lstrip().startswith('if (!out || !size) return false;'))

    def test_the_surface_checks_and_applies_only_the_bound_slots(self):
        # An editor binds fewer slots than the Surface holds; the empty ones would fail draw::valid
        # and drop every frame, so neither the check nor the apply is whole-frame.
        host = code(HOST)
        present = re.search(r'\nvoid surface_present\(const Surface& frame\) \{(.*?)\n\}', host, re.S).group(1)
        self.assertNotIn('Renderer::apply(', present)
        self.assertNotIn('draw::valid', present)
        self.assertIn('bound_valid(frame)', present)
        for part, count in (('path', 'bound_paths'), ('node', 'bound_nodes'), ('text', 'bound_texts')):
            self.assertRegex(present, rf'< {count}; \+\+\w\) Renderer::apply_{part}\(&object, frame, \w\);')
        check = re.search(r'\nbool bound_valid\(const studio::Surface& frame\) \{(.*?)\n\}', host, re.S).group(1)
        for count in ('bound_paths', 'bound_nodes', 'bound_texts', 'bound_palette'):
            self.assertIn(count, check)

    def test_the_pool_keeps_stocks_headroom(self):
        header = API.read_text()
        self.assertIn('pool_capacity = 0x03f00000u, pool_headroom = 8u << 20;', header)
        self.assertIn('typedef draw::Frame<surface_paths, surface_points, surface_nodes, surface_texts, surface_palette> Surface;',
                      header)


class Tooling(unittest.TestCase):
    def extension(self, folder, data, cpp=EXTENSION, artwork=True):
        root = Path(folder) / 'studio_test'
        root.mkdir(exist_ok=True)
        (root / 'extension.json').write_text(json.dumps(data))
        if cpp is not None:
            (root / 'extension.cpp').write_text(cpp)
        if artwork:
            (root / 'sample_studio.svg').write_text(ARTWORK)
        return root

    def test_the_extension_field(self):
        from tooling import extensions
        data = {'id': 'studio_test', 'name': 'Studio test', 'description': 'A test.',
                'claims': [], 'actions': [], 'sample_studio': {}}
        with tempfile.TemporaryDirectory() as folder:
            e = extensions.extension_at(self.extension(folder, data))
            self.assertTrue(e.sample_studio and e.linked)
            row = extensions.metadata([e], {'studio_test': 'personal'})[0]['sample_studio']
            self.assertEqual(row['asset'], 'sample_studio.svg')
            self.assertEqual(base64.b64decode(row['artwork']), ARTWORK.encode())
            self.assertEqual(row['sha256'], hashlib.sha256(ARTWORK.encode()).hexdigest())
        unqualified = EXTENSION.replace('const studio::Player player', 'const int player')   # the word alone isn't enough
        for broken in (dict(cpp=None), dict(artwork=False), dict(cpp=EXTENSION.replace('player', 'other')), dict(cpp=unqualified)):
            with tempfile.TemporaryDirectory() as folder, self.assertRaises(Exception, msg=str(broken)):
                extensions.extension_at(self.extension(folder, data, **broken))
        with tempfile.TemporaryDirectory() as folder, self.assertRaises(Exception):
            extensions.extension_at(self.extension(folder, dict(data, sample_studio={'x': 1})))   # no options yet
        text = (ROOT / 'tooling/extensions.py').read_text()
        self.assertIn("require(len(studios) <= 1, 'Sample Studio has multiple owners: '", text)
        self.assertNotIn('build them apart', text)   # a master processor shares the build (master_bus.cpp)
        self.assertIn('sdk_sample_studio_', text)   # the editor and player accessors the SDK's sites call

    def test_service_user_from_the_extension(self):
        from tooling import native

        class Machine:
            kind = 'sample'
            definition = {'resources': {}}

        class Synth:
            kind = 'synth'
            definition = {'resources': {}}
        self.assertNotIn('sampleStudio', native.service_users([Machine()]))
        self.assertIn('sampleStudio', native.service_users(Built([Machine()], [Studio()])))
        with self.assertRaises(Exception):
            native.service_users(Built([Synth()], [Studio()]))   # needs the samples service

        class Identity(Machine):
            definition = {'resources': {'deviceIdentity': 1}}
        # the device identity and the studio share a build: their B3 ranges are apart (below)
        self.assertIn('sampleStudio', native.service_users(Built([Machine(), Identity()], [Studio()])))
        self.assertIn('deviceIdentity', native.service_users(Built([Machine(), Identity()], [Studio()])))

    def test_units_belong_to_the_service(self):
        from tooling import native
        self.assertEqual(native.SAMPLE_STUDIO_UNITS, ('sample_studio_host', 'sample_studio_hooks', 'sample_studio_audio'))
        for unit in native.SAMPLE_STUDIO_UNITS:
            self.assertEqual(native.UNIT_SERVICES[unit], 'sample_studio')
            self.assertTrue((PLATFORM / (unit + ('.s' if unit.endswith('hooks') else '.cpp'))).is_file())

    def test_the_artwork_is_installed_only_with_the_extension(self):
        from unittest import mock
        from tooling import native
        profile = {'stock_catalog_sha256': native.sha(b'{}')}
        with tempfile.TemporaryDirectory() as folder, \
                mock.patch.object(native, 'extend_catalog', lambda *a: b'{}'), \
                mock.patch.object(native.stock_content, 'check_output', lambda *a: None):
            studio = Studio()
            studio.master_processor = None
            studio.root = Path(folder)
            (studio.root / 'sample_studio.svg').write_text(ARTWORK)
            catalog = 'content/op1_factory.json'
            self.assertEqual(set(native.prepare_assets(b'{}', profile, Built([]))), {catalog})
            outputs = native.prepare_assets(b'{}', profile, Built([], [studio]))
            self.assertEqual(outputs['content/display/sample_studio.svg'], ARTWORK.encode())
            self.assertEqual(set(outputs), {catalog, 'content/display/sample_studio.svg'})


if __name__ == '__main__':
    unittest.main()
