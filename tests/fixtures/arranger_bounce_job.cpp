#include <cstdio>
#include <cstdlib>
#include <cstring>
#include "stock.hpp"
void host_barrier() {}
void* fake_address(opxy::u32);
namespace opxy { namespace platform {
template<class F> F* fake_stock(u32 address) { return reinterpret_cast<F*>(fake_address(address)); }
u32 fake_word(u32);
}}
#define stock fake_stock
#define word fake_word
#include <arranger_bounce_job.cpp>
#undef stock
#undef word

namespace test {
u32 current = 100, pending, revision = 1, read_index, write_index;
u32 constructed, destroyed, loaded, applied, accepted, queued, wake;
bool locked;
void* queued_job;
void require(bool ok, const char* why) { if (!ok) { std::puts(why); std::exit(1); } }
void construct(void*) { require(!locked, "construct outside queue mutex"); ++constructed; }
void destruct(void*) { require(!locked, "destruct outside queue mutex"); ++destroyed; }
void loading(void* object) { ++loaded; static_cast<u8*>(object)[0xdcc] = 1; }
u32 get_generation() { return revision; }
void lock(u32 handle, i32 wait) { require(handle == 7 && wait == -1 && !locked, "queue lock"); locked = true; }
void unlock(u32 handle, u32, u32, u32) {
    if (handle == 7) { require(locked, "queue unlock"); locked = false; }
    else { require(handle == 8 && !locked, "wake after unlock"); ++wake; }
}
void enqueue(u32 queue, void** pointer) {
    require(locked && queue == 0x08037d78, "enqueue under mutex");
    require(sdk_arranger_bounce_job_busy(), "publish ownership before enqueue");
    queued_job = *pointer; ++queued;
    require(*reinterpret_cast<u32*>(static_cast<u8*>(queued_job) + 4) == 0xffffffffu, "never coalesce with preset jobs");
}
void finish() { load(queued_job); apply(queued_job); destroy(queued_job); }
}
opxy::u32 opxy::platform::fake_word(u32 address) {
    using namespace test;
    switch(address) {
    case 0x840a0524: return current; case 0x840a0528: return pending;
    case 0x08037d70: return 7; case 0x08037d6c: return 8;
    case 0x08037f78: return read_index; case 0x08037f7c: return write_index;
    }
    require(false, "unknown memory read"); return 0;
}
void* fake_address(opxy::u32 address) {
    using namespace test;
    switch(address) {
    case 0x8d584224: return reinterpret_cast<void*>(&get_generation);
    case 0x8d4e1de0: return reinterpret_cast<void*>(&construct);
    case 0x8d4ddaa8: return reinterpret_cast<void*>(&destruct);
    case 0x8d4dec40: return reinterpret_cast<void*>(&loading);
    case 0x8c03e6f4: return reinterpret_cast<void*>(&lock);
    case 0x8c03e9c8: return reinterpret_cast<void*>(&unlock);
    case 0x8d4dd02c: return reinterpret_cast<void*>(&enqueue);
    }
    require(false, "unknown stock call"); return nullptr;
}
void sdk_arranger_bounce_destination_apply(void* object) {
    ++test::applied;
    if (static_cast<u8*>(object)[0xdcc]) ++test::accepted;
    test::require(sdk_arranger_bounce_job_busy(), "GUI callback cannot free worker ownership");
}
int main() {
    using namespace test;
    require(!sdk_arranger_bounce_job_begin(30, 1), "reject wrong engine");
    require(sdk_arranger_bounce_job_begin(2, 1), "enqueue sampler");
    require(!sdk_arranger_bounce_job_begin(3, 1), "reject concurrent reuse");
    finish(); require(loaded == 1 && accepted == 1 && !sdk_arranger_bounce_job_busy(), "complete lifecycle");
    require(sdk_arranger_bounce_job_begin(3, 2), "enqueue drum");
    sdk_arranger_bounce_job_cancel(); finish();
    require(loaded == 1 && applied == 2 && accepted == 1, "cancel before load still completes without commit");
    require(sdk_arranger_bounce_job_begin(2, 1), "enqueue project-bound job");
    load(queued_job); ++revision; apply(queued_job); destroy(queued_job);
    require(accepted == 1 && !sdk_arranger_bounce_job_busy(), "stale completion cannot apply");
    write_index = 127;
    require(!sdk_arranger_bounce_job_begin(2, 1), "full queue rejected without native fatal path");
    require(constructed == destroyed && queued == 3 && wake == 3 && !locked && !sdk_arranger_bounce_job_busy(), "cleanup exactly once");
    return 0;
}
