"""Where a synth's voices live: module memory, or an arena reserved from the stock heap at boot.

A synth declares only its voice size, alignment and capacity. From SDK 1.19, a
synth whose voice pool is larger than STATIC_VOICE_LIMIT keeps its voices in one
arena the SDK reserves from the stock heap the first time the synth registers on
the audio role at boot, instead of in the shared module memory. The arena is held
until power-off. Their bookkeeping (16 bytes a voice) and the 256 handles stay in
module memory. Every other synth, and every synth that declares an older SDK,
keeps its voices in module memory as before. Both places are DDR cached alike
(abi-1.2.1.md, "Synth voices").
"""
from .definitions import ROOT
from .formats import require

# The first SDK version that places a large voice pool on the heap. A renumber
# changes this line (and SDK_VERSION in machines.py).
VOICE_HEAP_SDK = (1, 19)
# Voice pools up to this many bytes stay in module memory. Every published synth
# fits with room to spare: the largest pool in the catalog is 74,880 bytes.
STATIC_VOICE_LIMIT = 128 * 1024
# Stock sounds at most 27 notes; the adapter keeps one spare voice (voice.cpp.in).
STOCK_SLOTS = 27
# The stock heap a collection's heap voices may hold, all together, for the whole
# session. Stock's free heap headroom has not been measured on a device, so this
# stays well below the 2 MiB the transient sample analysis budgets may use.
VOICE_HEAP_LIMIT = 1024 * 1024
# The arena's code in a heap synth's adapter (voice.cpp.in's VOICE_ARENA).
ARENA_TEMPLATE = ROOT / 'sdk/platform/opxy-1.2.1/voice_heap_arena.cpp.in'


def pool_bytes(resources):
    """Bytes of a synth's voices: voiceBytes for each of min(voiceCapacity, 27) + 1 voices."""
    return resources['voiceBytes'] * (min(resources['voiceCapacity'], STOCK_SLOTS) + 1)


def on_heap(m):
    """Whether a synth's voices go to a heap arena: its pool exceeds the static limit and it
    declares SDK 1.19 or later."""
    return (m.kind == 'synth' and pool_bytes(m.definition['resources']) > STATIC_VOICE_LIMIT and
            m.sdk >= VOICE_HEAP_SDK)


def arena_bytes(m):
    """Bytes a heap synth reserves: its pool, plus room to align the first voice."""
    resources = m.definition['resources']
    return pool_bytes(resources) + resources['voiceAlignment'] - 1


def check_budget(machines):
    """A collection's heap voice arenas together stay within VOICE_HEAP_LIMIT."""
    heap = [m for m in machines if on_heap(m)]
    voices = sum(arena_bytes(m) for m in heap)
    require(voices <= VOICE_HEAP_LIMIT,
            'Heap voices (%s: %d bytes) exceed the %d bytes of stock heap a collection\'s synths may hold; '
            'select fewer of them, or lower a voiceCapacity or voiceBytes'
            % (', '.join(m.id for m in heap), voices, VOICE_HEAP_LIMIT))


def placement(m):
    """voice.cpp.in's storage placeholders: the static text, or a heap arena's."""
    if not on_heap(m):
        return dict(SLOT_VOICE='Voice voice; ', VOICE_ARENA='', VOICE_START_GUARD='',
                    VOICE_OF_SLOT='slot.voice', VOICE_OF_HELD='slot->voice')
    arena = ARENA_TEMPLATE.read_text(encoding='utf-8')
    for key, value in (('PREFIX', m.prefix), ('ARENA_BYTES', arena_bytes(m))):
        require(('@%s@' % key) in arena, 'Heap voice template placeholder missing')
        arena = arena.replace('@%s@' % key, str(value))
    return dict(SLOT_VOICE='', VOICE_OF_SLOT='voice_of(slot)', VOICE_OF_HELD='voice_of(*slot)',
                VOICE_START_GUARD='    if (!voice_arena.voices) return 0;  // no arena: the note stays silent\n',
                VOICE_ARENA=arena)
