#include <cstdlib>
#include <cstring>
#include <benchmark.cpp>

namespace {
Test stock_tests[9];
unsigned calls, available = 1, lose_after_stock;
void need(bool value) { if (!value) std::abort(); }
u32 records[35][11], resets, exports;
opxy::benchmark::Stats result;
opxy::benchmark::Stats measured_callback() { return result; }
void reset_records() { ++resets; std::memset(records, 0, sizeof(records)); }
void export_records(u32 blocks) { need(blocks == 1500); ++exports; }
void benchmark_one(void* controller, const Test* test, u32 index, u32 total) {
    need(controller == reinterpret_cast<void*>(0x84c696fc));
    need(index == calls && total == 9 + available);
    if (calls < 9) need(test == &stock_tests[calls]);
    else {
        need(std::strcmp(test->name, "unfamiliar") == 0);
        need(test->engine == static_cast<u32>(reinterpret_cast<Address>(unfamiliar_engine_id)));
        need(test->setup == 19 && test->parameter == 0x40000000);
    }
    ++calls;
}
}

extern "C" {
const char unfamiliar_engine_id[] = "engine";
int unfamiliar_ready() { return available && !(lose_after_stock && calls >= 9); }
}

namespace opxy { namespace platform {
Address host_stock_address(Address address) {
    if (address == 0x8c0c7908) return reinterpret_cast<Address>(reset_records);
    if (address == 0x8c0c796c) return reinterpret_cast<Address>(export_records);
    if (address >= 0x08034f3c && address < 0x08034f3c + sizeof(records))
        return reinterpret_cast<Address>(records) + address - 0x08034f3c;
    if (address == 0x8409f3b4) return reinterpret_cast<Address>(stock_tests);
    if (address == 0x8d57c848) return reinterpret_cast<Address>(benchmark_one);
    need(address == 0x84c696fc);
    return address;
}
}}

int main() {
    sdk_benchmark_run(); need(calls == 10);
    calls = 0; available = 0;
    sdk_benchmark_run(); need(calls == 9);
    calls = 0; available = 1; lose_after_stock = 1;
    sdk_benchmark_run(); need(calls == 9);
    // Publish the callback's exact 64-bit accumulator in the selected record;
    // zero every unrelated record, including those from the preceding synth.
    const Machine effect = {"effect", 0, 0, measured_callback, 7};
    result = {300, 100, 0xfffffff0u, 2, 1500, 1};
    std::memset(records, 0xff, sizeof(records));
    callback_report(effect);
    need(resets == 1 && exports == 1);
    need(records[7][4] == 300 && records[7][5] == 0 && records[7][6] == 100);
    need(records[7][8] == 0xfffffff0u && records[7][9] == 2 && records[7][10] == 1500);
    need(records[3][8] == 0 && records[5][10] == 0);
    result.audible = 0;
    callback_report(effect);
    need(records[7][10] == 0 && exports == 2);
    result.audible = 1; result.count = 1499;
    callback_report(effect);
    need(records[7][10] == 0 && exports == 3);
}
