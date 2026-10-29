// Host/browser builds without a sample engine expose unavailable telemetry.
// Tests may provide their own deterministic provider instead of linking this file.
#include <opxy/sample_display.hpp>
namespace opxy {
bool sample_display_snapshot(u32, SampleDisplaySnapshot& out) {
    out = SampleDisplaySnapshot{};
    return false;
}
}
