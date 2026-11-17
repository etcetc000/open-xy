"""Sample Studio's capability (docs/sample-studio.md, sdk/include/opxy/sample_studio.hpp).

Pure Python, no host compile. Checks the five library sites and their trampolines, that every
service the header declares is defined by the SDK, that the master-bus receiver hands the
player its channels and stock the rest, and the tooling: the extension field, its exports and
artwork, one studio per build, and the units and asset it brings.
"""
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
    root = None


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
        self.assertIn('sampleStudio', profile['services']['drum_sends'])
        bus = [p for p in profile['patches'] if p.get('service') == 'master_bus']
        self.assertEqual(sorted(p['address'] for p in bus), ['0x8d511b80', '0x8d5b145e'])

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
            self.assertIn('const studio::Editor* e = editor();', body.group(1), symbol)
        self.assertIn('const studio::Editor* editor() { return sdk_sample_studio_editor(); }', host)
        frame = re.search(r'\nvoid sdk_sample_studio_frame\(void\* screen, i32 elapsed\) \{(.*?)\n\}', host, re.S).group(1)
        self.assertIn('e->frame(elapsed)', frame)


class Services(unittest.TestCase):
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
        self.assertIn('const u32 kFirstId = 138, kChannels = 15;', audio)
        receiver = re.search(r'void sdk_master_receive\(u32 base, u32 id, i32 value\) \{(.*?)\n\}', audio, re.S).group(1)
        self.assertEqual(receiver.count('0x8d50eb8c'), 1)   # stock's setter has no bounds check
        self.assertLess(receiver.index('id >= kFirstId && id < kFirstId + kChannels'), receiver.index('0x8d50eb8c'))
        self.assertIn('p->receive(id - kFirstId, value);', receiver)
        master = re.search(r'void sdk_master_process\(void\* object, i32\* left, i32\* right\) \{(.*?)\n\}', audio, re.S).group(1)
        self.assertLess(master.index('0x11a02c10'), master.index('p->master(left, right)'))   # stock's EQ first

    def test_the_voice_hooks_stay_weak(self):
        send = (PLATFORM / 'drum_send.cpp').read_text()
        for name in ('sdk_studio_voice', 'sdk_studio_gain', 'sdk_studio_level', 'sdk_studio_mixer_begin'):
            self.assertIn('__attribute__((weak))', send[send.index('void ' + name):][:200], name)
            self.assertRegex(AUDIO.read_text(), r'\n(void) ' + name + r'\(')

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
            self.assertEqual(extensions.metadata([e], {'studio_test': 'personal'})[0]['sample_studio'], True)
        for broken in (dict(cpp=None), dict(artwork=False), dict(cpp=EXTENSION.replace('player', 'other'))):
            with tempfile.TemporaryDirectory() as folder, self.assertRaises(Exception, msg=str(broken)):
                extensions.extension_at(self.extension(folder, data, **broken))
        with tempfile.TemporaryDirectory() as folder, self.assertRaises(Exception):
            extensions.extension_at(self.extension(folder, dict(data, sample_studio={'x': 1})))   # no options yet
        text = (ROOT / 'tooling/extensions.py').read_text()
        self.assertIn("require(len(studios) <= 1, 'Sample Studio has multiple owners: '", text)
        self.assertIn("require(not (studios and processors), 'Sample Studio and a master processor share hook sites", text)
        self.assertIn('sdk_sample_studio_', text)   # the editor and player accessors the SDK's sites call

    def test_service_user_from_the_extension(self):
        from tooling import native

        class Machine:
            kind = 'sample'
            definition = {}

        class Synth:
            kind = 'synth'
            definition = {}
        self.assertNotIn('sampleStudio', native.service_users([Machine()]))
        self.assertIn('sampleStudio', native.service_users(Built([Machine()], [Studio()])))
        with self.assertRaises(Exception):
            native.service_users(Built([Synth()], [Studio()]))   # needs the samples service

    def test_units_belong_to_the_service(self):
        from tooling import native
        self.assertEqual(native.SAMPLE_STUDIO_UNITS, ('sample_studio_host', 'sample_studio_hooks', 'sample_studio_audio'))
        for unit in native.SAMPLE_STUDIO_UNITS:
            self.assertEqual(native.UNIT_SERVICES[unit], 'sample_studio')
            self.assertTrue((PLATFORM / (unit + ('.s' if unit.endswith('hooks') else '.cpp'))).is_file())

    def test_the_artwork_is_installed_only_with_the_extension(self):
        from tooling import native
        original = native._prepare_assets_without_sample_studio
        native._prepare_assets_without_sample_studio = lambda *a: {}
        try:
            with tempfile.TemporaryDirectory() as folder:
                studio = Studio()
                studio.root = Path(folder)
                (studio.root / 'sample_studio.svg').write_text(ARTWORK)
                self.assertEqual(native.prepare_assets(None, None, []), {})
                self.assertEqual(native.prepare_assets(None, None, Built([], [studio])),
                                 {'content/display/sample_studio.svg': ARTWORK.encode()})
        finally:
            native._prepare_assets_without_sample_studio = original


if __name__ == '__main__':
    unittest.main()
