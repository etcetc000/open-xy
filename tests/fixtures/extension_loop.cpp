// The 1.2.1 bar-loop hooks with stock doubles: a modelled track time setter
// (8d5ac4d8) that records each call, and the pattern/scene state loops depend on.
#include <bar_loop.hpp>
#include <extension_runtime.hpp>
#include <extension_loop.cpp>

#define CHECK(value) do { if (!(value)) return __LINE__; } while (false)
using namespace opxy;
using namespace opxy::platform;
namespace {
const u32 st = bar_loop::step_time;
const u32 kFakeProject = 0x86000000;
alignas(4) unsigned char objects[16][0x290];
alignas(4) unsigned char project_bytes[0x400];
u32 project_word = kFakeProject;
u32 steps_of[16], shown_of[16], scale_of[16];
struct Call { u32 track, time, events, steps, edit; };
Call calls[16];
u32 call_count, restarts, restarted_track, special;
i32 step_target;
bool failed;
u32 jump_on_last;  // a step component on the last step jumps here (0: none)

u32& field_of(u32 track, u32 offset) { return *reinterpret_cast<u32*>(&objects[track][offset]); }
u32 object(u32 track) { return 0x860c6adc + 0x108 + track * 0x290; }

// Stock's time setter, reduced to the fields the hooks read: time, the last
// processed time (events) and the current step.
void set_time(u32 o, u32 time, u32 events, u32 steps, u32 edit) {
    const u32 track = (o - object(0)) / 0x290;
    failed |= track >= 16 || o != object(track);
    if (call_count < 16) calls[call_count] = {track, time, events, steps, edit};
    ++call_count;
    field_of(track, 0x198) = time;
    field_of(track, 0x19c) = events ? time : time - 1;
    field_of(track, 0x1a0) = time / st;
    if (jump_on_last && steps && time % st == st - 1) field_of(track, 0x198) = jump_on_last;
}
u32 steps(u32 t) { failed |= t >= 16; return steps_of[t]; }
u32 shown(u32 t) { return shown_of[t]; }
void show(u32 t, u32 bar) { failed |= t >= 16; shown_of[t] = bar; }
u32 scale(u32 t) { return scale_of[t]; }
void restart(u32 live, u32 track) { failed |= live != 0x86bd027c; ++restarts; restarted_track = track; }
u32 special_track() { return 8; }
void clear_special() { ++special; }
u32 components(u32 context, u32 track, u32 previous, i32* step, u32* extra) {
    failed |= context != 77 || !step || !extra; (void)track; (void)previous;
    *step = step_target;
    return 1;
}
u32 call(u32 i, u32 track, u32 time, u32 events, u32 steps_on) {
    return calls[i].track == track && calls[i].time == time && calls[i].events == events && calls[i].steps == steps_on;
}
extensions::BarLoop loop_of(u32 track, u8 bar) {
    extensions::BarLoop l;
    for (u32 t = 0; t < 16; ++t) l.bar[t] = extensions::no_bar;
    l.bar[track] = bar;
    return l;
}
// play_from: the sequencer's play bytes, the transport and the sync mode.
alignas(4) unsigned char controller_bytes[0x100];  // 0x860c6adc + 0x3000
alignas(4) u32 transport_words[16];                 // 0x86bd0064
u32 requests, request_flag, resets, reset_count_in, follow_on, resyncs;
unsigned char& controller_byte(u32 offset) { return controller_bytes[offset - 0x3000]; }
// Stock's arm (8d592998): recording armed as Record + Play arms it.
u32 arms;
void arm(u32 seq, u32 on) { failed |= seq != 0x860c6adc; ++arms; controller_byte(0x305f) = static_cast<unsigned char>(on); }
void request(u32 transport, u32 flag) { failed |= transport != 0x86bd0064; ++requests; request_flag = flag; }
void reset(u32 transport, u32 count_in) {
    failed |= transport != 0x86bd0064; ++resets; reset_count_in = count_in; transport_words[1] = 0;
}
u32 follow(u32 controller) { failed |= controller != 0x860c6adc; return follow_on; }
// Stock's resync: the track's time from the scene clock (song ticks * factor at scale 3), mod its length.
void resync(u32 o, u32 check) {
    const u32 track = (o - object(0)) / 0x290;
    failed |= check != 0 || track >= 16 || !steps_of[track];
    ++resyncs;
    field_of(track, 0x198) = (transport_words[1] * 1680u) % (steps_of[track] * st);
}
// The bar menu: its screen (node ids at +0x38), the track it shows, the theme colour.
alignas(4) unsigned char bar_screen[0x60];
u32 menu_track, theme = 0xffe0c0a0, colored_node, colored;
unsigned char painted[4];
u32 bar_menu_track() { return menu_track; }
void paint(u32 screen, u32 node, const unsigned char* rgba) {
    failed |= screen != 0x84eb6000;
    ++colored; colored_node = node;
    for (u32 i = 0; i < 4; ++i) painted[i] = rgba[i];
}
// Stock's set_text (8d6011f0): the last text given to each bar's number node.
char numbered[4][4]; u32 numberings;
void number_text(u32 node, const char* text) {
    failed |= node < 0x500 || node > 0x503;
    ++numberings;
    u32 i = 0;
    for (; text[i] && i < 3; ++i) numbered[node - 0x500][i] = text[i];
    numbered[node - 0x500][i] = 0;
}
bool shows(u32 bar, const char* text) {
    const char* a = numbered[bar];
    while (*a && *a == *text) { ++a; ++text; }
    return *a == *text;
}
u8& scene_byte() { return project_bytes[6]; }
u8& pattern_byte(u32 track) { return project_bytes[0x98 + scene_byte() * 33 + track]; }
}

namespace opxy { namespace platform {
Address host_stock_address(Address address) {
    if (address >= object(0) && address < object(16)) return reinterpret_cast<Address>(&objects[0][0]) + (address - object(0));
    if (address >= kFakeProject && address < kFakeProject + sizeof(project_bytes))
        return reinterpret_cast<Address>(project_bytes) + (address - kFakeProject);
    static const u32 factors[16] = {5040, 3360, 2240, 1680, 1260, 840, 560, 420, 336, 280, 240, 210, 210, 140, 105, 105};
    if (address >= 0x840aa47c && address < 0x840aa47c + 64) return reinterpret_cast<Address>(factors) + (address - 0x840aa47c);
    if (address >= 0x860c6adc + 0x3000 && address < 0x860c6adc + 0x3100)
        return reinterpret_cast<Address>(controller_bytes) + (address - 0x860c6adc - 0x3000);
    if (address >= 0x84eb6000 && address < 0x84eb6000 + sizeof(bar_screen))
        return reinterpret_cast<Address>(bar_screen) + (address - 0x84eb6000);
    if (address >= 0x86bd0064 && address < 0x86bd0064 + sizeof(transport_words))
        return reinterpret_cast<Address>(transport_words) + (address - 0x86bd0064);
    switch (address) {
        case 0x8d5aa9ee: return reinterpret_cast<Address>(&request);
        case 0x8d592998: return reinterpret_cast<Address>(&arm);
        case 0x8d5aaa38: return reinterpret_cast<Address>(&reset);
        case 0x8d592234: return reinterpret_cast<Address>(&follow);
        case 0x8d5abf80: return reinterpret_cast<Address>(&resync);
        case 0x840a0524: return reinterpret_cast<Address>(&project_word);
        case 0x8d5ac4d8: return reinterpret_cast<Address>(&set_time);
        case 0x8d586800: return reinterpret_cast<Address>(&steps);
        case 0x8d585fb4: return reinterpret_cast<Address>(&shown);
        case 0x8d587458: return reinterpret_cast<Address>(&show);
        case 0x8d586b50: return reinterpret_cast<Address>(&scale);
        case 0x8d5accf8: return reinterpret_cast<Address>(&restart);
        case 0x8d584468: return reinterpret_cast<Address>(&special_track);
        case 0x8d58b248: return reinterpret_cast<Address>(&clear_special);
        case 0x8d583e44: return reinterpret_cast<Address>(&components);
        case 0x8d4d6f14: return reinterpret_cast<Address>(&bar_menu_track);
        case 0x8d6011f0: return reinterpret_cast<Address>(&number_text);
        case 0x8d520608: return reinterpret_cast<Address>(&paint);
        case 0x84eb3ad8: return reinterpret_cast<Address>(&theme);
    }
    __builtin_trap();
}
}}

int main() {
    for (u32 t = 0; t < 16; ++t) { objects[t][0x194] = static_cast<unsigned char>(t); scale_of[t] = 3; }
    steps_of[2] = 64; steps_of[3] = 40; steps_of[8] = 16;
    // Without a loop every move is forwarded unchanged.
    sdk_extensions_loop_advance(object(2), 100, 1, 1, 0);
    CHECK(call_count == 1 && call(0, 2, 100, 1, 1));
    sdk_extensions_loop_position(object(2), 70 * st, 1, 0, 0);
    CHECK(call_count == 2 && call(1, 2, 70 * st, 1, 0));

    // The sequencer view: bars, playing bar, shown bar and song-time bar length.
    extensions::Sequencer s;
    field_of(2, 0x198) = 33 * st; shown_of[2] = 3; scale_of[3] = 0;
    extension_sequencer(s);
    CHECK(s.track[2].bars == 4 && s.track[2].bar == 2 && s.track[2].shown == 3 && s.track[2].bar_ticks == 7680);
    CHECK(s.track[3].bars == 3 && s.track[3].bar_ticks == 7680 * 1680 / 5040 && s.track[0].bars == 0);
    CHECK(s.track[2].loop == extensions::no_bar);
    scale_of[3] = 3;

    // Hold track 2 in its third bar: steps 32-47.
    pattern_byte(2) = 1;
    CHECK(extension_loop(loop_of(2, 2)));
    extension_sequencer(s); CHECK(s.track[2].loop == 2 && s.track[3].loop == extensions::no_bar);
    // Moves inside the bar are forwarded.
    call_count = 0;
    sdk_extensions_loop_advance(object(2), 40 * st, 1, 1, 0);
    CHECK(call_count == 1 && call(0, 2, 40 * st, 1, 1));
    // Past its end: play the bar out, restart it as a pattern restarts, jump to
    // its start without events (keeping the last step as the one left), play on.
    call_count = 0;
    sdk_extensions_loop_advance(object(2), 48 * st + 100, 1, 1, 0);
    CHECK(call_count == 3 && call(0, 2, 48 * st - 1, 1, 1) && call(1, 2, 32 * st, 0, 0) &&
          call(2, 2, 32 * st + 100, 1, 1));
    CHECK(restarts == 1 && restarted_track == 2 && special == 0);
    // Jumps land at the same place within the bar: play start on its first
    // step, step 5 of the first bar on step 5 of the looped one.
    call_count = 0;
    sdk_extensions_loop_position(object(2), 0, 0, 0, 0);
    CHECK(call_count == 1 && call(0, 2, 32 * st, 0, 0));
    field_of(2, 0x19c) = 45 * st;
    call_count = 0;
    sdk_extensions_loop_position(object(2), 5 * st, 1, 0, 0);  // backward, with events
    CHECK(call_count == 2 && call(0, 2, 37 * st, 0, 0) && call(1, 2, 37 * st, 1, 0));
    // Step-component go-to targets land in the bar.
    i32 step = 0; u32 extra = 0; step_target = 3;
    CHECK(sdk_extensions_loop_step_target(77, 2, 40, &step, &extra) == 1 && step == 35);
    step_target = 40;
    CHECK(sdk_extensions_loop_step_target(77, 2, 40, &step, &extra) == 1 && step == 40);
    step_target = -1;
    CHECK(sdk_extensions_loop_step_target(77, 2, 40, &step, &extra) == 1 && step == 47);
    // A component on the bar's last step that jumps elsewhere keeps its jump.
    field_of(2, 0x198) = 47 * st; jump_on_last = 34 * st; call_count = 0;
    sdk_extensions_loop_advance(object(2), 48 * st + 5, 1, 1, 0);
    CHECK(call_count == 1 && restarts == 1);
    jump_on_last = 0;

    // A short last bar (40 steps: bar 2 is steps 32-39), and track 8's extra restart.
    CHECK(extension_loop(loop_of(3, 2)));
    field_of(3, 0x198) = 39 * st; call_count = 0;
    sdk_extensions_loop_advance(object(3), 40 * st + 7, 1, 1, 0);
    CHECK(call_count == 3 && call(1, 3, 32 * st, 0, 0) && call(2, 3, 32 * st + 7, 1, 1));
    CHECK(extension_loop(loop_of(8, 0)));
    field_of(8, 0x198) = 15 * st; restarts = 0;
    sdk_extensions_loop_advance(object(8), 16 * st, 1, 1, 0);
    CHECK(restarts == 1 && special == 1);
    // Play start shows a looped track's bar where stock shows bar 1; other tracks keep stock's.
    CHECK(extension_loop(loop_of(2, 3)));
    shown_of[2] = shown_of[3] = 2;
    sdk_extensions_loop_shown(2, 0); sdk_extensions_loop_shown(3, 0);
    CHECK(shown_of[2] == 3 && shown_of[3] == 0);
    CHECK(extension_loop(loop_of(2, extensions::no_bar)));
    sdk_extensions_loop_shown(2, 0); CHECK(shown_of[2] == 0);
    // Bars that don't exist are not taken.
    CHECK(extension_loop(loop_of(2, 4)));
    extension_sequencer(s); CHECK(s.track[2].loop == extensions::no_bar);
    // A bar removed from under one track lets go of that track only.
    {
        extensions::BarLoop both = loop_of(2, 3); both.bar[8] = 0;
        CHECK(extension_loop(both));
        steps_of[2] = 48;  // bar 4 removed
        extension_sequencer(s);
        CHECK(s.track[2].loop == extensions::no_bar && s.track[8].loop == 0 && s.track[2].bars == 3);
        steps_of[2] = 64;
        extension_sequencer(s); CHECK(s.track[2].loop == extensions::no_bar && s.track[8].loop == 0);
        CHECK(extension_loop(loop_of(2, extensions::no_bar)));
    }

    // A scene whose looped tracks play the same patterns keeps the loop (stock
    // changes the scene index when recording starts); one where a looped track
    // plays another pattern, a pattern or a project change ends it, and moves
    // are stock's again.
    CHECK(extension_loop(loop_of(2, 1)));
    project_bytes[0x98 + 33 + 2] = project_bytes[0x98 + 2];  // scene 1: track 2 on the same pattern
    scene_byte() = 1;
    extension_sequencer(s); CHECK(s.track[2].loop == 1);
    scene_byte() = 0;
    project_bytes[0x98 + 2 * 33 + 2] = project_bytes[0x98 + 2] + 1;  // scene 2: track 2 on another pattern
    scene_byte() = 2; call_count = 0;
    sdk_extensions_loop_position(object(2), 0, 0, 0, 0);
    CHECK(call_count == 1 && call(0, 2, 0, 0, 0));
    extension_sequencer(s); CHECK(s.track[2].loop == extensions::no_bar);
    scene_byte() = 0;
    CHECK(extension_loop(loop_of(2, 1)));
    ++pattern_byte(2);
    extension_sequencer(s); CHECK(s.track[2].loop == extensions::no_bar);
    CHECK(extension_loop(loop_of(2, 1)));
    project_bytes[7] ^= 0x10;  // the flag a scene pick or the song screen changes: the loop holds
    extension_sequencer(s); CHECK(s.track[2].loop == 1);
    project_bytes[7] ^= 0x10;
    ++project_bytes[7];  // another song selected
    extension_sequencer(s); CHECK(s.track[2].loop == extensions::no_bar);
    --project_bytes[7];
    CHECK(extension_loop(loop_of(2, 1)));
    project_word = kFakeProject + 0x10;
    extension_sequencer(s); CHECK(s.track[2].loop == extensions::no_bar);

    // play_from: tracks 2 (64 steps), 3 (40) and 8 (16) at scale 1; the scene is
    // 4 bars long. Refused past the scene.
    CHECK(!extension_play_from(4 * 7680) && !requests);
    // From bar 3: asked as the Play key asks, without the count-in.
    controller_byte(0x3061) = 1;
    CHECK(extension_play_from(2 * 7680) && requests == 1 && request_flag == 1 && controller_byte(0x3061) == 0);
    // Play start: stock's reset, then the scene clock at bar 3 and every track
    // placed from it (track 3 in its bar 3, track 8 in its only bar), each
    // showing the bar it starts in.
    follow_on = 1; shown_of[2] = shown_of[3] = shown_of[8] = 0;
    sdk_extensions_play_reset(0x86bd0064, 0);
    CHECK(resets == 1 && transport_words[1] == 2 * 7680u && resyncs == 3);
    CHECK(field_of(2, 0x198) == 32 * st && field_of(3, 0x198) == 32 * st && field_of(8, 0x198) == 0);
    CHECK(shown_of[2] == 2 && shown_of[3] == 2 && shown_of[8] == 0);
    // Taken once: the next start is stock's.
    sdk_extensions_play_reset(0x86bd0064, 0);
    CHECK(resets == 2 && transport_words[1] == 0 && resyncs == 3);
    // With the count-in (recording armed): asked with it, as Play asks. The
    // tracks move to the place at play start, so a note played during the
    // count-in is picked up there, while the clock stays at zero; the clock
    // takes the place when the count-in ends, once.
    controller_byte(0x305f) = 1;
    CHECK(extension_play_from(7680) && controller_byte(0x3061) == 1);
    field_of(2, 0x198) = 0; shown_of[2] = 0;
    sdk_extensions_play_reset(0x86bd0064, 1);
    CHECK(resets == 3 && reset_count_in == 1 && transport_words[1] == 0 && resyncs == 6);
    CHECK(field_of(2, 0x198) == 16 * st && shown_of[2] == 1);
    sdk_extensions_count_in_end(0x86bd0064, 0);
    CHECK(resets == 4 && reset_count_in == 0 && transport_words[1] == 7680u && resyncs == 9);
    CHECK(field_of(2, 0x198) == 16 * st && shown_of[2] == 1);
    sdk_extensions_count_in_end(0x86bd0064, 0);
    CHECK(resets == 5 && transport_words[1] == 0 && resyncs == 9);
    // A count-in stopped before it ends: the next start, and its count-in, are stock's.
    CHECK(extension_play_from(7680));
    sdk_extensions_play_reset(0x86bd0064, 1);
    CHECK(resyncs == 12);
    sdk_extensions_play_reset(0x86bd0064, 0);
    CHECK(transport_words[1] == 0 && resyncs == 12);
    sdk_extensions_count_in_end(0x86bd0064, 0);
    CHECK(transport_words[1] == 0 && resyncs == 12);
    // Playing with the count-in on, Play takes its other path: no start.
    controller_byte(0x305e) = 1; CHECK(!extension_play_from(7680) && requests == 3);
    controller_byte(0x305e) = 0; controller_byte(0x305f) = 0;
    // Record held (seq + 0x3060), stopped: recording is armed as Record + Play
    // arms it, with no start asked for; the tracks move to the place at once
    // (the clock stays at stock's zero). Play start's move to the top goes
    // to each track's place instead. The start stock then makes (the first note,
    // or Play with its count-in) takes the place.
    controller_byte(0x3060) = 1; field_of(2, 0x198) = 0; shown_of[2] = 0;
    CHECK(extension_play_from(7680) && arms == 1 && controller_byte(0x305f) == 1 && requests == 3);
    CHECK(field_of(2, 0x198) == 16 * st && transport_words[1] == 0 && shown_of[2] == 1);
    controller_byte(0x3060) = 0;
    sdk_extensions_loop_position(object(3), 0, 1, 0, 0);
    CHECK(field_of(3, 0x198) == 16 * st);
    sdk_extensions_play_reset(0x86bd0064, 0);  // the first note started it
    CHECK(transport_words[1] == 7680u && field_of(2, 0x198) == 16 * st);
    controller_byte(0x3060) = 1;
    CHECK(extension_play_from(2 * 7680) && arms == 2);
    controller_byte(0x3060) = 0;
    sdk_extensions_play_reset(0x86bd0064, 1);  // Play, with its count-in
    CHECK(transport_words[1] == 0 && field_of(2, 0x198) == 32 * st);
    sdk_extensions_count_in_end(0x86bd0064, 0);
    CHECK(transport_words[1] == 2 * 7680u);
    // Disarmed before any start: the next start is stock's.
    controller_byte(0x3060) = 1;
    CHECK(extension_play_from(7680) && arms == 3);
    controller_byte(0x3060) = 0; controller_byte(0x305f) = 0;
    sdk_extensions_play_reset(0x86bd0064, 0);
    CHECK(transport_words[1] == 0);
    // Record held while playing: nothing.
    controller_byte(0x3060) = 1; controller_byte(0x305e) = 1;
    CHECK(!extension_play_from(7680) && arms == 3);
    controller_byte(0x3060) = 0; controller_byte(0x305e) = 0; controller_byte(0x305f) = 0;
    // Without the step display following, tracks start in place and keep their shown bar.
    follow_on = 0; shown_of[2] = 1;
    CHECK(extension_play_from(3 * 7680));
    sdk_extensions_play_reset(0x86bd0064, 0);
    CHECK(transport_words[1] == 3 * 7680u && field_of(2, 0x198) == 48 * st && shown_of[2] == 1);
    // Playing: asked the same way, a restart from there, as Play restarts from the top.
    controller_byte(0x305e) = 1; follow_on = 1;
    CHECK(extension_play_from(7680) && requests == 5 && request_flag == 1);
    sdk_extensions_play_reset(0x86bd0064, 0);
    CHECK(transport_words[1] == 7680u && field_of(2, 0x198) == 16 * st && shown_of[2] == 1);
    controller_byte(0x305e) = 0;

    // The bar menu draws track 2's looped bar (bar 2 of 4) in grey, half the
    // theme colour, shown (black) or not; other bars, tracks and nodes as stock.
    const u32 screen = 0x84eb6000;
    for (u32 i = 0; i < 4; ++i) *reinterpret_cast<unsigned short*>(bar_screen + 0x38 + 2 * i) = 0x40 + i;
    const unsigned char white[4] = {0xa0, 0xc0, 0xe0, 0xff}, black[4] = {0, 0, 0, 0xff};
    CHECK(extension_loop(loop_of(2, 1)));
    menu_track = 2;
    sdk_extensions_bar_color(screen, 0x41, white);
    CHECK(colored == 1 && colored_node == 0x41 && painted[0] == 0x50 && painted[1] == 0x60 &&
          painted[2] == 0x70 && painted[3] == 0xff);
    sdk_extensions_bar_color(screen, 0x41, black);
    CHECK(colored == 2 && painted[0] == 0x50 && painted[3] == 0xff);
    sdk_extensions_bar_color(screen, 0x40, black);
    CHECK(colored == 3 && colored_node == 0x40 && painted[0] == 0 && painted[2] == 0);
    sdk_extensions_bar_color(screen, 0x99, white);
    CHECK(colored == 4 && painted[0] == 0xa0);
    menu_track = 3;
    sdk_extensions_bar_color(screen, 0x41, white);
    CHECK(colored == 5 && painted[0] == 0xa0);
    CHECK(extension_loop(loop_of(2, extensions::no_bar)));
    menu_track = 2;
    sdk_extensions_bar_color(screen, 0x41, white);
    CHECK(colored == 6 && painted[0] == 0xa0);
    // Each bar's number gets a dot while the menu's track plays that bar, set
    // only when it changes; stopped, or after the menu is built again, stock's.
    for (u32 i = 0; i < 4; ++i) *reinterpret_cast<u32*>(bar_screen + 0x50 + 4 * i) = 0x500 + i;
    controller_byte(0x305e) = 1; field_of(2, 0x198) = 20 * st;  // track 2 in its bar 2
    for (u32 i = 0; i < 4; ++i) sdk_extensions_bar_color(screen, 0x40 + i, white);
    CHECK(numberings == 4 && shows(0, "1") && shows(1, "2.") && shows(2, "3") && shows(3, "4"));
    for (u32 i = 0; i < 4; ++i) sdk_extensions_bar_color(screen, 0x40 + i, white);
    CHECK(numberings == 4);
    field_of(2, 0x198) = 33 * st;  // bar 3
    for (u32 i = 0; i < 4; ++i) sdk_extensions_bar_color(screen, 0x40 + i, white);
    CHECK(numberings == 6 && shows(1, "2") && shows(2, "3."));
    controller_byte(0x305e) = 0;
    for (u32 i = 0; i < 4; ++i) sdk_extensions_bar_color(screen, 0x40 + i, white);
    CHECK(numberings == 7 && shows(2, "3"));
    extension_bar_menu_built();
    sdk_extensions_bar_color(screen, 0x40, white);
    CHECK(numberings == 8 && shows(0, "1"));
    for (u32 i = 0; i < 4; ++i) *reinterpret_cast<u32*>(bar_screen + 0x50 + 4 * i) = 0;

    // While bars loop, the scene clock follows the longest track (2, 64 steps)
    // right after it advances, so it stays inside the looped bar and never
    // reaches the scene's end; the other tracks leave it alone.
    extension_sequencer(s);
    CHECK(extension_loop(loop_of(2, 1)));
    transport_words[1] = 3;
    sdk_extensions_loop_advance(object(3), 5 * st, 0, 0, 0);
    CHECK(transport_words[1] == 3);
    sdk_extensions_loop_advance(object(2), 20 * st, 0, 0, 0);
    CHECK(transport_words[1] == 20 * st / 1680);
    // In the scene's last bar it stays half a step short of the scene's end
    // (64 steps of 480 song ticks; half a step is 240), so the transport's next
    // tick can't reach it before the track wraps back.
    CHECK(extension_loop(loop_of(2, 3)));
    sdk_extensions_loop_advance(object(2), 64 * st - 100, 0, 0, 0);
    CHECK(transport_words[1] == 64 * 480 - 240 - 1);
    sdk_extensions_loop_advance(object(2), 60 * st, 0, 0, 0);
    CHECK(transport_words[1] == 60 * 480);
    CHECK(extension_loop(loop_of(2, 1)));
    // Ending the loop: once more, then the clock runs on as stock's.
    CHECK(extension_loop(loop_of(2, extensions::no_bar)));
    sdk_extensions_loop_advance(object(2), 21 * st, 0, 0, 0);
    CHECK(transport_words[1] == 21 * st / 1680);
    transport_words[1] = 7;
    sdk_extensions_loop_advance(object(2), 22 * st, 0, 0, 0);
    CHECK(transport_words[1] == 7);
    // The same when the loop ends because something changed (another song).
    CHECK(extension_loop(loop_of(2, 1)));
    u8& song = project_bytes[project_word - kFakeProject + 7];
    ++song;
    extension_sequencer(s); CHECK(s.track[2].loop == extensions::no_bar);
    sdk_extensions_loop_advance(object(2), 24 * st, 0, 0, 0);
    CHECK(transport_words[1] == 24 * st / 1680);
    transport_words[1] = 7;
    sdk_extensions_loop_advance(object(2), 25 * st, 0, 0, 0);
    CHECK(transport_words[1] == 7);
    --song;
    // Stopped, arming a loop moves each held track to its bar's first step, and
    // ending it moves them back to the top; playing, neither moves them.
    controller_byte(0x305d) = controller_byte(0x305e) = 0;
    field_of(2, 0x198) = 0; field_of(3, 0x198) = 0; field_of(8, 0x198) = 0;
    {
        extensions::BarLoop l = loop_of(2, 2); l.bar[3] = 1;
        CHECK(extension_loop(l));
        CHECK(field_of(2, 0x198) == 32 * st && field_of(3, 0x198) == 16 * st && field_of(8, 0x198) == 0);
        CHECK(extension_loop(loop_of(2, extensions::no_bar)));
        CHECK(field_of(2, 0x198) == 0 && field_of(3, 0x198) == 0);
        controller_byte(0x305e) = 1; field_of(2, 0x198) = 5 * st;
        CHECK(extension_loop(loop_of(2, 2)) && field_of(2, 0x198) == 5 * st);
        CHECK(extension_loop(loop_of(2, extensions::no_bar)) && field_of(2, 0x198) == 5 * st);
        controller_byte(0x305e) = 0;
    }
    CHECK(!failed);
    return 0;
}
