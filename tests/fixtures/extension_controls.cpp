#include <extension_controls.hpp>
#include <cstdio>
#include <cstdlib>
using namespace opxy;
using namespace opxy::platform;
namespace {
void require(bool value, const char* message) {
    if (!value) { std::fprintf(stderr, "%s\n", message); std::exit(1); }
}
u32 forwarded[32][4], claimed_keys[16], claimed_modifiers[16], forwards, claims;
bool claimed_accepted[16], active = true;
bool modifier(u32 key) { return active && key == 1; }
bool chord(u32 mod, u32 key) { return mod == 1 && (key == 50 || key == 53 || key == 54); }
void forward(const u32* e) {
    require(forwards < 32, "bounded forwarding");
    for (u32 i = 0; i < 4; ++i) forwarded[forwards][i] = e[i];
    ++forwards;
}
void claimed(const u32* e, u32 mod, bool accepted) {
    require(claims < 16, "bounded claims");
    claimed_keys[claims] = e[0]; claimed_modifiers[claims] = mod; claimed_accepted[claims++] = accepted;
}
ControlRouter router = {};
const ControlRouter::Host host = {modifier, chord, forward, claimed};
void key(u32 k, bool down = true, bool enabled = true, bool clean = true, bool normal = true) {
    const u32 raw[4] = {k, down ? 100u : 0u, 123, 456};
    router.route(raw, enabled, clean, normal, host);
}
void reset() { router = {}; forwards = claims = 0; }
}
int main() {
    key(1); require(!forwards, "modifier deferred");
    key(1, false); require(forwards == 2 && forwarded[0][1] == 100 && !forwarded[1][1], "tap replayed");
    require(forwarded[0][2] == 123 && forwarded[1][3] == 456, "raw payload preserved");
    reset(); key(1); key(7); key(7, false); key(1, false);
    require(forwards == 4 && forwarded[0][0] == 1 && forwarded[1][0] == 7, "other combination ordered");
    reset(); key(1); key(54); key(54); key(54, false); key(1, false);
    require(!forwards && claims == 2 && claimed_keys[0] == 54 && claimed_modifiers[0] == 1,
            "chord consumed and repeats suppressed");
    reset(); key(1); key(53); key(1, false); key(53, false);
    require(!forwards && claims == 2, "modifier released first");
    reset(); key(1); key(50); key(50, false); key(54); key(54, false); key(1, false);
    require(!forwards && claims == 4, "multiple gestures during one modifier hold");
    reset(); key(1); key(53, true, true, false); key(53, false); key(1, false);
    require(forwards == 4 && !claims, "extra modifier keeps stock behavior");
    reset(); key(1, true, true, true, false); key(54); key(54, false); key(1, false);
    require(forwards == 4 && !claims, "modal controls keep stock behavior");
    reset(); active = false; key(1); key(54); key(1, false); key(54, false);
    require(forwards == 4 && !claims, "absent subscriber keeps stock behavior"); active = true;
    reset(); key(1); key(1, false, false);
    require(!forwards && !claims, "disabled modifier cannot become a late tap");
    reset(); key(1); key(54); key(54, false, false); key(1, false, false);
    require(!forwards && claims == 2 && !claimed_accepted[1], "consumed release stays balanced when disabled");
    reset(); key(1); key(7, true, false); key(1, false);
    require(forwards == 1 && forwarded[0][0] == 7, "cancelled pending modifier stays suppressed");
    reset(); key(256); require(forwards == 1, "out-of-range stock key forwarded safely");
    return 0;
}
