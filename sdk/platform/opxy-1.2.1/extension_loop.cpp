// OS 1.2.1 bar loops for the loop_bars action. Every playhead move of a
// sequencer track goes through one stock function (8d5ac4d8: track, time,
// play events, follow steps, edit) from three call sites: advance by a tick
// (8d5ab84c), set position (8d5ab7d0: step jumps and play start) and resync
// from the song clock (8d5ac000). Stock also applies a step component's go-to
// target inside it (8d583e44, called at 8d5ac76e). These four replacements keep
// a looped track in its bar and forward everything else unchanged. A fifth
// (8d598038) shows the looped bar at play start, where stock shows bar 1, and
// a sixth (8d5474ee) draws the looped bar's box in the bar menu in grey.
// While bars loop, the scene clock follows the longest track, so it never
// reaches the scene's end and a song (one with scenes) stays in its scene.
//
// play_from: tracks take their place from the transport's scene clock (its
// word at +4, song ticks into the scene): at play start and at each pattern
// end (8d5abf80: time = clock * 1680 / factor[scale], mod the length). Play
// start resets the transport (8d5aaa38 at 8d597ffc), and a count-in resets it
// again when it ends (11a0aa80); both calls are replaced so a requested start
// sets the scene clock and places every track from it.
// The transport's total count (+8, the metronome's) and +0x1c (the SDK's
// machine clock) start from zero as on any start.
#include "../bar_loop.hpp"
#include "../extension_runtime.hpp"
#include "stock.hpp"

extern "C" {
void sdk_extensions_loop_advance(opxy::u32, opxy::u32, opxy::u32, opxy::u32, opxy::u32)
    __asm__("sdk_extensions_loop_advance");
void sdk_extensions_loop_position(opxy::u32, opxy::u32, opxy::u32, opxy::u32, opxy::u32)
    __asm__("sdk_extensions_loop_position");
opxy::u32 sdk_extensions_loop_step_target(opxy::u32, opxy::u32, opxy::u32, opxy::i32*, opxy::u32*)
    __asm__("sdk_extensions_loop_step_target");
void sdk_extensions_loop_shown(opxy::u32, opxy::u32) __asm__("sdk_extensions_loop_shown");
void sdk_extensions_play_reset(opxy::u32, opxy::u32) __asm__("sdk_extensions_play_reset");
void sdk_extensions_count_in_end(opxy::u32, opxy::u32) __asm__("sdk_extensions_count_in_end");
void sdk_extensions_bar_color(opxy::u32, opxy::u32, const opxy::u8*) __asm__("sdk_extensions_bar_color");
}

namespace opxy { namespace platform {
// Named, so the adapter's units can share one translation unit in host tests.
namespace loop_stock {
const u32 kProjectWord = 0x840a0524;
const u32 kLiveProject = 0x86bd027c;
const u32 kTrackObjects = 0x860c6adc + 0x108;  // the sequencer's 16 tracks
const u32 kTrackStride = 0x290;
const u32 kTimeScale = 0x840aa47c;             // track time per song tick, by scale
// Track object fields.
const u32 kIndex = 0x194, kTime = 0x198, kProcessed = 0x19c, kStep = 0x1a0, kStarted = 0x1a4;

using SetTime = void(u32, u32, u32, u32, u32);
void set_time(u32 object, u32 time, u32 events, u32 steps_on, u32 edit) {
    stock<SetTime>(0x8d5ac4d8)(object, time, events, steps_on, edit);
}
u32 steps(u32 track) { return stock<u32(u32)>(0x8d586800)(track); }
u32 object_of(u32 track) { return kTrackObjects + track * kTrackStride; }
volatile u32& field(u32 object, u32 offset) { return *at<volatile u32>(object + offset); }
// The current scene's pattern index for a track (scene records at +0x98).
u32 scene(u32 project) { return byte(project + 6); }
// The selected song's number. Bit 4 of the same byte is a flag stock tests
// (8d5842bc) and changes when a scene is picked by hand or the song screen
// opens, without another song being selected.
u32 song(u32 project) { return byte(project + 7) & 0x0f; }
u32 pattern_index(u32 project, u32 track) { return byte(project + 0x98 + scene(project) * 33 + track); }

// play_from's start, until play start takes it.
const u32 kSequencer = 0x860c6adc;   // the sequencer
const u32 kTransport = 0x86bd0064;
// counting: play start began a count-in; the place is taken when it ends.
// armed: recording was armed for it (Record held); it waits for stock's start,
// by Play (with the count-in) or by the first note, unless recording is
// disarmed first.
struct Start { u32 tick; bool pending, counting, armed; };
Start start;
// The scene's length in song ticks: its longest track.
u32 scene_ticks() {
    u32 longest = 0;
    for (u32 t = 0; t < extensions::sequencer_tracks; ++t) {
        const u32 scale = stock<u32(u32)>(0x8d586b50)(t);
        const u32 factor = scale < 16 ? word(kTimeScale + scale * 4) : 0;
        if (!factor) continue;
        const u32 ticks = steps(t) * bar_loop::step_time / factor;
        if (ticks > longest) longest = ticks;
    }
    return longest;
}
// The Play key (8c0929b8) asks the transport to start, stopped or playing
// (playing, that restarts from the top), unless this byte is set.
// Record held (stock's Play then arms recording instead of starting).
bool record_held() { return byte(kSequencer + 0x3060) != 0; }
void place(u32 transport, bool counting);
// Where a track starts for the pending start: the scene clock at its tick, in
// the track's time (as 8d5abf80 places it), mod its length.
u32 start_place(u32 track) {
    const u32 scale = stock<u32(u32)>(0x8d586b50)(track);
    const u32 factor = scale < 16 ? word(kTimeScale + scale * 4) : 0;
    const u32 length = steps(track) * bar_loop::step_time;
    // start.tick is below the scene's length, so tick * factor fits in 32 bits
    // (at most 64 steps at the slowest scale times the fastest factor, ~2.5e9).
    return factor && length ? (start.tick * factor) % length : 0;
}

// The held bars, and what they were taken against: a different project,
// a looped track playing another pattern, or another song selected, ends them
// all. The scene index alone is not checked: a song changes it at its scene
// ends, and a scene change that matters changes a looped track's pattern.
struct Held {
    u8 bar[extensions::sequencer_tracks]; u8 pattern[extensions::sequencer_tracks];
    u32 project, song; bool any;
};
Held held;
// A loop ended: the scene clock is put back in step with the tracks once more
// (the loop moved them).
bool resync_clock;
// The longest track (its length is the scene's) and its time per song tick,
// kept by extension_sequencer: the track the scene clock follows.
struct ClockTrack { u32 track, factor, length; };
ClockTrack clock_track = {extensions::sequencer_tracks, 0, 0};

void release() {
    if (held.any) resync_clock = true;
    for (u32 t = 0; t < extensions::sequencer_tracks; ++t) held.bar[t] = extensions::no_bar;
    held.any = false;
}
bool looping(u32 track, bar_loop::Range& r) {
    if (!held.any || track >= extensions::sequencer_tracks || held.bar[track] == extensions::no_bar) return false;
    const u32 project = word(kProjectWord);
    if (!project || project != held.project || song(project) != held.song ||
        pattern_index(project, track) != held.pattern[track]) {
        release();
        return false;
    }
    // A bar removed from under one track lets go of that track only.
    if (!bar_loop::range(held.bar[track], steps(track), r)) {
        held.bar[track] = extensions::no_bar;
        return false;
    }
    return true;
}
// Stock's own pattern-wrap bookkeeping (8d5ac77c): the pass restarts, and
// parameter locks return to the track's saved values.
void restart(u32 object, u32 track) {
    *at<volatile unsigned char>(object + kStarted) = 0;
    stock<void(u32, u32)>(0x8d5accf8)(kLiveProject, track);
    if (track == stock<u32()>(0x8d584468)()) stock<void()>(0x8d58b248)();
}
}

void extension_sequencer(extensions::Sequencer& sequencer) {
    using namespace loop_stock;
    const u32 project = word(kProjectWord);
    ClockTrack longest = {extensions::sequencer_tracks, 0, 0};

    for (u32 t = 0; t < extensions::sequencer_tracks; ++t) {
        extensions::SequencerTrack& k = sequencer.track[t];
        k = extensions::SequencerTrack();
        k.loop = extensions::no_bar;
        if (!project) continue;
        const u32 n = steps(t);
        const u32 bars = bar_loop::bars(n);
        if (!bars || bars > bar_loop::max_bars) continue;
        k.bars = static_cast<u8>(bars);
        const u32 bar = word(object_of(t) + kTime) / bar_loop::bar_time;
        k.bar = static_cast<u8>(bar < bars ? bar : bars - 1);
        k.shown = static_cast<u8>(stock<u32(u32)>(0x8d585fb4)(t));
        const u32 scale = stock<u32(u32)>(0x8d586b50)(t);
        const u32 factor = scale < 16 ? word(kTimeScale + scale * 4) : 0;
        k.bar_ticks = factor ? bar_loop::bar_time / factor : 0;
        const u32 length = factor ? n * bar_loop::step_time / factor : 0;
        if (length > longest.length) longest = {t, factor, length};
        bar_loop::Range r;
        if (looping(t, r)) k.loop = held.bar[t];
    }
    clock_track = longest;
}

// Does what the Play key does (8c0929b8), from the place: stopped, asks the
// transport to start with the count-in Play would give (seq + 0x305f, set
// with recording armed); playing, a restart, except where Play takes its other
// path (count-in on). With Record held (seq + 0x3060), stopped, it arms
// recording as Play does (8d592998(seq, 1)) and moves the tracks there at
// once; the start that follows takes the place.
bool extension_play_from(u32 song_tick) {
    using namespace loop_stock;
    const bool playing = byte(kSequencer + 0x305d) || byte(kSequencer + 0x305e);
    const u8 count_in = byte(kSequencer + 0x305f);
    if (!word(kProjectWord) || song_tick >= scene_ticks()) return false;
    if (record_held()) {
        if (playing) return false;
        stock<void(u32, u32)>(0x8d592998)(kSequencer, 1);
        start = {song_tick, true, false, true};
        place(kTransport, true);
        return true;
    }
    if (playing && count_in) return false;
    start = {song_tick, true, false, false};
    *at<volatile unsigned char>(kSequencer + 0x3061) = count_in;
    stock<void(u32, u32)>(0x8d5aa9ee)(kTransport, 1);
    return true;
}

// Stopped, the tracks wait where play will start: a loop armed moves each
// held track to its bar's first step, and ending it moves them back to the
// top, where stock's play starts.
bool extension_loop(const extensions::BarLoop& loop) {
    using namespace loop_stock;
    const bool stopped = !byte(kSequencer + 0x305d) && !byte(kSequencer + 0x305e);
    bool was[extensions::sequencer_tracks];
    for (u32 t = 0; t < extensions::sequencer_tracks; ++t) {
        bar_loop::Range r;
        was[t] = looping(t, r);
    }
    release();
    const u32 project = word(kProjectWord);
    if (!project) return false;
    held.project = project;
    held.song = song(project);
    for (u32 t = 0; t < extensions::sequencer_tracks; ++t) {
        bar_loop::Range r;
        if (loop.bar[t] == extensions::no_bar || !bar_loop::range(loop.bar[t], steps(t), r)) {
            if (stopped && was[t] && steps(t)) set_time(object_of(t), 0, 0, 0, 0);
            continue;
        }
        held.bar[t] = loop.bar[t];
        held.pattern[t] = static_cast<u8>(pattern_index(project, t));
        held.any = true;
        if (stopped) set_time(object_of(t), r.start, 0, 0, 0);
    }
    return true;
}
}}

using namespace opxy;
using namespace opxy::platform;

// Advance by a tick (8d5ab84c). A move past the bar's end plays the bar out,
// restarts it as stock restarts a pattern, and plays on from its start. The
// jump between passes plays no events: stock would take a backward move for a
// pattern wrap and play the rest of the pattern.
namespace opxy { namespace platform { namespace loop_stock {
// While bars loop, and once more after they end, the scene clock is set from
// the longest track right after it advances: its place in song ticks, which
// tempo-synced timing follows. Looping, that place stays inside the looped bar;
// in the scene's last bar it is held half a step short of the scene's end,
// since the transport adds a tick's time before the track wraps back, so the
// clock never reaches the end (where stock moves a song on). After the loop,
// the next scene end comes when the tracks reach it.
const u32 kSceneEndMargin = bar_loop::step_time / 2;  // in track time; song ticks below
void follow(u32 track) {
    if (track != clock_track.track || !clock_track.factor || (!held.any && !resync_clock)) return;
    u32 place = field(object_of(track), kTime) / clock_track.factor;
    if (place >= clock_track.length) place = 0;
    const u32 margin = kSceneEndMargin / clock_track.factor;
    if (held.any && place + margin >= clock_track.length)
        place = clock_track.length > margin ? clock_track.length - margin - 1 : 0;
    *at<volatile u32>(kTransport + 4) = place;
    if (!held.any) resync_clock = false;
}
void advance(u32 object, u32 time, u32 events, u32 steps_on, u32 edit);
}}}

void sdk_extensions_loop_advance(u32 object, u32 time, u32 events, u32 steps_on, u32 edit) {
    using namespace loop_stock;
    advance(object, time, events, steps_on, edit);
    follow(byte(object + kIndex));
}
void opxy::platform::loop_stock::advance(u32 object, u32 time, u32 events, u32 steps_on, u32 edit) {
    const u32 track = byte(object + kIndex);
    bar_loop::Range r;
    if (!looping(track, r)) return set_time(object, time, events, steps_on, edit);
    if (!bar_loop::inside(r, field(object, kTime)) || time < r.start) {
        set_time(object, bar_loop::fold(r, time), 0, 0, 0);  // Enter the bar without events.
        return;
    }
    if (time < r.end) return set_time(object, time, events, steps_on, edit);
    const u32 last = r.end - 1;
    set_time(object, last, events, steps_on, edit);
    // A step component on the bar's last step jumped elsewhere in the bar.
    if (field(object, kTime) != last) return;
    restart(object, track);
    const u32 step = field(object, kStep);
    set_time(object, r.start, 0, 0, 0);
    field(object, kStep) = step;  // The new pass enters its first step from the last one.
    set_time(object, bar_loop::landing(r, time), events, steps_on, edit);
}

// Set position (8d5ab7d0) and resync from the song clock (8d5ac000): fold the
// target into the bar. A backward target with events is first reached without
// them, so stock does not take it for a pattern wrap.
void sdk_extensions_loop_position(u32 object, u32 time, u32 events, u32 steps_on, u32 edit) {
    using namespace loop_stock;
    const u32 track = byte(object + kIndex);
    bar_loop::Range r;
    // Recording armed for a place: play start's move to the top goes to the
    // place instead, so a first note is never recorded at the top.
    if (!looping(track, r) && time == 0 && start.pending && start.armed && !start.counting && steps(track))
        return set_time(object, start_place(track), events, steps_on, edit);
    if (!looping(track, r) || bar_loop::inside(r, time)) return set_time(object, time, events, steps_on, edit);
    const u32 folded = bar_loop::fold(r, time);
    if (events && static_cast<i32>(folded) < static_cast<i32>(field(object, kProcessed)))
        set_time(object, folded, 0, 0, edit);
    set_time(object, folded, events, steps_on, edit);
}

// Step components (8d5ac76e): a go-to target lands in the looped bar.
u32 sdk_extensions_loop_step_target(u32 context, u32 track, u32 previous, i32* step, u32* extra) {
    using namespace loop_stock;
    const u32 result = stock<u32(u32, u32, u32, i32*, u32*)>(0x8d583e44)(context, track, previous, step, extra);
    bar_loop::Range r;
    if ((result & 0xff) && step && looping(track, r)) *step = bar_loop::fold_step(r, *step);
    return result;
}

// Play start (8d597f7c) positions every track, then shows bar 1 of each while
// the step display follows the playhead (8d598038: 8d587458(track, 0)). Stock
// follows a track into each bar it enters; a looped track never enters
// another, so it shows its looped bar from the start.
void sdk_extensions_loop_shown(u32 track, u32 bar) {
    using namespace loop_stock;
    bar_loop::Range r;
    if (looping(track, r)) bar = held.bar[track];
    stock<void(u32, u32)>(0x8d587458)(track, bar);
}

namespace opxy { namespace platform { namespace loop_stock {
// The scene clock at the start's place, every track placed from it as its
// next pattern end would (loops fold those places into their bars), each
// showing the bar it starts in, where stock would show bar 1. Before a
// count-in only the tracks move (a note played early is picked up there);
// the clock stays at stock's zero until the count-in ends.
void place(u32 transport, bool counting) {
    if (transport != kTransport || start.tick >= scene_ticks()) return;
    volatile u32& clock = *at<volatile u32>(transport + 4);
    const u32 before = clock;
    clock = start.tick;
    const bool follow = stock<u32(u32)>(0x8d592234)(kSequencer) != 0;
    for (u32 t = 0; t < extensions::sequencer_tracks; ++t) {
        if (!steps(t)) continue;
        const u32 object = object_of(t);
        stock<void(u32, u32)>(0x8d5abf80)(object, 0);
        if (!follow) continue;
        const u32 bars = bar_loop::bars(steps(t));
        const u32 bar = field(object, kTime) / bar_loop::bar_time;
        stock<void(u32, u32)>(0x8d587458)(t, bar < bars ? bar : bars - 1);
    }
    if (counting) clock = before;
}
}}}

// Play start's transport reset (8d597ffc). A start asked for by play_from takes
// its place here, or, with a count-in, when the count-in ends.
void sdk_extensions_play_reset(u32 transport, u32 count_in) {
    using namespace loop_stock;
    stock<void(u32, u32)>(0x8d5aaa38)(transport, count_in);
    if (!start.pending) return;
    // A start after a count-in that never ended (stopped during it), or after
    // recording armed for the place was disarmed, is stock's.
    if (start.counting || transport != kTransport || (start.armed && !byte(kSequencer + 0x305f))) {
        start.pending = false;
        return;
    }
    if (count_in) {
        start.counting = true;
        place(transport, true);
        return;
    }
    start.pending = false;
    place(transport, false);
}

// The count-in's end (11a0aa80, in the transport tick): stock resets the
// transport again, from the top; a start that counted in takes its place.
void sdk_extensions_count_in_end(u32 transport, u32 count_in) {
    using namespace loop_stock;
    stock<void(u32, u32)>(0x8d5aaa38)(transport, count_in);
    if (!start.pending || !start.counting || transport != kTransport) return;
    start.pending = false;
    place(transport, false);
}

// The bar menu (8d547474) colours each of its 4 bar boxes through one stock
// call per bar (8d520608: screen, node id, RGBA), with the node ids at
// screen + 0x38: the shown bar's box in black, the others in the theme's
// colour. The looped bar of the track it shows (8d4d6f14) is drawn instead at
// half the theme colour's brightness, shown or not, so it reads as one grey.
// Each bar's number ("barN" > "val", a text node at screen + 0x50 + 4 * N,
// as stock's own texts set with 8d6011f0) gains a dot while that bar plays.
namespace opxy { namespace platform { namespace loop_stock {
// The number each bar's text node shows, so it is set only when it changes.
struct Numbers { u32 node[4]; bool dot[4]; };
Numbers numbers;
void number(u32 screen, u32 bar, bool dot) {
    const u32 node = word(screen + 0x50 + 4 * bar);
    if (!node || (numbers.node[bar] == node && numbers.dot[bar] == dot)) return;
    const char text[3] = {char('1' + bar), dot ? '.' : '\0', '\0'};
    stock<void(u32, const char*)>(0x8d6011f0)(node, text);
    numbers.node[bar] = node;
    numbers.dot[bar] = dot;
}
}}}
void opxy::platform::extension_bar_menu_built() { opxy::platform::loop_stock::numbers = {}; }
void sdk_extensions_bar_color(u32 screen, u32 node, const u8* color) {
    using namespace loop_stock;
    using SetColor = void(u32, u32, const u8*);
    const u32 track = stock<u32()>(0x8d4d6f14)();
    bar_loop::Range r;
    u32 bar = 4;
    for (u32 i = 0; i < 4; ++i)
        if (*at<volatile unsigned short>(screen + 0x38 + 2 * i) == node) bar = i;
    if (bar < 4) {
        const bool playing = byte(kSequencer + 0x305d) || byte(kSequencer + 0x305e);
        const bool here = playing && track < extensions::sequencer_tracks && steps(track) &&
                          field(object_of(track), kTime) / bar_loop::bar_time == bar;
        number(screen, bar, here);
    }
    if (bar == 4 || !looping(track, r) || held.bar[track] != bar)
        return stock<SetColor>(0x8d520608)(screen, node, color);
    const u32 theme = word(0x84eb3ad8);
    u8 grey[4];
    for (u32 i = 0; i < 3; ++i) grey[i] = static_cast<u8>(((theme >> (8 * i)) & 0xff) / 2);
    grey[3] = color[3];
    stock<SetColor>(0x8d520608)(screen, node, grey);
}

