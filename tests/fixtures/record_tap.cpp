#include <opxy/record_tap.hpp>
using opxy::extensions::RecordTap;
#define CHECK(value) do { if (!(value)) return __LINE__; } while (false)

int main() {
    RecordTap tap;
    CHECK(tap.press(100, true));
    CHECK(!tap.release(100100));
    CHECK(!tap.press(200100, true)); // The second tap must retain the first snapshot.
    CHECK(tap.release(300100));
    CHECK(!tap.release(300101)); // No repeated capture on duplicate release.
    tap.press(400100, true);
    CHECK(!tap.release(500100)); // A triple tap consumes just one pair.
    CHECK(tap.press(850101, true)); // Timeout replaces a stale snapshot.
    CHECK(!tap.release(900101)); // Gap is too long.
    tap.press(950101, true);
    CHECK(!tap.release(1200102)); // A hold is not a tap, even after a short tap.
    tap.press(1250102, true);
    CHECK(!tap.release(1300102));
    CHECK(!tap.press(1350102, false));
    CHECK(!tap.release(1400102)); // A modified press cancels the pair.
    tap.press(1450102, true);
    CHECK(!tap.release(1500102));
    tap.cancel();
    CHECK(tap.press(1550102, true));
    CHECK(!tap.release(1600102)); // Another control between taps cancels.
    tap.press(1650102, true);
    tap.cancel();
    CHECK(!tap.release(1700102)); // A Record chord cancels while held.
    tap.press(1750102, true);
    CHECK(!tap.press(1750202, true));
    CHECK(!tap.release(1800102)); // Key repeat cancels instead of double-tapping.

    // Keep the full microsecond clock: its low word wraps about every 72 minutes.
    RecordTap wrap;
    wrap.press(0xffff0000u, true);
    CHECK(!wrap.release(0xffff1000u));
    wrap.press(0x100001000ull, true);
    CHECK(wrap.release(0x100002000ull));
    RecordTap idle;
    idle.press(100, true);
    CHECK(!idle.release(200));
    idle.press(0x100000100ull, true);
    CHECK(!idle.release(0x100000200ull));

    RecordTap boundary;
    boundary.press(0, true);
    CHECK(!boundary.release(250000));
    boundary.press(600000, true);
    CHECK(boundary.release(850000));
    return 0;
}
