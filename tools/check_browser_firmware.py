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
from tooling import screen, startup_label
from tooling.adapters import catalog_defaults
from tooling.firmware import active_services, assemble, hook_instruction, patch_active
from tooling.native import configuration, package_collection


def check_startup_label(module, symbols, base):
    """The linked thunk is exactly the reviewed one: R2 replay, stock draw call, stock continuation."""
    entry, text = symbols[startup_label.SYMBOL], symbols['kit_startup_label_text']
    require(startup_label.thunk_ok(module, entry['value'] - base, entry['size'], text['value']),
            'Startup thunk changed')
    at = text['value'] - base
    require(text['size'] == len(startup_label.LABEL) and module[at:at + text['size']] == startup_label.LABEL,
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
    # 0x8c0c88bc measures the text using this stock width table, adding
    # one pixel per glyph. The existing draw call centers it at x=240.
    offset = mapped_offset(ldr(stock), 0x840131c4, 96)
    widths = stock[offset:offset + 96]
    label = startup_label.LABEL[:-1]
    require(all(32 <= c < 128 for c in label), 'Startup label outside stock font')
    width = sum(widths[c - 32] + 1 for c in label)
    left = 240 - width // 2
    require(0 < width and 100 <= left and left + width <= 400, 'Startup label exceeds its drawing area')
    # The browser links the native objects in native order, so selecting every
    # machine reproduces the native package exactly.
    everything = directory / 'reference' / str(2 ** len(kit['machines']) - 1)
    require(everything.with_suffix('.ldr').read_bytes() == (directory / 'source/candidate/os.ldr').read_bytes(),
            'Browser firmware for the full collection differs from the native package')
    for index, result in enumerate(json.loads((directory / 'reference/selections.json').read_text()), 1):
        prefix = directory / 'reference' / str(index)
        sections, symbols = elf32(prefix.with_suffix('.elf').read_bytes())
        module = prefix.with_suffix('.bin').read_bytes()
        require(sha(module) == result['module_sha256'], 'GNU module digest differs')
        low = kit['profile']['module_base']
        check_fallbacks(module, symbols, low)
        check_startup_label(module, symbols, low)
        require(symbols['__extensions_initialized_end']['value'] - low == result['sizes']['initialized'] and
                symbols['__extensions_module_end']['value'] - low == result['sizes']['allocated'] and
                sections.get('.bss', {}).get('size', 0) == result['sizes']['bss'], 'GNU memory allocation differs')
        for name, value in result['globals'].items():
            require(symbols[name]['value'] == value, 'GNU symbol differs: ' + name)
        profile = copy.deepcopy(kit['profile'])
        services = active_services(kit['profile'], (m['kind'] for m in kit['machines'] if m['id'] in result['selected']))
        for patch in profile['patches']:
            if not patch_active(patch, services):
                patch['after'] = patch['before']
            elif patch['kind'] == 'hook':
                patch['after'] = hook_instruction(patch, symbols[patch['symbol']]['value']).hex()
            elif patch['kind'] == 'startup':
                patch['after'] = startup_label.jump(symbols[patch['symbol']]['value']).hex()
        expected = assemble(stock, module, profile, kit['profile'], len(profile['patches']), services)
        require(expected == prefix.with_suffix('.ldr').read_bytes(), 'Python/browser firmware differs')
        selected = [m for m in kit['machines'] if m['id'] in result['selected']]
        catalog = json.loads((content / 'op1_factory.json').read_bytes())
        for machine in selected:
            key = 'fx_types' if machine['kind'] == 'effect' else 'engine_types'
            catalog[key].append(json.loads(machine['catalog'].strip().lstrip(',')))
        actual_catalog = (prefix / 'content/op1_factory.json').read_bytes()
        require(json.loads(actual_catalog) == catalog, 'Browser catalog differs')
        expected_files = {'os.ldr': expected, 'content/op1_factory.json': actual_catalog}
        for machine in selected:
            if machine['kind'] != 'effect':
                name = 'content/presets/' + machine['id'] + '/default.json'
                data = (prefix / name).read_bytes()
                preset = json.loads(data)
                definition = definitions[machine['id']]
                defaults = catalog_defaults(definition)
                entry = json.loads(machine['catalog'].strip().lstrip(','))
                require(preset['type'] == entry['type'] and preset['platform'] == 'OP-XY' and
                        preset['version'] == 4 and preset['engine']['params'] == defaults and
                        preset['regions'] == [] and preset['fx']['active'] is False and
                        preset['lfo']['active'] is False, 'Default preset differs from instrument definition')
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
    print('Independent GNU/Python verification passed for every selection (module, symbols, BSS, LDR, assets, ZIP).')


if __name__ == '__main__':
    main()
