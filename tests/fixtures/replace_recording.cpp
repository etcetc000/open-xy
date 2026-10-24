#include <extension_runtime.cpp>
#include "../../extensions/replace/extension.cpp"
#include <recording_stock.cpp>
#define CHECK(value) do { if (!(value)) return __LINE__; } while (false)
using namespace opxy;
using namespace opxy::platform;
namespace {
RecordingContext context = {1,2,0,7680,3,255,true};
bool playing = true;
u32 locked, phase, fixture_track, active_count, resets, undos, publishes, forwards, quant_calls;
u32 active_entries[60];
recording::Container notes, undo_notes;
bool failed;
const ExtensionEntry selected[] = {{"replace",false,false,&extensions::replace::hooks,true}};
void clear() {
    notes = {};
    for (u32 i=0;i<120;++i) notes.slots[i].next = i==119 ? -1 : static_cast<i16>(i+1);
    notes.free=0; notes.head=notes.tail=-1; notes.count=0;
    active_count=0;
}
u32 add(i32 position, bool held=false) {
    const u32 slot=static_cast<u32>(notes.free);
    auto& s=notes.slots[slot]; notes.free=s.next;
    s.previous=notes.tail; s.next=-1;
    if (notes.tail!=-1) notes.slots[notes.tail].next=static_cast<i16>(slot); else notes.head=static_cast<i16>(slot);
    notes.tail=static_cast<i16>(slot); ++notes.count;
    s.note={position,60,60,100,static_cast<u8>(held?2:0),0};
    return slot;
}
void new_take() {
    locked=0; recording_sync(); clear(); phase=0; locked=1; recording_sync();
    resets=undos=publishes=0;
}
i32 input_position;
void insert_forward(u32 state,u32 pitch,u32 track,u32 velocity) {
    failed |= state!=0x860c9628 || pitch!=60 || track || velocity!=100;
    const u32 before=active_count;
    const u32 slot=add(input_position,true);
    active_entries[before*3]=0x100004+slot*16;
    active_entries[before*3+1]=0xaabbcc00; // Track is a BYTE, padding is not zero.
    active_entries[before*3+2]=0; active_count=before+1;
}
void input(i32 position) { input_position=position; sdk_recording_insert(0x860c9628,60,0,100); }
void release_all() {
    active_count=0;
    for (u32 i=0;i<120;++i) notes.slots[i].note.flags=0;
}
recording::Container* get_container(u32 track,u32 kind) { failed |= track!=0 || kind; return &notes; }
i32 quantize(i32 position,u32 amount,u32 scale) {
    ++quant_calls; failed |= scale!=3;
    return amount==255 ? ((position+240)/480)*480 : position;
}
void undo_reset(u32 c) { failed |= c!=0x860cf9c8; ++resets; }
void undo_snapshot(u32 c,u32 t) { failed |= c!=0x860cf9c8 || t; ++undos; undo_notes=notes; }
void publish(u32 t) { failed |= t; ++publishes; }
void forward(u32 c,u32 delta) {
    failed |= c!=0x200000; ++forwards;
    phase=(phase+delta)%(context.length*1680);
}
void advance(u32 delta) { sdk_recording_advance(0x200000,delta); }
}
namespace opxy { namespace platform {
const ExtensionEntry* extension_collection(u32& n) { n=1; return selected; }
RecordingContext extension_context(bool& p) { p=playing; return context; }
i32 extension_position(u32) {
    i32 p=static_cast<i32>(phase/1680);
    if (p>static_cast<i32>(context.length)-240) p-=context.length;
    return p;
}
u32 extension_scaled_delta(u32,u32 delta) { return delta<<8; }
i32 extension_collision_radius(u32) { return 180; }
u32 extension_write(u32,const RecordedNote*,u32) { return 0; }
void extension_notify(const char*) {}
Address host_stock_address(Address a) {
    if (a>=0x100000 && a<0x100788) return reinterpret_cast<Address>(&notes)+(a-0x100000);
    if (a>=0x860c9030 && a<0x860c9120) return reinterpret_cast<Address>(active_entries)+(a-0x860c9030);
    switch(a) {
        case 0x860d37f7: return reinterpret_cast<Address>(&locked);
        case 0x860c9120: return reinterpret_cast<Address>(&active_count);
        case 0x200194: return reinterpret_cast<Address>(&fixture_track);
        case 0x200198: return reinterpret_cast<Address>(&phase);
        case 0x8d58ed0a: return reinterpret_cast<Address>(&insert_forward);
        case 0x8d585c60: return reinterpret_cast<Address>(&get_container);
        case 0x8d5aa714: return reinterpret_cast<Address>(&quantize);
        case 0x8d58fd38: return reinterpret_cast<Address>(&undo_reset);
        case 0x8d591f40: return reinterpret_cast<Address>(&undo_snapshot);
        case 0x8d585d00: return reinterpret_cast<Address>(&publish);
        case 0x8d5a9b5c: return reinterpret_cast<Address>(&forward);
    }
    __builtin_trap();
}
}}
int main() {
    clear(); add(0); locked=1; advance(1); CHECK(notes.count==1 && !undos); // Default overdub.
    recording_mode_set(true); locked=0; phase=0; advance(1); CHECK(notes.count==1 && !undos); // Short Record taps.
    new_take(); add(0); add(480);
    advance(1); CHECK(notes.count==1 && undos==1 && undo_notes.count==2 && publishes==1);
    advance(480*1680); CHECK(notes.count==0 && undos==1 && publishes==2);
    // Late quantized input belongs to this lap and is erased at next zero.
    new_take(); phase=12*1680; input(12); release_all();
    CHECK(undos==1 && undo_notes.count==0);
    advance((context.length-12)*1680); CHECK(notes.count==1);
    advance(1); CHECK(notes.count==0 && undos==1);
    // Early pickup belongs to the upcoming lap, then expires one lap later.
    new_take(); phase=(context.length-12)*1680; input(-12); release_all();
    advance(12*1680); advance(1); CHECK(notes.count==1);
    advance(context.length*1680-1); advance(1); CHECK(notes.count==0);
    // Both the active pointer and the note flag independently protect held notes.
    new_take(); input(0); notes.slots[0].note.flags=0;
    advance(context.length*1680); advance(1); CHECK(notes.count==1);
    active_count=0; notes.slots[0].note.flags=2;
    advance(context.length*1680-1); advance(1); CHECK(notes.count==1);
    release_all(); advance(context.length*1680-1); advance(1); CHECK(notes.count==0);
    // A seek/discontinuity does not erase an arbitrary region.
    new_take(); add(0); advance(context.length*1680+1); CHECK(notes.count==1 && !undos);
    // Changes to quantization discard prior pass ownership.
    new_take(); input(0); release_all(); ++context.pattern; advance(1); CHECK(notes.count==0);
    // Invalid list and invalid active pointer leave notes untouched.
    new_take(); add(0); notes.slots[0].next=0; advance(1); CHECK(notes.count==1 && !undos);
    new_take(); add(0); active_count=1; active_entries[0]=0x100005; active_entries[1]=0;
    advance(1); CHECK(notes.count==1 && !undos);
    // Input on another track or a full pattern must not replace selected undo.
    new_take(); CHECK(recording_input_count(0x860c9628,7)==20 && !undos);
    CHECK(recording_input_count(0x12345678,0)==20 && !undos);
    for (u32 i=0;i<120;++i) add(static_cast<i32>(i*60));
    CHECK(recording_input_count(0x860c9628,0)==20 && !undos);
    // Raw forwarding is unconditional, even while stopped or on another track.
    new_take(); add(0); playing=false; advance(1); CHECK(notes.count==1);
    playing=true; fixture_track=7; advance(1); CHECK(notes.count==1);
    CHECK(forwards>10 && quant_calls && !failed);
    return 0;
}
