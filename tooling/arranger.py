"""Stock continuations for the authenticated arranger entry hooks."""
from pathlib import Path
import re
import struct

from .formats import require


def trampoline_bytes(patch):
    original = bytes.fromhex(patch['before'])
    continuation = int(patch['address'], 16) + len(original)
    return original + struct.pack('<4H', 0xdc00, continuation >> 16, continuation & 65535, 0)


def general_row_bytes(address, symbols):
    from .firmware import branch_instruction
    code = bytes.fromhex('00e80000')
    for offset, target in ((4, 'sdk_hook_save'), (8, 'sdk_arranger_general_rows'), (12, 'sdk_hook_restore')):
        code += branch_instruction(address + offset, symbols[target]['value'], True)
    return code + bytes.fromhex('01e80000063000de088c286d000000dc098cec720000')


def checked_ranges(profile, module, symbols, text):
    ranges = []
    for patch in profile['patches']:
        if patch.get('general_rows'):
            symbol = symbols[patch['symbol']]
            expected = general_row_bytes(symbol['value'], symbols)
            at = symbol['value'] - profile['module_base']
            require(symbol['type'] == 2 and symbol['size'] == len(expected) and
                    0 <= at and at + len(expected) <= len(module) and
                    text['address'] <= symbol['value'] and
                    symbol['value'] + len(expected) <= text['address'] + text['size'] and
                    module[at:at + len(expected)] == expected, 'Changed native General continuation')
            # Only the authenticated CALL.A/JUMP.A tail is invisible to old objdump.
            ranges.append((symbol['value'] + 22, symbol['value'] + len(expected)))
            continue
        if not patch.get('trampoline'):
            continue
        symbol = symbols[patch['trampoline']]
        at = symbol['value'] - profile['module_base']
        expected = trampoline_bytes(patch)
        require(symbol['type'] == 2 and 0 <= at and at + len(expected) <= len(module) and
                text['address'] <= symbol['value'] and
                symbol['value'] + symbol['size'] <= text['address'] + text['size'],
                'Stock continuation outside module text')
        require(symbol['size'] == len(expected) and module[at:at + len(expected)] == expected,
                'Changed stock continuation')
        ranges.append((symbol['value'], symbol['value'] + len(expected)))
    return ranges


def generate(directory, profile):
    lines = ['.include "sdk/platform/opxy-1.2.1/hook.inc"', '.section .text', '.align 2']
    for patch in profile['patches']:
        if patch.get('general_rows'):
            require(patch['symbol'] == 'sdk_arranger_general_rows_hook' and
                    patch['address'] == '0x8c0972e2' and patch['length'] == 10 and
                    patch['before'] == 'ffe347820630ffe3207d' and
                    patch['call'] is False and patch['encoding'] == 'absolute_jump',
                    'Invalid native General row hook')
            name = patch['symbol']
            lines += [f'.global {name}', f'.type {name}, STT_FUNC', f'{name}:',
                      'HOOK_SAVE', 'CALL sdk_arranger_general_rows;', 'HOOK_RESTORE', 'R0 = R6;']
            # Replay the displaced row destructor, then resume after all ten bytes.
            tail = struct.pack('<4H', 0xde00, 0x8c08, 0x6d28, 0) + struct.pack('<4H', 0xdc00, 0x8c09, 0x72ec, 0)
            lines += ['.byte ' + ', '.join(hex(byte) for byte in tail), f'.size {name}, .-{name}']
            continue
        edit = patch.get('edit_index')
        if edit:
            name = patch['symbol']
            require(re.fullmatch(r'sdk_arranger_edit_[0-9a-f]+', name) and
                    edit['pointer'] in ('P0', 'P1', 'P2') and edit['result'] in ('R5', 'R6', 'R7') and
                    edit['kind'] in ('load_compare', 'parallel') and
                    (edit['compare'] in ('R0', 'R2', 'R3') or
                     (edit['kind'] == 'parallel' and edit['compare'] is None)), 'Invalid edit-index thunk')
            require(patch['kind'] == 'hook' and patch['call'] is True and
                    patch['length'] == (4 if edit['kind'] == 'load_compare' else 8),
                    'Invalid edit-index footprint')
            lines += [f'.global {name}', f'.type {name}, STT_FUNC', f'{name}:']
            if edit['kind'] == 'parallel':
                lines.append('.byte ' + ', '.join(hex(byte) for byte in bytes.fromhex(patch['before'])))
            lines += ['HOOK_SAVE', f'R0 = {edit["pointer"]};', 'CALL sdk_arranger_edit_index;',
                      f'{edit["result"]} = R0;', 'HOOK_RESTORE']
            if edit['kind'] == 'load_compare':
                lines.append(f'CC = {edit["result"]} < {edit["compare"]} (IU);')
            lines += ['RTS;', f'.size {name}, .-{name}']
            continue
        name = patch.get('trampoline')
        if not name:
            continue
        require(re.fullmatch(r'(sdk_arranger_stock_|stock_arranger_)[a-z0-9_]+', name),
                'Invalid stock continuation symbol')
        require(patch['kind'] == 'hook' and patch.get('encoding') == 'absolute_jump',
                'A stock continuation requires an entry jump')
        code = trampoline_bytes(patch)
        lines += [f'.global {name}', f'.type {name}, STT_FUNC', f'{name}:',
                  '.byte ' + ', '.join(hex(byte) for byte in code), f'.size {name}, .-{name}']
    destination = Path(directory) / 'arranger_stock.s'
    destination.write_text('\n'.join(lines) + '\n', encoding='utf-8')
    return destination
