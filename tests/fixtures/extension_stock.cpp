// The runtime and the 1.2.1 adapter end to end, with stock doubles at their mapped
// addresses and the SDK's probe extension (tests/fixtures/extension_probe) subscribed.
#include <extension_runtime.cpp>
#include <extension_probe.cpp>
#include <extension_collection.cpp>
#define __builtin_bfin_ssync() ((void)0)
#include <extension_stock.cpp>

#define CHECK(value) do { if (!(value)) return __LINE__; } while (false)
using namespace opxy;
using namespace opxy::platform;
namespace {
alignas(4) unsigned char project_bytes[0xd7c+8*0x53f44];
bool change_during_read;
u32 generation=1,pending_project=0,record_latched=0;
u32 get_generation(){return generation;}
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

// Any instrument track has a pattern; writes may target another than the selected one.
u32 last_track, last_delta;
u32 get_pattern(u32 t) { failed |= t >= 8; last_track = t; return pattern_id; }
u32 get_length(u32 t) { failed |= t >= 8; return track_length; }
u32 get_scale(u32 t) { failed |= t >= 8; return scale; }
u32 get_quantization(u32 t) { failed |= t != selected; return quantization; }
i32 get_position(u32 controller, u32 t) {
    failed |= controller != 0x860c6adc || t != selected;
    return position;
}
u32 scaled_delta(u32 t, u32 delta) { failed |= t != selected; return scale == 5 ? delta / 2 : delta; }
u32 shift_down() { return shift; }
u32 key_down(u32 key) { return other_held && key == other_held; }
void get_clock(extensions::Time* out) { *out = clock_time; }
void forward_loop(u32 controller, u32 elapsed) {
    failed |= controller != 0x860c6adc || elapsed != 17; ++loop_calls;
}
void forward_tick(u32 state, u32 delta) {
    failed |= state != 0x860c072c; last_delta = delta; ++tick_calls;
}
void forward_stop(u32 controller) { failed |= controller != 0x860c6adc; ++stop_calls; }
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
    if (!reject_note) sdk_extensions_note_accepted(a1,a2);
}
void accepted_note(u32, u32) {}
u32 steps_held() { return step_entry; }
void forward_off(u32 a0,u32 a1,u32 a2,u32 a3,u32 a4,const void* e) {
    args[0]=a0; args[1]=a1; args[2]=a2; args[3]=a3; args[4]=a4;
    last_event=e; ++off_calls;
}
unsigned char* container(u32 t, u32 mode) { failed |= t >= 8 || mode != 0; last_track = t; return pattern_notes; }
void undo_reset(u32 controller) { failed |= controller != 0x860c6adc; ++resets; }
void undo_snapshot(u32 controller,u32 t) {
    failed |= controller != 0x860c6adc || t != last_track || resets != undos + 1; ++undos;
}
notes::Container& pattern() { return *reinterpret_cast<notes::Container*>(pattern_notes); }
// A valid stock list holding `filled` notes, the first `held` of them still recording.
void fill_pattern(u32 filled, u32 held = 0) {
    notes::Container& list = pattern();
    for (u32 i = 0; i < notes::capacity; ++i) {
        list.slots[i].previous = i < filled ? static_cast<i16>(i) - 1 : -1;
        list.slots[i].next = i + 1 < notes::capacity && i + 1 != filled ? static_cast<i16>(i + 1) : -1;
        list.slots[i].note = {static_cast<i32>(i) * 10, 10, 40, 50, static_cast<u8>(i < held ? notes::held_flag : 0), 0};
    }
    list.head = filled ? 0 : -1; list.tail = filled ? static_cast<i16>(filled - 1) : -1;
    list.free = filled < notes::capacity ? static_cast<i16>(filled) : -1;
    list.count = static_cast<i16>(filled);
}
// Stock insertion itself is qualified separately against authenticated firmware.
// Here the boundary double appends to the list, rejects at capacity and checks
// transaction/payloads.
__UINT64_TYPE__ insert(void* c, const PatternNote* note, i32 radius) {
    failed |= c != pattern_notes || resets != undos || undos == 0 || note->flags != 0;
    notes::Container& list = pattern();
    if (list.count == 120 || list.free < 0) return 0xffffull << 32;
    const i16 slot = list.free;
    list.free = list.slots[slot].next;
    list.slots[slot] = {list.tail, -1, *note};
    if (list.tail >= 0) list.slots[list.tail].next = slot; else list.head = slot;
    list.tail = slot; ++list.count;
    inserted[writes++ % 120] = {note->position,note->length,note->pitch,note->velocity};
    last_radius=radius;
    return static_cast<__UINT64_TYPE__>(slot) << 32;
}
void publish(u32 t) { failed |= t != last_track; ++publishes; }
void popup(const char* text,u32 a,u32 b,u32 duration) {
    const char expected[]="probed";
    for (u32 i=0;i<sizeof(expected);++i) failed |= text[i]!=expected[i];
    failed |= a!=1 || b || duration!=0x3f000000u; ++popups;
}
void key(u32 key, bool pressed, u32 elapsed=50000) {
    clock_time += elapsed;
    const u32 event[4]={key, pressed ? 100u : 0u, 123, 456};
    failed |= sdk_extensions_key_queue(event) != 0x08023c78;
}
void record() { key(50,true); key(50,false); }
void play() { key(51,true); key(51,false); }
void note(u32 pitch=60,u32 velocity=87) {
    sdk_extensions_live_note_on(0x860c6adc,selected,pitch,0xffffffffu,0xffffffffu,velocity,0,1,123,456,33,nullptr);
}
void advance(u32 delta) { sdk_extensions_recorder_tick(0x860c072c,delta); }
void release(u32 pitch=60) { sdk_extensions_live_note_off(0x860c6adc,selected,pitch,1,2,nullptr); }
}

namespace opxy { namespace platform {
Address host_stock_address(Address address) {
    if(change_during_read && address==0x10000000+0xd7c+2*0x53f44+0x53f4+0x457c) {
        ++generation;change_during_read=false;
    }
    if(address>=0x10000000 && address<0x10000000+sizeof(project_bytes))
        return reinterpret_cast<Address>(project_bytes)+(address-0x10000000);
    switch (address) {
        case 0x8d584224: return reinterpret_cast<Address>(&get_generation);
        case 0x840a0528: return reinterpret_cast<Address>(&pending_project);
        case 0x860c9b3b: return reinterpret_cast<Address>(&record_latched);
        case 0x840a0524: return reinterpret_cast<Address>(&project_id);
        case 0x840a0534: return reinterpret_cast<Address>(&selected);
        case 0x08023c48: return reinterpret_cast<Address>(&input_enabled);
        case 0x8400b9d0: return reinterpret_cast<Address>(&screen);
        case 0x860c9b39: return reinterpret_cast<Address>(&playing);
        case 0x860c9b3a: return reinterpret_cast<Address>(&counting);
        case 0x860c9b3c: return reinterpret_cast<Address>(&held_record);
        case 0x860c072c: return reinterpret_cast<Address>(&recorder_mode);
        case 0x860c072d: return reinterpret_cast<Address>(&recorder_alt);
        case 0x8d586014: return reinterpret_cast<Address>(&get_pattern);
        case 0x8d586800: return reinterpret_cast<Address>(&get_length);
        case 0x8d586b50: return reinterpret_cast<Address>(&get_scale);
        case 0x8d586c8c: return reinterpret_cast<Address>(&get_quantization);
        case 0x8d592a94: return reinterpret_cast<Address>(&get_position);
        case 0x8d5950e0: return reinterpret_cast<Address>(&scaled_delta);
        case 0x8d51f184: return reinterpret_cast<Address>(&shift_down);
        case 0x8d5b7d7c: return reinterpret_cast<Address>(&key_down);
        case 0x8d5be3a0: return reinterpret_cast<Address>(&get_clock);
        case 0x8d5986b8: return reinterpret_cast<Address>(&forward_loop);
        case 0x8d590b0a: return reinterpret_cast<Address>(&forward_tick);
        case 0x8d591964: return reinterpret_cast<Address>(&forward_stop);
        case 0x8c0b5d58: return reinterpret_cast<Address>(&forward_event);
        case 0x8d595738: return reinterpret_cast<Address>(&forward_on);
        case 0x8d5b7d54: return reinterpret_cast<Address>(&accepted_note);
        case 0x8d5b7e34: return reinterpret_cast<Address>(&steps_held);
        case 0x8d595420: return reinterpret_cast<Address>(&forward_off);
        case 0x8d5875c8: return reinterpret_cast<Address>(&container);
        case 0x8d591930: return reinterpret_cast<Address>(&undo_reset);
        case 0x8d593c18: return reinterpret_cast<Address>(&undo_snapshot);
        case 0x8d58f24c: return reinterpret_cast<Address>(&insert);
        case 0x8d587668: return reinterpret_cast<Address>(&publish);
        case 0x8d549168: return reinterpret_cast<Address>(&popup);
    }
    static const u32 ratios[32]={3,1,2,1,4,3,1,1,3,4,1,2,1,3,1,4,1,5,1,6,1,7,1,8,1,9,1,12,1,16,1,32};
    if (address>=0x840a36e0 && address<0x840a3760) return reinterpret_cast<Address>(ratios)+(address-0x840a36e0);
    __builtin_trap();
}
}}

using extensions::probe::seen;
using extensions::probe::command;
using extensions::probe::written;

int main() {
    fill_pattern(0);
    // The controller update forwards and refreshes the context.
    sdk_extensions_controller_update(0x860c6adc,17); CHECK(loop_calls==1 && seen.contexts>=1);
    const extensions::RecordingContext& c = seen.context.recording;
    CHECK(c.project==1 && c.pattern==2 && c.track==0 && c.length==16*480 && c.scale==3);
    CHECK(c.editable && c.recordable && !seen.context.playing);

    // Live input is forwarded unchanged; while stopped it is only activity.
    note(64,91);
    const u32 expected[]={0x860c6adc,0,64,0xffffffffu,0xffffffffu,91,0,1,123,456,33};
    for (u32 i=0;i<11;++i) CHECK(args[i]==expected[i]);
    CHECK(on_calls==1 && seen.activity==1 && seen.notes_on==0);
    release(64); CHECK(off_calls==1 && seen.notes_off==1 && seen.off.pitch==64);

    // Playing: an accepted live onset on the selected track is recordable, with
    // stock's position and its scale's collision radius.
    play(); CHECK(seen.context.playing);
    position=123; note(60,87);
    CHECK(seen.notes_on==1 && seen.note.pitch==60 && seen.note.velocity==87);
    CHECK(seen.note.position==123 && seen.note.collision_radius==180);
    // Rejected notes, step entry, sequencer calls and other tracks are not.
    reject_note=true; note(); reject_note=false;
    step_entry=true; note(); step_entry=false;
    sdk_extensions_note_accepted(selected,60);
    sdk_extensions_live_note_on(0x860c6adc,7,72,1,2,80,3,4,5,6,7,nullptr);
    CHECK(seen.notes_on==1);
    // A step or sample recorder workflow leaves the pattern editable, not recordable.
    recorder_mode=1; sdk_extensions_controller_update(0x860c6adc,17);
    CHECK(c.editable && !c.recordable); note(); CHECK(seen.notes_on==1);
    recorder_mode=0; sdk_extensions_controller_update(0x860c6adc,17);

    // Recorder ticks forward unchanged and reach the probe scaled to Q8.
    advance(60); CHECK(tick_calls==1 && last_delta==60 && seen.ticks==1 && seen.tick.delta_q8==(60u<<8) && !seen.tick.discontinuity);
    advance(0x00800000u); CHECK(seen.tick.discontinuity && seen.tick.delta_q8==0);

    // Stop forwards, and the context sees it within the same update.
    playing=counting=0; sdk_extensions_stop(0x860c6adc); CHECK(stop_calls==1 && !seen.context.playing);

    // Queued keys: stock's own handling first, then the event with the clock's time.
    record(); CHECK(seen.control.key==50 && !seen.control.pressed && seen.control.accepted);
    CHECK(seen.control.unmodified && seen.control.time==clock_time && event_calls==4);
    key(10,true); CHECK(seen.control.key==10 && seen.control.accepted);
    other_held=55; key(50,true); CHECK(!seen.control.unmodified); key(50,false); other_held=0;
    shift=1; key(50,true); CHECK(!seen.control.unmodified && !seen.control.accepted); key(50,false); shift=0;
    consume_record=true; key(50,true); CHECK(!seen.control.accepted); key(50,false); consume_record=false;
    input_enabled=0; key(10,true); CHECK(!seen.control.accepted); input_enabled=1;
    CHECK(!writes && !undos && !popups);

    // A write replaces the sequence in one undo transaction and publishes once.
    fill_pattern(120);
    command=1; record();
    CHECK(written==2 && writes==2 && resets==1 && undos==1 && publishes==1 && popups==1);
    CHECK(pattern().count==2 && inserted[0].position==-12 && inserted[0].length==40 && inserted[0].pitch==60);
    CHECK(inserted[1].position==480 && inserted[1].velocity==100 && last_radius==180);
    bool used[notes::capacity]; CHECK(notes::valid(pattern(), used));
    // A note stock is still recording refuses the replacement before any undo.
    fill_pattern(5, 1); record();
    CHECK(written==0 && writes==2 && undos==1 && publishes==1 && popups==1 && pattern().count==5);
    // Inserting keeps the existing notes.
    fill_pattern(3); command=2; record();
    CHECK(written==2 && writes==4 && undos==2 && pattern().count==5);
    // Writes reach another instrument track.
    fill_pattern(0); command=3; record();
    CHECK(written==2 && writes==6 && last_track==3 && undos==3 && publishes==3);
    // Not accepted, nothing is granted.
    command=1; consume_record=true; key(50,true); key(50,false); consume_record=false;
    CHECK(writes==6 && undos==3);

    CHECK(!failed);
    // Read source B on track2, while the ordinary selected getter still says A.
    project_id=0x10000000;playing=counting=held_record=0;
    const u32 pool=0xd7c+2*0x53f44, b=pool+0x53f4;
    *reinterpret_cast<u32*>(project_bytes+pool+0x53f40)=2;
    project_bytes[b]=16;project_bytes[b+8]=3;
    fill_pattern(2);
    auto& target=*reinterpret_cast<notes::Container*>(project_bytes+b+0x457c);
    target=pattern();target.slots[0].note.pitch=72;
    const u32 selected_before_read=last_track;
    extensions::PatternInfo info{};
    CHECK(extension_discover_pattern(2,1,info) && info.length==7680 && info.scale==3);
    CHECK(info.ref.project==project_id && info.ref.generation==generation && info.ref.index==1);
    RecordedNote output[120]{};u32 count=99;
    CHECK(extension_read_pattern(info.ref,output,120,count) && count==2 && output[0].pitch==72);
    CHECK(last_track==selected_before_read && undos==3 && publishes==3);
    command=4;record();CHECK(written==2 && undos==3 && publishes==3); // No scene selection or edits.
    command=5;record();CHECK(extensions::probe::request_id && written==0);
    extensions_gui_poll();CHECK(written==0);extensions_sync();
    CHECK(written==2 && extensions::probe::completed_id==extensions::probe::request_id && extensions::probe::read_success);
    record();++generation;extensions_gui_poll();extensions_sync();
    CHECK(!written && !extensions::probe::read_success);--generation;
    CHECK(!extension_read_pattern(info.ref,output,1,count) && count==0 && output[0].pitch==72);
    change_during_read=true;CHECK(!extension_read_pattern(info.ref,output,120,count) && !count);--generation;
    ++generation; CHECK(!extension_read_pattern(info.ref,output,120,count) && !count);--generation;
    pending_project=1;CHECK(!extension_discover_pattern(2,1,info));pending_project=0;
    playing=1;CHECK(!extension_read_pattern(info.ref,output,120,count));playing=0;
    record_latched=1;CHECK(!extension_read_pattern(info.ref,output,120,count));record_latched=0;
    target.slots[0].note.flags=notes::held_flag;
    CHECK(!extension_read_pattern(info.ref,output,120,count) && output[0].pitch==72);
    target.slots[0].note.flags=0;target.slots[0].next=0;
    CHECK(!extension_read_pattern(info.ref,output,120,count));
    fill_pattern(0);target=pattern();
    CHECK(extension_read_pattern(info.ref,nullptr,0,count) && count==0);
    CHECK(!extension_discover_pattern(8,0,info) && !extension_discover_pattern(2,2,info));
    *reinterpret_cast<u32*>(project_bytes+pool+0x53f40)=17;
    CHECK(!extension_discover_pattern(2,1,info));
    return 0;
}

bool sdk_arrangement_post_reply(const extensions::ArrangementContext&, const u8*, u32) { return false; }
