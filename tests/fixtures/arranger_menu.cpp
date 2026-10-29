#include "../../sdk/platform/opxy-1.2.1/arranger_menu.hpp"
#include <cstdio>
#include <cstdlib>
using opxy::platform::ArrangerMenuGesture;
static void require(bool ok, const char* message) {
    if (!ok) { std::puts(message); std::exit(1); }
}
constexpr ArrangerMenuGesture initial;
int main() {
    require(!initial.pending && !initial.opened, "constant initialization");
    ArrangerMenuGesture gesture;
    require(!gesture.poll(1000) && !gesture.release(1000), "idle events ignored");
    gesture.press(100);
    require(gesture.pending && !gesture.opened, "press starts pending gesture");
    require(!gesture.poll(549), "below threshold");
    require(gesture.poll(550) && gesture.opened, "exact hold threshold opens");
    require(!gesture.poll(551) && !gesture.poll(1000), "opens only once");
    require(!gesture.release(1001) && !gesture.pending && !gesture.opened, "held release never taps");
    gesture.press(1000);
    gesture.press(1300);
    require(gesture.poll(1450), "repeated press does not restart timer");
    gesture.press(1500);
    require(!gesture.poll(2000), "repeated press after opening cannot reopen");
    gesture.cancel();
    gesture.press(2000);
    require(gesture.release(2449), "short tap");
    require(!gesture.release(2450), "release cannot repeat tap");
    gesture.press(3000);
    require(!gesture.release(3450), "long release without poll never taps");
    gesture.press(0xffffff00u);
    require(!gesture.poll(0xc1u) && gesture.poll(0xc2u), "hold threshold across clock wrap");
    require(!gesture.release(0xc3u), "wrapped hold release");
    gesture.press(0xfffffff0u);
    require(gesture.release(0x20u), "short tap across wrap");
    gesture.press(5000);
    gesture.cancel();
    require(!gesture.pending && !gesture.opened && !gesture.poll(6000) && !gesture.release(6000), "cancel clears gesture");
    gesture.press(6000);
    require(gesture.release(6001), "new tap after cancel");
}
