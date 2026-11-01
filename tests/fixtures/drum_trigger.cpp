#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <drum_values_stock.cpp>

using namespace opxy;
using namespace opxy::platform;
static void require(bool value) { if (!value) std::abort(); }
static u32 engine = 100, pitch = 61, calls;
static bool missing;
static u32 region[8], vector_words[3] = {0,0x1000,0x1020};
static i32 offsets[8];
static void defaults(u32 key, u8* model, u8* values) {
    require(key == 54); *model = 0;
    for (u32 i = 0; i < 8; ++i) values[i] = 64;
}
static u32 count(u32) { return 8; }
static const DrumMachine machine = {reinterpret_cast<const char*>(100),nullptr,2,defaults,
    nullptr,nullptr,count,0,nullptr,nullptr,nullptr};
const DrumMachine* sdk_drum_machine(u32 id) { return id == 100 ? &machine : nullptr; }
static u32 last_pitch(u32 track) { require(track == 0); ++calls; return pitch; }
static u32 track_id(u32 track) { require(track == 0); return engine; }
static void* pad(u32 track, u32 note, u32*, u32) {
    require(track == 0 && note == pitch); return missing ? nullptr : region;
}
static const u32* parameters(u32 track) { require(track == 0); return vector_words; }
Address opxy::platform::host_stock_address(Address address) {
    if (address == 0x8d4d6cb0) return reinterpret_cast<Address>(&last_pitch);
    if (address == 0x8d58781c) return reinterpret_cast<Address>(&track_id);
    if (address == 0x8d58926c) return reinterpret_cast<Address>(&pad);
    if (address == 0x8d4d72a8) return reinterpret_cast<Address>(&parameters);
    if (address == 0x1000) return reinterpret_cast<Address>(offsets);
    std::abort();
}
int main() {
    auto* bytes = reinterpret_cast<unsigned char*>(region);
    bytes[24] = drum_mark_choke(2); bytes[26] = 54;
    u8 raw[8] = {112,40,48,64,0,64,64,0};
    require(drum_pack(1,raw,reinterpret_cast<i32*>(region)+1,drum_codec(2,count)));
    for (u32 i = 0; i < 8; ++i) offsets[i] = 0x40000000;
    draw::Input input = {}; input.model = 7; input.note_serial = 99; input.slots[0] = 123;
    sdk_drum_trigger(0,&input);
    require(input.trigger_key == 54 && input.trigger_model == 1 && input.trigger_controls[0] == 0x70000000);
    require(input.model == 7 && input.note_serial == 99 && input.slots[0] == 123);
    offsets[0] = offsets[1] = 0; offsets[3] = 0x7fffffff;
    sdk_drum_trigger(0,&input);
    require(input.trigger_controls[0] == 0x30000000 && !input.trigger_controls[1] && input.trigger_controls[3] == 0x7fffffff);
    // No parameter vector means neutral offsets, just as the audio adapter.
    vector_words[1] = 0; sdk_drum_trigger(0,&input);
    require(input.trigger_controls[0] == 0x70000000);
    // Played transposed notes retain their stored canonical pad identity.
    pitch = 73; sdk_drum_trigger(0,&input); require(input.trigger_key == 54);
    bytes[24] = 0; sdk_drum_trigger(0,&input);
    require(input.trigger_model == 0 && input.trigger_controls[0] == 0x40000000);
    missing = true; sdk_drum_trigger(0,&input); require(!input.trigger_key && !input.trigger_model);
    for (auto value : input.trigger_controls) require(!value);
    missing = false; engine = 18; sdk_drum_trigger(0,&input); require(!input.trigger_key);
    engine = 100; pitch = 128; sdk_drum_trigger(0,&input); require(!input.trigger_key);
    const u32 before = calls; sdk_drum_trigger(8,&input); require(calls == before && !input.trigger_key);
    std::puts("played drum snapshot: live/sequenced source, canonical pad, offsets and selected controls remain independent");
}
