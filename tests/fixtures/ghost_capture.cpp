// Execute the service AND the version adapter; only stock addresses are mapped.
#include <opxy/record_tap.hpp>
#include <extension_runtime.cpp>
#include "../../extensions/capture/extension.cpp"
#include <extension_collection.cpp>
#include <ghost_capture_stock.cpp>

#define CHECK(value) do { if (!(value)) return __LINE__; } while (false)
using namespace opxy;
using namespace opxy::platform;
namespace {
u32 project_id = 1, selected = 0, input_enabled = 1, screen = 0;
u32 pattern_id = 2, track_length = 16, scale = 3, quantization = 0;
i32 position = 0;
u32 playing = 0, counting = 0, held_record = 0, recorder_mode = 0, recorder_alt = 0;
u32 other_held = 0, shift = 0;
extensions::Time clock_time;
alignas(4) unsigned char pattern_notes[0x788];
u32 on_calls, off_calls, tick_calls, loop_calls, stop_calls, event_calls, writes, publishes, popups, resets, undos;
u32 args[11]; const void* last_event;
RecordedNote inserted[120];
i32 last_radius;
bool failed, consume_record, reject_note, step_entry;

u32 get_pattern(u32 t) { failed |= t != selected; return pattern_id; }
u32 get_length(u32 t) { failed |= t != selected; return track_length; }
u32 get_scale(u32 t) { failed |= t != selected; return scale; }
u32 get_quantization(u32 t) { failed |= t != selected; return quantization; }
i32 get_position(u32 controller, u32 t) {
    failed |= controller != 0x860cf9c8 || t != selected;
    return position;
}
u32 scaled_delta(u32 t, u32 delta) { failed |= t != selected; return scale == 5 ? delta / 2 : delta; }
u32 shift_down() { return shift; }
u32 key_down(u32 key) { return other_held && key == other_held; }
void get_clock(extensions::Time* out) { *out = clock_time; }
void forward_loop(u32 controller, u32 elapsed) {
    failed |= controller != 0x860cf9c8 || elapsed != 17; ++loop_calls;
}
void forward_tick(u32 state, u32 delta) {
    failed |= state != 0x860c9628 || delta > 10000; ++tick_calls;
}
void forward_stop(u32 controller) { failed |= controller != 0x860cf9c8; ++stop_calls; }
void forward_event(const u32* e) {
    ++event_calls;
    if (!input_enabled) return;
    if (e[0] == 50 && !consume_record && !shift) held_record = e[1] != 0;
    if (e[0] == 51 && e[1]) playing = 1;
    if (e[0] == 52 && e[1]) playing = counting = 0;
}
void forward_on(u32 a0,u32 a1,u32 a2,u32 a3,u32 a4,u32 a5,u32 a6,u32 a7,u32 a8,u32 a9,u32 a10,const void* e) {
    const u32 a[] = {a0,a1,a2,a3,a4,a5,a6,a7,a8,a9,a10};
    for (u32 i=0;i<11;++i) args[i]=a[i];
    last_event=e; ++on_calls;
    if (!reject_note) sdk_capture_accepted(a1,a2);
}
void accepted_note(u32, u32) {}
u32 steps_held() { return step_entry; }
void forward_off(u32 a0,u32 a1,u32 a2,u32 a3,u32 a4,const void* e) {
    args[0]=a0; args[1]=a1; args[2]=a2; args[3]=a3; args[4]=a4;
    last_event=e; ++off_calls;
}
unsigned char* container(u32 t, u32 mode) { failed |= t != selected || mode != 0; return pattern_notes; }
void undo_reset(u32 controller) { failed |= controller != 0x860cf9c8; ++resets; }
void undo_snapshot(u32 controller,u32 t) {
    failed |= controller != 0x860cf9c8 || t != selected || resets != undos + 1; ++undos;
}
// Stock insertion itself is qualified separately against authenticated firmware.
// Here the boundary double rejects at capacity and checks transaction/payloads.
__UINT64_TYPE__ insert(void* c, const PatternNote* note, i32 radius) {
    failed |= c != pattern_notes || resets != undos || undos == 0 || note->flags != 0;
    auto* count = reinterpret_cast<i16*>(pattern_notes + 0x786);
    if (*count == 120) return 0xffffull << 32;
    inserted[writes++ % 120] = {note->position,note->length,note->pitch,note->velocity};
    last_radius=radius;
    return static_cast<__UINT64_TYPE__>((*count)++) << 32;
}
void publish(u32 t) { failed |= t != selected; ++publishes; }
void popup(const char* text,u32 a,u32 b,u32 duration) {
    const char expected[]="captured";
    for (u32 i=0;i<sizeof(expected);++i) failed |= text[i]!=expected[i];
    failed |= a!=1 || b || duration!=0x3f000000u; ++popups;
}
void key(u32 key, bool pressed, u32 elapsed=50000) {
    clock_time += elapsed;
    const u32 event[4]={key, pressed ? 100u : 0u, 123, 456};
    failed |= sdk_capture_queued_event(event) != 0x08024030;
}
void double_tap() { key(50,true); key(50,false); key(50,true); key(50,false); }
void play() { key(51,true); key(51,false); }
void stop() { key(52,true); key(52,false); }
void note(u32 pitch=60,u32 velocity=87) {
    sdk_capture_note_on(0x860cf9c8,selected,pitch,0xffffffffu,0xffffffffu,velocity,0,1,123,456,33,nullptr);
}
void advance(u32 delta) { sdk_capture_tick(0x860c9628,delta); }
void release(u32 pitch=60) { sdk_capture_note_off(0x860cf9c8,selected,pitch,1,2,nullptr); }
}

namespace opxy { namespace platform {
Address host_stock_address(Address address) {
    switch (address) {
        case 0x8409d2c8: return reinterpret_cast<Address>(&project_id);
        case 0x8409d2d8: return reinterpret_cast<Address>(&selected);
        case 0x08024000: return reinterpret_cast<Address>(&input_enabled);
        case 0x84008c64: return reinterpret_cast<Address>(&screen);
        case 0x860d37f5: return reinterpret_cast<Address>(&playing);
        case 0x860d37f6: return reinterpret_cast<Address>(&counting);
        case 0x860d37f8: return reinterpret_cast<Address>(&held_record);
        case 0x860c9628: return reinterpret_cast<Address>(&recorder_mode);
        case 0x860c9629: return reinterpret_cast<Address>(&recorder_alt);
        case 0x8d5846ac: return reinterpret_cast<Address>(&get_pattern);
        case 0x8d584e98: return reinterpret_cast<Address>(&get_length);
        case 0x8d5851e8: return reinterpret_cast<Address>(&get_scale);
        case 0x8d585324: return reinterpret_cast<Address>(&get_quantization);
        case 0x8d590eb4: return reinterpret_cast<Address>(&get_position);
        case 0x8d593408: return reinterpret_cast<Address>(&scaled_delta);
        case 0x8d51e5bc: return reinterpret_cast<Address>(&shift_down);
        case 0x8d5b5ef4: return reinterpret_cast<Address>(&key_down);
        case 0x8d5bc500: return reinterpret_cast<Address>(&get_clock);
        case 0x8d5969f0: return reinterpret_cast<Address>(&forward_loop);
        case 0x8d58ef12: return reinterpret_cast<Address>(&forward_tick);
        case 0x8d58fd6c: return reinterpret_cast<Address>(&forward_stop);
        case 0x8c0b68b8: return reinterpret_cast<Address>(&forward_event);
        case 0x8d593a70: return reinterpret_cast<Address>(&forward_on);
        case 0x8d5b5ecc: return reinterpret_cast<Address>(&accepted_note);
        case 0x8d5b5fac: return reinterpret_cast<Address>(&steps_held);
        case 0x8d593758: return reinterpret_cast<Address>(&forward_off);
        case 0x8d585c60: return reinterpret_cast<Address>(&container);
        case 0x8d58fd38: return reinterpret_cast<Address>(&undo_reset);
        case 0x8d591f40: return reinterpret_cast<Address>(&undo_snapshot);
        case 0x8d58d654: return reinterpret_cast<Address>(&insert);
        case 0x8d585d00: return reinterpret_cast<Address>(&publish);
        case 0x8d548580: return reinterpret_cast<Address>(&popup);
    }
    static const u32 ratios[32]={3,1,2,1,4,3,1,1,3,4,1,2,1,3,1,4,1,5,1,6,1,7,1,8,1,9,1,12,1,16,1,32};
    if (address>=0x840a040c && address<0x840a048c) return reinterpret_cast<Address>(ratios)+(address-0x840a040c);
    __builtin_trap();
}
}}

int main() {
    sdk_capture_loop(0x860cf9c8,17); CHECK(loop_calls==1);
    double_tap(); CHECK(!writes && !popups && !undos);
    play(); position=123; note(); advance(240); release(); stop();
    CHECK(on_calls==1 && off_calls==1 && tick_calls==1);
    CHECK(args[0]==0x860cf9c8 && args[1]==0 && args[2]==60 && args[3]==1 && args[4]==2);
    double_tap(); CHECK(writes==1 && popups==1 && publishes==1 && undos==1);
    CHECK(inserted[0].position==123 && inserted[0].length==240 && inserted[0].velocity==87 && last_radius==180);
    double_tap(); CHECK(writes==1 && popups==1);
    // Verify every argument to the twelve-word stock call, including timing.
    play(); note(64,91);
    const u32 expected[]={0x860cf9c8,0,64,0xffffffffu,0xffffffffu,91,0,1,123,456,33};
    for (u32 i=0;i<11;++i) CHECK(args[i]==expected[i]);
    CHECK(!last_event);
    advance(60); stop(); play(); stop(); double_tap(); CHECK(writes==1);
    // Context edits while stopped invalidate; each is observed at the normal loop boundary.
    for (u32 field=0;field<6;++field) {
        play(); note(); advance(20); stop();
        switch(field) {
            case 0: ++project_id; break; case 1: ++pattern_id; break;
            case 2: selected^=1; break; case 3: ++track_length; break;
            case 4: scale=5; break; case 5: ++quantization; break;
        }
        sdk_capture_loop(0x860cf9c8,17); double_tap(); CHECK(writes==1);
    }
    // Track scaling and stock's signed pickup placement survive capture.
    play(); position=track_length*480-10; note(67,99); advance(121); stop();
    double_tap(); CHECK(writes==2 && inserted[1].position==-10 && inserted[1].length==60 && last_radius==90);
    // Controls, long holds, chords, and a page consuming Record never capture.
    play(); position=50; note(); advance(100); release(); stop();
    key(50,true); key(50,false); key(10,true); key(10,false); key(50,true); key(50,false); CHECK(writes==2);
    key(50,true); key(50,false,300000); CHECK(writes==2);
    other_held=55; double_tap(); other_held=0; CHECK(writes==2);
    consume_record=true; double_tap(); consume_record=false; CHECK(writes==2);
    input_enabled=0; double_tap(); input_enabled=1; CHECK(writes==2);
    // Capacity failure keeps history, does not replace undo or claim success.
    *reinterpret_cast<i16*>(pattern_notes+0x786)=120;
    double_tap(); CHECK(writes==2 && popups==2 && undos==2);
    *reinterpret_cast<i16*>(pattern_notes+0x786)=0;
    double_tap(); CHECK(writes==3 && popups==3 && undos==3);
    // Other tracks and recorder workflows are forwarded but never captured.
    play(); sdk_capture_note_on(0x860cf9c8,7,72,1,2,80,3,4,5,6,7,nullptr);
    stop(); double_tap(); CHECK(writes==3);
    recorder_mode=1; play(); note(); advance(20); stop(); double_tap(); CHECK(writes==3);
    recorder_mode=0; sdk_capture_loop(0x860cf9c8,17);
    // Immediate live notes between taps cancel, even without a queued key event.
    play(); position=0; note(); advance(60); release();
    key(50,true); key(50,false); note(62); release(62); key(50,true); key(50,false);
    CHECK(writes==3);
    key(10,true); key(10,false); stop();
    // Stock accepts only the available prefix when the pattern fills mid-batch.
    *reinterpret_cast<i16*>(pattern_notes+0x786)=119;
    double_tap(); CHECK(writes==4 && popups==4 && undos==4 && publishes==4);
    *reinterpret_cast<i16*>(pattern_notes+0x786)=0;
    play(); note(); advance(60);
    playing=counting=0; sdk_capture_stop(0x860cf9c8);
    playing=1; advance(1); // External restart before the next GUI/controller poll.
    stop(); double_tap(); CHECK(writes==4 && stop_calls==1);
    // A sequencer call reaches the shared acceptance function without the live
    // input scope. Rejected live notes and step entry must also stay out.
    play(); sdk_capture_accepted(selected,60);
    reject_note=true; note(); reject_note=false;
    step_entry=true; note(); step_entry=false;
    stop(); double_tap(); CHECK(writes==4);

    // An early first note survives loop wrap and time spent making the gesture.
    scale=3; play(); position=-12; note(65,97); advance(40); release(65);
    advance(track_length*480-28);
    key(50,true); advance(300); key(50,false); key(50,true); key(50,false);
    CHECK(writes==5 && popups==5 && inserted[4].position==-12);
    CHECK(inserted[4].length==40 && inserted[4].pitch==65 && inserted[4].velocity==97);
    // An expired pair must take a fresh snapshot, not reuse the earlier take.
    position=20; note(66); advance(20);
    key(50,true); key(50,false); advance(60);
    key(50,true,350001); key(50,false); key(50,true); key(50,false);
    CHECK(writes==6 && inserted[5].length==80); release(66);
    // Context changes invalidate a snapshot already anchored by the first tap.
    position=0; note(); advance(30); release(); key(50,true); key(50,false);
    ++quantization; sdk_capture_loop(0x860cf9c8,17);
    key(50,true); key(50,false); key(50,true); key(50,false); CHECK(writes==6);

    // Reported workflow: two pages at 1x, quantization on, one silent pass,
    // early pickup into the next pass, then Stop after the loop wraps again.
    // Later passes replace the same note without expiring the first step.
    stop(); track_length=32; scale=3; quantization=255; play();
    advance(7680); advance(7668); position=32*480-12;
    note(60,93); advance(12); release(60);
    position=480; note(67,80); advance(60); release(67);
    for (u32 pass=0;pass<3;++pass) {
        advance(7680); advance(7680);
        note(67,90+pass); advance(60); release(67);
    }
    advance(7680); advance(7680); advance(960); stop(); double_tap();
    CHECK(writes==8 && inserted[6].position==-12 && inserted[6].pitch==60);
    CHECK(inserted[6].velocity==93 && inserted[6].length==12);
    CHECK(inserted[7].position==480 && inserted[7].pitch==67 && inserted[7].velocity==92);
    CHECK(!failed);
    return 0;
}
