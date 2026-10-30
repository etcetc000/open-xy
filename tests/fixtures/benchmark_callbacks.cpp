#include <cstdlib>
#include <benchmark.hpp>
static unsigned allocations, releases, clock_value;
static bool fail;
extern "C" opxy::u32 sdk_benchmark_cycles() { clock_value += 100; return clock_value; }
namespace opxy { namespace benchmark {
void* allocate(u32 bytes) { if (fail) return nullptr; ++allocations; return std::calloc(1, bytes); }
void release(void* data) { ++releases; std::free(data); }
}}
#include <generated_bench.cpp>
int main() {
    const opxy::benchmark::Stats result = BENCHMARK();
    if (result.count != 1500 || result.maximum != 100 || result.minimum != 100 ||
        result.total_low != 150000 || result.total_high || !result.audible || allocations != releases) std::abort();
    // The clock wrap must not corrupt unsigned elapsed cycles or the total.
    opxy::benchmark::Stats wrap{};
    wrap.add(0x20u - 0xfffffff0u);
    if (wrap.total_low != 48) std::abort();
#ifdef SAMPLE_BENCHMARK
    fail = true;
    const opxy::benchmark::Stats missing = BENCHMARK();
    if (missing.count || missing.audible || allocations != releases) std::abort();
#endif
}
