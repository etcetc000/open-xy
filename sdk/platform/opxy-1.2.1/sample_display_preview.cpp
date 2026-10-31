#include "sample_display_preview.hpp"
#include "stock_sample.hpp"
#include "stock.hpp"
#include "barrier.hpp"
namespace preview {
using namespace opxy;
using namespace opxy::platform;
struct Job { const Address* methods; i32 id; u32 reserved[4]; };
Job job;
Address methods[7];
volatile u32 busy, wanted_cookie, wanted_key, published_cookie;
u32 job_cookie, job_key, retained_key, attempted_cookie, attempted_at;
const void* retained;
const i16* published_data;
SampleDisplaySnapshot result;
u32 zero(void*) { return 0; }
void destroy(void*) { barrier(); busy = 0; }
void load(void*) {
    // Invalidate before releasing: readers never retain or dereference our PCM.
    published_cookie = 0; barrier();
    if (retained) {
        stock<void(u32,u32)>(0x8d509df8)(word(0x84048894), retained_key);
        retained = nullptr;
    }
    if (!job_cookie || !job_key || wanted_cookie != job_cookie || wanted_key != job_key) return;
    const void* object = stock<const void*(u32,u32)>(0x8d509d70)(word(0x84048894), job_key);
    if (!object) return; // Assignment can still be loading; poll retries boundedly.
    const SampleView sample = stock_sample_view(object);
    if (!sample.data || !sample.frames || wanted_cookie != job_cookie || wanted_key != job_key) {
        stock<void(u32,u32)>(0x8d509df8)(word(0x84048894), job_key); return;
    }
    result = SampleDisplaySnapshot{};
    result.generation = 1; // Static preview; real voices replace this with their lifetime serial.
    result.frames = sample.frames;
    result.flags = sample_waveform_approximate | sample_waveform_ready;
    // The same bounded 512 source positions as live telemetry; at most1024 reads.
    for (u32 i = 0; i < 512; ++i) {
        const u32 frame = sample.frames / 512 * i + sample.frames % 512 * i / 512;
        const u32 bin = i / 4;
        for (u32 channel = 0; channel < sample.channels; ++channel) {
            const i16 value = sample_at(sample, frame, channel);
            if (!(i % 4) && !channel) result.minimum[bin] = result.maximum[bin] = value;
            else {
                if (value < result.minimum[bin]) result.minimum[bin] = value;
                if (value > result.maximum[bin]) result.maximum[bin] = value;
            }
        }
    }
    if (wanted_cookie != job_cookie || wanted_key != job_key) {
        stock<void(u32,u32)>(0x8d509df8)(word(0x84048894), job_key); return;
    }
    retained = object; retained_key = job_key; published_data = sample.data;
    barrier(); published_cookie = job_cookie;
}
bool enqueue() {
    methods[0] = methods[1] = reinterpret_cast<Address>(&destroy);
    methods[2] = reinterpret_cast<Address>(&load);
    for (u32 i = 3; i < 7; ++i) methods[i] = reinterpret_cast<Address>(&zero);
    job.methods = methods; job.id = -1; // No coalescing or stock heap destructor.
    job_cookie = wanted_cookie; job_key = wanted_key;
    busy = 1; barrier();
    const u32 lock = word(0x08037d70);
    stock<void(u32,i32)>(0x8c03e6f4)(lock, -1);
    const u32 read = word(0x08037f78), write = word(0x08037f7c);
    if (read > 127 || write > 127 || ((write + 1) & 127) == read) {
        stock<void(u32,u32,u32,u32)>(0x8c03e9c8)(lock,0,0,0); busy = 0; return false;
    }
    void* pointer = &job;
    stock<void(u32,void**)>(0x8d4dd02c)(0x08037d78,&pointer);
    stock<void(u32,u32,u32,u32)>(0x8c03e9c8)(lock,0,0,0);
    stock<void(u32,u32,u32,u32)>(0x8c03e9c8)(word(0x08037d6c),0,0,0);
    return true;
}
}
void sdk_sample_preview_request(opxy::u32 cookie, opxy::u32 key) {
    using namespace preview;
    if (wanted_cookie == cookie && wanted_key == key) return;
    published_cookie = 0; barrier();
    wanted_cookie = key ? cookie : 0; wanted_key = key; barrier();
}
void sdk_sample_preview_poll(opxy::u32 time) {
    using namespace preview;
    barrier();
    if (busy || (published_cookie && published_cookie == wanted_cookie)) return;
    if (!wanted_cookie && !retained) return;
    if (attempted_cookie == wanted_cookie && time - attempted_at < 250) return;
    attempted_cookie = wanted_cookie; attempted_at = time;
    enqueue();
}
bool sdk_sample_preview_snapshot(opxy::u32 cookie, opxy::SampleDisplaySnapshot& out) {
    using namespace preview;
    if (!cookie || published_cookie != cookie || wanted_cookie != cookie) return false;
    barrier(); out = result; barrier();
    return published_cookie == cookie && wanted_cookie == cookie;
}
bool sdk_sample_preview_matches(opxy::u32 cookie, const opxy::SampleView& sample) {
    using namespace preview;
    if (!cookie || published_cookie != cookie || wanted_cookie != cookie) return false;
    barrier(); const bool match = published_data == sample.data && result.frames == sample.frames;
    barrier(); return match && published_cookie == cookie && wanted_cookie == cookie;
}
