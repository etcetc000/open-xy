"""The browser builder export (`sdk.py kit`) run end to end: the native package, the kit's objects and
fallbacks, the subset check against GNU ld (tools/check_browser_kit.mjs) and the builder page: without it a
broken export could pass every other test, since none runs it past its refusals."""
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import unittest

from off_path_code import FIXTURES, kit_folder, player_folder, toolchain
from tooling.pipeline import ROOT


def stock_os():
    path = os.environ.get('OPXY_TEST_STOCK_OS')
    return Path(path) if path and Path(path).is_file() else None


# Links the kit's whole collection as the builder does, numbered with a native package's layout, with the
# extensions and settings given, and writes the module and the firmware (profiling on or off).
LINK = '''
import {readFileSync, writeFileSync} from 'node:fs';
import {openKit, linkKit, patchFirmware, readLayout} from './builder/core.mjs';
const [kitFile, stockFile, layoutFile, extensions, settings, profiling, out] = process.argv.slice(1);
const kit = await openKit(JSON.parse(readFileSync(kitFile, 'utf8')));
const numbers = readLayout(kit, JSON.parse(readFileSync(layoutFile, 'utf8'))).machines;
const linked = linkKit(kit, kit.machines.map(m => m.id), numbers, JSON.parse(extensions), JSON.parse(settings));
writeFileSync(out + '.bin', linked.module);
writeFileSync(out + '.ldr', await patchFirmware(kit, linked, new Uint8Array(readFileSync(stockFile)),
  {settings: JSON.parse(settings), profiling: profiling === 'on'}));
'''

# Imports a machine pack into a site kit and links every selection of the result and of a kit exported
# natively with the same machines: modules, numbers and firmware must be the same. Prints the count.
IMPORT = '''
import {readFileSync} from 'node:fs';
import {openKit, linkKit, patchFirmware, readMachinePack, importMachinePack} from './builder/core.mjs';
const [siteFile, packFile, nativeFile, stockFile] = process.argv.slice(1);
const imported = await importMachinePack(JSON.parse(readFileSync(siteFile, 'utf8')), readMachinePack(readFileSync(packFile)));
const native = await openKit(JSON.parse(readFileSync(nativeFile, 'utf8')));
const stock = new Uint8Array(readFileSync(stockFile));
const ids = native.machines.map(m => m.id), same = (a, b) => a.length === b.length && a.every((v, i) => v === b[i]);
if (imported.machines.map(m => m.id).join() !== ids.join()) throw new Error('machine order');
let compared = 0;
for (let mask = 1; mask < 2 ** ids.length; mask++) {
  const selection = ids.filter((_, i) => mask & 2 ** i);
  const a = linkKit(imported, selection), b = linkKit(native, selection);
  if (!same(a.module, b.module) || JSON.stringify(a.numbers) !== JSON.stringify(b.numbers))
    throw new Error('module differs: ' + selection.join('+'));
  if (!same(await patchFirmware(imported, a, stock, {profiling:false}), await patchFirmware(native, b, stock, {profiling:false})))
    throw new Error('firmware differs: ' + selection.join('+'));
  compared++;
}
console.log(JSON.stringify({compared, build:imported.build}));
'''


@unittest.skipUnless(toolchain() and stock_os(), 'needs the pinned Blackfin tools (OPXY_TEST_COMPILER) and the '
                                                 'stock OS (OPXY_TEST_STOCK_OS)')
class BrowserKitExport(unittest.TestCase):
    def work(self):
        folder = Path(tempfile.mkdtemp(prefix='kit-export-'))
        self.addCleanup(shutil.rmtree, folder, ignore_errors=True)
        return folder

    def export(self, *machines, extensions=()):
        out = self.work() / 'kit'
        command = [sys.executable, str(ROOT / 'sdk.py'), 'kit', '--personal', '--stock-os', str(stock_os()),
                   '--out', str(out), '--toolchain', str(toolchain() / 'bin'), '--compiler', str(toolchain()),
                   '--host-cxx', os.environ.get('OPXY_TEST_HOST_CXX', 'g++'),
                   *(a for m in machines for a in ('--machine', str(m))),
                   *(a for e in extensions for a in ('--extension', str(e)))]
        result = subprocess.run(command, cwd=ROOT, capture_output=True, text=True)
        self.assertEqual(result.returncode, 0, result.stdout[-3000:] + result.stderr[-3000:])
        self.assertTrue((out / 'opxy-builder.html').is_file(), 'no builder page written')
        self.assertTrue((out / 'kit.json').is_file())
        return out

    def build(self, *machines, extensions=(), profiling='on'):
        """sdk.py build of these folders, without the control sweep, as the kit's own native build."""
        out = self.work() / 'build'
        command = [sys.executable, str(ROOT / 'sdk.py'), 'build', '--stock-os', str(stock_os()), '--out', str(out),
                   '--toolchain', str(toolchain() / 'bin'), '--compiler', str(toolchain()), '--sweep', 'off',
                   '--profiling', profiling,
                   *(a for m in machines for a in ('--machine', str(m))),
                   *(a for e in extensions for a in ('--extension', str(e)))]
        result = subprocess.run(command, cwd=ROOT, capture_output=True, text=True)
        self.assertEqual(result.returncode, 0, result.stdout[-3000:] + result.stderr[-3000:])
        return out

    def link(self, kit, package, extensions, settings, profiling='on'):
        """The builder's module and OS for the kit's collection, numbered as `package` numbers it."""
        out = self.work() / 'site'
        result = subprocess.run(['node', '--input-type=module', '-e', LINK, str(kit / 'kit.json'), str(stock_os()),
                                 str(package / 'layout.json'), json.dumps(extensions), json.dumps(settings), profiling, str(out)],
                                cwd=ROOT, capture_output=True, text=True)
        self.assertEqual(result.returncode, 0, result.stderr[-3000:])
        return out.with_suffix('.bin').read_bytes(), out.with_suffix('.ldr').read_bytes()

    def test_a_public_kit_collection_exports(self):
        with tempfile.TemporaryDirectory(prefix='kit-src-') as folder:
            kit = Path(folder) / 'probekit'
            kit_folder(kit, {'padMix': 1})
            self.export(kit, FIXTURES / 'probesynth')

    def test_sdk_1_25_machines_export_beside_older_ones(self):
        # A player reading Controls.instrument and a modelEdit kit, beside an older player and kit:
        # the kit's subset check links every selection of them (tools/check_browser_kit.mjs), so
        # the builder builds each alone or with the others from the one set of objects.
        with tempfile.TemporaryDirectory(prefix='kit-src-') as folder:
            player = Path(folder) / 'probeplayer'
            player_folder(player)
            self.export(FIXTURES / 'probepads', FIXTURES / 'probekit25', FIXTURES / 'probekit21', player)

    def test_the_cpu_extension_on_the_site_is_sdk_py_build(self):
        # The SDK's cpu extension in a kit, whole. The export's own checks link every checked selection with
        # and without it, and with fast_helpers at 1, against GNU ld, and the whole collection with it against
        # the kit's native package.
        machines = (FIXTURES / 'probesynth', FIXTURES / 'probefx')
        cpu = ROOT / 'extensions/cpu'
        kit_dir = self.export(*machines, extensions=[cpu])
        kit = json.loads((kit_dir / 'kit.json').read_text(encoding='utf-8'))
        declared = json.loads((cpu / 'extension.json').read_text(encoding='utf-8'))['capabilities']
        entry = next(e for e in kit['extensions'] if e['id'] == 'cpu')
        # A kit without audio snapshots carries the transport's stock calls too, so a machine pack's
        # addresses equal its site's whatever either's catalog holds.
        self.assertTrue({'stock_telemetry_tick', 'stock_telemetry_receive'} <= set(kit['definitions']))
        self.assertEqual((entry['capabilities'], entry['origin']), (declared, 'official'))
        self.assertEqual(sorted(o.get('service') for o in kit['objects'] if o.get('service') in
                                ('idle', 'block_trims', 'output_slack')),
                         ['block_trims'] * 2 + ['output_slack'] * 4)
        self.assertEqual({c: sorted(o['name'] for o in v['1']) for c, v in kit['unitSources'].items()},
                         {'fast_helpers': ['sdk/divide.o', 'sdk/memory.o']})

        # Without the extension: the same OS as sdk.py build of the machines alone, so a kit that offers
        # cpu costs a build that leaves it out nothing.
        plain = self.build(*machines)
        module, firmware = self.link(kit_dir, plain, [], {})
        self.assertEqual(module, (plain / 'analysis/module.bin').read_bytes())
        self.assertEqual(firmware, (plain / 'candidate/os.ldr').read_bytes())

        # With it and fast_helpers at 1, profiling off as the site's downloads: the same OS as sdk.py build
        # with a settings extension naming cpu's capabilities at that setting.
        settings = self.work() / 'cpu-fast'
        settings.mkdir()
        (settings / 'extension.json').write_text(json.dumps({
            'id': 'cpu', 'name': 'CPU, fast helpers on', 'description': 'Test: cpu with fast helpers on.',
            'claims': [], 'actions': [], 'capabilities': declared, 'settings': {'fast_helpers': 1}}), encoding='utf-8')
        fast = self.build(*machines, extensions=[settings], profiling='off')
        self.assertIn('unit_sources', json.loads((fast / 'analysis/compile-report.json').read_text(encoding='utf-8')))
        module, firmware = self.link(kit_dir, fast, ['cpu'], {'fast_helpers': 1}, 'off')
        self.assertEqual(module, (fast / 'analysis/module.bin').read_bytes())
        self.assertEqual(firmware, (fast / 'candidate/os.ldr').read_bytes())

    def test_a_site_kit_takes_a_pack_needing_its_variants_as_a_native_kit(self):
        # A site with a synth, a player and a kit of earlier SDKs carries what a pack of a player
        # reading its track instrument, kits with the drum-kit controls and modelEdit, and a synth
        # reading the last control touched needs: the services' units and its SDK objects built so. Imported, every selection links as a kit exported with all of them does;
        # without the pack, the site's own build is unchanged.
        from test_machine_pack import ci_synth_folder
        from tooling import machine_pack, native
        work = self.work()
        player_folder(work / 'probeplayer')
        ci_synth_folder(work / 'probeci')
        site = (FIXTURES / 'probesynth', work / 'probeplayer', FIXTURES / 'probekit16')
        pack = (FIXTURES / 'probepads', FIXTURES / 'probekit23', FIXTURES / 'probekit25', work / 'probeci')
        site_dir = self.export(*site)
        kit = json.loads((site_dir / 'kit.json').read_text(encoding='utf-8'))
        self.assertEqual(kit['variants']['build'], dict(services=['amp_envelope', 'kit_controls', 'played_pad',
                                                                  'track_instrument'],
                                                        defines=[d for d in native.DEFINE_ORDER if d in native.VARIANT_DEFINES], headers=['control_interaction']))
        built = {o['name'] for o in kit['variants']['objects']}
        self.assertTrue({'sdk/synth_controls.o', 'sdk/drum_kit_controls.o', 'sdk/drum_amp_envelope.o',
                         'sdk/drum_trigger.o', 'sdk/track_instrument.o', 'sdk/track_instrument_hooks.o'} <= built, built)
        # Neither the players' bridge nor any machine's own object has a variant.
        self.assertFalse(built & {'sdk/players.o', 'probeplayer/player_object.o'}, built)
        self.assertTrue(all(o['owner'] is None for o in kit['variants']['objects']))
        # Without a pack, the site's build: its kit links the same image as sdk.py build of its machines.
        plain = self.build(*site)
        module, firmware = self.link(site_dir, plain, [], {})
        self.assertEqual(module, (plain / 'analysis/module.bin').read_bytes())
        self.assertEqual(firmware, (plain / 'candidate/os.ldr').read_bytes())
        # The pack, as its author exports it from the site's collection with the pack's machines.
        native_dir = self.export(*site, *pack)
        whole = json.loads((native_dir / 'kit.json').read_text(encoding='utf-8'))
        for m in whole['machines']:
            if m['id'] in ('probesynth', 'probeplayer', 'probekit16'):
                m['origin'] = 'catalog'
        pack_file = machine_pack.write(whole, self.work(), 'Test pack')
        result = subprocess.run(['node', '--input-type=module', '-e', IMPORT, str(site_dir / 'kit.json'), str(pack_file),
                                 str(native_dir / 'kit.json'), str(stock_os())], cwd=ROOT, capture_output=True, text=True)
        self.assertEqual(result.returncode, 0, result.stderr[-3000:])
        outcome = json.loads(result.stdout)
        self.assertEqual(outcome['compared'], 2 ** 7 - 1)
        self.assertEqual(outcome['build'], {key: whole['build'][key] for key in ('services', 'defines', 'headers')})

    def test_audio_snapshot_machines_export_and_link_in_any_selection(self):
        # A synth and an effect showing their own audio, beside a machine without: the export's
        # GNU ld parity check links the whole selection and each machine alone, so each owner is also linked left out,
        # its record and its registry row with it.
        kit_dir = self.export(FIXTURES / 'probescope', FIXTURES / 'probemeter', FIXTURES / 'probesynth')
        kit = json.loads((kit_dir / 'kit.json').read_text(encoding='utf-8'))
        machines = {m['id']: m for m in kit['machines']}
        for owner in ('probescope', 'probemeter'):
            self.assertIn('audioTransport', machines[owner]['features'])
        self.assertTrue(all('absent' not in m for m in kit['machines']))
        # The transport's objects link only with its service; its hooks continue to stock.
        services = {o['name']: o.get('service') for o in kit['objects']}
        self.assertEqual({name for name, service in services.items() if service == 'telemetry'},
                         {'sdk/telemetry_protocol.o', 'sdk/telemetry_native.o', 'sdk/telemetry_hooks.o',
                          'sdk/telemetry_registry.o'})
        self.assertTrue({'stock_telemetry_tick', 'stock_telemetry_receive'} <= set(kit['definitions']))


if __name__ == '__main__':
    unittest.main()
