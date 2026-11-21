"""Native links without a service: a collection links whichever services its machines use.

The step-lock handles (lock_handles.cpp) are the drum_locks service's, linked only with a
padMix kit, a Shift-row synth or a drumline_page extension; units every build links reach them through startup.cpp, which forwards to
them or answers 0. These link a synth alone, a kit without padMix and a padMix kit with
the pinned Blackfin tools, and check that a browser selection without a service finds a
fallback for every symbol a shared object imports from it. The last links the 1.16
fixture kit alone and beside the 1.18 one: only with the 1.18 kit does the build link
the played_pad service's snapshot (drum_trigger.cpp).
"""
import copy
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import unittest

from tooling import native
from tooling.native import service_fallbacks
from tooling.pipeline import ROOT, TOOLCHAIN_HOME

FIXTURES = ROOT / 'tests/fixtures/machines'
# The compiler native qualification was given (tooling.checks), else the fetched toolchain.
TOOLS = Path(os.environ.get('OPXY_TEST_COMPILER') or ROOT / TOOLCHAIN_HOME) / 'bin'
KIT_ENGINE = '''#include "core.h"
namespace probekit {
inline void initialize(Voice& voice, Shared&, opxy::u32) { voice.phase = 0; }
inline void start(Voice& voice, Shared&, opxy::u32) { voice.phase = 0; }
inline void destroy(Voice&, Shared&, opxy::u32) {}
inline void drum_defaults(opxy::u32 key, opxy::u8* model, opxy::u8* controls) {
    *model = static_cast<opxy::u8>(key & 1);
    for (unsigned i = 0; i < 8; ++i) controls[i] = 64;
}
inline void drum_select(Voice&, Shared&, opxy::u32, opxy::u32) {}
inline const char* model_name(opxy::u32 model) { return model ? "B" : "A"; }
inline const char* parameter_name(opxy::u32, opxy::u32) { return "P"; }
inline opxy::u32 parameter_count(opxy::u32) { return 8; }
inline void render(Voice& voice, Shared&, opxy::u32, opxy::Output output, opxy::Parameters, opxy::Pitch pitch) {
    for (unsigned i = 0; i < opxy::block_frames; ++i) {
        output[i] = static_cast<opxy::i32>(voice.phase >> 2) - 0x20000000;
        voice.phase += pitch[i];
    }
}
}
'''


def kit_folder(folder, pad_mix):
    """A two-model kit made from the probesynth fixture, with or without padMix."""
    source = FIXTURES / 'probesynth'
    definition = json.loads((source / 'definition.json').read_text(encoding='utf-8'))
    definition.update(id='probekit', name='Probe Kit', presets={})
    for control, parameter in enumerate(copy.deepcopy(definition['parameters'])):
        parameter.update(id=parameter['id'] + '_alt', label='Alt ' + str(control), control=control)
        definition['parameters'].append(parameter)
    definition['resources'].update(modelCount=2)
    if pad_mix:
        definition['resources']['padMix'] = 1
    folder.mkdir()
    (folder / 'definition.json').write_text(json.dumps(definition), encoding='utf-8')
    (folder / 'machine.json').write_text(json.dumps({'schema': 1, 'sdk': '1.10', 'kind': 'drum',
                                                     'description': 'Test fixture: a two-model kit.'}), encoding='utf-8')
    (folder / 'core.h').write_text('#pragma once\n#include <opxy/audio.hpp>\nnamespace probekit {\n'
                                   'struct Voice { opxy::u32 phase; };\nstruct Shared {};\n}\n', encoding='utf-8')
    (folder / 'engine.cpp').write_text(KIT_ENGINE, encoding='utf-8')
    for name in ('display.cpp', 'display.h', 'panel.svg.fragment'):
        (folder / name).write_text((source / name).read_text(encoding='utf-8').replace('probesynth', 'probekit'),
                                   encoding='utf-8')
    return folder


def run(command):
    hidden = dict(creationflags=subprocess.CREATE_NO_WINDOW) if os.name == 'nt' else {}
    return subprocess.run([str(c) for c in command], capture_output=True, text=True, timeout=600, cwd=ROOT, **hidden)


def symbols(obj):
    """(defined globals, undefined) of a Blackfin object, from objdump's symbol table."""
    objdump = TOOLS / ('bfin-elf-objdump' + ('.exe' if os.name == 'nt' else ''))
    table = run([objdump, '-t', obj])
    if table.returncode:
        raise AssertionError(table.stdout + table.stderr)
    defined, undefined = set(), set()
    for line in table.stdout.splitlines():
        fields = line.split()
        if len(fields) < 4 or not fields[0].isalnum():
            continue
        name = fields[-1]
        if '*UND*' in line:
            undefined.add(name)
        elif ' g ' in line:
            defined.add(name)
    return defined, undefined


@unittest.skipUnless((TOOLS / ('bfin-elf-g++' + ('.exe' if os.name == 'nt' else ''))).is_file(),
                     'No Blackfin toolchain in local/toolchain (python sdk.py toolchain fetch, or set OPXY_TEST_COMPILER)')
class ServiceLinks(unittest.TestCase):
    def compile(self, folder, *machines):
        out = Path(folder) / 'module'
        result = run([sys.executable, ROOT / 'sdk.py', 'compile', *(a for m in machines for a in ('--machine', m)),
                      '--toolchain', TOOLS, '--compiler', TOOLS.parent, '--out', out])
        self.assertEqual(result.returncode, 0, result.stdout[-4000:] + result.stderr[-4000:])
        return out, json.loads((out / 'compile-report.json').read_text(encoding='utf-8'))

    def test_a_synth_alone_links_without_the_kit_services(self):
        with tempfile.TemporaryDirectory(prefix='links-synth-') as folder:
            _out, report = self.compile(folder, FIXTURES / 'probesynth')
            self.assertFalse({'drums', 'drum_sends', 'drum_locks'} & set(report['services']))

    def test_a_kit_without_pad_mix_links_without_the_step_locks(self):
        with tempfile.TemporaryDirectory(prefix='links-kit-') as folder:
            kit = kit_folder(Path(folder) / 'probekit', pad_mix=False)
            _out, report = self.compile(folder, kit, FIXTURES / 'probesynth')
            self.assertIn('drums', report['services'])
            self.assertFalse({'drum_sends', 'drum_locks'} & set(report['services']))

    def test_every_shared_import_of_a_kit_service_has_a_browser_fallback(self):
        # A browser selection leaves out the objects of a service it does not use; what a
        # shared object imports from them must resolve to a fallback (builder/core.mjs).
        with tempfile.TemporaryDirectory(prefix='links-mix-') as folder:
            kit = kit_folder(Path(folder) / 'probekit', pad_mix=True)
            out, report = self.compile(folder, kit, FIXTURES / 'probesynth')
            self.assertTrue({'drum_sends', 'drum_locks'} <= set(report['services']))
            fallbacks = service_fallbacks(set(report['services']))
            shared_imports, provided = set(), {}
            for obj in sorted((out / 'sdk').glob('*.o')):
                defined, undefined = symbols(obj)
                service = native.UNIT_SERVICES.get(obj.stem)
                if service is None:
                    shared_imports |= undefined
                else:
                    provided.setdefault(service, set()).update(defined)
            for service in ('drum_sends', 'drum_locks'):
                with self.subTest(service=service):
                    needed = shared_imports & provided.get(service, set())
                    self.assertEqual(needed - set(fallbacks.get(service, {})), set())
            self.assertEqual(shared_imports & provided['drum_locks'],
                             {'sdk_step_locks_' + name for name in
                              ('size', 'write', 'begin', 'byte', 'loaded', 'held_value', 'held_set', 'frame')})


    def test_only_a_kit_declaring_sdk_1_18_links_the_played_pad_snapshot(self):
        # A real build of the 1.16 fixture kit, alone and beside the 1.18 one: the Python
        # path compiles and links sdk_drum_trigger only with the 1.18 kit, and the browser
        # path (builder/core.mjs linkKit, over the same objects) links it only when the
        # 1.18 kit is selected.
        import base64
        from tooling import browser_kit
        from tooling.machines import FIXED_NUMBERS, read_collection
        kits = [FIXTURES / 'probekit16', FIXTURES / 'probekit18']
        with tempfile.TemporaryDirectory(prefix='links-played-') as folder:
            out, report = self.compile(Path(folder) / 'old', kits[0])
            self.assertNotIn('played_pad', report['services'])
            self.assertFalse((out / 'sdk/drum_trigger.o').exists())
            self.assertNotIn('sdk_drum_trigger', symbols(out / 'module.elf')[0] | symbols(out / 'module.elf')[1])
            out, report = self.compile(Path(folder) / 'both', *kits)
            self.assertIn('played_pad', report['services'])
            self.assertIn('sdk_drum_trigger', symbols(out / 'sdk/drum_trigger.o')[0])
            profile, machines, definitions = native.configuration(read_collection(None, kits))
            owners, services = native.object_owners(machines, profile), native.object_services(machines, profile)
            self.assertEqual(services['sdk/drum_trigger.o'], 'played_pad')
            objects = [dict(name=name, owner=owner, service=services.get(name),
                            bytes=base64.b64encode((out / name).read_bytes()).decode('ascii'))
                       for name, owner in owners.items()]
            entries = [browser_kit.machine_entry(m, profile, {}, Path(folder), 'clang++') for m in machines]
            kit = dict(version='0.0.0', profile=profile, fixed=FIXED_NUMBERS, machines=entries,
                       definitions={key: value for key, value in definitions.items() if key in native.DEFINITIONS})
            script = """
import {linkKit, readObject} from %s;
let input = '';
for await (const chunk of process.stdin) input += chunk;
const {kit, objects, selections} = JSON.parse(input);
kit.objects = objects.map(({bytes, service, ...o}) =>
  ({...readObject(Buffer.from(bytes, 'base64')), ...o, ...(service ? {service} : {})}));
process.stdout.write(JSON.stringify(selections.map(selection => {
  const linked = linkKit(kit, selection);
  return {objects: linked.objects.map(o => o.name), trigger: linked.globals.has('sdk_drum_trigger'),
          services: [...linked.services]};
})));
""" % json.dumps((ROOT / 'builder/core.mjs').as_uri())
            selections = [['probekit16'], ['probekit18'], ['probekit16', 'probekit18']]
            result = subprocess.run(['node', '--input-type=module', '-e', script], capture_output=True, text=True,
                                    encoding='utf-8', cwd=ROOT, timeout=120,
                                    input=json.dumps(dict(kit=kit, objects=objects, selections=selections)))
            self.assertEqual(result.returncode, 0, result.stderr[-4000:])
            for selection, linked in zip(selections, json.loads(result.stdout)):
                with self.subTest(selection=selection):
                    expected = 'probekit18' in selection
                    self.assertEqual('sdk/drum_trigger.o' in linked['objects'], expected)
                    self.assertEqual(linked['trigger'], expected)
                    self.assertEqual('played_pad' in linked['services'], expected)
                    self.assertIn('drums', linked['services'])


if __name__ == '__main__':
    unittest.main()
