"""The SDK effect page: stock's boxes and bars, bright and dim rows, the sliding band, badge, meters and motion."""
import os
from pathlib import Path
import subprocess
import tempfile
import unittest

from tooling.definitions import ROOT
from test_sample_services import HOST_CXX

PROGRAM = r'''
#include <opxy/effect_page.hpp>
#include <cstring>
using namespace opxy::draw;
using opxy::u32;
using opxy::i32;
// The effect's own node 0, then the page's 20 from node 1.
typedef Frame<0, 0, 21, 16, 1> F;
const char* const labels[8] = {"Rate", "Depth", "Delay", "Feedback", "Shape", "Stereo", "Level", "Dry"};
const char captions[8][header_capacity] = {"a", "b", "c", "d", "e", "f", "g", "h"};
const u32 band_node = 1, bright_box = 2, dim_box = 5, bright_bar = 9, dim_bar = 13, meters = 17, numeral = 19;
bool whole(i32 q10) { return q10 % unit == 0; }
// Nothing the page draws is repainted: every node keeps its authored fill.
bool unpainted(const F& frame) {
    for (u32 i = 0; i < F::node_count; ++i)
        if (frame.nodes[i].paint != keep) return false;
    return true;
}
bool at(const F& frame, u32 node, i32 x, i32 y) { return frame.nodes[node].x == x * unit && frame.nodes[node].y == y * unit; }
// A meter line: its drop below the band's middle row.
bool meter(const F& frame, u32 m, i32 band, i32 drop) { return at(frame, meters + m, m ? 440 : 0, band + 32 + drop); }
// The edited row's boxes and bars bright, the other's dim; the fourth bright column is the band.
bool rows(const F& frame, u32 lit) {
    const i32 top[2] = {36, 133};
    for (u32 col = 0; col < 4; ++col) {
        const i32 x = 40 + 100 * static_cast<i32>(col);
        if ((col < 3 && !at(frame, bright_box + col, x, top[lit])) || !at(frame, dim_box + col, x, top[1 - lit])) return false;
        if (frame.nodes[bright_bar + col].x != x * unit || frame.nodes[dim_bar + col].x != x * unit) return false;
    }
    return true;
}
// A bar's top below its box's, as stock 1.2.1 draws it: floor(57 (1 - value)).
i32 bar(const F& frame, u32 node, i32 top) { return whole(frame.nodes[node].y) ? frame.nodes[node].y / unit - top : -1; }
int main() {
    PageHeader state; state.reset();
    Input input = {};
    F frame = {};
    place(frame, 0, 200 * unit, 100 * unit, unit, 0, keep);  // the effect's own node
    input.track = 14;
    for (u32 i = 0; i < 4; ++i) input.shown[i] = i;
    input.slots[0] = 0x7fffffff;  // full scale: the bar at the box's top
    input.slots[1] = 0x40000000;  // half: floor(28.5)
    input.slots[3] = -5;          // negative: at the bottom
    input.slots[4] = 0x20000000;  // a quarter: floor(42.75)
    effect_page(state, input, frame, labels, captions, 1);
    if (!valid(frame) || state.position || !unpainted(frame)) return 1;
    if (!at(frame, band_node, 0, 36) || !rows(frame, 0)) return 2;
    if (bar(frame, bright_bar, 36) != 0 || bar(frame, bright_bar + 1, 36) != 28 || bar(frame, bright_bar + 2, 36) != 57 ||
        bar(frame, bright_bar + 3, 36) != 57 || bar(frame, dim_bar, 133) != 42) return 3;
    // Meters at rest: on the band's middle row at each end.
    if (!meter(frame, 0, 36, 0) || !meter(frame, 1, 36, 0)) return 4;
    if (frame.nodes[0].x != 200 * unit) return 5;
    // Bright titles 0..3 on the edited row, dim 8..11 on the other; lower case, 5 px above the boxes.
    if (std::strcmp(frame.texts[3].chars, "feedback") || frame.texts[0].y != 31 * unit) return 6;
    if (std::strcmp(frame.texts[8].chars, "shape") || frame.texts[8].y != 128 * unit) return 7;
    // Captions 80 px below the box's top, as stock's.
    if (std::strcmp(frame.texts[4].chars, "a") || frame.texts[4].y != 116 * unit || frame.texts[12].y != 213 * unit)
        return 8;
    // Send A draws one numeral stroke.
    if (!at(frame, numeral, 213, 9) || frame.nodes[numeral + 1].x != hidden_x * unit) return 9;
    if (std::strcmp(frame.header[0], "a")) return 10;
    // Each line drops trunc(70 x level), as stock moves its own: 69 at full scale, 35 at -6 dB.
    input.level_in = 0x7fffffff;
    input.level_out = 0x40000000;
    effect_page(state, input, frame, labels, captions, 1);
    if (!meter(frame, 0, 36, 69) || !meter(frame, 1, 36, 35) || !valid(frame)) return 11;
    i32 drop = 0;
    for (long long level = 1; level <= 0x7fffffff; level += level / 3 + 1) {
        input.level_in = static_cast<i32>(level);
        effect_page(state, input, frame, labels, captions, 1);
        const i32 now = frame.nodes[meters].y / unit - 68;
        if (now < drop || now != static_cast<i32>(70 * level >> 31) || !whole(frame.nodes[meters].y)) return 12;
        drop = now;
    }
    input.level_in = 0x7fffffff;
    // Every value keeps every 8 px bar inside its 65 px box, falling as the value falls.
    i32 last = 0;
    for (long long value = 0x7fffffff; value >= -0x80000000ll; value -= 0x00ffffff) {
        input.slots[0] = static_cast<i32>(value);
        effect_page(state, input, frame, labels, captions, 1);
        const i32 offset = bar(frame, bright_bar, 36);
        if (offset < last || offset > 57) return 13;
        last = offset;
    }
    if (last != 57) return 13;
    // Page 2 eases in: the band and its meters slide on whole pixels, the boxes stay put.
    input.slots[0] = 0x7fffffff;
    for (u32 i = 0; i < 4; ++i) input.shown[i] = i + 4;
    input.elapsed = 16;
    effect_page(state, input, frame, labels, captions, 1);
    effect_page(state, input, frame, labels, captions, 1);
    const i32 band = frame.nodes[band_node].y / unit;
    if (band <= 36 || band >= 133 || !whole(frame.nodes[band_node].y) || !meter(frame, 0, band, 69)) return 14;
    if (!rows(frame, state.position >= unit / 2 ? 1 : 0) || !unpainted(frame)) return 15;
    input.elapsed = 154;
    effect_page(state, input, frame, labels, captions, 1);
    if (state.position != unit || !at(frame, band_node, 0, 133) || !meter(frame, 1, 133, 35)) return 16;
    // The bright set has moved to page 2's row, the dim one to page 1's, each with its row's values.
    if (!rows(frame, 1) || bar(frame, bright_bar, 133) != 42 || bar(frame, dim_bar, 36) != 0) return 17;
    if (std::strcmp(frame.texts[0].chars, "shape") || frame.texts[0].y != 128 * unit) return 18;
    if (std::strcmp(frame.texts[8].chars, "rate") || frame.texts[8].y != 31 * unit) return 19;
    if (std::strcmp(frame.header[0], "e") || !unpainted(frame)) return 20;
    // Send B shows two strokes.
    input.track = 15;
    effect_page(state, input, frame, labels, captions, 1);
    if (!at(frame, numeral, 211, 9) || !at(frame, numeral + 1, 215, 9) || !valid(frame)) return 21;
    // Captions are stock's: floor(100 * value / 2^31) as two digits.
    const struct { i32 value; const char* text; } captions[] = {
        {0x7fffffff, "99"}, {0x40000000, "50"}, {-1, "00"}, {0x20000000, "25"}, {0x60000000, "75"},
        {0x1fffffff, "24"},  // just under a quarter, as stock shows it
        {0x3fffffff, "49"}, {0x59999999, "69"}, {0x01000000, "00"}, {0x7b000000, "96"}};
    for (u32 i = 0; i < sizeof captions / sizeof captions[0]; ++i) {
        char number[header_capacity];
        header_number(number, header_capacity, captions[i].value);
        if (std::strcmp(number, captions[i].text)) return 30 + i;
    }
    return 0;
}
'''


class EffectPageTests(unittest.TestCase):
    def test_rows_bars_badge_meters_and_motion(self):
        if not HOST_CXX:
            self.skipTest('No working host C++ compiler (set OPXY_TEST_HOST_CXX)')
        hidden = {'creationflags': subprocess.CREATE_NO_WINDOW} if os.name == 'nt' else {}
        with tempfile.TemporaryDirectory() as folder:
            source, binary = Path(folder) / 'page.cpp', Path(folder) / 'page.exe'
            source.write_text(PROGRAM, encoding='utf-8')
            subprocess.run([HOST_CXX, '-std=c++11', '-O1', '-Wall', '-Werror', '-I', str(ROOT / 'sdk/include'),
                            str(source), '-o', str(binary)], check=True, capture_output=True, timeout=120, **hidden)
            result = subprocess.run([str(binary)], capture_output=True, timeout=30, **hidden)
            self.assertEqual(result.returncode, 0, result.stderr)
