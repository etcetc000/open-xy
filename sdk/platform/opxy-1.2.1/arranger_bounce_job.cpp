#include "arranger_bounce_job.hpp"
#include "stock.hpp"
#include "barrier.hpp"

namespace {
using namespace opxy;
using namespace opxy::platform;
// Stock owns this job until its worker destructor, including after cancellation.
alignas(8) u8 job[0xdd0];
Address methods[9];
volatile u32 busy, cancelled;
u32 project, generation;
bool bound() {
    return !cancelled && word(0x840a0524) == project && !word(0x840a0528) &&
        stock<u32()>(0x8d584224)() == generation;
}
void destroy(void* object) {
    stock<void(void*)>(0x8d4ddaa8)(object);
    barrier(); busy = 0;
}
void load(void* object) {
    barrier();
    if (bound()) stock<void(void*)>(0x8d4dec40)(object);
}
void apply(void* object) {
    barrier();
    if (!bound()) job[0xdcc] = 0;
    sdk_arranger_bounce_destination_apply(object);
}
void put(u32 offset, u32 value) { *reinterpret_cast<u32*>(job + offset) = value; }
}

bool sdk_arranger_bounce_job_begin(u32 engine, u32 track) {
    if (busy || track >= 8 || (engine != 2 && engine != 3) || !word(0x840a0524) || word(0x840a0528)) return false;
    for (u32 i = 0; i < sizeof(job); ++i) job[i] = 0;
    stock<void(void*)>(0x8d4e1de0)(job + 0x10);
    methods[0] = methods[1] = reinterpret_cast<Address>(&destroy);
    methods[2] = reinterpret_cast<Address>(&load);
    methods[3] = 0x8d4dd94c;
    methods[4] = 0x8d4dd000;
    methods[5] = 0x8d4dd940;
    methods[6] = reinterpret_cast<Address>(&apply);
    methods[7] = methods[8] = 0;
    put(0, static_cast<u32>(reinterpret_cast<Address>(methods)));
    put(4, 0xffffffffu); // Noncoalescing: stock must not delete this request as a newer preset job.
    put(8, track); put(12, engine);
    project = word(0x840a0524);
    generation = stock<u32()>(0x8d584224)();
    cancelled = 0;
    busy = 1; barrier();
    const u32 lock = word(0x08037d70);
    stock<void(u32, i32)>(0x8c03e6f4)(lock, -1);
    const u32 read = word(0x08037f78), write = word(0x08037f7c);
    if (read > 127 || write > 127 || ((write + 1) & 127) == read) {
        stock<void(u32, u32, u32, u32)>(0x8c03e9c8)(lock, 0, 0, 0);
        destroy(job);
        return false;
    }
    void* pointer = job;
    stock<void(u32, void**)>(0x8d4dd02c)(0x08037d78, &pointer);
    stock<void(u32, u32, u32, u32)>(0x8c03e9c8)(lock, 0, 0, 0);
    stock<void(u32, u32, u32, u32)>(0x8c03e9c8)(word(0x08037d6c), 0, 0, 0);
    return true;
}
bool sdk_arranger_bounce_job_busy() { barrier(); return busy != 0; }
void sdk_arranger_bounce_job_cancel() { cancelled = 1; barrier(); }
