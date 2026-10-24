"""Curated, ordered firmware extensions. No dynamic loading or arbitrary hooks."""
import re
from .contributors import validate_source_boundary
from .formats import require
from .pipeline import ROOT, document

PROFILE = 'profiles/extensions.json'


def collection():
    data = document(ROOT / PROFILE)
    require(set(data) == {'schema', 'extensions'} and data['schema'] == 1,
            'Invalid extension collection schema')
    entries = data['extensions']
    require(isinstance(entries, list) and len(entries) <= 16, 'At most 16 extensions')
    ids, shortcut = set(), False
    for entry in entries:
        require(isinstance(entry, dict) and set(entry) == {'id', 'record_double_tap', 'edit_notes'},
                'Invalid extension declaration')
        name = entry['id']
        require(isinstance(name, str) and re.fullmatch(r'[a-z][a-z0-9_]{0,31}', name), 'Invalid extension ID')
        require(name not in ids, 'Duplicate extension ID: ' + name)
        ids.add(name)
        require(type(entry['record_double_tap']) is bool and type(entry['edit_notes']) is bool,
                'Extension claims must be booleans')
        require(not entry['record_double_tap'] or not shortcut, 'Record double-tap has multiple owners')
        shortcut |= entry['record_double_tap']
        require(not entry['edit_notes'] or entry['record_double_tap'],
                'Note editing requires the Record double-tap claim')
        root = ROOT / 'extensions' / name
        require((root / 'extension.cpp').is_file(), 'Missing extension source: ' + name)
        for path in root.rglob('*'):
            if path.is_file() and path.suffix in ('.cpp', '.cc', '.h', '.hpp', '.inc'):
                validate_source_boundary(path.read_text(encoding='utf-8'), path.relative_to(ROOT).as_posix())
    return entries


def generate_collection(folder):
    entries = collection()
    lines = ['#include "extension_runtime.hpp"']
    for entry in entries:
        lines.append('namespace opxy { namespace extensions { namespace ' + entry['id'] +
                     ' { extern const Hooks hooks; }}}')
    lines += ['namespace opxy { namespace platform {', 'namespace {',
              'const ExtensionEntry collection_entries[] = {']
    for entry in entries:
        lines.append('    {"%s", %s, %s, &extensions::%s::hooks},' % (
            entry['id'], str(entry['record_double_tap']).lower(), str(entry['edit_notes']).lower(), entry['id']))
    # One unused sentinel also makes an empty collection standard C++11.
    lines += ['    {nullptr, false, false, nullptr}', '};', '}',
              'const ExtensionEntry* extension_collection(u32& count) {',
              f'    count = {len(entries)};', '    return collection_entries;', '}', '}}', '']
    target = folder / 'extension_collection.cpp'
    target.write_text('\n'.join(lines), encoding='utf-8')
    return target
