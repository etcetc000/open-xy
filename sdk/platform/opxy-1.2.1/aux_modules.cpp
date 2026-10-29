// Stock OS 1.2.1 aux tracks 1..6 (tracks 8..13) with aux modules, chosen as
// an instrument track's engine is: Shift and M1 list the track's stock module
// and the collection's aux modules (on the sends, stock's effects), and Shift
// and the track's key list the presets of the module on it. Stock's auxiliary
// controller swallowed Shift and M1 on every aux track, and opened a selector
// for Shift and the track's key (8c08f60c) only on the sends.
//
// A track holds a module the way an instrument track holds an engine: by its
// ID in the engine word of each of its patterns, which stock saves with the
// project (stock leaves 18 there on aux tracks). While a module is on a track,
// its page replaces M1 and the encoders on it edit the module's controls, the
// track's words 4..7, through stock's editor, which stores, locks and
// publishes them as any track parameter. M2..M4 stay stock's.
//
// The stock module beneath keeps what doesn't fight the module and loses
// what would act unseen. The MIDI and CV tracks still play their notes to
// external gear, and the MIDI track sends no CC for the module's controls.
// Elsewhere the track's own notes, live or sequenced, start nothing, and the
// brain transposes no routed track: their own controls are behind the
// module's page. GUI only.
#include "aux_modules.hpp"
#include "stock.hpp"
#include "stock_browser.hpp"
#include "synth_controls.hpp"

extern "C" {
opxy::u32 sdk_aux_screen() __asm__("sdk_aux_screen");
opxy::u32 sdk_aux_turn(opxy::u32, opxy::u32) __asm__("sdk_aux_turn");
void sdk_aux_select(opxy::u32) __asm__("sdk_aux_select");
void sdk_aux_engine_key() __asm__("sdk_aux_engine_key");
void sdk_aux_preset_key(opxy::u32) __asm__("sdk_aux_preset_key");
opxy::u32 sdk_aux_silent(opxy::u32) __asm__("sdk_aux_silent");
opxy::u32 sdk_aux_quiet(opxy::u32, opxy::u32) __asm__("sdk_aux_quiet");
opxy::u32 sdk_aux_routing(opxy::u32) __asm__("sdk_aux_routing");
}

namespace {
using opxy::i32;
using opxy::u32;
using opxy::u8;
using namespace opxy::platform;
namespace browser = opxy::platform::stock_browser;

// The auxiliary controller's page: 0 on M1 .. 3 on M4 (8c08f53c reads it).
const u32 kAuxPage = 0x0801eae4;
// The GUI's project, as both controllers pass it to stock's editor.
const u32 kProject = 0x86bd027c;
// The project stock's track getter (8d587708) reads: track t's patterns start
// at + 0xd7c + t * 0x53f44, 0x53f4 bytes apart, their count at + 0x53f40, and
// a pattern's engine word is at + 0x18 (8d58781c reads it; 8d598ce8 saves it).
const u32 kTrackProject = 0x840a0524;
const u32 kPatterns = 0xd7c, kTrackBytes = 0x53f44, kPatternBytes = 0x53f4, kPatternCount = 0x53f40;
const u32 kEngineWord = 0x18;
// Stock's engine word on an aux track, in every project it saves.
const u32 kStockAuxEngine = 18;
const char* const kStockModules[kAuxTracks] = {"brain", "punch-in", "midi", "cv", "audio", "tape"};
// The tracks whose stock module plays on beneath a module: MIDI and CV.
const u32 kMidiTrack = 10, kCvTrack = 11;
// The send tracks, and the GUI's one auxiliary controller: its vtable, and its
// static instance, constructed on first use, with its guard byte.
const u32 kSendTracks = 14;
const u32 kAuxController = 0x0803fb6c, kAuxInstance = 0x08014db4, kAuxInstanceGuard = 0x08014dc8;

// The module on an aux track: the ready one whose engine ID the track's
// current pattern holds.
const AuxModule* module(u32 track) {
    if (track - kAuxFirst >= kAuxTracks) return nullptr;
    const u32 engine = track_engine(track);
    for (u32 i = 0; i < sdk_aux_module_count; ++i) {
        const AuxModule& m = sdk_aux_modules[i];
        if (m.engine() == engine) return m.ready() == 1 ? &m : nullptr;
    }
    return nullptr;
}

i32 stored(u32 track, u32 slot) {
    return *reinterpret_cast<const volatile i32*>(kProject + 0x4080 + track * 0xe8 + slot * 4);
}
// Stock's set: stores the word and sends it to the audio side (8d5acd9c),
// as for any edit.
void set(u32 track, u32 slot, i32 value) { stock<void(u32, u32, u32, i32)>(0x8d5acf28)(kProject, track, slot, value); }

// Puts an engine ID in every pattern of a track, as stock keeps one per pattern.
void hold(u32 track, u32 engine) {
    const u32 project = word(kTrackProject);
    if (!project) return;
    const u32 patterns = project + kPatterns + track * kTrackBytes;
    const u32 count = word(patterns + kPatternCount);
    for (u32 i = 0; i < count && i < 16; ++i)
        *reinterpret_cast<volatile u32*>(patterns + i * kPatternBytes + kEngineWord) = engine;
}

// A module put on a track: each macro control picks up its parameter from
// instrument track 1. The frame's refresh (synth_controls.cpp) then takes these
// values, so the other tracks follow only once a control is turned.
void place(u32 track, u32 choice) {
    if (track - kAuxFirst >= kAuxTracks || choice > sdk_aux_module_count) return;
    const AuxModule* m = choice ? &sdk_aux_modules[choice - 1] : nullptr;
    hold(track, m ? m->engine() : kStockAuxEngine);
    if (!m || module(track) != m) return;
    for (u32 i = 0; i < 4; ++i)
        if (m->macro[i] >= 0) set(track, kAuxControlWord + i, stored(0, static_cast<u32>(m->macro[i])));
}

// The open list, one at a time: modules or presets, its track, and what each
// row is (a module's index + 1, or 0 for the stock module; or a preset).
const u32 kRows = kAuxPresetLimit > kAuxModuleLimit + 1 ? kAuxPresetLimit : kAuxModuleLimit + 1;
browser::StockSelector<kRows> selector = {};
u32 selecting;
u8 rows[kRows];
const AuxModule* presenting;

void choose(void*, u32 index) { place(selecting, rows[index]); }

// A preset sets the module's four controls through stock's set, as turning
// them would; the frame's macros then carry it to the instrument tracks.
void recall(void*, u32 index) {
    const AuxModule* m = module(selecting);
    if (!m || m != presenting || index >= m->presets) return;
    for (u32 i = 0; i < 4; ++i) set(selecting, kAuxControlWord + i, m->preset_values[index][i]);
}

// Shift and the track's key on aux track 1..6: the presets of its module.
void present(u32 track) {
    const AuxModule* m = module(track);
    if (!m || !m->presets) return;
    const char* labels[kRows] = {};
    const u32 count = m->presets < kRows ? m->presets : kRows;
    for (u32 i = 0; i < count; ++i) labels[i] = m->preset_names[i];
    selecting = track;
    presenting = m;
    // Titled as stock's preset browser is: the aux track's number over "preset".
    selector.open("preset", labels, count, 0, browser::Callbacks{nullptr, recall, nullptr},
                  static_cast<i32>(track - kAuxFirst));
}
}

// Shift and M1 on aux track 1..6, or the selector stock opens for a send:
// the track's stock module and the collection's aux modules.
void sdk_aux_select(u32 track) {
    const u32 a = track - kAuxFirst;
    if (a >= kAuxTracks) return;
    const char* labels[kRows] = {kStockModules[a]};
    rows[0] = 0;
    const AuxModule* current = module(track);
    u32 count = 1, initial = 0;
    for (u32 i = 0; i < sdk_aux_module_count && count <= kAuxModuleLimit; ++i) {
        if (sdk_aux_modules[i].ready() != 1) continue;
        if (current == &sdk_aux_modules[i]) initial = count;
        labels[count] = sdk_aux_modules[i].label;
        rows[count++] = static_cast<u8>(i + 1);
    }
    if (count == 1) return;
    selecting = track;
    // Titled as stock's preset browser is: the aux track's number over "aux".
    selector.open("aux", labels, count, initial, browser::Callbacks{nullptr, choose, nullptr},
                  static_cast<opxy::i32>(a));
}

// 8c08f6ec: Shift and M1 on any aux track, which stock's controller swallowed
// (its Shift-key method, 8c08f6b0, for key code 6). On aux tracks 1..6 the
// module list; on the sends, stock's effect list, as Shift and the send's key
// open it (8c08f60c on the controller's instance).
void sdk_aux_engine_key() {
    const u32 track = byte(kSelectedTrack);
    if (track - kAuxFirst < kAuxTracks) {
        sdk_aux_select(track);
    } else if (track - kSendTracks < 2 && byte(kAuxInstanceGuard) && word(kAuxInstance) == kAuxController) {
        stock<void(u32, u32)>(0x8c08f60c)(kAuxInstance, track);
    }
}

// 8c08f624: the exit of stock's selector for Shift and an aux track's key
// (8c08f60c) on any track but a send; R4 holds the track. Aux tracks 1..6 list
// their module's presets there, as an instrument track's key lists its engine's.
void sdk_aux_preset_key(u32 track) { present(track); }

// 8c08f540: the auxiliary controller's page choice (its method 11). On M1 of
// a track with a module, the module's page; 0 leaves the choice to stock.
u32 sdk_aux_screen() {
    const AuxModule* m = word(kAuxPage) == 0 ? module(byte(kSelectedTrack)) : nullptr;
    return m ? m->screen() : 0;
}

// 8c08fa14: the auxiliary controller's turn (its method 6), with the encoder
// 0..3, or 4..7 with Shift. On M1 of a track with a module, the turn edits the
// module's control through stock's editor and the controller returns 1;
// Shift turns a continuous control in tenths, as on an owned synth's page.
// 0 leaves the turn to stock.
u32 sdk_aux_turn(u32 encoder, u32 event) {
    const u32 track = byte(kSelectedTrack);
    const AuxModule* m = word(kAuxPage) == 0 && encoder < 8 ? module(track) : nullptr;
    if (!m) return 0;
    const u32 slot = kAuxControlWord + (encoder & 3);
    const SynthControls* controls = sdk_synth_controls_of(m->engine());
    if (event && controls && (controls->continuous & (1u << (encoder & 3))) && shift_held()) {
        const i32* source = reinterpret_cast<const i32*>(event);
        const i32 fine[3] = {source[0] / 10, source[1], source[2]};
        stock<u32(u32, u32, u32, const i32*)>(0x8d5ad5bc)(kProject, track, slot, fine);
    } else {
        stock<u32(u32, u32, u32, u32)>(0x8d5ad5bc)(kProject, track, slot, event);
    }
    return 1;
}

// 8d595738 (aux_hooks.s): 1 drops a note start, sequenced or played, on a
// track holding a module, except on the MIDI and CV tracks, whose notes go on
// to external gear.
u32 sdk_aux_silent(u32 track) { return track != kMidiTrack && track != kCvTrack && module(track); }

// 8d58a730 (aux_hooks.s): 1 drops the MIDI track's CC for a parameter it
// publishes when the parameter is one of a module's controls (words 4..7);
// the stock pages' CCs still go out.
u32 sdk_aux_quiet(u32 track, u32 parameter) {
    return parameter - kAuxControlWord < 4 && module(track);
}

// 8d595752: a note start asks for the tracks routed to the brain (aux track 1,
// 8d587a0c(8)) to transpose the note; none while the brain track holds a module.
u32 sdk_aux_routing(u32 track) {
    if (track == kAuxFirst && module(track)) return 0;
    return stock<u32(u32)>(0x8d587a0c)(track);
}
