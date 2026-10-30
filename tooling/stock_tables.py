"""Relocated engine-record, screen-slot and effect-record tables for exact stock OP-XY 1.2.1.

The static audit (docs/abi-1.2.1.md, "Relocated tables") found every direct
reader of the three tables: each loads its table's base as a 32-bit immediate,
and a few compare an ID or loop index with the stock count, or clear the stock
table length. Table edits rewrite only those immediates, except the
compositor's screen bound, whose loop advance is hooked instead
(screen_order.hpp), and the engine validity bound, which imm7 cannot raise
past 63 and is hooked too (registration_stock.cpp.in). Each table is declared once below; the edits, layout and
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
# 63 is the largest bound imm7 encodes, which caps the effect table. The engine
# table's imm7 bound, in the validity check every engine lookup calls, is hooked
# instead (ENGINE_BOUND), so engines and pages run to 256: the effect records,
# then the engine records, then the screen slots fill the last 16 KiB.
ENGINES = Table('engine records', 0x080394fc, 33, 0x8d4ccf50, 256, RECORD_BYTES,
                (0x8d505292, 0x8d50533e, 0x8d505366, 0x8d50652a, 0x8d5067e2, 0x8d506c74, 0x8d506e9e),
                ((0x8d5067dc, 'imm16', RECORD_BYTES),))
SCREENS = Table('screen slots', 0x84eb3228, 81, 0x8d4cff50, 256, 4,
                (0x8d520792, 0x8d5207c8),
                ((0x8d5208be, 'imm16', 1), (0x8d520d3e, 'imm16', 1)))
# Bounds: the validity check, the factory dispatch and the send-track (14/15)
# type setter. The per-track filter setter keeps stock's bound, so filter
# types stay below the effect sentinel.
EFFECTS = Table('effect records', 0x0803916c, 19, 0x8d4cc380, 63, RECORD_BYTES,
                (0x8d505276, 0x8d5052b6, 0x8d505314, 0x8d5066ba, 0x8d506cb8, 0x8d506f28),
                ((0x8d505304, 'imm7', 1), (0x8d506f20, 'imm7', 1), (0x8d50e768, 'imm7', 1)))
TABLES = (ENGINES, SCREENS, EFFECTS)  # the profile's order

STOCK_ENGINE_RECORDS, STOCK_ENGINE_COUNT = ENGINES.stock, ENGINES.stock_count
STOCK_SCREEN_SLOTS, STOCK_SCREEN_COUNT = SCREENS.stock, SCREENS.stock_count
STOCK_EFFECT_RECORDS, STOCK_EFFECT_COUNT = EFFECTS.stock, EFFECTS.stock_count
ENGINE_RECORDS, ENGINE_COUNT = ENGINES.address, ENGINES.count
SCREEN_SLOTS, SCREEN_COUNT = SCREENS.address, SCREENS.count
EFFECT_RECORDS, EFFECT_COUNT = EFFECTS.address, EFFECTS.count
TABLES_START = min(t.address for t in TABLES)
RESERVATION_END = 0x8d4d0380
# Stock "none" values: the resolver/preset/GUI sentinels and the dispatcher's
# no-screen value. Their record and slot are never registered.
ENGINE_SENTINEL, PAGE_SENTINEL, EFFECT_SENTINEL = STOCK_ENGINE_COUNT, STOCK_SCREEN_COUNT, STOCK_EFFECT_COUNT
# Effect IDs stock registers: send effects on page 21, filter types on page 3.
STOCK_EFFECTS = (0, 5, 12, 13, 14, 15, 9, 10, 16, 17, 18)
# A send track reports its effect ID as its engine, and stock tests it as one:
# parameter stepping (0x8d5ad254) special-cases engines 2-4, and other paths
# engines 6 and 29-31. Effects avoid every stock engine ID and the "none" 33.
ENGINE_LOOKALIKES = (2, 3, 4, 6, 7, 18, 19, 20, 22, 29, 30, 31, 32, ENGINE_SENTINEL)

# (site, kind, stock value, relocated value)
EDITS = tuple(edit for table in TABLES for edit in table.edits())
SIZES = dict(imm32=8, imm16=4, imm7=2)
# The compositor's loop advance: R7 += 1; R0 = 81 (imm16); CC = R7 < R0.
COMPOSITOR_ADVANCE, COMPOSITOR_BOUND = 0x8d5214c2, 0x8d5214c4
# The engine validity check (0x8d50534e), between its LINK and UNLINK: R1 = 33;
# CC = R0 < R1. A call there sets R1 to the relocated count and CC alike; the
# function's UNLINK restores the RETS the call replaces.
ENGINE_BOUND = 0x8d505356


# Template values for the stock side of registration and the page renderers.
REGISTRATION = dict(ENGINE_RECORDS='%#x' % ENGINE_RECORDS, ENGINE_LIMIT=str(ENGINE_COUNT),
                    ENGINE_SENTINEL=str(ENGINE_SENTINEL), EFFECT_RECORDS='%#x' % EFFECT_RECORDS,
                    EFFECT_LIMIT=str(EFFECT_COUNT), EFFECT_SENTINEL=str(EFFECT_SENTINEL),
                    SCREEN_SLOTS='%#x' % SCREEN_SLOTS, PAGE_LIMIT=str(SCREEN_COUNT))


def free_effects():
    """Effect IDs the relocated table holds that stock neither registers, uses as "none" nor reads as an engine."""
    return [n for n in range(EFFECT_COUNT)
            if n not in STOCK_EFFECTS and n != EFFECT_SENTINEL and n not in ENGINE_LOOKALIKES]


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
    check_engine_bound([p for p in profile['patches'] if p['kind'] == 'hook'])


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


def check_engine_bound(hooks):
    """The engine validity check's stock-count bound is replaced by its hook."""
    bound = [p for p in hooks if int(p['address'], 16) == ENGINE_BOUND]
    # Never gated: the relocated engine table is always in place, so its bound must be too.
    require(len(bound) == 1 and bound[0]['symbol'] == 'sdk_engine_bound' and bound[0]['length'] == 4 and
            bound[0]['call'] is True and 'service' not in bound[0] and 'profiling' not in bound[0],
            'The engine validity bound must be hooked in every build')
    before = bytes.fromhex(bound[0]['before'])
    require(decode('imm7', before[:2])[1] == STOCK_ENGINE_COUNT and before[2:] == bytes.fromhex('8808'),
            'Engine bound hook preimage differs from the audited R1 = 33; CC = R0 < R1')
