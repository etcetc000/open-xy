#include "opxy/draw.hpp"
using namespace opxy::draw;
using F = Frame<128, 4, 128, 80, 16>;
static F frame;
int main() {
    for (opxy::u32 p = 0; p < F::path_count; ++p) {
        frame.paths[p].key = p + 1;
        frame.paths[p].points[0] = {-64 * unit, 0};
        frame.paths[p].points[1] = {64 * unit, 0};
        frame.paths[p].points[2] = {64 * unit, 8 * unit};
        frame.paths[p].points[3] = {-64 * unit, 8 * unit};
        frame.nodes[p] = {128 * unit, 32 * unit, 2 * unit, unit, 0, p % 16, fill_and_stroke};
    }
    for (opxy::u32 t = 0; t < F::text_count; ++t) {
        frame.texts[t].scale = unit;
        frame.texts[t].chars[0] = 'A' + t % 16;
    }
    if (!valid(frame)) return 1;
    frame.paths[127].points[1].x = frame.paths[127].points[2].x = 32 * unit;
    if (!valid(frame) || frame.paths[0].points[1].x != 64 * unit) return 2;
    frame.paths[127].points[2].x = 65 * unit;
    if (valid(frame)) return 3;
    frame.paths[127].points[2].x = 32 * unit;
    frame.texts[79].scale = 3 * unit;
    return valid(frame) ? 4 : 0;
}
