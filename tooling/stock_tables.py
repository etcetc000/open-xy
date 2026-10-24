"""Relocated engine-record, screen-slot and effect-record tables for exact stock OP-XY 1.2.0.

The static audit (docs/abi-1.2.0.md, "Relocated tables") found every direct
reader of the three tables: each loads its table's base as a 32-bit immediate,
and a few compare an ID or loop index with the stock count, or clear the stock
table length. Table edits rewrite only those immediates, except the
compositor's screen bound, whose loop advance is hooked instead
(screen_order.hpp). Each table is declared once below; the edits, layout and
template values derive from the declarations, and this module decodes the
profile's bytes independently of them.
"""
import struct
from typing import NamedTuple

from .formats import require


class Table(NamedTuple):
    """A stock table moved into the reservation: where it was and is, its
    entry count and size, the sites loading its base, and the sites whose
    immediate is the stock count times a unit (1 for a bound, the entry size
    for a clear length)."""
    name: str
    stock: int
    stock_count: int
    address: int
    count: int
    entry_bytes: int
    bases: tuple
    bounds: tuple

    def edits(self):
        return (*[(site, 'imm32', self.stock, self.address) for site in self.bases],
                *[(site, kind, self.stock_count * unit, self.count * unit) for site, kind, unit in self.bounds])

    @property
    def end(self):
        return self.address + self.count * self.entry_bytes


RECORD_BYTES = 48
# 63 is the largest bound imm7 encodes, which caps the engine and effect tables.
ENGINES = Table('engine records', 0x846ea2e8, 33, 0x8d4cff60, 63, RECORD_BYTES,
                (0x8d504c42, 0x8d504cee, 0x8d504d16, 0x8d505eda, 0x8d506192, 0x8d506624, 0x8d50684e),
                ((0x8d504d06, 'imm7', 1), (0x8d50618c, 'imm16', RECORD_BYTES)))
SCREENS = Table('screen slots', 0x84ebbfc0, 81, 0x8d4cfd60, 128, 4,
                (0x8d51fbca, 0x8d51fc00),
                ((0x8d51fcf6, 'imm16', 1), (0x8d520176, 'imm16', 1)))
# Bounds: the validity check, the factory dispatch and the send-track (14/15)
# type setter. The per-track filter setter keeps stock's bound, so filter
# types stay below the effect sentinel.
EFFECTS = Table('effect records', 0x846e9f58, 19, 0x8d4ced60, 63, RECORD_BYTES,
                (0x8d504c26, 0x8d504c66, 0x8d504cc4, 0x8d50606a, 0x8d506668, 0x8d5068d8),
                ((0x8d504cb4, 'imm7', 1), (0x8d5068d0, 'imm7', 1), (0x8d50e0ec, 'imm7', 1)))
TABLES = (ENGINES, SCREENS, EFFECTS)  # the profile's order

STOCK_ENGINE_RECORDS, STOCK_ENGINE_COUNT = ENGINES.stock, ENGINES.stock_count
STOCK_SCREEN_SLOTS, STOCK_SCREEN_COUNT = SCREENS.stock, SCREENS.stock_count
STOCK_EFFECT_RECORDS, STOCK_EFFECT_COUNT = EFFECTS.stock, EFFECTS.stock_count
ENGINE_RECORDS, ENGINE_COUNT = ENGINES.address, ENGINES.count
SCREEN_SLOTS, SCREEN_COUNT = SCREENS.address, SCREENS.count
EFFECT_RECORDS, EFFECT_COUNT = EFFECTS.address, EFFECTS.count
TABLES_START = min(t.address for t in TABLES)
RESERVATION_END = 0x8d4d0d60
# Stock "none" values: the resolver/preset/GUI sentinels and the dispatcher's
# no-screen value. Their record and slot are never registered.
ENGINE_SENTINEL, PAGE_SENTINEL, EFFECT_SENTINEL = STOCK_ENGINE_COUNT, STOCK_SCREEN_COUNT, STOCK_EFFECT_COUNT
# Effect IDs stock registers: send effects on page 21, filter types on page 3.
STOCK_EFFECTS = (0, 5, 12, 13, 14, 15, 9, 10, 16, 17, 18)

# (site, kind, stock value, relocated value)
EDITS = tuple(edit for table in TABLES for edit in table.edits())
SIZES = dict(imm32=8, imm16=4, imm7=2)
# The compositor's loop advance: R7 += 1; R0 = 81 (imm16); CC = R7 < R0.
COMPOSITOR_ADVANCE, COMPOSITOR_BOUND = 0x8d5208fa, 0x8d5208fc


# Template values for the stock side of registration and the page renderers.
REGISTRATION = dict(ENGINE_RECORDS='%#x' % ENGINE_RECORDS, ENGINE_LIMIT=str(ENGINE_COUNT),
                    ENGINE_SENTINEL=str(ENGINE_SENTINEL), EFFECT_RECORDS='%#x' % EFFECT_RECORDS,
                    EFFECT_LIMIT=str(EFFECT_COUNT), EFFECT_SENTINEL=str(EFFECT_SENTINEL),
                    SCREEN_SLOTS='%#x' % SCREEN_SLOTS, PAGE_LIMIT=str(SCREEN_COUNT))


def free_effects():
    """Effect IDs the relocated table holds that stock neither registers nor uses as "none"."""
    return [n for n in range(EFFECT_COUNT) if n not in STOCK_EFFECTS and n != EFFECT_SENTINEL]


def check_layout():
    """The tables are disjoint and lie inside the reservation's table bytes."""
    spans = sorted((t.address, t.end) for t in TABLES)
    require(all(a[1] <= b[0] for a, b in zip(spans, spans[1:])) and spans[-1][1] <= RESERVATION_END,
            'Relocated tables overlap or exceed the reservation')


def check_profile(profile):
    """The profile's table geometry, identity bounds and table edits agree with this independent audit."""
    check_layout()
    ids = profile['identities']
    require(ids['engines'] == ENGINE_COUNT and ids['engine_none'] == ENGINE_SENTINEL and
            ids['pages'] == SCREEN_COUNT and ids['page_none'] == PAGE_SENTINEL and
            ids['effects'] == free_effects(), 'Identity bounds differ from the tables')
    end = profile['module_base'] + profile['reserved_bytes']
    require(end == RESERVATION_END and end - profile['table_bytes'] == TABLES_START, 'Table bytes differ from the tables')
    check_edits([p for p in profile['patches'] if p['kind'] == 'table'])
    check_compositor([p for p in profile['patches'] if p['kind'] == 'hook'])


def decode(kind, raw):
    """Return (opcode bits, immediate) for one of the three rewritten forms."""
    require(len(raw) == SIZES[kind], 'Unexpected instruction size')
    if kind == 'imm32':  # Blackfin+ Dreg/Preg = imm32
        opcode, high, low, zero = struct.unpack('<4H', raw)
        require(opcode & 0xffc0 == 0xda00 and zero == 0, 'Expected 32-bit immediate load')
        return opcode, high << 16 | low
    if kind == 'imm16':  # Dreg/Preg = imm16 (X)
        opcode, value = struct.unpack('<2H', raw)
        require(opcode & 0xffc0 == 0xe100, 'Expected 16-bit immediate load')
        return opcode, value
    (word,) = struct.unpack('<H', raw)  # Dreg = imm7 (X)
    require(word & 0xfc00 == 0x6000, 'Expected 7-bit immediate load')
    value = word >> 3 & 0x7f
    return word & ~(0x7f << 3), value - 0x80 if value & 0x40 else value


def check_edits(patches):
    """Exactly the audited table edits, in order, each changing only its immediate."""
    require(len(patches) == len(EDITS), 'Unexpected table edit count')
    for patch, (site, kind, stock, relocated) in zip(patches, EDITS):
        require(int(patch['address'], 16) == site and patch['length'] == SIZES[kind], 'Unexpected table edit site')
        before, after = bytes.fromhex(patch['before']), bytes.fromhex(patch['after'])
        opcode, value = decode(kind, before)
        require(value == stock, 'Table edit preimage differs from audited stock value')
        require(decode(kind, after) == (opcode, relocated), 'Table edit must change only the immediate')


def check_compositor(hooks):
    """The compositor's stock-count bound is covered by its traversal hook."""
    advance = [p for p in hooks if int(p['address'], 16) == COMPOSITOR_ADVANCE]
    require(len(advance) == 1 and advance[0]['symbol'] == 'sdk_screen_advance' and advance[0]['length'] == 8,
            'The compositor loop advance must be hooked')
    at = COMPOSITOR_BOUND - COMPOSITOR_ADVANCE
    bound = bytes.fromhex(advance[0]['before'])[at:at + SIZES['imm16']]
    require(decode('imm16', bound)[1] == STOCK_SCREEN_COUNT, 'Compositor hook preimage differs from the audited bound')
