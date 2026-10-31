#include <map>
#include <vector>
#include <cstring>
#include <cstdio>
#include <cstdlib>
#include "stock.hpp"
#include "arranger_bounce.hpp"
void host_barrier() {}
void* fake_function(opxy::u32);
unsigned char* fake_memory(opxy::u32);
namespace opxy { namespace platform {
template<class F> F* fake_stock(u32 address) { return reinterpret_cast<F*>(fake_function(address)); }
template<class T> T* fake_at(Address address) { return reinterpret_cast<T*>(fake_memory(static_cast<u32>(address))); }
u32 fake_word(u32 address) { u32 n; std::memcpy(&n, fake_memory(address), 4); return n; }
u32 fake_byte(u32 address) { return *fake_memory(address); }
}}
#define stock fake_stock
#define at fake_at
#define word fake_word
#define byte fake_byte
#include <arranger_bounce_remote.cpp>
#undef stock
#undef at
#undef word
#undef byte

namespace test {
using namespace opxy;
using namespace opxy::platform;
namespace wire = opxy::platform::bounce_wire;
const u32 recorder = 0x8429d5b0;
unsigned char recorder_memory[64], job_memory[40], job_path[97];
u32 native_trims;
std::map<u32, u32> globals;
std::vector<wire::Message> replies;
u32 milliseconds, source = 2, enabled, starts, stops, captures, writes, native_messages, completed;
bool master, inside_capture, writer_ok = true;
void require(bool ok, const char* why) { if (!ok) { std::puts(why); std::exit(1); } }
void put_word(u32 address, u32 n) { std::memcpy(fake_memory(address), &n, 4); }
void put_byte(u32 address, u8 n) { *fake_memory(address) = n; }
u32 clock_ms(u32, u32) { return milliseconds; }
u32 role() { return master; }
u32 get_source() { return source; }
u32 get_enabled() { return enabled; }
void set_source(u32 n) { source = n; }
void set_enabled(u32 n) { enabled = n; }
void stop_native(u32 address) { require(address == recorder, "stop recorder identity"); ++stops; put_byte(recorder + 0x25, 0); }
void process(u32 address, const void*, u32 words) {
    require(address == recorder, "capture recorder identity"); ++captures;
    put_word(recorder + 12, fake_word(recorder + 12) + words);
}
u32 writer(const char*, u32, u32, const void*, u32, u32, i32) { ++writes; return writer_ok; }
void poll_native() {}
void trim_native(u32 job) {
    ++native_trims;
    // Native507b84 removes silence from both ends of the queued sample.
    put_word(job+0x1c, fake_word(job+0x1c)+128);
    put_word(job+0x20, fake_word(job+0x20)-128);
}
void setup_job(const char* path, u32 frames) {
    std::memset(job_memory,0,sizeof(job_memory));
    std::strcpy(reinterpret_cast<char*>(job_path),path);
    put_word(0x1004,0x2000);put_word(0x1014,std::strlen(path));put_word(0x1018,96);
    put_word(0x101c,0x3000);put_word(0x1020,frames);put_byte(0x1024,60);put_byte(0x1025,1);
}
void saved_native(u32) { ++completed; }
void send(u32 endpoint, u32 type, const void* payload, u32 size, u32 zero, u32 first, u32 second) {
    require(!inside_capture, "capture never sends IPC");
    require(endpoint == 0x841460b4 && type == 0xa4 && !zero && first == 255 && second == 255,
        "stock message transport contract");
    wire::Message reply;
    require(wire::decode(payload, size, true, reply), "reply serialized in master direction");
    replies.push_back(reply);
}
void native(const void* data, u32 size) {
    ++native_messages;
    const u8* payload = static_cast<const u8*>(data);
    if (!payload || !size) return;
    if (size == 1 && payload[0] == 0) {
        ++starts;
        put_word(0x842e2a18, recorder); put_word(recorder + 12, 0);
        put_byte(recorder + 0x25, 1); put_byte(recorder + 0x26, 1); put_byte(recorder + 0x27, 1);
    } else if (size == 1 && payload[0] == 1) {
        put_word(0x842e2a18, 0); sdk_arranger_bounce_stopped(recorder);
    }
}
void command(wire::Kind kind, u32 cookie, u32 a = 0, u32 b = 0, u32 c = 0) {
    const wire::Message message = {kind, cookie, {a, b, c}};
    u8 payload[wire::kSize];
    require(wire::encode(message, payload), "encode command");
    sdk_arranger_bounce_message(payload, sizeof(payload));
}
u32 reply_count(wire::Kind kind, u32 cookie) {
    u32 count = 0;
    for (const auto& item : replies) if (item.kind == kind && item.cookie == cookie) ++count;
    return count;
}
wire::Message last_reply(wire::Kind kind, u32 cookie) {
    for (auto i = replies.rbegin(); i != replies.rend(); ++i)
        if (i->kind == kind && i->cookie == cookie) return *i;
    require(false, "expected reply missing"); return {wire::Error, 0, {0, 0, 0}};
}
void capture(u32 blocks) {
    const size_t count = replies.size();
    inside_capture = true;
    for (u32 i = 0; i < blocks; ++i) sdk_arranger_bounce_capture(recorder, recorder_memory, 128);
    inside_capture = false;
    require(count == replies.size(), "capture publishes local counters only");
}
void save(const char* path, u32 begin, u32 end) {
    const u32 length = static_cast<u32>(std::strlen(path));
    require(length <= 96, "test path length");
    std::vector<u8> payload(length + 11);
    payload[0] = 10; payload[1] = static_cast<u8>(length);
    std::memcpy(payload.data() + 2, path, length);
    wire::write_word(payload.data() + 2 + length, begin);
    wire::write_word(payload.data() + 6 + length, end);
    payload[10 + length] = 60;
    sdk_arranger_bounce_message(payload.data(), static_cast<u32>(payload.size()));
}
void begin(u32 cookie) {
    command(wire::Prepare, cookie, 10000);
    require(source == 4 && enabled == 1 && fake_word(0x842e2a18) == recorder,
        "slave prepare starts its own recorder");
}
void release(u32 cookie) {
    command(wire::Release, cookie);
    sdk_arranger_bounce_remote_poll();
    require(reply_count(wire::Released, cookie) != 0, "release acknowledged");
    require(source == 2 && !enabled && !fake_word(0x842e2a18), "release restores source and enable");
}
}

unsigned char* fake_memory(opxy::u32 address) {
    if (address>=0x1000 && address<0x1028) return test::job_memory+address-0x1000;
    if (address>=0x2000 && address<0x2061) return test::job_path+address-0x2000;
    if (address >= test::recorder && address < test::recorder + sizeof(test::recorder_memory))
        return test::recorder_memory + address - test::recorder;
    return reinterpret_cast<unsigned char*>(&test::globals[address]);
}

void* fake_function(opxy::u32 address) {
    using namespace test;
#define F(a, function) if (address == a) return reinterpret_cast<void*>(&function)
    F(0x8d5be290, clock_ms); F(0x8d5c3eac, role); F(0x8d4db3f0, get_source); F(0x8d4db468, get_enabled);
    F(0x8d4db404, set_source); F(0x8d4db47c, set_enabled); F(0x8d50713c, stop_native);
    F(0x8d5072d8, process); F(0x8d4fe15c, writer); F(0x8d4d86c0, poll_native);
    F(0x11a00ec0, send); F(0x8d4d83b8, native);
    F(0x8d4d8910, saved_native); F(0x8d507b84, trim_native);
#undef F
    std::printf("unknown stock %x\n", address); std::exit(1);
}
bool sdk_arranger_bounce_busy() { return false; }
void sdk_arranger_bounce_receive(const opxy::platform::bounce_wire::Message*) {
    test::require(false, "slave cannot access master receive state");
}

int main() {
    using namespace test;
    put_word(0x842e2a18, 0x1234);
    command(wire::Prepare, 1, 10000);
    require(!starts && source == 2 && !enabled && fake_word(0x842e2a18) == 0x1234,
        "active other recorder remains untouched");
    require(reply_count(wire::Error, 1) == 1 && reply_count(wire::Released, 1) == 1,
        "busy prepare terminates explicitly");
    put_word(0x842e2a18, 0);
    put_byte(test::recorder + 0x26, 7);
    begin(2);
    require(!reply_count(wire::Ready, 2), "prepare alone cannot claim captured audio");
    const wire::Message bad_message = {wire::Prepare, 99, {10000, 0, 0}};
    u8 malformed[wire::kSize]; require(wire::encode(bad_message, malformed), "encode malformed fixture");
    malformed[3] = 2;
    sdk_arranger_bounce_message(malformed, sizeof(malformed));
    require(starts == 1 && fake_word(0x842e2a18) == test::recorder,
        "wrong protocol version cannot replace owned session");
    capture(1);
    require(!fake_byte(test::recorder + 0x26), "silent capture bypasses threshold");
    sdk_arranger_bounce_remote_poll();
    require(last_reply(wire::Ready, 2).args[0] == 64 && last_reply(wire::Ready, 2).args[1] == 1,
        "ready reports first real slave capture");
    capture(2); sdk_arranger_bounce_remote_poll();
    require(last_reply(wire::Progress, 2).args[0] == 192, "progress counts stereo frames");
    command(wire::Stop, 1);
    require(fake_word(0x842e2a18) == test::recorder, "stale stop cannot stop current recorder");
    command(wire::Stop, 2);
    require(last_reply(wire::Stopped, 2).args[0] == 192 && source == 4 && enabled == 1,
        "stop acknowledges exact frames but retains resample ownership");
    release(2);
    require(fake_byte(test::recorder + 0x26) == 7, "release restores prior threshold");
    const u32 start_count = starts;
    command(wire::Prepare, 2, 10000);
    require(starts == start_count, "late released prepare cannot restart recorder");

    begin(3); command(wire::Stop, 3);
    require(last_reply(wire::Stopped, 3).args[0] == 0 && !reply_count(wire::Ready, 3),
        "stop before first sample acknowledges zero without ready");
    release(3);

    const char* path = "/fat32/samples/user/bounce.wav";
    begin(4); capture(128); command(wire::Stop, 4);
    const u32 forwarded_before = native_messages;
    save(path, 0, 9000);
    require(native_messages == forwarded_before && reply_count(wire::Error, 4),
        "save beyond captured range rejected before native queue");
    save(path, 0, 8192);
    command(wire::Release, 4);
    require(!reply_count(wire::Released, 4) && source == 4 && enabled == 1,
        "release waits for queued disk work");
    sdk_arranger_bounce_write("/fat32/samples/user/other.wav", 2, 44100, recorder_memory, 16384, 0, 60);
    sdk_arranger_bounce_remote_poll();
    require(!reply_count(wire::WriteResult, 4) && !reply_count(wire::Released, 4),
        "unrelated writer cannot acknowledge bounce");
    sdk_arranger_bounce_write(path, 1, 44100, recorder_memory, 16384, 0, 60);
    sdk_arranger_bounce_remote_poll();
    require(!reply_count(wire::WriteResult, 4) && !reply_count(wire::Released, 4) && source == 4 && enabled == 1,
        "writer return is not worker completion and cannot release capture memory");
    sdk_arranger_bounce_remote_saved(8192); sdk_arranger_bounce_remote_poll();
    require(last_reply(wire::WriteResult, 4).args[0] == 0, "wrong channel writer cannot report success");
    require(reply_count(wire::Released, 4) && source == 2 && !enabled, "failed writer still releases ownership");

    begin(5); capture(128); command(wire::Stop, 5); save(path, 64, 8192);
    test::writer_ok = false;
    sdk_arranger_bounce_write(path, 2, 44100, recorder_memory, 16256, 0, 60);
    sdk_arranger_bounce_remote_saved(8128);
    sdk_arranger_bounce_remote_poll();
    require(!last_reply(wire::WriteResult, 5).args[0], "disk failure reported explicitly");
    release(5); test::writer_ok = true;

    begin(6); capture(128); command(wire::Stop, 6); save(path, 64, 8192);
    setup_job(path,8128);
    const u32 trims_before=native_trims;
    sdk_arranger_bounce_trim(0x1000);
    require(native_trims==trims_before && fake_word(0x101c)==0x3000 && fake_word(0x1020)==8128,
        "explicit bounce range retains leading and trailing rests before native writer");
    sdk_arranger_bounce_write(path, 2, 44100, recorder_memory, fake_word(0x1020)*2, 0, 60);
    sdk_arranger_bounce_remote_saved(fake_word(0x1020));
    sdk_arranger_bounce_remote_poll();
    const wire::Message result = last_reply(wire::WriteResult, 6);
    require(result.args[0] == 1 && result.args[1] == 8128, "valid writer confirms saved range frames");
    release(6);
    setup_job(path,8128);sdk_arranger_bounce_trim(0x1000);
    require(native_trims==trims_before+1 && fake_word(0x1020)==8000,
        "ordinary sample recording retains native silence trim");

    begin(7); capture(128); command(wire::Stop, 7); save(path, 64, 8192);
    setup_job("/fat32/samples/user/foreign.wav",8128);sdk_arranger_bounce_trim(0x1000);
    require(fake_word(0x1020)==8000,"foreign queued sample cannot borrow bounce trim exemption");
    setup_job(path,8192);sdk_arranger_bounce_trim(0x1000);
    require(fake_word(0x1020)==8064,"wrong queued range cannot borrow bounce trim exemption");
    setup_job(path,8128);put_byte(0x1024,61);sdk_arranger_bounce_trim(0x1000);
    require(fake_word(0x1020)==8000,"wrong queued key retains native behavior");
    setup_job(path,8128);put_byte(0x1025,0);sdk_arranger_bounce_trim(0x1000);
    require(fake_word(0x1020)==8000,"non-bounce mono job retains native behavior");
    sdk_arranger_bounce_write(path, 2, 44100, recorder_memory, 16384, 0, 60);
    sdk_arranger_bounce_remote_saved(8192);
    sdk_arranger_bounce_remote_poll();
    require(!last_reply(wire::WriteResult, 7).args[0], "wrong writer range cannot report success");
    release(7);

    begin(8); milliseconds += 22001; sdk_arranger_bounce_remote_poll();
    require(reply_count(wire::Error, 8) == 1 && reply_count(wire::Stopped, 8) == 1 &&
        !fake_word(0x842e2a18), "slave watchdog stops capture without master activity");
    require(source == 4 && enabled == 1, "watchdog stop retains ownership until release");
    release(8);
    put_word(0x84a50b80, 1);
    const u32 before_queue_reject = starts;
    command(wire::Prepare, 9, 10000);
    require(starts == before_queue_reject && reply_count(wire::Released, 9), "busy disk queue rejects prepare");
    put_word(0x84a50b80, 0);
    command(wire::Prepare, 10, 6000);
    require(starts == before_queue_reject && reply_count(wire::Error, 10), "invalid duration rejected before recorder mutation");

    begin(11); capture(128); command(wire::Stop, 11); save(path, 64, 8192);
    sdk_arranger_bounce_remote_saved(8128); sdk_arranger_bounce_remote_poll();
    require(!last_reply(wire::WriteResult, 11).args[0], "completion without matching writer fails closed");
    release(11);
    begin(12); capture(128); command(wire::Stop, 12); save(path, 64, 8192);
    sdk_arranger_bounce_write(path, 2, 44100, recorder_memory, 16256, 0, 60);
    sdk_arranger_bounce_remote_saved(8127); sdk_arranger_bounce_remote_poll();
    require(!last_reply(wire::WriteResult, 12).args[0], "completion count must match writer frames");
    release(12);

    const u32 before_capture = test::captures, before_write = writes, before_native = native_messages;
    sdk_arranger_bounce_capture(test::recorder, recorder_memory, 128);
    sdk_arranger_bounce_write(path, 2, 44100, recorder_memory, 128, 0, 60);
    const u8 unrelated[] = {7, 1, 2}; sdk_arranger_bounce_message(unrelated, sizeof(unrelated));
    require(test::captures == before_capture + 1 && writes == before_write + 1 && native_messages == before_native + 1,
        "inactive wrappers retain native behavior");
    std::puts("remote bounce ownership and wire checks passed");
}
