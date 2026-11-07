#include <checkpoint_control.hpp>
#include <cstdio>
#include <cstdlib>
#include <cstring>
using namespace opxy;
using namespace opxy::platform;
namespace cp = opxy::platform::checkpoint;
namespace {
void require(bool value, const char* message) {
    if (!value) { std::fprintf(stderr, "%s\n", message); std::exit(1); }
}
u32 state[2] = {101, 202}, resets, forgotten;
bool ready = true, present[16];
i32 kept_values[16][8];
int available() { return ready ? 1 : 0; }
u32 save_first(u8* out) { std::memcpy(out, &state[0], 4); return 4; }
u32 save_second(u8* out) { std::memcpy(out, &state[1], 4); return 4; }
void load_first(const u8* in, u32 n) { require(n == 4, "first saved length"); std::memcpy(&state[0], in, n); }
void load_second(const u8* in, u32 n) { require(n == 4, "second saved length"); std::memcpy(&state[1], in, n); }
const i32* kept(u32 track, u32 pattern) { return track == 12 && present[pattern] ? kept_values[pattern] : nullptr; }
void restore(u32 track, u32 pattern, const i32* values) {
    if (track != 12) return;
    present[pattern] = values != nullptr;
    if (values) for (u32 i = 0; i < 8; ++i) kept_values[pattern][i] = values[i];
}
}
extern "C" {
const ControlMachine sdk_control_machines[] = {
    {nullptr, available, nullptr, nullptr, "first", 4, save_first, load_first, true, 0, nullptr, {}, false},
    {nullptr, available, nullptr, nullptr, "second", 4096, save_second, load_second, true, 0, nullptr, {}, false}
};
const u32 sdk_control_machine_count = 2;
AuxService sdk_aux = {nullptr, nullptr, kept, restore, nullptr};
void sdk_control_forget() { ++forgotten; }
}
namespace opxy { namespace platform { void control_loaded(const ControlMachine&) { ++resets; } }}
int main() {
    u8 buffer[513] = {}; buffer[512] = 123;
    const cp::PatternRef patterns[] = {{0, 0}, {12, 1}, {12, 2}};
    present[1] = true; kept_values[1][0] = 303;
    u32 bytes = 0;
    require(cp::kept::save(buffer, bytes, 512, patterns, 3) && buffer[512] == 123, "bounded SDK state capture");
    require(bytes == 120 && cp::kept::matches(buffer, bytes, patterns, 3), "compare the actual saved bytes");
    require(cp::kept::validate(buffer, bytes), "saved SDK state validates");
    require(!cp::kept::validate(buffer, bytes - 1), "truncated SDK state refused");
    ready = false; require(!cp::kept::validate(buffer, bytes), "missing registration refused"); ready = true;
    state[0] = 999; state[1] = 888; kept_values[1][0] = 777;
    present[2] = present[3] = true;
    require(!cp::kept::matches(buffer, bytes, patterns, 3), "changed state is not a redundant recall");
    cp::kept::load(buffer, bytes);
    require(state[0] == 101 && state[1] == 202 && resets == 2 && forgotten == 1, "machine state restored after reset");
    require(present[1] && kept_values[1][0] == 303 && !present[2] && present[3], "restore presence without changing unrelated patterns");
    require(cp::kept::matches(buffer, bytes, patterns, 3), "comparison ignores unrelated aux patterns");
    require(!cp::kept::save(buffer, bytes, 15, patterns, 3), "saved-data capacity failure");
    return 0;
}
