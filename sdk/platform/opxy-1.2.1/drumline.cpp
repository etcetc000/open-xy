// Stock Drumline's second page (drumline.hpp), linked only with a drumline_page
// extension. The GUI functions run from the shared hooks through
// drumline_service(); the render wrapper and the page's texts from this unit's
// own hooks (drumline_hooks.s). The extension's page and processor come from
// sdk_drumline_page_extension, which the extension's own object defines.
#include "drumline.hpp"
#include "stock.hpp"
#include "lock_handles.hpp"

using opxy::i32;
using opxy::u32;
using opxy::u8;
using namespace opxy::platform;

extern "C" {
u32 sdk_dbox_render(unsigned char* self, i32* out, const i32* region, const i32* parameters) __asm__("sdk_dbox_render");
void sdk_drumline_page(unsigned char* page) __asm__("sdk_drumline_page");
const void* sdk_drumline_service() __asm__("sdk_drumline_service");
}

namespace {
const DrumlinePage& page_of() { return *sdk_drumline_page_extension(); }

u32 track_object(u32 track) { return stock<u32(u32)>(0x8d587708)(track); }
// A track's pad record in slot 0..23 (8d58926c finds slot `key` of a Drumline track).
unsigned char* pad_slot(u32 track, u32 slot) {
    return slot < kDrumlinePads ? at<unsigned char>(track_object(track) + kDrumlineRecords + slot * kDrumlineRecordBytes)
                                : nullptr;
}
bool is_drumline(u32 t) { return t < kTracks && track_engine(t) == kDrumlineEngine; }
// The key stock's editor edits and its dbox page shows (stock.hpp): the key it
// holds for editing, else the track's last played key. Page 2 shows and locks
// the same, as stock's page 1 does.
u32 edit_key(u32 track) {
    const u32 held = drum_held_key();
    return held < 128 ? held : drum_played_key(track);
}
// A pad record's choices.
void pad_choices(const unsigned char* record, opxy::drumline::Choices& choices) {
    const DrumlinePage& page = page_of();
    drumline_unpack(page, record ? drumline_codes_of(page, *reinterpret_cast<const u32*>(record)) : 0, choices);
}

// Each track's page: false the first (stock's), true the second.
bool second[kTracks];

// A turn's detents, bounded as the per-key editor bounds them (sdk_drum_edit).
i32 detents(const u32* event) {
    i32 delta = static_cast<i32>(event[1]);
    return delta > 127 ? 127 : delta < -127 ? -127 : delta;
}
// A choice moved `delta` detents within its parameter's choices.
u32 moved(const DrumlinePageParameter& p, u32 choice, i32 delta) {
    return static_cast<u32>(drum_lock_clamp(static_cast<i32>(choice) + delta, 0, static_cast<i32>(p.count) - 1));
}

// 8c0b147a on page 2, no step held: an unshifted encoder edits its parameter of
// the key stock passes (the key it edits, edit_key), one choice a detent; stock's
// republication as its editor does it. An encoder past the page's parameters does
// nothing. Shift's encoders (4..7) are stock's TYPE, RANDOM, PAN, VOLUME on
// either page.
bool edit(u32 track, u32 key, u32 control, const u32* event) {
    if (track >= kTracks || control >= opxy::page_controls || !second[track]) return false;
    const DrumlinePage& page = page_of();
    unsigned char* record = static_cast<unsigned char*>(pad_region(track, key));
    if (!record || !event || control >= page.count) return true;
    u32* word = reinterpret_cast<u32*>(record);
    opxy::drumline::Choices choices;
    drumline_unpack(page, drumline_codes_of(page, *word), choices);
    const u32 next = moved(page.parameters[control], choices.choice[control], detents(event));
    if (next == choices.choice[control]) return true;
    choices.choice[control] = static_cast<u8>(next);
    *word = drumline_word(page, drumline_pack(page, choices));
    stock<void()>(0x8d585ee8)();
    stock<void(u32)>(0x8d4d5f18)(key);
    return true;
}

void press(u32 track, u32 encoder) {
    if (track < kTracks && encoder < opxy::page_controls) second[track] = !second[track];
}

// The held step's choices of the key stock edits: the pad's, each locked one in its place.
void held_choices(u32 track, const unsigned char* record, opxy::drumline::Choices& choices) {
    const DrumlinePage& page = page_of();
    pad_choices(record, choices);
    const u32 key = edit_key(track);
    for (u32 i = 0; i < page.count; ++i) {
        i32 value = 0;
        u32 choice;
        if (step_locks::held_value(track, key, drumline_pool_field(i), value) &&
            drumline_lock_choice(page.parameters[i], value, choice))
            choices.choice[i] = static_cast<u8>(choice);
    }
}

// 8c0b15aa with a step held, an unshifted encoder on page 2: the step's lock of
// that parameter for the key stock edits, a choice a detent from what the step
// plays, in the handles' pool. Turned back to the key's own choice it is
// unlocked. Page 1 is stock's.
bool lock(u32 track, u32 position, const u32* event) {
    if (track >= kTracks || position >= opxy::page_controls || !second[track]) return false;
    const DrumlinePage& page = page_of();
    if (!event || position >= page.count) return true;
    const u32 key = edit_key(track), field = drumline_pool_field(position);
    const DrumlinePageParameter& p = page.parameters[position];
    opxy::drumline::Choices own;
    pad_choices(static_cast<const unsigned char*>(pad_region(track, key)), own);
    i32 value = 0;
    u32 now = own.choice[position];
    const bool locked = step_locks::held_value(track, key, field, value) && drumline_lock_choice(p, value, now);
    const u32 next = moved(p, now, detents(event));
    if (next == now && locked) return true;
    const bool keep = next != own.choice[position];
    if (keep || locked) step_locks::held_set(track, key, field, keep, static_cast<i32>(next) + 1);
    return true;
}

// 8d5ad776: every publication of a Drumline pad carries its choices; a note's
// with its step's locks for its key.
void publish(u32 track, u32 slot, i32* payload, u32 handle, u32 key) {
    const unsigned char* record = pad_slot(track, slot);
    if (!record || !payload) return;
    const DrumlinePage& page = page_of();
    opxy::drumline::Choices choices;
    pad_choices(record, choices);
    if (handle) {
        i32 values[step_locks::kFields];
        const u32 mask = step_locks::handle_fields(handle, key, values);
        drumline_lock_choices(page, mask, values, choices);
    }
    drumline_carry(payload, drumline_pack(page, choices));
}

// Per stock dbox voice: its payload from its first block, its choices and
// whether the processor runs, and the processor's state.
struct Voice {
    i32 payload[8];
    opxy::drumline::Choices choices;
    bool processed;
    opxy::drumline::Voice state;
};
Voice voices[kDboxVoices];
// The dbox object's block count: 0 from its note's start (8d513c0e) until its
// first block counts it (11a06fa6, 11a0748a).
const u32 kBlockCount = 0x30;
// The dbox page's nodes (8d531040..8d53118a): the four value texts, the four
// titles, and the six play-mode icons (8d53101e..8d531038), each shown while
// its byte +0x5c is set.
const u32 kValueNodes = 0x44, kTitleNodes = 0x54, kModeIconNodes = 0x6c, kModeIcons = 6, kNodeShown = 0x5c;

const Drumline service = {is_drumline, edit, press, publish, lock};
}

const void* sdk_drumline_service() { return &service; }

// The drum renderer's call of a stock dbox voice's render (8d51b55a, through
// sdk_dbox_methods): stock's render 11a06d28 with the voice's payload as its
// first block found it, so every per-hit value stays with its hit; then the
// extension's processor on the voice's mono block, after stock's level and
// envelope and before the renderer pans it, while one of its own parameters is
// off its default. The send hooks read the codes kept here. Audio role.
u32 sdk_dbox_render(unsigned char* self, i32* out, const i32* region, const i32* parameters) {
    typedef u32 Render(unsigned char*, i32*, const i32*, const i32*);
    const u32 index = dbox_index(static_cast<u32>(reinterpret_cast<Address>(self)));
    if (index >= kDboxVoices || !region) {
        if (index < kDboxVoices) sdk_drum_send_dbox[index] = 0;
        return stock<Render>(0x11a06d28)(self, out, region, parameters);
    }
    Voice& v = voices[index];
    const DrumlinePage& page = page_of();
    const opxy::drumline::Processor* processor = page.processor;
    if (!*reinterpret_cast<const u32*>(self + kBlockCount)) {
        for (u32 i = 0; i < 8; ++i) v.payload[i] = region[i];
        drumline_unpack(page, drumline_carried(v.payload), v.choices);
        v.processed = processor && drumline_processed(page, v.choices);
        for (u32 i = 0; i < opxy::drumline::voice_words; ++i) v.state.word[i] = 0;
        if (v.processed && processor->start) processor->start(v.state, v.choices);
    }
    sdk_drum_send_dbox[index] = static_cast<u8>(drumline_sends(page, v.choices));
    const u32 result = stock<Render>(0x11a06d28)(self, out, v.payload, parameters);
    if (v.processed && processor->process) processor->process(v.state, v.choices, out);
    return result;
}

// The method table sdk_dbox_render_select gives a dbox voice: stock calls +12.
typedef u32 (*DboxRender)(unsigned char*, i32*, const i32*, const i32*);
extern "C" {
extern const DboxRender sdk_dbox_methods[4] __asm__("sdk_dbox_methods");
const DboxRender sdk_dbox_methods[4] = {nullptr, nullptr, nullptr, sdk_dbox_render};
}

// 8d531192: both of stock's dbox page branches end here (the Shift branch
// 8d530fb8..8d531190 falls through; the unshifted one, 8d531268..8d5314e2,
// jumps back), each having set the four titles (nodes at page + 0x54..0x60) and
// values (+0x44..0x50); the play-mode icons are the six nodes at +0x6c..0x80,
// shown by the byte +0x5c. Unshifted on page 2 the page's titles and values
// replace stock's (with a step held, as that step plays them; blank past its
// parameters) and the icons hide (stock sets them again on its next pass). Shift
// and page 1 are stock's.
void sdk_drumline_page(unsigned char* node) {
    const u32 track = byte(kSelectedTrack);
    if (!node || !drumline_track(track) || !second[track] || shift_held()) return;
    const unsigned char* record = static_cast<const unsigned char*>(pad_region(track, edit_key(track)));
    if (!record) return;
    const DrumlinePage& page = page_of();
    opxy::drumline::Choices choices;
    if (step_locks::step_held()) held_choices(track, record, choices);
    else pad_choices(record, choices);
    for (u32 i = 0; i < kModeIcons; ++i)
        if (const u32 icon = *reinterpret_cast<const u32*>(node + kModeIconNodes + 4 * i)) at<unsigned char>(icon)[kNodeShown] = 0;
    for (u32 i = 0; i < 4; ++i) {
        void* value = at<void>(*reinterpret_cast<const u32*>(node + kValueNodes + 4 * i));
        void* title = at<void>(*reinterpret_cast<const u32*>(node + kTitleNodes + 4 * i));
        const bool shown = i < page.count;
        set_text(title, shown ? page.parameters[i].label : "");
        set_text(value, shown ? page.parameters[i].choices[choices.choice[i]] : "");
    }
}
