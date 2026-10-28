"""Compare browser output to independent GNU ELF and Python container readers."""
import copy
import json
from pathlib import Path
import subprocess
import sys
import zipfile

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT))
from tooling.formats import elf32, ldr, mapped_offset, require, sha
from tooling import screen, startup_label, steps
from tooling.adapters import catalog_defaults
from tooling.firmware import active_services, assemble, hook_instruction, patch_active
from tooling.machines import assign_steps
from tooling.native import configuration, package_collection


def check_startup_label(module, symbols, base, expected):
    """The linked thunk is exactly the reviewed one: R2 replay, stock draw call, stock continuation."""
    entry, text = symbols[startup_label.SYMBOL], symbols['kit_startup_label_text']
    require(startup_label.thunk_ok(module, entry['value'] - base, entry['size'], text['value']),
            'Startup thunk changed')
    at = text['value'] - base
    require(text['size'] == len(expected) and module[at:at + text['size']] == expected,
            'Unexpected startup label')


# Unselected machines' callbacks resolve to kit_absent (startup.cpp.in): R0 = 0; RTS.
ABSENT = bytes.fromhex('0060 1000')


def check_fallbacks(module, symbols, base):
    """The linked absent-machine callback declines without side effects."""
    absent = symbols['kit_absent']
    at = absent['value'] - base
    require(absent['size'] == len(ABSENT) and module[at:at + len(ABSENT)] == ABSENT,
            'Absent-machine callback differs from its source')


def main():
    directory, stock_file, content = map(lambda p: Path(p).resolve(), sys.argv[1:])
    subprocess.run(['node', str(ROOT / 'tools/check_browser_firmware.mjs'), str(directory), str(stock_file), str(content)], check=True)
    kit = json.loads((directory / 'kit.json').read_text(encoding='utf-8'))
    # Definitions of the machines the kit was built from, wherever they live.
    definitions = {m.id: m.definition for m in configuration(package_collection(directory / 'source'))[1]}
    stock = stock_file.read_bytes()
    require(sha(stock) == kit['profile']['stock_sha256'], 'Unexpected stock input')
    # 0x8c0c7d04 measures the text using this stock width table, adding
    # one pixel per glyph. The existing draw call centers it at x=240.
    offset = mapped_offset(ldr(stock), 0x84015fb4, 96)
    widths = stock[offset:offset + 96]
    boot_label = startup_label.label(kit['version'])
    label = boot_label[:-1]
    require(all(32 <= c < 128 for c in label), 'Startup label outside stock font')
    width = sum(widths[c - 32] + 1 for c in label)
    left = 240 - width // 2
    require(0 < width and 100 <= left and left + width <= 400, 'Startup label exceeds its drawing area')
    # The browser links the native objects in native order, so selecting every
    # machine reproduces the native package exactly.
    everything = directory / 'reference' / str(2 ** len(kit['machines']) - 1)
    source_profiling = json.loads((directory / 'source/manifest.json').read_bytes()).get('profiling')
    require(type(source_profiling) is bool, 'Source package profiling must be a boolean')
    source_suffix = '.ldr' if source_profiling else '.profiling-off.ldr'
    require(everything.with_suffix(source_suffix).read_bytes() == (directory / 'source/candidate/os.ldr').read_bytes(),
            'Browser firmware for the full collection differs from the native package')
    for result in json.loads((directory / 'reference/selections.json').read_text()):
        prefix = directory / 'reference' / str(result['mask'])
        sections, symbols = elf32(prefix.with_suffix('.elf').read_bytes())
        module = prefix.with_suffix('.bin').read_bytes()
        require(sha(module) == result['module_sha256'], 'GNU module digest differs')
        low = kit['profile']['module_base']
        check_fallbacks(module, symbols, low)
        check_startup_label(module, symbols, low, boot_label)
        require(symbols['__extensions_initialized_end']['value'] - low == result['sizes']['initialized'] and
                symbols['__extensions_module_end']['value'] - low == result['sizes']['allocated'] and
                sections.get('.bss', {}).get('size', 0) == result['sizes']['bss'], 'GNU memory allocation differs')
        for name, value in result['globals'].items():
            require(symbols[name]['value'] == value, 'GNU symbol differs: ' + name)
        services = active_services(kit['profile'], (m['kind'] for m in kit['machines'] if m['id'] in result['selected']))
        # The selection's step machines, placed as tooling.machines.assign_steps places them.
        chosen = [m for m in kit['machines'] if m['id'] in result['selected'] and m['kind'] == 'step']
        replaced = assign_steps(steps.slots(kit['profile']), None, [(m['id'], m['identity']['step']) for m in chosen])
        owners = {slot: next(m for m in chosen if m['id'] == name) for name, slot in replaced.items()}
        services |= {steps.service(slot) for slot in owners}
        modes = {}
        for profiling in (True, False):
            profile = copy.deepcopy(kit['profile'])
            for patch in profile['patches']:
                if not patch_active(patch, services, profiling):
                    patch['after'] = patch['before']
                elif patch['kind'] == 'hook':
                    patch['after'] = hook_instruction(patch, symbols[patch['symbol']]['value']).hex()
                elif patch['kind'] == 'startup':
                    patch['after'] = startup_label.jump(symbols[patch['symbol']]['value']).hex()
                elif patch['kind'] == 'step':
                    patch['after'] = steps.after(patch, owners[patch['slot']]['prefix'],
                                                 lambda name: symbols[name]['value']).hex()
            expected = assemble(stock, module, profile, kit['profile'], len(profile['patches']), services, profiling)
            suffix = '.ldr' if profiling else '.profiling-off.ldr'
            require(expected == prefix.with_suffix(suffix).read_bytes(), 'Python/browser firmware differs: ' + suffix)
            modes[profiling] = expected
        # The modes differ only inside the reviewed profiling hook windows.
        restored = bytearray(modes[False])
        for patch in kit['profile']['patches']:
            if patch.get('profiling') is False:
                at, count = int(patch['file_offset'], 16), patch['length']
                require(modes[True][at:at + count] == bytes.fromhex(patch['before']),
                        'SDK default changed profiling instructions')
                restored[at:at + count] = modes[True][at:at + count]
        require(bytes(restored) == modes[True], 'Profiling mode changed unrelated bytes')
        expected = modes[True]
        selected = [m for m in kit['machines'] if m['id'] in result['selected']]
        catalog = json.loads((content / 'op1_factory.json').read_bytes())
        for machine in selected:
            if machine['kind'] == 'step':
                continue  # a step component replaces a stock component: no menu entry
            key = 'fx_types' if machine['kind'] == 'effect' else 'engine_types'
            catalog[key].append(json.loads(machine['catalog'].strip().lstrip(',')))
        actual_catalog = (prefix / 'content/op1_factory.json').read_bytes()
        require(json.loads(actual_catalog) == catalog, 'Browser catalog differs')
        expected_files = {'os.ldr': expected, 'content/op1_factory.json': actual_catalog}
        for machine in selected:
            if machine['kind'] in ('synth', 'sample'):
                name = 'content/presets/' + machine['id'] + '/default.json'
                data = (prefix / name).read_bytes()
                preset = json.loads(data)
                own = machine.get('presets', {})
                if 'default' in own:
                    require(preset == own['default'], 'Machine preset differs: default')
                else:
                    definition = definitions[machine['id']]
                    defaults = catalog_defaults(definition)
                    entry = json.loads(machine['catalog'].strip().lstrip(','))
                    require(preset['type'] == entry['type'] and preset['platform'] == 'OP-XY' and
                            preset['version'] == 4 and preset['engine']['params'] == defaults and
                            preset['regions'] == [] and preset['fx']['active'] is False and
                            preset['lfo']['active'] is False, 'Default preset differs from instrument definition')
                expected_files[name] = data
                for preset_name, document in own.items():
                    name = 'content/presets/' + machine['id'] + '/' + preset_name + '.json'
                    data = (prefix / name).read_bytes()
                    require(json.loads(data) == document, 'Machine preset differs: ' + preset_name)
                    expected_files[name] = data
            if machine.get('artwork'):
                name = 'content/display/' + machine['asset']
                art = screen.compose(machine['artwork'].encode())
                require((prefix / name).read_bytes() == art, 'Browser artwork differs')
                expected_files[name] = art
        with zipfile.ZipFile(prefix.with_suffix('.zip')) as archive:
            require(set(archive.namelist()) == set(expected_files) and archive.testzip() is None, 'Invalid ZIP file')
            for name, data in expected_files.items():
                require(archive.read(name) == data, 'ZIP payload differs')
    print('Independent GNU/Python verification passed for the checked selections (module, symbols, BSS, LDR, assets, ZIP).')


if __name__ == '__main__':
    main()
