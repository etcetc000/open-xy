#include <cstdlib>
#include <cstring>
#include <initializer_list>
static void host_barrier() {}
#include <sample_display_stock.cpp>
using namespace opxy;
using namespace opxy::platform;
static void check(bool value) { if (!value) std::abort(); }
alignas(8) static unsigned char region[128];
static unsigned screen, requested_track, requested_key, reads, selected_key = 60;
static unsigned gates[8][128], gate_reads;
static unsigned jobs, preparations;
static void (*job)(unsigned);
static bool request_during_prepare;
extern "C" void sdk_sample_display_prepare() {
    ++preparations;
    if (request_during_prepare) sdk_sample_display_wake();
}
static void queue_stub(unsigned context, unsigned priority, void(*callback)(unsigned), unsigned flags) {
    check(!context && !priority && !flags); ++jobs; job = callback;
}
static bool owned = true, available = true;
static int ready() { return 1; }
static const SampleMachine machine{reinterpret_cast<const char*>(46), SamplePage::drawing, ready,
    nullptr, nullptr, nullptr, nullptr, 0, nullptr, nullptr, nullptr, nullptr, 1};
extern "C" const SampleMachine* sdk_sample_machine(unsigned engine) { return owned && engine == 46 ? &machine : nullptr; }
extern "C" unsigned sdk_sample_cache_stock_hash(const char* path) { check(std::strcmp(path, "sample.wav") == 0); return 11; }
extern "C" bool sdk_sample_display_read(unsigned track, unsigned key, SampleDisplay* out) {
    ++reads; requested_track = track; requested_key = key; *out = SampleDisplay{};
    if (!key || !available) return false;
    out->frames = 100; out->generation = 3; out->rate = 44100; out->channels = 2; return true;
}
static unsigned track_engine_stub(unsigned track) { check(track == 2); return 46; }
static unsigned key_stub(unsigned track) { check(track == 2); return selected_key; }
static unsigned gate_stub(unsigned track, unsigned note) {
    check(track < 8 && note < 128); ++gate_reads; return gates[track][note];
}
static void* region_stub(unsigned track, unsigned key, unsigned*, unsigned flags) {
    check(track == 2 && key == selected_key && flags == 0); return region;
}
Address opxy::platform::host_stock_address(Address address) {
    if (address == 0x8d4dd728) return reinterpret_cast<Address>(&queue_stub);
    if (address == kScreen) return reinterpret_cast<Address>(&screen);
    if (address == 0x8d58781c) return reinterpret_cast<Address>(&track_engine_stub);
    if (address == 0x8d4d6c98) return reinterpret_cast<Address>(&key_stub);
    if (address == 0x8d58926c) return reinterpret_cast<Address>(&region_stub);
    if (address == 0x8d5b7d10) return reinterpret_cast<Address>(&gate_stub);
    std::abort();
}
int main() {
    sdk_sample_display_wake(); sdk_sample_display_wake();
    check(jobs == 1 && !preparations);
    request_during_prepare = true;
    job(0);
    check(jobs == 2 && preparations == 1); // a newer request is never lost
    request_during_prepare = false;
    job(0);
    check(preparations == 2 && !queued);
    auto* words = reinterpret_cast<unsigned*>(region);
    words[0] = 100; words[2] = 0xffffffffu; region[31] = 1;
    std::strcpy(reinterpret_cast<char*>(region + 32), "sample.wav");
    SampleDisplay output{};
    // GUI gates are independent of sample-adapter/audio globals. Include
    // MIDI endpoints and another track, then hold across selection changes.
    for (unsigned key : {0u, 60u, 61u, 64u, 67u, 127u}) gates[2][key] = 1;
    gates[1][60] = 1;
    check(opxy_sample_display(2, &output) && output.frames == 100 && output.active_notes == 6);
    check(gate_reads == 128);
    check(output.begin == 0 && output.end == 100 && output.reverse == 1);
    check(requested_track == 2 && requested_key == 11);
    for (selected_key = 0; selected_key < 128; ++selected_key)
        check(opxy_sample_display(2, &output) && output.active_notes == 6 && output.frames == 100);
    selected_key = 60;
    available = false;
    check(!opxy_sample_display(2, &output) && !output.frames && output.active_notes == 6);
    gates[2][60] = 0;
    check(!opxy_sample_display(2, &output) && output.active_notes == 5);
    std::memset(gates[2], 0, sizeof gates[2]);
    check(!opxy_sample_display(2, &output) && !output.active_notes);
    screen = kSampleBrowserScreen;
    const unsigned gated = gate_reads;
    check(!opxy_sample_display(2, &output) && requested_key == 0 && !output.active_notes);
    check(gate_reads == gated);
    screen = 0; owned = false;
    check(!opxy_sample_display(2, &output) && requested_key == 0);
    const unsigned before = reads;
    check(!opxy_sample_display(8, &output) && reads == before);
    check(!opxy_sample_display(2, nullptr));
    owned = true;
    std::memset(region + 32, 'x', 96);
    check(!opxy_sample_display(2, &output) && requested_key == 0); // unterminated path
}
