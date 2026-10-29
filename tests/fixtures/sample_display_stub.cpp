#include <opxy/sample_display.hpp>
#include "../../sdk/host/sample_display_stub.cpp"
#include <cstdlib>
void require(bool value) { if (!value) std::exit(1); }
int main() {
    opxy::SampleDisplaySnapshot snapshot{};
    snapshot.frames = 1000;
    snapshot.marker_count = 4;
    require(!opxy::sample_display_snapshot(0, snapshot));
    require(!snapshot.frames && !snapshot.marker_count && !snapshot.flags);
}
