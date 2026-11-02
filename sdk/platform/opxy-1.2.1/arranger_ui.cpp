// Optional GUI-role arranger page. No audio callbacks or project writes.
#include "arranger_state.hpp"
#include "arranger_resolver.hpp"
#include "arranger_edit_guard.hpp"
#include "arranger_transport.hpp"
#include "arranger_ui_model.hpp"
#include "arranger_clip.hpp"
#include "arranger_history.hpp"
#include "arranger_timing.hpp"
#include "arranger_piano.hpp"
#include "arranger_phase.hpp"
#include "arranger_menu.hpp"
#include "arranger_bounce.hpp"
#include "arranger_bounce_destination.hpp"
#include "arranger_project.hpp"
#include "screen.hpp"
#include "arranger_pending_add.hpp"
#include "arranger_song_import.hpp"

extern "C" {
extern const char arranger_screen_slots[] __asm__("__stock_screen_slots");
opxy::u32 stock_gui_role() __asm__("stock_gui_role");
void sdk_arranger_screen_startup() __asm__("sdk_arranger_screen_startup");
int sdk_arranger_screen_owned(opxy::u32) __asm__("sdk_arranger_screen_owned");
opxy::u32 sdk_arranger_page(void*) __asm__("sdk_arranger_page");
opxy::u32 sdk_arranger_active() __asm__("sdk_arranger_active");
void sdk_arranger_plain(const void*) __asm__("sdk_arranger_plain");
void sdk_arranger_shifted(const void*) __asm__("sdk_arranger_shifted");
void sdk_arranger_release(opxy::u32) __asm__("sdk_arranger_release");
void sdk_arranger_poll() __asm__("sdk_arranger_poll");
void sdk_arranger_encoder(void*, opxy::u32, const void*) __asm__("sdk_arranger_encoder");
bool sdk_arranger_project_changed(bool, opxy::u32) __asm__("sdk_arranger_project_changed");
void sdk_arranger_project_poll() __asm__("sdk_arranger_project_poll");
void sdk_arranger_project_restored(bool, opxy::u32) __asm__("sdk_arranger_project_restored");
void sdk_arranger_project_mode_changed(bool) __asm__("sdk_arranger_project_mode_changed");
bool sdk_arranger_record_target(opxy::u32, opxy::u32*) __asm__("sdk_arranger_record_target");
void stock_arranger_plain(const void*) __asm__("stock_arranger_plain");
void stock_arranger_shifted(const void*) __asm__("stock_arranger_shifted");
void stock_arranger_release(opxy::u32) __asm__("stock_arranger_release");
void stock_arranger_poll() __asm__("stock_arranger_poll");
void sdk_arranger_raw_event(const void*) __asm__("sdk_arranger_raw_event");
void stock_arranger_raw_event(const void*) __asm__("stock_arranger_raw_event");
void sdk_arranger_encoder_dispatch(opxy::u32, const void*) __asm__("sdk_arranger_encoder_dispatch");
void stock_arranger_encoder_dispatch(opxy::u32, const void*) __asm__("stock_arranger_encoder_dispatch");
opxy::u32 sdk_arranger_edit_index(const opxy::u8*) __asm__("sdk_arranger_edit_index");
const opxy::u8* sdk_arranger_edit_source(opxy::u32, bool) __asm__("sdk_arranger_edit_source");
}

namespace {
using namespace opxy;
using namespace opxy::screen;
using platform::stock;
// Follow the relocated table even when the SDK grows its engine capacity.
u32 slots_address() { return reinterpret_cast<platform::Address>(arranger_screen_slots); }
const u32 visible_clips = 64;
u32 window_ticks = 15360;
u32 view_start;
struct Object {
    Base base;
    PathNode* clips[visible_clips];
    u32 clip_fill, muted_fill;
    Leaf* clip_labels[visible_clips];
    PathNode* bar_grid[17];
    PathNode* piano_rows[24];
    PathNode* lane_lines[8];
    PathNode* lane_highlight;
    PathNode* selection;
    PathNode* cursor;
    PathNode* playhead;
    PathNode* loop_band;
    PathNode* menu_background;
    PathNode* menu_highlight;
    Leaf* menu_rows[8];
    Leaf* menu_disabled[8];
    Leaf* menu_selected;
    Leaf* title;
    Leaf* lanes[8];
    Leaf* status;
    Leaf* controls;
    Leaf* feedback;
    u32 shown_position;
    bool bound;
    u8 padding[3];
};
Object screen_state;
u32 page_id;
bool active;
u32 cursor, ui_track, selected_pattern;
platform::ArrangerClipboard clipboard{};
platform::ArrangerHistory history;
u32 timing_project, timing_generation;
u8 timing_lengths[8][16], timing_scales[8][16];
bool bar_binding;
u32 bar_project, bar_generation, bar_track, bar_pattern, bar_slot;
i32 selected_slot = -1;
bool fine_grid;
bool editing, exit_edit_pending;
bool edit_pressed[72], swallowed_release[72];
platform::ArrangerRawEditGate raw_edit_gate;
u32 edit_project, edit_generation, edit_track, edit_pattern, return_track, return_controller;
const char* message = "";
u32 message_at;
u32 ui_now() { return stock<u32(u32, u32)>(0x8d5be290)(0, 0); }
void announce(const char* value) { message = value; message_at = ui_now(); }
const char* bounce_failure_text() {
    static const char* const failures[] = {"BOUNCE FAILED", "CAPTURE / TRANSPORT FAILED",
        "CAPTURE TOO SHORT", "SAMPLE WRITE FAILED", "SAVED SAMPLE INVALID", "DESTINATION APPLY FAILED"};
    const u32 failure = sdk_arranger_bounce_failure();
    return failures[failure < 6 ? failure : 0];
}
// Queue writer0x8c0b5eb4 copies exactly four words; key and value are first.
struct Event { u32 words[4]; };
Event pending_stop;
platform::ArrangerMenuGesture menu_gesture;
bool arrange_owned, menu_open;
u32 menu_index, menu_tool, menu_project, menu_generation, menu_slot, menu_cursor;
arranger::Model menu_preview;
u32 menu_source;
const char* const menu_labels[] = {
    "Split at cursor (Shift + 1)", "Make unique (Shift + 2)",
    "Undo (Shift + 3)", "Redo (Shift + 4)",
    "Loop selected clip (Shift + 5)", "Loop on/off (Shift + 6)",
    "Set loop start (Shift + 7)", "Set loop end (Shift + 8)",
    "Trim left (Stop + E4)", "Trim right at cursor (Stop + -)",
    "Slip contents (Shift + Stop + E3)", "Move to track (Stop + E3)",
    "Edit notes (click E4)", "Piano roll (Shift + click E4)",
    "Bounce to Sampler", "Bounce to Drum Sampler",
    "Add clip (+)", "Paste (Shift + +)", "Duplicate (Stop + +)",
    "Cut (Shift + -)", "Delete (-)",
    "Add existing clip", "Import Song at cursor", "Toggle Mute Clip (Stop + click E1)"
};
const u32 menu_count = sizeof(menu_labels) / sizeof(menu_labels[0]);
platform::ArrangerStopGesture stop_gesture;
platform::ArrangerStopSequence stop_sequence;
i32 resizing_slot = -1;
platform::ArrangerBounceChoice bounce_choice;
u32 bounce_last_status;
i32 piano_insert(void* container, const platform::PianoNote* note) {
    const u8* bytes = static_cast<const u8*>(container);
    const u32 index = bytes[0x780] | (u32(bytes[0x781]) << 8);
    if (index >= 120) return -1;
    stock<void(void*, const platform::PianoNote*, i32)>(0x8d58f24c)(container, note, -1);
    return static_cast<i32>(index);
}
platform::PianoEditor piano(piano_insert);
bool piano_view;
platform::ArrangerPendingAdd pending_add{};
i32 pending_add_slot = -1;
u32 pending_add_source, pending_add_count;
arranger::Model pending_add_before{};
bool pending_add_uncommitted;
u32 piano_project, piano_generation, piano_slot, piano_track, piano_pattern;
u32 piano_time, piano_pitch = 60, piano_low = 56, piano_start, piano_window = 7680, piano_rows = 8;
i32 piano_selected = -1;


bool held(u32 key) { return stock<u32(u32)>(0x8d5b7d7c)(key) != 0; }
void forward(const void* event, bool shifted) {
    if (shifted) stock_arranger_shifted(event);
    else stock_arranger_plain(event);
}

bool edit_valid() {
    if (!editing || !platform::arranger_edit_binding_valid(editing, stock_gui_role(), edit_project,
        platform::word(0x840a0524), edit_generation, stock<u32()>(0x8d584224)(), edit_track,
        platform::byte(platform::kSelectedTrack), edit_pattern) ||
        stock<u32()>(0x8c093d54)() != 0) return false;
    return platform::arranger_pattern_source(reinterpret_cast<const u8*>(edit_project),
                                            edit_track, edit_pattern, false) != nullptr;
}
// BAR opens native timing controller 35 (8c08425c); keep its exact source
// independent of the stock scene selection until the physical key is released.
bool bar_binding_valid() {
    if (!bar_binding || !active || !stock_gui_role() || !sdk_arranger_project_edit_ready() ||
        bar_project != platform::word(0x840a0524) || bar_generation != stock<u32()>(0x8d584224)() ||
        platform::selected_track() != bar_track) return false;
    const arranger::Clip* clip = sdk_arranger_model().clip(bar_slot);
    return clip && clip->track == bar_track && clip->pattern == bar_pattern &&
        platform::arranger_pattern_source(reinterpret_cast<const u8*>(bar_project), bar_track, bar_pattern, false);
}
bool step_held() {
    for (u32 key = 56; key < 72; ++key) if (held(key)) return true;
    return false;
}
bool any_edit_press() {
    for (u32 key = 0; key < 72; ++key) if (edit_pressed[key]) return true;
    return false;
}
void close_edit(bool restore) {
    editing = exit_edit_pending = false;
    for (u32 key = 0; key < 72; ++key) edit_pressed[key] = false;
    if (restore && edit_project == platform::word(0x840a0524) &&
        edit_generation == stock<u32()>(0x8d584224)())
        stock<void(u32, u32)>(0x8d585290)(return_track, 0);
    stock<void(u32)>(0x8c094178)(return_controller);
    stock<void()>(0x8c093f50)();
    announce("RETURNED FROM NOTE EDIT");
}

void open_edit(u32 initiating_key = 13) {
    if (stop_gesture.pending) { announce("RELEASE STOP FIRST"); return; }
    sdk_arranger_project_poll();
    if (!sdk_arranger_project_edit_ready()) { announce("PROJECT BUSY"); return; }
    const arranger::Clip* clip = selected_slot < 0 ? nullptr :
        sdk_arranger_model().clip(static_cast<u32>(selected_slot));
    if (!clip) { announce("SELECT A CLIP FIRST"); return; }
    const u32 project = platform::word(0x840a0524);
    if (!platform::arranger_pattern_source(reinterpret_cast<const u8*>(project), clip->track, clip->pattern, false)) {
        announce("PATTERN UNAVAILABLE"); return;
    }
    // Only the exact initiating encoder click and Shift may cross into the editor.
    // Rejected presses otherwise still affect stock held-key polling.
    for (u32 key = 0; key < 72; ++key)
        if (key != initiating_key && key != 55 && held(key)) { announce("RELEASE OTHER KEYS FIRST"); return; }
    return_track = platform::byte(platform::kSelectedTrack);
    return_controller = stock<u32()>(0x8c093d54)();
    edit_project = project;
    edit_generation = stock<u32()>(0x8d584224)();
    edit_track = clip->track; edit_pattern = clip->pattern;
    stock<void(u32, u32)>(0x8d585290)(edit_track, 0);
    editing = true;
    exit_edit_pending = false;
    stock<void(u32)>(0x8c094178)(0); // Stock synth controller: keyboard and step editor.
    stock<void()>(0x8c093f50)();
}

void decimal(char* out, u32 n) {
    char digits[10];
    u32 count = 0;
    do { digits[count++] = static_cast<char>('0' + n % 10); n /= 10; } while (n);
    for (u32 i = 0; i < count; ++i) out[i] = digits[count - i - 1];
    out[count] = 0;
}
char* append(char* out, const char* text) { while (*text) *out++ = *text++; *out = 0; return out; }
char* number(char* out, u32 n) { decimal(out, n); while (*out) ++out; return out; }

bool view_active() {
    const u32* controller = stock<const u32*()>(0x8c0944b0)();
    return sdk_arranger_project_linear() && !editing && controller && controller[1] == 40;
}

void select_clip(bool feedback = true) {
    selected_slot = -1;
    for (u32 i = 0; i < arranger::Model::capacity; ++i) {
        const arranger::Clip* c = sdk_arranger_model().clip(i);
        if (c && c->track == ui_track && cursor >= c->start && cursor - c->start < c->length) {
            selected_slot = static_cast<i32>(i);
            selected_pattern = c->pattern;
            break;
        }
    }
    if (feedback) announce(selected_slot < 0 ? "GAP" : "CLIP SELECTED");
}

void sync_selected_lane(bool entering = false) {
    if (!entering && !view_active()) return;
    if (platform::arranger_sync_lane(platform::selected_track(),
            editing || piano_view || pending_add.pending || sdk_arranger_bounce_busy(), ui_track)) {
        selected_pattern = 0;
        select_clip(false);
        history.breakGroup();
        resizing_slot = -1;
    }
}

const u8* piano_source() {
    if (!piano_view || !sdk_arranger_project_edit_ready() || piano_project != platform::word(0x840a0524) ||
        piano_generation != stock<u32()>(0x8d584224)()) return nullptr;
    const arranger::Clip* c = sdk_arranger_model().clip(piano_slot);
    if (!c || c->track != piano_track || c->pattern != piano_pattern) return nullptr;
    return platform::arranger_pattern_source(reinterpret_cast<const u8*>(piano_project), piano_track, piano_pattern, false);
}
void piano_select() { piano_selected = piano.hit(static_cast<i32>(piano_time), piano_pitch); }
bool piano_editable() {
    if (!piano_source()) { piano_view = false; announce("PIANO SOURCE CHANGED"); return false; }
    if (platform::byte(0x860c9b39) || platform::byte(0x860c9b3a) ||
        platform::byte(0x860c9b3b) || platform::byte(0x860c9b3c) || platform::word(0x860c0224)) {
        announce("STOP AND DISARM REC TO EDIT NOTES"); return false;
    }
    return true;
}
void piano_changed(bool changed) {
    if (!changed) { announce("NOTE EDIT REJECTED / NO CHANGE"); return; }
    u8* source = const_cast<u8*>(piano_source());
    if (!source) { piano_view = false; return; }
    source[0x14] &= static_cast<u8>(~8u);
    platform::barrier();
    sdk_arranger_project_changed(active, ui_track);
    sdk_arranger_publish(active, false);
    announce("NOTES UPDATED (SHARED SOURCE)");
}
void open_piano() {
    if (selected_slot < 0 || !sdk_arranger_project_edit_ready()) { announce("SELECT A CLIP FIRST"); return; }
    const arranger::Clip* c = sdk_arranger_model().clip(static_cast<u32>(selected_slot));
    if (!c) return;
    const u32 project = platform::word(0x840a0524), generation = stock<u32()>(0x8d584224)();
    const u8* source = platform::arranger_pattern_source(reinterpret_cast<const u8*>(project), c->track, c->pattern, false);
    if (!source) return;
    piano.bind(const_cast<u8*>(source), project, generation);
    if (!piano.valid() || !piano.span()) { announce("INVALID NOTE SOURCE"); return; }
    piano.reset(); history.breakGroup();
    piano_project = project; piano_generation = generation; piano_slot = static_cast<u32>(selected_slot);
    piano_track = c->track; piano_pattern = c->pattern; piano_time = 0;
    // Start on actual source notes, not a fixed C4 viewport that can look empty.
    platform::PianoNote first;
    piano_pitch = 60;
    if (piano.first(first)) {
        piano_time = first.position > 0 ? static_cast<u32>(first.position) : 0;
        if (piano_time >= piano.span()) piano_time = piano.span() - 1;
        piano_pitch = first.pitch;
    }
    piano_rows = 8; piano_low = platform::piano_pitch_low(piano_pitch, piano_rows);
    piano_window = 7680; piano_start = piano_time / piano_window * piano_window;
    piano_view = true; piano_select();
    announce("E1 CURSOR E2 ZOOM E3 PITCH E4 LENGTH");
}
void piano_key(u32 key, bool shifted) {
    piano.breakGroup();
    if (key == 4) { piano_view = false; announce("LINEAR"); return; }
    if (key == 50) { announce("EXIT PIANO TO RECORD"); return; }
    if (key >= 14 && key <= 21) {
        if (shifted && (key == 16 || key == 17)) {
            if (piano_editable()) { piano_changed(key == 16 ? piano.undo() : piano.redo()); piano_select(); }
        } else if (!shifted) { piano_pitch = piano_low + piano_rows - 1 - (key - 14) * piano_rows / 8; piano_select(); }
        return;
    }
    if (!piano_editable()) return;
    if (key == 54) {
        platform::PianoNote n = {static_cast<i32>(piano_time), fine_grid ? 1u : 480u,
            static_cast<u8>(piano_pitch), 100, 0, 0};
        if (n.duration > piano.span()) n.duration = piano.span();
        piano_changed(piano.add(n, piano_selected));
    } else if (key == 53) {
        piano_changed(piano_selected >= 0 && piano.erase(static_cast<u32>(piano_selected))); piano_select();
    }
}
void piano_encoder(u32 encoder, i32 delta) {
    if (!delta || !piano_source()) return;
    const u32 index = encoder % 4;
    const bool shifted = encoder >= 4 || platform::shift_held();
    const u32 step = fine_grid || shifted ? 1 : 480;
    if (index < 2) piano.breakGroup();
    if (index == 0 && !shifted) {
        piano_time = platform::arranger_cursor_time(piano_time, delta, step);
        if (piano_time >= piano.span()) piano_time = piano.span() - 1;
        piano_select();
    } else if (index == 1 && shifted) {
        piano_rows = platform::piano_vertical_zoom(piano_rows, delta);
        piano_low = platform::piano_pitch_low(piano_pitch, piano_rows);
    } else if (index == 1) {
        const u32 next = platform::arranger_zoom(piano_window, delta > 0 ? 1 : -1, 480);
        const u32 relative = piano_time >= piano_start ? piano_time - piano_start : 0;
        const u32 scaled = platform::arranger_zoom_anchor(relative, piano_window, next);
        piano_start = piano_time > scaled ? piano_time - scaled : 0;
        piano_window = next;
    } else if (index == 2 && (shifted || piano_selected < 0)) {
        // In a gap E3 moves the pitch cursor, so + can insert on any row.
        piano_pitch = platform::arranger_shift_time(piano_pitch, delta, 1);
        if (piano_pitch > 127) piano_pitch = 127;
        piano_select();
    } else {
        if (!piano_editable() || piano_selected < 0) return;
        platform::PianoNote n;
        if (!piano.note(static_cast<u32>(piano_selected), n)) return;
        if (index == 0) {
            const u32 time = platform::arranger_shift_time(n.position < 0 ? 0 : static_cast<u32>(n.position), delta, step);
            n.position = static_cast<i32>(time < piano.span() ? time : piano.span() - 1);
        } else if (index == 2) {
            const u32 pitch = platform::arranger_shift_time(n.pitch, delta, 1);
            n.pitch = static_cast<u8>(pitch > 127 ? 127 : pitch);
        } else if (shifted) {
            const u32 velocity = platform::arranger_shift_time(n.velocity, delta, 1);
            n.velocity = static_cast<u8>(velocity < 1 ? 1 : velocity > 127 ? 127 : velocity);
        } else {
            n.duration = platform::arranger_shift_time(n.duration, delta, step);
            if (!n.duration) n.duration = 1;
            if (n.duration > piano.span()) n.duration = piano.span();
        }
        if (piano.update(static_cast<u32>(piano_selected), n, 1u + static_cast<u32>(piano_selected) * 8u + index + (shifted ? 4u : 0u))) {
            piano_time = n.position < 0 ? 0 : static_cast<u32>(n.position); piano_pitch = n.pitch; piano_changed(true);
        }
    }
}

void begin_history(u32 group = 0) {
    history.begin(sdk_arranger_model(), ui_track, cursor, platform::word(0x840a0524),
        stock<u32()>(0x8d584224)(), group);
}

bool commit_edit(bool changed, const char* success) {
    if (!changed) { history.cancel(); announce("EDIT REJECTED: GAP / COLLISION / LIMIT"); return false; }
    if (!sdk_arranger_project_changed(active, ui_track)) {
        history.reset(); announce("PROJECT CHANGED; EDIT CANCELLED"); return false;
    }
    history.commit(sdk_arranger_model());
    sdk_arranger_publish(active, false);
    announce(success);
    return true;
}

// Observe native edits only within one live project. Loading establishes a
// baseline; it must not reinterpret the saved duration of an existing clip.
void reconcile_source_timing() {
    if (!active || pending_add.pending || menu_tool || menu_open || bounce_choice.open ||
        sdk_arranger_bounce_busy() || !sdk_arranger_project_edit_ready()) return;
    const u32 project = platform::word(0x840a0524), generation = stock<u32()>(0x8d584224)();
    if (!project) return;
    if (timing_project != project || timing_generation != generation) {
        timing_project = project; timing_generation = generation;
        for (u32 track = 0; track < 8; ++track) for (u32 pattern = 0; pattern < 16; ++pattern)
            timing_lengths[track][pattern] = 0;
    }
    arranger::Model& model = sdk_arranger_model();
    bool changed = false, native_changed = false;
    for (u32 track = 0; track < 8; ++track) for (u32 pattern = 0; pattern < 16; ++pattern) {
        const u8* source = platform::arranger_pattern_source(reinterpret_cast<const u8*>(project), track, pattern, false);
        if (!source || !source[0] || source[8] >= 16) { timing_lengths[track][pattern] = 0; continue; }
        const u32 old_length = timing_lengths[track][pattern], old_scale = timing_scales[track][pattern];
        if (old_length && (old_length != source[0] || old_scale != source[8])) {
            native_changed = true;
            for (u32 slot = 0; slot < arranger::Model::capacity; ++slot) {
                const arranger::Clip* selected = model.clip(slot);
                if (!selected || selected->track != track || selected->pattern != pattern) continue;
                const arranger::Clip before = *selected;
                const u32 steps = source[0] > 64 ? 64 : source[0];
                if (!platform::arranger_rescale_clip(model, slot, old_scale, source[8], steps)) continue;
                arranger::Clip value = *model.clip(slot);
                value.source_length = u32(source[0]) * 480;
                value.offset %= value.source_length;
                if (model.replace(slot, value) && (before.length != value.length ||
                    before.source_length != value.source_length || before.offset != value.offset)) changed = true;
            }
        }
        timing_lengths[track][pattern] = source[0]; timing_scales[track][pattern] = source[8];
    }
    if (native_changed) {
        // Undo stores geometry but not native timing; stale snapshots cannot be replayed.
        history.reset(); resizing_slot = -1;
        if (changed && sdk_arranger_project_changed(active, ui_track)) {
            sdk_arranger_publish(active, false); select_clip(false); announce("SOURCE TIMING UPDATED");
        }
    }
}

struct BarTimingEvent {
    bool release;
    BarTimingEvent(u32 key, bool down) : release(key == 25 && !down) {
        if (key != 25 || !down || bar_binding || !view_active() || editing || piano_view ||
            pending_add.pending || menu_open || menu_tool || bounce_choice.open || sdk_arranger_bounce_busy() ||
            !sdk_arranger_project_edit_ready() || platform::shift_held()) return;
        const arranger::Clip* clip = selected_slot < 0 ? nullptr : sdk_arranger_model().clip(static_cast<u32>(selected_slot));
        if (!clip || clip->track != ui_track) return;
        // Establish the old rate before stock can change it on this same event.
        reconcile_source_timing();
        bar_project = platform::word(0x840a0524); bar_generation = stock<u32()>(0x8d584224)();
        bar_track = clip->track; bar_pattern = clip->pattern; bar_slot = static_cast<u32>(selected_slot);
        bar_binding = true;
    }
    ~BarTimingEvent() {
        if (bar_binding) reconcile_source_timing();
        if (release || (bar_binding && !bar_binding_valid())) bar_binding = false;
    }
};

platform::ArrangerAddContext add_context() {
    return {platform::word(0x840a0524), stock<u32()>(0x8d584224)(), ui_track, cursor, selected_pattern, selected_slot};
}
bool add_permitted(bool record_intent = false) {
    return view_active() && !piano_view && !menu_open && !menu_tool && !bounce_choice.open &&
        !platform::byte(platform::kPlaying) && !platform::byte(platform::kPlaying + 1) &&
        !platform::byte(platform::kPlaying + 2) &&
        (record_intent || !platform::byte(platform::kPlaying + 3)) &&
        !platform::byte(0x860c072c) && !platform::byte(0x860c072d) &&
        (record_intent || !sdk_arranger_record_status()) && !sdk_arranger_bounce_busy();
}
const char* add_progress() {
    using A = platform::ArrangerPendingAdd;
    switch (pending_add.stage) {
    case A::project_check: return "ADDING: PROJECT CHECK";
    case A::history: return "ADDING: HISTORY";
    case A::create: return "ADDING: CREATE PATTERN";
    case A::commit: return "ADDING: COMMIT";
    case A::select: return "ADDING: SELECT";
    case A::draw: return "ADDING: DRAW";
    default: return "ADDED EMPTY CLIP";
    }
}
void add_clip(bool record_intent = false) {
    if (!pending_add.enqueue(add_context(), add_permitted(record_intent), record_intent)) { announce("ADD REQUIRES STOPPED TIMELINE"); return; }
    pending_add_slot = -1; pending_add_source = pending_add.request.pattern; pending_add_uncommitted = false;
    announce(add_progress());
}
void cancel_pending_add(const char* reason) {
    if (pending_add_uncommitted) {
        const bool bound = pending_add.bound(add_context());
        u32* count = reinterpret_cast<u32*>(pending_add.request.project + 0xd7c +
            pending_add.request.track * 0x53f44 + 0x53f40);
        platform::arranger_cancel_added_clip(sdk_arranger_model(), pending_add_before,
            count, pending_add_count, bound);
        platform::barrier();
    }
    pending_add_uncommitted = false;
    pending_add.cancel(); history.cancel(); announce(reason);
}
void process_pending_add() {
    if (!pending_add.pending) return;
    using A = platform::ArrangerPendingAdd;
    const platform::ArrangerAddContext current = add_context();
    if (!pending_add.bound(current) || !add_permitted(pending_add.record_after) ||
        (pending_add.stage <= A::create && !pending_add.context_matches(current)) ||
        (pending_add.stage != A::project_check && !sdk_arranger_project_edit_ready())) {
        cancel_pending_add("ADD CANCELLED: CONTEXT CHANGED"); return;
    }
    if (pending_add.draw_complete) {
        // Start outside rendering, only after the published clip's final frame.
        const bool started = sdk_arranger_request_record(pending_add.request.track, 0xffffffffu) &&
            sdk_arranger_request_start(pending_add.request.cursor, false, true);
        pending_add.cancel();
        announce(started ? "RECORDING NEW CLIP" : "REC START REJECTED / CLIP KEPT");
        return;
    }
    if (!pending_add.ready(ui_now()) || pending_add.draw_requested) return;
    switch (pending_add.stage) {
    case A::project_check:
        sdk_arranger_project_poll();
        if (!pending_add.pending) return;
        if (!sdk_arranger_project_edit_ready() || !pending_add.bound(add_context())) {
            cancel_pending_add("ADD CANCELLED: PROJECT NOT READY"); return;
        }
        if (!platform::arranger_pattern_source(reinterpret_cast<u8*>(pending_add.request.project),
                pending_add.request.track, pending_add_source, false)) pending_add_source = 0;
        break;
    case A::history:
        pending_add_before = sdk_arranger_model(); begin_history(); break;
    case A::create:
        pending_add_count = platform::word(pending_add.request.project + 0xd7c +
            pending_add.request.track * 0x53f44 + 0x53f40);
        pending_add_slot = platform::arranger_add_empty_clip(sdk_arranger_model(),
            reinterpret_cast<u8*>(pending_add.request.project), pending_add.request.track,
            pending_add.request.cursor, pending_add_source, stock<void(u8*)>(0x8d5844d0));
        if (pending_add_slot < 0) { cancel_pending_add("ADD REJECTED: NO SPACE / SOURCE"); return; }
        pending_add_uncommitted = true;
        break;
    case A::commit:
        if (!commit_edit(pending_add_slot >= 0, "ADDED EMPTY CLIP")) {
            cancel_pending_add("ADD COMMIT REJECTED"); return;
        }
        pending_add_uncommitted = false;
        break;
    case A::select: select_clip(false); break;
    case A::draw: pending_add.draw_requested = true; return;
    default: return;
    }
    pending_add.next(); announce(add_progress());
}

void timeline_key(u32 key, bool shifted) {
    // The raw key path only captures a request; lifecycle checks and mutation
    // run as paint-gated stages in GUI polling, outside stock key dispatch.
    if (key == 54 && !shifted && !stop_gesture.pending) { add_clip(); return; }
    sdk_arranger_project_poll();
    if (!sdk_arranger_project_edit_ready() || !view_active()) { announce("PROJECT BUSY"); return; }
    arranger::Model& model = sdk_arranger_model();
    const arranger::Clip* selected = selected_slot < 0 ? nullptr : model.clip(static_cast<u32>(selected_slot));
    if (key >= 14 && key <= 21) {
        const u32 lane = key - 14;
        if (shifted && lane < 2) {
            if (!selected) { announce("SELECT A CLIP FIRST"); return; }
            if (lane == 1 && platform::arranger_clip_track_locked(reinterpret_cast<const u8*>(platform::word(0x840a0524)), ui_track, ui_track)) {
                announce("UNLOCK INSTRUMENT FIRST"); return;
            }
            begin_history();
            const bool changed = lane == 0 ? platform::arranger_split_clip(model, reinterpret_cast<u8*>(platform::word(0x840a0524)), static_cast<u32>(selected_slot), cursor) >= 0 :
                platform::arranger_make_unique(model, reinterpret_cast<u8*>(platform::word(0x840a0524)),
                    static_cast<u32>(selected_slot));
            if (commit_edit(changed, lane == 0 ? "SPLIT AT CURSOR" : "INDEPENDENT SOURCE")) select_clip(false);
        } else if (shifted && (lane == 2 || lane == 3)) {
            const u32 project = platform::word(0x840a0524), generation = stock<u32()>(0x8d584224)();
            const bool changed = lane == 2 ? history.undo(model, ui_track, cursor, project, generation) :
                history.redo(model, ui_track, cursor, project, generation);
            if (!changed) { announce(lane == 2 ? "NOTHING TO UNDO" : "NOTHING TO REDO"); return; }
            stock<void(u32, u32)>(0x8d585290)(ui_track, 0);
            select_clip(false);
            commit_edit(true, lane == 2 ? "UNDO" : "REDO");
        } else if (shifted && lane >= 4) {
            begin_history();
            bool changed = false;
            if (lane == 4 && selected) changed = model.setLoop(true, selected->start, selected->start + selected->length);
            if (lane == 5) changed = model.setLoop(!model.loopEnabled(), model.loopStart(), model.loopEnd());
            if (lane == 6) {
                const u32 end = model.loopEnd() > cursor ? model.loopEnd() : platform::arranger_shift_time(cursor, 1, 7680);
                changed = model.setLoop(model.loopEnabled(), cursor, end);
            }
            if (lane == 7) changed = model.setLoop(model.loopEnabled(), model.loopStart(), cursor);
            commit_edit(changed, "LOOP UPDATED");
        } else {
            ui_track = lane;
            stock<void(u32, u32)>(0x8d585290)(ui_track, 0);
            selected_pattern = 0;
            select_clip();
            if (sdk_arranger_project_changed(active, ui_track)) sdk_arranger_publish(active, false);
        }
        return;
    }
    const bool stopped_modifier = stop_gesture.pending;
    if (stopped_modifier && (key == 53 || key == 54)) stop_gesture.modify();
    if (key == 54) {
        if (shifted) {
            if (!clipboard.valid) { announce("CLIPBOARD EMPTY"); return; }
            u8* project = reinterpret_cast<u8*>(platform::word(0x840a0524));
            if (clipboard.value.track != ui_track && platform::arranger_clip_track_locked(project, clipboard.value.track, ui_track)) {
                announce("UNLOCK SOURCE / DESTINATION INSTRUMENT"); return;
            }
            begin_history();
            const i32 slot = platform::arranger_paste_clip(model, project, clipboard.value, ui_track, cursor);
            if (commit_edit(slot >= 0, clipboard.value.track == ui_track ? "PASTED SHARED PATTERN" : "PASTED INDEPENDENT SOURCE")) {
                const arranger::Clip* pasted = model.clip(static_cast<u32>(slot));
                cursor = pasted->start + pasted->length;
                select_clip(false);
            }
        } else if (stopped_modifier) {
            if (!selected) { announce("SELECT A CLIP FIRST"); return; }
            const u32 end = selected->start + selected->length;
            begin_history();
            const i32 slot = model.duplicate(static_cast<u32>(selected_slot), end);
            if (commit_edit(slot >= 0, "DUPLICATED SHARED PATTERN")) {
                const arranger::Clip* copy = model.clip(static_cast<u32>(slot));
                cursor = copy->start + copy->length; select_clip(false);
            }
        } else add_clip();
    } else if (key == 53) {
        if (!selected) { announce("SELECT A CLIP FIRST"); return; }
        const u32 original_start = selected->start;
        begin_history();
        bool changed;
        if (shifted) changed = clipboard.cut(model, static_cast<u32>(selected_slot));
        else if (stopped_modifier) changed = cursor > selected->start && cursor < selected->start + selected->length &&
            model.resize(static_cast<u32>(selected_slot), cursor - selected->start);
        else changed = model.remove(static_cast<u32>(selected_slot));
        if (commit_edit(changed, shifted ? "CUT: SHIFT PLUS TO PASTE" : stopped_modifier ? "TRIMMED RIGHT" : "DELETED")) {
            if (shifted) cursor = original_start;
            select_clip(false);
        }
    }
}

void toggle_selected_mute() {
    sdk_arranger_project_poll();
    if (!sdk_arranger_project_edit_ready() || !view_active()) { announce("PROJECT BUSY"); return; }
    arranger::Model& model = sdk_arranger_model();
    const arranger::Clip* clip = selected_slot < 0 ? nullptr : model.clip(static_cast<u32>(selected_slot));
    if (!clip) { announce("SELECT A CLIP FIRST"); return; }
    const bool muted = !clip->muted;
    begin_history();
    commit_edit(model.setMuted(static_cast<u32>(selected_slot), muted), muted ? "MUTED" : "UNMUTED");
}

const char* menu_unavailable(u32 action) {
    if (!sdk_arranger_project_edit_ready()) return "PROJECT BUSY";
    if ((action == 21 || action == 22) &&
        (platform::byte(platform::kPlaying) || platform::byte(platform::kPlaying + 1) ||
         platform::byte(platform::kPlaying + 2) || platform::byte(platform::kPlaying + 3) ||
         sdk_arranger_record_status())) return "STOP TRANSPORT FIRST";
    const arranger::Model& model = sdk_arranger_model();
    const arranger::Clip* clip = selected_slot < 0 ? nullptr : model.clip(static_cast<u32>(selected_slot));
    if (action == 2) return history.canUndo() ? nullptr : "NOTHING TO UNDO";
    if (action == 3) return history.canRedo() ? nullptr : "NOTHING TO REDO";
    if (action == 17) return clipboard.valid ? nullptr : "CLIPBOARD EMPTY";
    if (action == 5 || action == 6 || action == 7 || action == 16 || action == 21 || action == 22) return nullptr;
    if (!clip) return "NO CLIP";
    if ((action == 14 || action == 15) && clip->muted) return "UNMUTE CLIP FIRST";
    if ((action == 0 || action == 8 || action == 9) && (cursor <= clip->start || cursor >= clip->start + clip->length))
        return "CURSOR MUST BE INSIDE CLIP";
    const u8* project = reinterpret_cast<const u8*>(platform::word(0x840a0524));
    if (action == 1 || action == 11 || action == 14 || action == 15) {
        if ((action == 14 || action == 15) && ui_track == 7) return "NO TRACK BELOW";
        const u32 target = action == 14 || action == 15 ? ui_track + 1 : ui_track;
        if (platform::arranger_clip_track_locked(project, ui_track, target)) return "INSTRUMENT LOCKED";
        if (action != 11 && *reinterpret_cast<const u32*>(project + 0xd7c + target * 0x53f44 + 0x53f40) >= 16)
            return "SOURCE POOL FULL";
    }
    return nullptr;
}
void menu_parameter_reset() {
    menu_tool = 0;
    menu_project = platform::word(0x840a0524);
    menu_generation = stock<u32()>(0x8d584224)();
    menu_cursor = cursor;
    menu_slot = static_cast<u32>(selected_slot);
    menu_preview = sdk_arranger_model();
    const u8* project = reinterpret_cast<const u8*>(menu_project);
    menu_source = menu_index == 22 ? (project && project[7] < 14 ? project[7] : 0) : selected_pattern;
}
void open_menu() {
    if (!view_active() || piano_view || stop_gesture.pending || sdk_arranger_bounce_busy() || bounce_choice.open) return;
    history.breakGroup(); stop_sequence.interrupt();
    menu_open = true; menu_index = selected_slot < 0 ? 16 : 0;
    menu_parameter_reset();
    announce("ACTIONS / SHORTCUT HELP");
}
bool menu_context_valid() {
    return view_active() && sdk_arranger_project_edit_ready() &&
        menu_project == platform::word(0x840a0524) && menu_generation == stock<u32()>(0x8d584224)();
}
void finish_menu_tool(bool apply) {
    if (!menu_tool) return;
    const u32 tool = menu_tool; menu_tool = 0;
    if (!apply || !menu_context_valid()) { announce("EDIT CANCELLED"); return; }
    if (tool == 22 || tool == 23) {
        begin_history();
        bool ok = false;
        if (tool == 22) {
            const i32 slot = platform::arranger_add_source_clip(sdk_arranger_model(),
                reinterpret_cast<const u8*>(menu_project), ui_track, menu_cursor, menu_source);
            ok = slot >= 0;
            if (ok) selected_slot = slot;
        } else {
            const auto result = platform::arranger_import_song(sdk_arranger_model(),
                reinterpret_cast<const u8*>(menu_project), menu_source, menu_cursor, menu_preview);
            ok = result == platform::SongImportResult::Ok;
            if (!ok) {
                history.cancel();
                announce(result == platform::SongImportResult::Empty ? "SONG EMPTY" :
                    result == platform::SongImportResult::NoRoom ? "COLLISION / TIMELINE FULL" :
                    result == platform::SongImportResult::CurrentSceneMuted ? "UNMUTE CURRENT SCENE FIRST" : "SONG STATE UNSUPPORTED");
                return;
            }
        }
        if (commit_edit(ok, tool == 22 ? "EXISTING CLIP ADDED" : "SONG INSTRUMENT CLIPS IMPORTED")) select_clip(false);
        return;
    }
    const arranger::Clip* changed = menu_preview.clip(menu_slot);
    const arranger::Clip* before = sdk_arranger_model().clip(menu_slot);
    if (!changed || !before) { announce("CLIP NO LONGER AVAILABLE"); return; }
    if (changed->track == before->track && changed->start == before->start && changed->length == before->length &&
        changed->offset == before->offset && changed->phase_remainder == before->phase_remainder) {
        announce("NO CHANGE"); return;
    }
    begin_history();
    const bool ok = tool == 12 ? platform::arranger_move_clip_track(sdk_arranger_model(),
        reinterpret_cast<u8*>(menu_project), menu_slot, changed->track) : sdk_arranger_model().replace(menu_slot, *changed);
    if (ok) {
        cursor = tool == 9 ? changed->start : menu_cursor;
        ui_track = changed->track; stock<void(u32, u32)>(0x8d585290)(ui_track, 0);
    }
    if (commit_edit(ok, tool == 9 ? "TRIMMED LEFT" : tool == 11 ? "SOURCE OFFSET UPDATED" : "MOVED TO TRACK"))
        select_clip(false);
}
void menu_tool_turn(i32 delta, bool fine) {
    if (!menu_context_valid()) { finish_menu_tool(false); return; }
    if (menu_tool == 22 || menu_tool == 23) {
        const u8* project = reinterpret_cast<const u8*>(menu_project);
        const u32 count = menu_tool == 23 ? 14 :
            *reinterpret_cast<const u32*>(project + 0xd7c + ui_track * 0x53f44 + 0x53f40);
        if (!count || count > 16) { announce("SOURCE POOL UNAVAILABLE"); return; }
        const i32 next = static_cast<i32>(menu_source) + delta;
        menu_source = next < 0 ? 0 : static_cast<u32>(next) >= count ? count - 1 : static_cast<u32>(next);
        return;
    }
    const arranger::Clip* clip = menu_preview.clip(menu_slot);
    if (!clip) { finish_menu_tool(false); return; }
    const u32 step = fine || fine_grid ? 1 : 480;
    bool ok = false;
    if (menu_tool == 9) {
        const u32 start = platform::arranger_shift_time(clip->start, delta, step);
        ok = platform::arranger_trim_clip_left(menu_preview, reinterpret_cast<u8*>(menu_project), menu_slot, start);
    } else if (menu_tool == 11) ok = menu_preview.slip(menu_slot, delta * static_cast<i32>(step));
    else if (menu_tool == 12) {
        const u32 target = delta > 0 ? (clip->track < 7 ? clip->track + 1 : 7) : (clip->track ? clip->track - 1 : 0);
        if (platform::arranger_clip_track_locked(reinterpret_cast<const u8*>(menu_project), ui_track, target)) {
            announce("INSTRUMENT LOCKED"); return;
        }
        ok = menu_preview.move(menu_slot, target, clip->start);
    }
    if (!ok) announce("LIMIT / COLLISION");
}
void run_menu_action(u32 initiating_key = 13) {
    const u32 action = menu_index;
    if (const char* reason = menu_unavailable(action)) { announce(reason); return; }
    menu_open = false;
    if (action == 23) { toggle_selected_mute(); return; }
    if (action == 21 || action == 22 || action == 10 || action == 11) {
        menu_tool = action + 1;
        finish_menu_tool(true);
        return;
    }
    if (action < 8) { timeline_key(14 + action, true); return; }
    if (action == 8) {
        if (!menu_context_valid()) { announce("EDIT CANCELLED"); return; }
        if (!platform::arranger_trim_clip_left(menu_preview, reinterpret_cast<u8*>(menu_project), menu_slot, cursor)) {
            announce("CURSOR MUST BE INSIDE CLIP"); return;
        }
        menu_tool = 9; finish_menu_tool(true); return;
    }
    if (action == 12) { open_edit(initiating_key); return; }
    if (action == 13) { open_piano(); return; }
    if (action == 14 || action == 15) {
        const u32 result = sdk_arranger_bounce_request(static_cast<u32>(selected_slot), action == 14 ? 2 : 3, false);
        if (result) announce(result == 1 ? "BOUNCE BUSY" : result == 3 ? "CLIP TOO SHORT / LONG" :
            result == 4 ? "PROJECT BUSY" : result == 5 ? "UNMUTE CLIP FIRST" : "FREE LANE BELOW REQUIRED / SHIFT SAMPLE TO REPLACE");
        return;
    }
    if (action == 16 || action == 17) { timeline_key(54, action == 17); return; }
    if (action == 19 || action == 20) { timeline_key(53, action == 19); return; }
    // Reuse the same trim/duplicate handlers without synthesizing a stock Stop.
    stop_gesture.begin();
    timeline_key(action == 9 ? 53 : 54, false);
    stop_gesture.modify(); stop_gesture.release();
}

// Own unit rectangles use the stock Q10 affine matrix, with bounded scales.
void rectangle(PathNode* node, u32 x, u32 y, u32 width, u32 height) {
    if (!node) return;
    const u32 at[6] = {0, 1, 3, 4, 6, 7};
    const i32 values[6] = {static_cast<i32>(width * 1024), 0, 0,
        static_cast<i32>(height * 1024), static_cast<i32>(x * 1024), static_cast<i32>(y * 1024)};
    set_words(&node->leaf, at, values);
}

void text_position(Leaf* node, u32 x, u32 y, bool visible) {
    const u32 at[6] = {0, 1, 3, 4, 6, 7};
    // Stock text geometry requires a nonsingular matrix. Zero scale at (0,0)
    // can leave glyph fragments over the title; park hidden leaves offscreen.
    const i32 values[6] = {1024, 0, 0, 1024,
        static_cast<i32>((visible ? x : 640) * 1024), static_cast<i32>((visible ? y : 320) * 1024)};
    set_words(node, at, values);
}

void clip_paint(Object* o, u32 index, bool muted) {
    PathNode* node = o->clips[index];
    const u32 fill = muted ? o->muted_fill : o->clip_fill;
    if (node && fill && node->fill != fill) { set_fill(node, fill); invalidate(node); }
}

void render_toast(Object* o);

bool render_piano(Object* o) {
    const u8* source = piano_source();
    if (!source || !piano.valid()) { piano_view = false; return false; }
    if (piano_pitch < piano_low) piano_low = piano_pitch;
    if (piano_pitch >= piano_low + piano_rows) piano_low = piano_pitch - piano_rows + 1;
    if (piano_time < piano_start || piano_time - piano_start >= piano_window)
        piano_start = piano_time / piano_window * piano_window;
    set_text(o->title, "PIANO ROLL");
    const char* names[] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
    for (u32 row = 0; row < 24; ++row) {
        const u32 y = platform::piano_row_y(row, piano_rows);
        const u32 height = platform::piano_row_y(row + 1, piano_rows) - y;
        const bool black = row < piano_rows && platform::piano_black_key(piano_low + piano_rows - 1 - row);
        rectangle(o->piano_rows[row], 2, y, black ? 472 : 0, black ? height : 0);
    }
    for (u32 label_index = 0; label_index < 8; ++label_index) {
        text_position(&o->lane_lines[label_index]->leaf, 0, 0, false);
        const u32 row = piano_rows <= 8 ? label_index : label_index * piano_rows / 8;
        if (row >= piano_rows) { text_position(o->lanes[label_index], 0, 0, false); continue; }
        const u32 pitch = piano_low + piano_rows - 1 - row;
        char label[12]; char* end = append(label, names[pitch % 12]);
        if (pitch < 12) append(end, "-1"); else number(end, pitch / 12 - 1);
        set_text(o->lanes[label_index], label);
        text_position(o->lanes[label_index], 3, platform::piano_row_y(row, piano_rows) + 12, true);
    }
    const u32 selected_row = piano_rows - 1 - (piano_pitch - piano_low);
    rectangle(o->lane_highlight, 2, platform::piano_row_y(selected_row, piano_rows), 32,
        platform::piano_row_y(selected_row + 1, piano_rows) - platform::piano_row_y(selected_row, piano_rows));
    rectangle(o->selection, 0, 0, 0, 0); rectangle(o->loop_band, 0, 0, 0, 0);
    const u32 spacing = platform::arranger_bar_spacing(piano_window);
    u32 offset = piano_start % spacing ? spacing - piano_start % spacing : 0;
    for (u32 i = 0; i < 17; ++i, offset += spacing)
        text_position(&o->bar_grid[i]->leaf, 36 + offset * 432 / piano_window, 27, offset < piano_window);
    u32 shown = 0, total = 0;
    for (u32 node = 0; node < platform::PianoEditor::capacity; ++node) {
        platform::PianoNote note;
        if (!piano.note(node, note) || note.pitch < piano_low || note.pitch >= piano_low + piano_rows) continue;
        if (note.position < -static_cast<i32>(piano.span()) || note.position >= static_cast<i32>(piano.span())) continue;
        const u32 duration = note.duration < piano.span() ? note.duration : piano.span();
        const i32 note_end = note.position + static_cast<i32>(duration);
        if (note_end <= static_cast<i32>(piano_start) || note.position >= static_cast<i32>(piano_start + piano_window)) continue;
        ++total; if (shown >= visible_clips) continue;
        const u32 left = note.position > static_cast<i32>(piano_start) ? static_cast<u32>(note.position) - piano_start : 0;
        const u32 end = static_cast<u32>(note_end) - piano_start;
        const u32 right = end < piano_window ? end : piano_window;
        const u32 x = 36 + left * 432 / piano_window;
        const u32 width = right * 432 / piano_window - left * 432 / piano_window;
        const u32 row = piano_rows - 1 - (note.pitch - piano_low);
        const u32 y = platform::piano_row_y(row, piano_rows) + 2;
        const u32 height = platform::piano_row_y(row + 1, piano_rows) - y - 1;
        clip_paint(o, shown, false);
        rectangle(o->clips[shown], x, y, width ? width : 1, height);
        if (static_cast<i32>(node) == piano_selected) rectangle(o->selection, x, y, width ? width : 1, height);
        text_position(o->clip_labels[shown], 0, 0, false); ++shown;
    }
    for (; shown < visible_clips; ++shown) {
        rectangle(o->clips[shown], 0, 0, 0, 0); text_position(o->clip_labels[shown], 0, 0, false);
    }
    rectangle(o->cursor, 36 + (piano_time - piano_start) * 432 / piano_window, 27, 1, 169);
    rectangle(o->playhead, 0, 0, 0, 0);
    const arranger::Clip* clip = sdk_arranger_model().clip(piano_slot);
    const u32 playback = sdk_arranger_playback_position();
    if (clip && source[0] && source[8] < 16 && playback >= clip->start && playback - clip->start < clip->length) {
        const u32 phase = platform::arranger_source_phase(*clip, source, playback) / 1680;
        if (phase >= piano_start && phase - piano_start < piano_window)
            rectangle(o->playhead, 36 + (phase - piano_start) * 432 / piano_window, 27, 2, 169);
    }
    char status[128]; char* end = append(status, "T"); end = number(end, piano_track + 1);
    end = append(end, " "); *end++ = static_cast<char>('A' + piano_pattern); *end = 0;
    end = append(end, " @"); end = number(end, piano_time);
    end = append(end, " N"); end = number(end, piano_pitch);
    end = append(end, " NOTES "); end = number(end, piano.count());
    end = append(end, " ROWS "); end = number(end, piano_rows);
    set_text(o->controls, platform::shift_held() ? "E1 MOVE / E2 PITCH ZOOM / E3 CURSOR / E4 VELOCITY" :
        "E1 CURSOR / E2 ZOOM / E3 NOTE UP-DOWN / E4 LENGTH");
    platform::PianoNote selected;
    if (piano_selected >= 0 && piano.note(static_cast<u32>(piano_selected), selected)) {
        end = append(end, " LEN "); end = number(end, selected.duration); end = append(end, " VEL ");
        end = number(end, selected.velocity); end = append(end, fine_grid ? " FINE" : " GRID");
    } else end = append(end, fine_grid ? " FINE / + ADD" : " GRID / + ADD");
    if (!total && piano.count()) append(end, " OUTSIDE VIEW");
    else if (total > visible_clips) { end = append(end, " SHOW64/"); number(end, total); }
    set_text(o->feedback, status);
    render_toast(o); o->shown_position = 0xffffffffu;
    draw(&o->base); return true;
}

void render_toast(Object* o) {
    char text[36]; u32 i = 0;
    if (ui_now() - message_at < 3000u) {
        while (message[i] && i < 32) { text[i] = message[i]; ++i; }
        if (message[i]) { text[29] = text[30] = text[31] = '.'; }
    }
    text[i] = 0; set_text(o->status, text);
}
void render_menu(Object* o) {
    rectangle(o->loop_band, 0, 0, 0, 0);
    rectangle(o->menu_background, 0, 22, 480, 174);
    const u32 first = menu_index < 7 ? 0 : menu_index - 6;
    rectangle(o->menu_highlight, 8, 28 + (menu_index - first) * 22, 464, 22);
    set_text(o->menu_selected, menu_labels[menu_index]);
    text_position(o->menu_selected, 16, 44 + (menu_index - first) * 22, true);
    for (u32 row = 0; row < 8; ++row) {
        const u32 action = first + row;
        const bool valid = row < 7 && action < menu_count;
        const bool disabled = valid && menu_unavailable(action);
        set_text(o->menu_rows[row], valid && !disabled && action != menu_index ? menu_labels[action] : "");
        set_text(o->menu_disabled[row], disabled && action != menu_index ? menu_labels[action] : "");
        text_position(o->menu_rows[row], 16, 44 + row * 22, row < 7);
        text_position(o->menu_disabled[row], 16, 44 + row * 22, row < 7);
    }
    char context[80]; char* end = append(context, "TRACK "); end = number(end, ui_track + 1);
    const arranger::Clip* clip = selected_slot < 0 ? nullptr : sdk_arranger_model().clip(static_cast<u32>(selected_slot));
    if (clip) { end = append(end, " / CLIP "); *end++ = 'A' + clip->pattern; *end = 0; }
    else end = append(end, " / GAP");
    end = append(end, " / "); end = number(end, menu_index + 1); end = append(end, "/"); end = number(end, menu_count);
    if (menu_index == 21 || menu_index == 22 || menu_index == 10 || menu_index == 11) {
        end = append(end, " / E2 ");
        if (menu_index == 21) { *end++ = 'A' + menu_source; *end = 0; }
        else if (menu_index == 22) number(end, menu_source + 1);
        else if (const arranger::Clip* value = menu_preview.clip(menu_slot))
            number(end, menu_index == 11 ? value->track + 1 : value->offset);
    }
    if (const char* reason = menu_unavailable(menu_index)) set_text(o->controls, reason);
    else set_text(o->controls, context);
    set_text(o->feedback, "E1 SELECT / E2 ADJUST / CLICK E1 OR E2 / ARRANGE CLOSE");
    render_toast(o); draw(&o->base);
}
void hide_menu(Object* o) {
    set_text(o->menu_selected, ""); text_position(o->menu_selected, 0, 0, false);
    rectangle(o->menu_background, 0, 0, 0, 0); rectangle(o->menu_highlight, 0, 0, 0, 0);
    for (u32 row = 0; row < 8; ++row) {
        set_text(o->menu_rows[row], ""); set_text(o->menu_disabled[row], "");
        text_position(o->menu_rows[row], 0, 0, false); text_position(o->menu_disabled[row], 0, 0, false);
    }
}

struct Renderer {
    typedef ::Object Object;
    static void init(Object* o) {
        clear(o);
        if (!o->base.svg) return;
        for (u32 i = 0; i < visible_clips; ++i) {
            char id[16] = "clip";
            decimal(id + 4, i);
            o->clips[i] = path_node(find(&o->base, id));
            char label_id[16] = "cliplabel";
            decimal(label_id + 9, i);
            o->clip_labels[i] = text_node(find(&o->base, label_id));
            if (!o->clips[i] || !o->clip_labels[i]) return;
        }
        for (u32 i = 0; i < 17; ++i) {
            char id[16] = "bargrid"; decimal(id + 7, i);
            o->bar_grid[i] = path_node(find(&o->base, id));
            if (!o->bar_grid[i]) return;
        }
        o->lane_highlight = path_node(find(&o->base, "lanehighlight"));
        o->selection = path_node(find(&o->base, "selection"));
        o->cursor = path_node(find(&o->base, "cursor"));
        o->playhead = path_node(find(&o->base, "playhead"));
        o->loop_band = path_node(find(&o->base, "loopband"));
        o->menu_background = path_node(find(&o->base, "menubackground"));
        o->menu_highlight = path_node(find(&o->base, "menuhighlight"));
        o->menu_selected = text_node(find(&o->base, "menuselected"));
        if (!o->menu_background || !o->menu_highlight || !o->menu_selected) return;
        for (u32 i = 0; i < 8; ++i) {
            char id[20] = "menurow"; decimal(id + 7, i);
            o->menu_rows[i] = text_node(find(&o->base, id));
            char dim[20] = "menudisabled"; decimal(dim + 12, i);
            o->menu_disabled[i] = text_node(find(&o->base, dim));
            if (!o->menu_rows[i] || !o->menu_disabled[i]) return;
        }
        for (u32 i = 0; i < 24; ++i) {
            char id[16] = "pianokey"; decimal(id + 8, i);
            o->piano_rows[i] = path_node(find(&o->base, id));
            if (!o->piano_rows[i]) return;
        }
        for (u32 i = 0; i < 8; ++i) {
            char id[16] = "laneline"; decimal(id + 8, i);
            o->lane_lines[i] = path_node(find(&o->base, id));
            if (!o->lane_lines[i]) return;
        }
        const PathNode* muted_donor = path_node(find(&o->base, "mutedclipstyle"));
        if (!muted_donor || !o->base.svg || !ordered(o->base.svg->fills)) return;
        o->clip_fill = o->clips[0]->fill; o->muted_fill = muted_donor->fill;
        const u32 fill_count = (o->base.svg->fills.end - o->base.svg->fills.begin) / 4;
        if (!o->clip_fill || !o->muted_fill || o->clip_fill > fill_count || o->muted_fill > fill_count) return;
        o->title = text_node(find(&o->base, "title"));
        for (u32 i = 0; i < 8; ++i) {
            char id[12] = "lane"; decimal(id + 4, i);
            o->lanes[i] = text_node(find(&o->base, id));
            if (!o->lanes[i]) return;
        }
        o->status = text_node(find(&o->base, "position"));
        o->controls = text_node(find(&o->base, "controls"));
        o->feedback = text_node(find(&o->base, "feedback"));
        o->shown_position = 0xffffffffu;
        o->bound = o->title && o->lane_highlight && o->selection && o->cursor && o->playhead && o->loop_band && o->status && o->controls && o->feedback;
    }
    static void frame(Object* o, i32) {
        if (!active || !o->bound) return;
        if (pending_add.pending && !pending_add.draw_requested) {
            // Keep the cached scene until publication and selection complete.
            set_text(o->feedback, add_progress()); set_text(o->status, add_progress());
            draw(&o->base); pending_add.acknowledge(ui_now()); return;
        }
        if (menu_open) { render_menu(o); return; }
        hide_menu(o);
        if (piano_view && render_piano(o)) return;
        set_text(o->title, "Linear by Veff");
        for (u32 i = 0; i < 24; ++i) rectangle(o->piano_rows[i], 0, 0, 0, 0);
        for (u32 i = 0; i < 8; ++i) {
            char label[4]; decimal(label, i + 1); set_text(o->lanes[i], label);
            text_position(o->lanes[i], 3, 43 + i * 21, true);
            text_position(&o->lane_lines[i]->leaf, 0, 0, true);
        }
        const arranger::Model& model = menu_tool ? menu_preview : sdk_arranger_model();
        const u32 position = sdk_arranger_playback_position();
        if (cursor < view_start || cursor - view_start >= window_ticks)
            view_start = cursor / window_ticks * window_ticks;
        const u32 start = view_start;
        rectangle(o->lane_highlight, 6, 28 + ui_track * 21, 468, 20);
        rectangle(o->selection, 0, 0, 0, 0);
        const u32 bar_spacing = platform::arranger_bar_spacing(window_ticks);
        const u32 remainder = start % bar_spacing;
        u32 bar_offset = remainder ? bar_spacing - remainder : 0;
        for (u32 i = 0; i < 17; ++i, bar_offset += bar_spacing) {
            const bool visible = bar_offset < window_ticks && bar_offset <= 0xffffffffu - start;
            text_position(&o->bar_grid[i]->leaf, visible ? 36 + bar_offset * 432 / window_ticks : 0, 27, visible);
        }
        u32 shown = 0, total = 0;
        for (u32 slot = 0; slot < arranger::Model::capacity; ++slot) {
            const arranger::Clip* c = model.clip(slot);
            if (!c || c->start + c->length <= start) continue;
            const u32 left = c->start > start ? c->start - start : 0;
            if (left >= window_ticks) continue;
            ++total;
            if (shown == visible_clips) continue;
            const u32 end = c->start + c->length - start;
            const u32 right = end < window_ticks ? end : window_ticks;
            const u32 x = left * 432 / window_ticks;
            const u32 width = right * 432 / window_ticks - x;
            if (static_cast<i32>(slot) == selected_slot)
                rectangle(o->selection, 35 + x, 30 + c->track * 21, (width ? width : 1) + 2, 17);
            clip_paint(o, shown, c->muted);
            rectangle(o->clips[shown], 36 + x, 31 + c->track * 21, width ? width : 1, 15);
            const char label[2] = {static_cast<char>('A' + c->pattern), 0};
            set_text(o->clip_labels[shown], label);
            text_position(o->clip_labels[shown], 36 + x + (width >= 10 ? width - 9 : 0),
                          42 + c->track * 21, width >= 10);
            ++shown;
        }
        for (; shown < visible_clips; ++shown) {
            rectangle(o->clips[shown], 0, 0, 0, 0);
            text_position(o->clip_labels[shown], 0, 0, false);
        }
        rectangle(o->cursor, 36 + (cursor - start) * 432 / window_ticks, 27, 1, 169);
        if (position >= start && position - start < window_ticks)
            rectangle(o->playhead, 36 + (position - start) * 432 / window_ticks, 27, 2, 169);
        else rectangle(o->playhead, 0, 0, 0, 0);
        if (model.loopEnabled() && model.loopEnd() > start &&
            (model.loopStart() <= start || model.loopStart() - start < window_ticks)) {
            const u32 left = model.loopStart() > start ? model.loopStart() - start : 0;
            const u32 right = model.loopEnd() - start < window_ticks ? model.loopEnd() - start : window_ticks;
            rectangle(o->loop_band, 36 + left * 432 / window_ticks, 24,
                      (right - left) * 432 / window_ticks, 2);
        } else rectangle(o->loop_band, 0, 0, 0, 0);
        render_toast(o);
        char info[128];
        const u32 recording = sdk_arranger_record_status();
        set_text(o->controls, fine_grid ? "E1 TIME (FINE) / E2 ZOOM / E3 MOVE / E4 LENGTH" :
            "E1 TIME (GRID) / E2 ZOOM / E3 MOVE / E4 LENGTH");
        set_text(o->feedback, "+ ADD / - DELETE / E4 CLICK EDIT / ARRANGE: ACTIONS");
        if (platform::shift_held()) {
            set_text(o->controls, "1 SPLIT / 2 UNIQUE / 3 UNDO / 4 REDO");
            set_text(o->feedback, "5 LOOP CLIP / 6 LOOP ON-OFF / 7 START / 8 END");
        }
        if (stop_gesture.pending) {
            set_text(o->controls, platform::shift_held() ? "E3 SLIP / E4 FINE LEFT TRIM" : "E3 TRACK / E4 LEFT TRIM");
            set_text(o->feedback, "+ DUPLICATE / - TRIM RIGHT / RELEASE STOP TO FINISH");
        }
        if (recording) set_text(o->feedback, recording == 1 ? "REC ARMED / PLAY TO RECORD" :
            recording == 2 ? "RECORDING INTO PLAYING CLIP" : "REC TARGET UNAVAILABLE: CLIP / GAP / QUEUE");
        if (menu_tool) {
            const arranger::Clip* preview = menu_preview.clip(menu_slot);
            char tool_label[80]; char* at = append(tool_label, menu_tool == 9 ? "TRIM LEFT " :
                menu_tool == 11 ? "SOURCE OFFSET " : menu_tool == 22 ? "EXISTING CLIP " :
                menu_tool == 23 ? "IMPORT SONG " : "MOVE TO TRACK ");
            if (menu_tool == 22) { *at++ = 'A' + menu_source; *at = 0; }
            else if (menu_tool == 23) at = number(at, menu_source + 1);
            else if (preview) at = number(at, menu_tool == 9 ? preview->start : menu_tool == 11 ? preview->offset : preview->track + 1);
            append(at, menu_tool >= 22 ? " / E2 SELECT" : fine_grid ? " / E2 ADJUST (FINE)" : " / E2 ADJUST (GRID)");
            set_text(o->controls, tool_label);
            set_text(o->feedback, "CLICK E1 OR E2 APPLY / ARRANGE CANCEL");
        }
        char* out = info; *out = 0;
        if (total > visible_clips) { out = append(out, "VISIBLE LIMIT: 64 / "); number(out, total); }
        const u32 bounce_status = sdk_arranger_bounce_status();
        if (sdk_arranger_bounce_busy()) {
            out = append(info, "BOUNCE "); out = number(out, sdk_arranger_bounce_progress() / 10);
            out = append(out, "% - STOP CANCEL");
            set_text(o->controls, bounce_status == 2 ? "RENDERING CLIP" : bounce_status == 7 ? "ASSIGNING SAMPLE BELOW" : "PREPARING / SAVING SAMPLE");
        } else if (bounce_choice.open) {
            set_text(o->controls, bounce_choice.engine == 2 ? "E1 SAMPLER / E2 FREE OR REPLACE BELOW" : "E1 DRUM / E2 FREE OR REPLACE BELOW");
            out = append(info, bounce_choice.confirmed ? "REPLACE BELOW? E4 CONFIRM / STOP CANCEL" :
                bounce_choice.replace ? "REPLACE BELOW - E4 NEXT / STOP CANCEL" : "FREE LANE BELOW - E4 BOUNCE / STOP CANCEL");
        }
        if (bounce_status == 6 && !sdk_arranger_bounce_busy() && !bounce_choice.open)
            append(info, bounce_failure_text());
        if (*info) set_text(o->feedback, info);
        if (pending_add.pending) set_text(o->feedback, add_progress());
        else if (sdk_arranger_project_status() == 1) set_text(o->feedback, "RESTORE FAILED / SAVE BLOCKED");
        else if (sdk_arranger_project_status() == 2) set_text(o->feedback, "SAVE FAILED / CHANGES NOT SAVED");
        else if (sdk_arranger_project_status() == 3) set_text(o->feedback, "RENAME FAILED / SAVE BLOCKED");
        draw(&o->base);
        if (pending_add.pending && pending_add.draw_requested) {
            if (pending_add.record_after) pending_add.draw_complete = true;
            else { pending_add.cancel(); announce("ADDED EMPTY CLIP"); }
        }
    }
};
const VTable<Renderer> vtable = {destroy<Object>, destroy<Object>,
    reinterpret_cast<const void*>(0x8d51fcd4), Renderer::frame, Renderer::init,
    reinterpret_cast<const void*>(0x8c0867f0)};
const char asset[] = "open-xy-arranger.svg";

bool registered() {
    return page_id >= 82 && page_id < 128 &&
        reinterpret_cast<Base* volatile*>(slots_address())[page_id] == &screen_state.base &&
        screen_state.base.vtable == &vtable && screen_state.base.loader;
}

// Shift+Arrange enters the same controller as plain Arrange. Stock's shifted
// handler selects Song/controller41, whose page getter is intentionally stock.
bool enter_arranger_menu() {
    if (!sdk_arranger_project_linear() || editing || !registered()) return false;
    stock<void(u32)>(0x8c094178)(40);
    piano_view = false;
    sync_selected_lane(true);
    open_menu();
    stock<void()>(0x8c093f50)();
    return true;
}

bool consume_play(bool shifted) {
    if (!shifted && held(50) && view_active() && !piano_view && !menu_open && !menu_tool) {
        select_clip(false);
        if (selected_slot < 0) {
            // Capture REC before its release; never start until the staged add succeeds.
            add_clip(true); swallowed_release[51] = true; return true;
        }
    }
    if (!sdk_arranger_request_start(cursor, shifted, held(50))) {
        announce("TRANSPORT BUSY"); swallowed_release[51] = true; return true;
    }
    // Audio owns both stopped starts and playing restarts. Forwarding stock
    // Play as well would toggle or start twice.
    swallowed_release[51] = true;
    return true;
}

void press(const void* raw, bool shifted) {
    if (pending_add.pending) return; // Stock encoder release can synthesize a click.
    const Event& event = *static_cast<const Event*>(raw);
    const u32 key = event.words[0];
    // Encoder release synthesizes a press before its matching release. A
    // rejected turn must not become an arranger action after leaving EDIT.
    if (key < 72 && swallowed_release[key]) return;
    if (editing) {
        if (!edit_valid()) {
            const bool was_forwarded = key < 72 && edit_pressed[key];
            close_edit(true);
            if (platform::arranger_edit_swallow_release(key, was_forwarded)) swallowed_release[key] = true;
            return;
        }
        if (key == 4) {
            swallowed_release[key] = true;
            exit_edit_pending = true;
            if (!any_edit_press()) close_edit(true);
            return;
        }
        // Basic note/step editing only. Pattern management and unheld engine
        // controls have independent stock writers outside the scoped block.
        if (!platform::arranger_edit_key_allowed(key, step_held(), exit_edit_pending)) {
            if (platform::arranger_edit_swallow_release(key, key < 72 && edit_pressed[key]))
                swallowed_release[key] = true;
            return;
        }
        if (key == 51 && consume_play(shifted)) return;
        if (key < 72) edit_pressed[key] = true;
        forward(raw, shifted);
        return;
    }
    if (menu_open || menu_tool) {
        if (key == 10 || key == 11) { if (menu_tool) finish_menu_tool(true); else run_menu_action(key); }
        return;
    }
    if (key == 4 && shifted && enter_arranger_menu()) return;
    if (view_active() && key >= 10 && key <= 13) {
        if (key == 10 && stop_gesture.pending) { stop_gesture.modify(); toggle_selected_mute(); return; }
        if (key == 10) { fine_grid = !fine_grid; announce(fine_grid ? "FINE: 1 TICK" : "GRID: 1 STEP"); }
        if (key == 13) { history.breakGroup(); if (shifted) open_piano(); else if (!piano_view) open_edit(); }
        return;
    }
    if (view_active() && key == 51) {
        if (consume_play(shifted)) return;
    }
    forward(raw, shifted);
}
}

void sdk_arranger_screen_startup() {
    if (!stock_gui_role() || page_id || screen_state.base.vtable) return;
    Base** slots = stock<Base**()>(0x8d52077c)();
    if (reinterpret_cast<platform::Address>(slots) != slots_address()) return;
    for (u32 page = 127; page >= 82; --page) {
        if (slots[page]) continue;
        base_construct(&screen_state.base, page, asset);
        screen_state.base.vtable = &vtable;
        if (slots[page] == &screen_state.base && screen_state.base.loader) page_id = page;
        return;
    }
}

int sdk_arranger_screen_owned(u32 page) { return page == page_id && registered(); }
u32 sdk_arranger_page(void*) {
    if (!sdk_arranger_project_linear() || editing || !registered()) return 47;
    // Stock can query the destination page before changing its top controller.
    sync_selected_lane(true);
    return page_id;
}
u32 sdk_arranger_active() { return active ? 1 : 0; }

void sdk_arranger_plain(const void* event) { press(event, false); }
void sdk_arranger_shifted(const void* event) { press(event, true); }
void sdk_arranger_raw_event(const void* raw) {
    sync_selected_lane();
    const Event& event = *static_cast<const Event*>(raw);
    const u32 key = event.words[0];
    const bool down = event.words[1] != 0;
    BarTimingEvent bar_event(key, down);
    if (pending_add.pending) {
        if (key == 52 && down && !held(key)) {
            raw_edit_gate.consume(key, true, true, false, false);
            cancel_pending_add("ADD / RECORD CANCELLED"); return;
        }
        // New presses stay out of stock held bookkeeping. Release keys already
        // forwarded before the operation, including audition notes and +.
        if (key < 72 && down && !held(key)) raw_edit_gate.consume(key, true, true, false, false);
        else if (!down && key < 72 && raw_edit_gate.blocked[key]) raw_edit_gate.consume(key, false, false, true, false);
        else if (!down) stock_arranger_raw_event(raw);
        return;
    }
    // Consume new modal presses before stock held bookkeeping. Releases of
    // keys already owned by stock still flow through, avoiding stuck notes.
    if (key < 72 && raw_edit_gate.blocked[key]) {
        raw_edit_gate.consume(key, down, false, true, false); return;
    }
    if (key == 4 && arrange_owned) {
        if (!down) {
            if (menu_gesture.poll(ui_now())) open_menu();
            const bool tap = menu_gesture.release(ui_now());
            arrange_owned = false;
            // Linear owns both tap and hold. Replaying a tap into stock would
            // toggle the native red track/Song view instead of opening actions.
            if (tap) open_menu();
        }
        return;
    }
    if (sdk_arranger_bounce_busy() || bounce_choice.open) {
        if (down && !held(key)) {
            if (key == 52) {
                sdk_arranger_bounce_cancel(); bounce_choice.open = bounce_choice.confirmed = false;
                announce("BOUNCE CANCELLED");
            } else if (bounce_choice.open && key == 13) {
                if (bounce_choice.accept()) {
                    const u32 result = selected_slot < 0 ? 2 : sdk_arranger_bounce_request(
                        static_cast<u32>(selected_slot), bounce_choice.engine, bounce_choice.replace);
                    if (!result) { bounce_choice.open = bounce_choice.confirmed = false; announce("BOUNCE STARTING"); }
                    else {
                        bounce_choice.cancel();
                        announce(result == 1 ? "BOUNCE BUSY" : result == 3 ? "CLIP TOO SHORT / LONG" :
                            result == 4 ? "PROJECT BUSY" : result == 5 ? "UNMUTE CLIP FIRST" : "LANE BELOW OCCUPIED / LOCKED / FULL");
                    }
                }
            }
            if (key < 72) raw_edit_gate.consume(key, true, true, false, false);
        } else if (!down) stock_arranger_raw_event(raw);
        return;
    }
    if (menu_open || menu_tool) {
        if (down && key < 72 && !held(key)) {
            if (key == 4 || (key == 52 && menu_tool)) {
                menu_open = false; finish_menu_tool(false);
                raw_edit_gate.consume(key, true, true, false, false); return;
            }
            if (key == 10 || key == 11) {
                if (menu_tool) finish_menu_tool(true);
                else run_menu_action(key);
                raw_edit_gate.consume(key, true, true, false, false); return;
            }
            // Existing shortcuts retain their handlers. Unmodified track keys
            // can change context; close the panel before forwarding the action.
            if (!menu_tool && ((key >= 14 && key <= 21) || key == 53 || key == 54 ||
                key == 23 || key == 51 || key == 50 || key == 52)) menu_open = false;
            else if (key != 55) {
                raw_edit_gate.consume(key, true, true, false, false); return;
            }
        }
        if (menu_open || menu_tool) { stock_arranger_raw_event(raw); return; }
    }
    if (key == 4 && down && held(55) && !held(key) && enter_arranger_menu()) {
        // Neither half enters stock held bookkeeping; Shift retains its own
        // original press/release ownership. Repeats are consumed by this gate.
        raw_edit_gate.consume(key, true, true, false, false);
        return;
    }
    if (key == 4 && down && view_active() && !piano_view && !held(key)) {
        arrange_owned = true; menu_gesture.press(ui_now()); return;
    }
    if (piano_view && !view_active()) piano_view = false;
    if (piano_view && down && (key == 4 || key == 50 || key == 53 || key == 54 || (key >= 14 && key <= 21))) {
        if (held(key)) return;
        piano_key(key, held(55));
        raw_edit_gate.consume(key, true, true, false, false);
        return;
    }
    if (down && key == 23 && held(55) && view_active() && !held(23)) {
        bool clean = !stop_gesture.pending;
        for (u32 other = 0; other < 72; ++other) if (other != 55 && held(other)) clean = false;
        if (clean && selected_slot >= 0 && sdk_arranger_project_edit_ready()) {
            piano_view = false;
            bounce_choice.begin();
        } else announce("SELECT CLIP / RELEASE OTHER KEYS");
        raw_edit_gate.consume(key, true, true, false, false);
        return;
    }
    if (down) history.breakGroup();
    if (down && key != 52 && key != 55) stop_sequence.interrupt();
    if (down) resizing_slot = -1;
    if (key == 52 && stop_gesture.pending) {
        if (!down) {
            if (stop_gesture.release()) {
                stock_arranger_raw_event(&pending_stop);
                stock_arranger_raw_event(raw);
                sdk_arranger_project_poll();
                if (active && sdk_arranger_project_edit_ready()) {
                    const u32 target = stop_sequence.target(cursor);
                    if (sdk_arranger_request_seek(target)) {
                        cursor = target; sdk_arranger_model().seek(target);
                        select_clip(false);
                        commit_edit(true, target ? "STOPPED AT CURSOR" : "STOPPED AT START");
                    }
                }
            }
        }
        return;
    }
    if (key == 52 && down && view_active() && !held(key)) {
        pending_stop = event; stop_gesture.begin();
        return;
    }
    if (key < 72 && raw_edit_gate.blocked[key]) {
        raw_edit_gate.consume(key, down, false, true, false);
        return;
    }
    if (down && key == 50 && (view_active() || editing)) {
        // Arm before stock inserts the held key. Shift+REC enters a different
        // recorder mode and has no validated linear capture contract.
        const bool ready = !held(55) && !platform::byte(0x860c072c) && !platform::byte(0x860c072d) &&
            sdk_arranger_project_edit_ready() && (!editing || (!exit_edit_pending && edit_valid())) &&
            sdk_arranger_request_record(editing ? edit_track : ui_track,
                                        editing ? edit_pattern : 0xffffffffu);
        if (!ready) {
            announce("REC UNAVAILABLE / RELEASE SHIFT");
            raw_edit_gate.consume(key, true, true, false, held(key));
            return;
        }
    }
    if (down && key == 10 && view_active() && stop_gesture.pending && !held(key)) {
        stop_gesture.modify();
        toggle_selected_mute();
        raw_edit_gate.consume(key, true, true, false, false);
        return;
    }
    if (down && view_active() && ((key >= 14 && key <= 21) || key == 53 || key == 54)) {
        // A key pressed in another view still owns its stock release.
        if (held(key)) return;
        if (key == 53 || key == 54) stop_gesture.modify();
        timeline_key(key, held(55));
        raw_edit_gate.consume(key, true, true, false, false);
        return;
    }
    const bool was_editing = editing;
    bool allowed = true;
    if (editing && down) {
        if (!edit_valid()) {
            allowed = false;
            close_edit(true);
        } else {
            allowed = key == 4 || platform::arranger_edit_key_allowed(key, step_held(), exit_edit_pending);
        }
    }
    if (raw_edit_gate.consume(key, down, was_editing, allowed, key < 72 && held(key))) return;
    stock_arranger_raw_event(raw);
}
void sdk_arranger_release(u32 key) {
    if (key < 72 && swallowed_release[key]) { swallowed_release[key] = false; return; }
    if (editing) {
        if (key < 72) edit_pressed[key] = false;
        // Releases always reach stock to clean audition notes, even after a
        // project change invalidates the binding. Invalid new presses do not.
        stock_arranger_release(key);
        if (!edit_valid() || (exit_edit_pending && !any_edit_press())) close_edit(true);
        return;
    }
    if (view_active() && key >= 10 && key <= 13) return;
    stock_arranger_release(key);
}

void sdk_arranger_encoder(void* controller, u32 encoder, const void* raw) {
    sync_selected_lane();
    if (pending_add.pending) return;
    if (!active) { stock<void(void*, u32, const void*)>(0x8c095ab8)(controller, encoder, raw); return; }
    const i32 delta = static_cast<i32>(static_cast<const Event*>(raw)->words[1]);
    const i32 bounded = delta < -128 ? -128 : delta > 128 ? 128 : delta;
    const u32 index = encoder % 4;
    if (!bounded) return;
    if (piano_view) { piano_encoder(encoder, bounded); return; }
    const u32 step = (fine_grid || (encoder >= 4 && index != 0)) ? 1 : 480;
    if (bounded) stop_sequence.interrupt();
    if (index != 3) resizing_slot = -1;
    arranger::Model& model = sdk_arranger_model();
    if (index < 2 && bounded) history.breakGroup();
    if (index == 0) {
        cursor = encoder >= 4 ? platform::arranger_boundary(model, ui_track, cursor, bounded) :
            platform::arranger_cursor_time(cursor, bounded, step);
        select_clip();
    } else if (index == 1) {
        if (cursor < view_start || cursor - view_start >= window_ticks)
            view_start = cursor / window_ticks * window_ticks;
        const i32 zoom_step = bounded > 0 ? 1 : bounded < 0 ? -1 : 0;
        const u32 next = platform::arranger_zoom(window_ticks, zoom_step);
        const u32 relative = cursor >= view_start ? cursor - view_start : 0;
        const u32 scaled = platform::arranger_zoom_anchor(relative, window_ticks, next);
        view_start = cursor > scaled ? cursor - scaled : 0;
        window_ticks = next;
        announce("ZOOM");
    } else {
        sdk_arranger_project_poll();
        if (!sdk_arranger_project_edit_ready() || !view_active()) { announce("PROJECT BUSY"); return; }
        const arranger::Clip* selected = selected_slot < 0 ? nullptr : model.clip(static_cast<u32>(selected_slot));
        if (!selected) { announce("SELECT A CLIP FIRST"); return; }
        if (stop_gesture.pending) {
            if (!bounded) return;
            stop_gesture.modify();
            const u32 slot = static_cast<u32>(selected_slot);
            const bool slip = index == 2 && platform::shift_held();
            begin_history(slot * 8u + (index == 3 ? 3u : slip ? 4u : 5u));
            bool changed;
            if (index == 3) {
                const u32 start = platform::arranger_shift_time(selected->start, bounded, step);
                changed = platform::arranger_trim_clip_left(model, reinterpret_cast<u8*>(platform::word(0x840a0524)), slot, start);
                if (changed) cursor = start;
            } else if (slip) {
                changed = model.slip(slot, bounded * static_cast<i32>(step));
            } else {
                const u32 target = bounded > 0 ? (ui_track < 7 ? ui_track + 1 : 7) : (ui_track ? ui_track - 1 : 0);
                if (platform::arranger_clip_track_locked(reinterpret_cast<const u8*>(platform::word(0x840a0524)), ui_track, target)) {
                    history.cancel(); announce("UNLOCK SOURCE / DESTINATION INSTRUMENT"); return;
                }
                changed = target != ui_track && platform::arranger_move_clip_track(model,
                    reinterpret_cast<u8*>(platform::word(0x840a0524)), slot, target);
                if (changed) { ui_track = target; stock<void(u32, u32)>(0x8d585290)(ui_track, 0); }
            }
            if (commit_edit(changed, index == 3 ? "TRIMMED LEFT" : slip ? "SOURCE OFFSET" : "MOVED TO TRACK"))
                select_clip(false);
        } else if (index == 2) {
            begin_history(1u + static_cast<u32>(selected_slot) * 8u);
            const u32 start = platform::arranger_shift_time(selected->start, bounded, step);
            if (commit_edit(model.move(static_cast<u32>(selected_slot), selected->track, start), "MOVED")) cursor = start;
        } else {
            if (!bounded) return;
            begin_history(2u + static_cast<u32>(selected_slot) * 8u);
            if (resizing_slot != selected_slot) {
                if (!sdk_arranger_request_seek(selected->start)) { history.cancel(); announce("SEEK UNAVAILABLE"); return; }
                cursor = selected->start; model.seek(cursor); resizing_slot = selected_slot;
            }
            const u8* source = platform::arranger_pattern_source(reinterpret_cast<const u8*>(platform::word(0x840a0524)),
                selected->track, selected->pattern, false);
            if (!source || !source[0] || source[8] >= 16) { history.cancel(); announce("SOURCE UNAVAILABLE"); return; }
            const u32 source_step = platform::arranger_step_ticks(source[8]);
            const u32 maximum = platform::arranger_clip_limit(model, static_cast<u32>(selected_slot), source[8],
                source[0] > 64 ? 64 : source[0]);
            const u32 minimum = source_step < maximum ? source_step : maximum;
            const u32 increment = (fine_grid || encoder >= 4) ? 1 : source_step;
            u32 length = platform::arranger_shift_time(selected->length, bounded, increment);
            if (length < minimum) length = minimum;
            if (length > maximum) length = maximum;
            if (commit_edit(model.resize(static_cast<u32>(selected_slot), length), "LENGTH UPDATED"))
                select_clip(false);
        }
    }
}

void sdk_arranger_encoder_dispatch(u32 encoder, const void* event) {
    BarTimingEvent bar_event(0, false);
    if (pending_add.pending) return;
    if (sdk_arranger_bounce_busy()) return;
    if (menu_open || menu_tool) {
        const i32 raw_delta = static_cast<i32>(static_cast<const Event*>(event)->words[1]);
        const i32 delta = raw_delta < -128 ? -128 : raw_delta > 128 ? 128 : raw_delta;
        if (!delta) return;
        if (menu_tool) { if (encoder % 4 == 1) menu_tool_turn(delta, encoder >= 4 || platform::shift_held()); }
        else if (encoder % 4 == 0) {
            const u32 previous = menu_index;
            if (delta > 0 && menu_index + 1 < menu_count) ++menu_index;
            if (delta < 0 && menu_index) --menu_index;
            if (menu_index != previous) menu_parameter_reset();
        } else if (encoder % 4 == 1 && (menu_index == 10 || menu_index == 11 || menu_index == 21 || menu_index == 22)) {
            menu_tool = menu_index + 1;
            menu_tool_turn(delta, encoder >= 4 || platform::shift_held());
            menu_tool = 0;
        } else if (encoder % 4 >= 2 && (stop_gesture.pending || platform::shift_held())) {
            menu_open = false; stock_arranger_encoder_dispatch(encoder, event);
        }
        return;
    }
    if (bounce_choice.open) {
        const i32 delta = static_cast<i32>(static_cast<const Event*>(event)->words[1]);
        bounce_choice.turn(encoder, delta);
        return;
    }
    if (editing) {
        if (!edit_valid()) {
            const u32 key = 10 + encoder % 4;
            if (held(key)) swallowed_release[key] = true;
            close_edit(true);
            return;
        }
        if (!step_held() || exit_edit_pending) return;
    }
    stock_arranger_encoder_dispatch(encoder, event);
}

const u8* sdk_arranger_edit_source(u32 track, bool instrument) {
    if (const u8* bounce = sdk_arranger_bounce_destination_source(track)) return bounce;
    if (bar_binding_valid() && track == bar_track) return platform::arranger_pattern_source(
        reinterpret_cast<const u8*>(bar_project), track, bar_pattern, instrument);
    return edit_valid() && track == edit_track ? platform::arranger_pattern_source(
        reinterpret_cast<const u8*>(edit_project), track, edit_pattern, instrument) : nullptr;
}

bool sdk_arranger_record_target(u32 track, u32* pattern) {
    if (!pattern || !active || !sdk_arranger_project_edit_ready()) return false;
    if (editing) {
        if (!edit_valid() || track != edit_track) return false;
        *pattern = edit_pattern;
        return true;
    }
    if (!view_active() || track != ui_track) return false;
    *pattern = 0xffffffffu;
    return true;
}

u32 sdk_arranger_edit_index(const u8* address) {
    const i32 bounce = sdk_arranger_bounce_destination_index(address);
    if (bounce >= 0) return static_cast<u32>(bounce);
    const u32 original = *address;
    if (bar_binding_valid()) {
        const u8* project = reinterpret_cast<const u8*>(bar_project);
        const u8* expected = project + 0x98 + 33u * project[6] + bar_track;
        if (address == expected) return bar_pattern;
    }
    if (!edit_valid()) return original;
    const u8* project = reinterpret_cast<const u8*>(edit_project);
    const u8* expected = project + 0x98 + 33u * project[6] + edit_track;
    return address == expected ? edit_pattern : original;
}

void sdk_arranger_poll() {
    stock_arranger_poll();
    if (pending_add.pending) { process_pending_add(); return; }
    sdk_arranger_project_poll();
    reconcile_source_timing();
    sync_selected_lane();
    sdk_arranger_publish_poll();
    sdk_arranger_bounce_poll();
    if (!view_active() || piano_view || sdk_arranger_bounce_busy() || bounce_choice.open) {
        menu_open = false; menu_tool = 0; menu_gesture.cancel();
    } else if (arrange_owned && menu_gesture.poll(ui_now())) open_menu();
    if (menu_tool && !menu_context_valid()) finish_menu_tool(false);
    if (piano_view && (!view_active() || !piano_source())) piano_view = false;
    const u32 status = sdk_arranger_bounce_status();
    if (status != bounce_last_status) {
        bounce_last_status = status;
        if (status == 4) { history.reset(); announce("BOUNCE SAVED"); select_clip(false); }
        else if (status == 5) announce("BOUNCE CANCELLED");
        else if (status == 6) announce(bounce_failure_text());
    }

}

void sdk_arranger_project_restored(bool enabled, u32 track) {
    timing_project = 0; bar_binding = false;
    pending_add_uncommitted = false; // Lifecycle already replaced the model; never restore old scratch.
    pending_add.cancel();
    bounce_choice.open = bounce_choice.confirmed = false;
    const bool was_editing = editing;
    editing = exit_edit_pending = false;
    for (u32 key = 0; key < 72; ++key) edit_pressed[key] = false;
    active = enabled;
    ui_track = track < 8 ? track : 0;
    cursor = sdk_arranger_model().position();
    selected_slot = -1;
    selected_pattern = 0;
    clipboard.valid = false;
    history.reset(); piano_view = false; piano.reset();
    menu_open = false; menu_tool = 0; menu_gesture.cancel();
    window_ticks = 15360;
    view_start = 0;
    fine_grid = false;
    stop_gesture.cancel();
    stop_sequence.interrupt(); resizing_slot = -1;
    select_clip(false);
    announce("PROJECT RESTORED");
    // Restore the NEW project's saved lane, never the old editor return track.
    // Keyboard input and REC must target the visible lane immediately on load.
    if (stock_gui_role() && platform::word(0x840a0524)) {
        if (enabled) stock<void(u32, u32)>(0x8d585290)(ui_track, 0);
        // Leave the obsolete note editor after changing project identity.
        if (was_editing) stock<void(u32)>(0x8c094178)(40);
    }
    stock<void()>(0x8c093f50)();
}

void sdk_arranger_project_mode_changed(bool enabled) {
    active = enabled;
    announce(enabled ? "LINEAR MODE" : "SONG MODE");
    stock<void()>(0x8c093f50)();
}
