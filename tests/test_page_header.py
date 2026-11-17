"""Shared header layout, page identity and interrupted animation contract."""
import os
from pathlib import Path
import subprocess
import tempfile
import unittest

from tooling.definitions import ROOT
from test_sample_services import HOST_CXX

PROGRAM = r'''
#include <opxy/page_header.hpp>
#include <cstring>
using namespace opxy::draw;
using opxy::u32;
using opxy::i32;
using opxy::u8;
typedef Frame<0, 0, 9, 16, 1> F;
const char* const labels[8] = {"One", "TWO", "Three", "Four", "Five", "Six", "Seven", "Eight"};
const char captions[8][header_capacity] = {"a", "b", "c", "d", "e", "f", "g", "h"};
int main() {
    PageHeader state; state.reset();
    Input input = {};
    F frame = {};
    place(frame, 0, 200 * unit, 100 * unit, unit, 0, keep);
    for (u32 i = 0; i < 4; ++i) input.shown[i] = i;
    page_header(state, input, frame, labels, captions, 1);
    if (!valid(frame) || state.position || frame.nodes[1].y || frame.nodes[0].x != 200 * unit) return 1;
    if (std::strcmp(frame.texts[0].chars, "one") || std::strcmp(frame.texts[1].chars, "two")) return 2;
    if (frame.texts[0].y != 10 * unit || frame.texts[8].y != 33 * unit || frame.texts[12].scale != unit) return 3;
    for (u32 i = 0; i < 4; ++i) input.shown[i] = i + 4;
    input.elapsed = 16;
    page_header(state, input, frame, labels, captions, 1);
    if (state.position != 0) return 4; // first switch frame has no jump
    page_header(state, input, frame, labels, captions, 1);
    const i32 before = state.position;
    if (before <= 0 || before >= unit) return 5;
    for (u32 i = 0; i < 4; ++i) input.shown[i] = i;
    page_header(state, input, frame, labels, captions, 1);
    if (state.position != before) return 6; // reversing keeps the current position
    input.elapsed = 154;
    page_header(state, input, frame, labels, captions, 1);
    if (state.position) return 7;
    for (u32 i = 0; i < 4; ++i) input.shown[i] = i + 4;
    ++input.track;
    page_header(state, input, frame, labels, captions, 1);
    if (state.position != unit || frame.nodes[1].y != 18 * unit || !valid(frame)) return 8;
    if (frame.texts[0].y != 28 * unit || frame.texts[8].y != 13 * unit || frame.texts[4].scale != unit) return 9;
    if (std::strcmp(frame.texts[0].chars, "five") || std::strcmp(frame.texts[8].chars, "one")) return 10;
    // Drone shape: page 2 shares the first parameter, so shown[0] cannot identify it.
    const u8 drone[2][4] = {{0,1,2,3}, {0,4,5,6}};
    input.reduced_motion = 1;
    for (u32 i = 0; i < 4; ++i) input.shown[i] = drone[1][i];
    page_header(state, input, frame, labels, captions, 1, drone);
    if (state.position != unit || std::strcmp(frame.texts[0].chars, "one") ||
        std::strcmp(frame.texts[8].chars, "one") || std::strcmp(frame.header[0], "a")) return 11;
    const u8 solar[2][4] = {{0,1,2,3}, {4,1,5,3}};
    for (u32 i = 0; i < 4; ++i) input.shown[i] = solar[0][i];
    page_header(state, input, frame, labels, captions, 1, solar);
    if (state.position || std::strcmp(frame.texts[1].chars, "two") || std::strcmp(frame.texts[9].chars, "two")) return 12;
    // Repeated presses, stale/large timing, track changes and reduced motion.
    u32 seed = 1;
    const i32 times[] = {-200, 0, 1, 17, 154, 0x7fffffff};
    for (u32 i = 0; i < 20000; ++i) {
        seed = seed * 1664525u + 1013904223u;
        const u32 page = seed >> 31;
        for (u32 c = 0; c < 4; ++c) input.shown[c] = solar[page][c];
        input.elapsed = times[(seed >> 8) % 6];
        input.reduced_motion = (seed & 31) == 0;
        if ((seed & 127) == 0) ++input.track;
        page_header(state, input, frame, labels, captions, 1, solar);
        if (!valid(frame) || state.position < 0 || state.position > unit) return 13;
    }
    char amount[header_capacity];
    header_amount(amount, header_capacity, 0x7fffffff);
    if (std::strcmp(amount, "100")) return 14;
    header_amount(amount, header_capacity, -1);
    if (std::strcmp(amount, "0")) return 15;
    return 0;
}
'''


# A synth's Shift row (SDK 1.13) in the header: both rows while Shift is held, page 1's
# entries on top and page 2's under them, as a drum kit's two Shift rows.
SHIFT_PROGRAM = r'''
#include <opxy/page_header.hpp>
#include <cstring>
using namespace opxy::draw;
using opxy::u32;
using opxy::i32;
using opxy::u8;
typedef Frame<0, 0, 9, 16, 1> F;
struct Row { u32 held, line; i32 values[8]; };
const char* const labels[8] = {"One", "Two", "Three", "Four", "Five", "Six", "Seven", "Eight"};
const char captions[8][header_capacity] = {"a", "b", "c", "d", "e", "f", "g", "h"};
// Entry 5 is absent.
const char* const shift_labels[8] = {"Noise", "Drift", "Octave", "Lowcut", "Slide", "", "Ratio", "Balance"};
const char shift_captions[8][header_capacity] = {"n", "d", "o", "l", "s", "", "r", "b"};
const u8 pages[2][4] = {{0, 1, 2, 3}, {4, 5, 6, 7}};
// What a renderer redraws: a text whose characters, place or size changed, a moved node.
static bool same_text(const Text& a, const Text& b) {
    return a.x == b.x && a.y == b.y && a.scale == b.scale && !std::strcmp(a.chars, b.chars);
}
static u32 texts_changed(const F& a, const F& b) {
    u32 n = 0;
    for (u32 i = 0; i < F::text_count; ++i) n += same_text(a.texts[i], b.texts[i]) ? 0 : 1;
    return n;
}
static u32 nodes_changed(const F& a, const F& b) {
    u32 n = 0;
    for (u32 i = 0; i < F::node_count; ++i) n += std::memcmp(&a.nodes[i], &b.nodes[i], sizeof(Node)) ? 1 : 0;
    return n;
}
static u32 headers_changed(const F& a, const F& b) {
    u32 n = 0;
    for (u32 i = 0; i < 4; ++i) n += std::strcmp(a.header[i], b.header[i]) ? 1 : 0;
    return n;
}
// The bright row's titles and values (texts 0..7), then the grey row's (8..15).
static bool shows(const F& f, const char* const (&want)[16]) {
    for (u32 i = 0; i < 16; ++i) if (std::strcmp(f.texts[i].chars, want[i])) return false;
    return true;
}
int main() {
    PageHeader state; state.reset();
    Input input = {};
    F frame = {};
    Row row = {};
    place(frame, 0, 200 * unit, 100 * unit, unit, 0, keep);
    for (u32 i = 0; i < 4; ++i) input.shown[i] = static_cast<u8>(i);
    input.elapsed = 16;
    page_header(state, input, frame, labels, captions, 1, pages, row, shift_labels, shift_captions);
    const char* const page1[16] = {"one", "two", "three", "four", "a", "b", "c", "d",
                                   "five", "six", "seven", "eight", "e", "f", "g", "h"};
    if (!valid(frame) || !shows(frame, page1)) return 1;
    const F rest = frame;
    // Shift held on page 1: page 1's entries bright on top, page 2's grey under them.
    row.held = 1;
    page_header(state, input, frame, labels, captions, 1, pages, row, shift_labels, shift_captions);
    const char* const held1[16] = {"noise", "drift", "octave", "lowcut", "n", "d", "o", "l",
                                   "slide", "", "ratio", "balance", "s", "", "r", "b"};
    if (!valid(frame) || !shows(frame, held1) || state.position) return 2;
    if (std::strcmp(frame.header[0], "n") || std::strcmp(frame.header[3], "l")) return 3;
    // Only the sixteen texts and the four header strings change; nothing moves.
    if (texts_changed(rest, frame) != 16 || nodes_changed(rest, frame) || headers_changed(rest, frame) != 4) return 4;
    for (u32 i = 0; i < 16; ++i)
        if (frame.texts[i].x != rest.texts[i].x || frame.texts[i].y != rest.texts[i].y ||
            frame.texts[i].scale != rest.texts[i].scale) return 5;
    // Released: the page's own titles and values again, exactly as before.
    row.held = 0;
    page_header(state, input, frame, labels, captions, 1, pages, row, shift_labels, shift_captions);
    if (texts_changed(rest, frame) || nodes_changed(rest, frame) || headers_changed(rest, frame) || !shows(frame, page1)) return 6;
    // Shift and a press: page 2's entries brighten under page 1's, the rows keep their order.
    row.held = 1;
    row.line = 1;
    for (u32 i = 0; i < 4; ++i) input.shown[i] = static_cast<u8>(4 + i);
    page_header(state, input, frame, labels, captions, 1, pages, row, shift_labels, shift_captions);
    input.elapsed = 154;
    page_header(state, input, frame, labels, captions, 1, pages, row, shift_labels, shift_captions);
    if (state.position != unit || frame.nodes[1].y != 18 * unit) return 7;
    const char* const held2[16] = {"slide", "", "ratio", "balance", "s", "", "r", "b",
                                   "noise", "drift", "octave", "lowcut", "n", "d", "o", "l"};
    if (!valid(frame) || !shows(frame, held2)) return 8;
    // The bright row is the lower one: page 2's entries sit at y = 28, page 1's at 13.
    if (frame.texts[0].y != 28 * unit || frame.texts[8].y != 13 * unit || frame.texts[4].scale != unit) return 9;
    if (std::strcmp(frame.header[0], "s") || frame.header[1][0] || std::strcmp(frame.header[3], "b")) return 10;
    const F held = frame;
    row.held = 0;
    page_header(state, input, frame, labels, captions, 1, pages, row, shift_labels, shift_captions);
    const char* const page2[16] = {"five", "six", "seven", "eight", "e", "f", "g", "h",
                                   "one", "two", "three", "four", "a", "b", "c", "d"};
    if (!shows(frame, page2) || texts_changed(held, frame) != 16 || nodes_changed(held, frame) ||
        headers_changed(held, frame) != 4) return 11;
    // A shared column (six controls): the entries still sit by entry, the bar by the shown slots.
    const u8 six[2][4] = {{0, 1, 2, 3}, {4, 5, 2, 3}};
    input.reduced_motion = 1;
    row.held = 1;
    for (u32 i = 0; i < 4; ++i) input.shown[i] = six[1][i];
    page_header(state, input, frame, labels, captions, 1, six, row, shift_labels, shift_captions);
    if (state.position != unit || !shows(frame, held2)) return 12;
    for (u32 i = 0; i < 4; ++i) input.shown[i] = six[0][i];
    page_header(state, input, frame, labels, captions, 1, six, row, shift_labels, shift_captions);
    if (state.position || !shows(frame, held1)) return 13;
    return 0;
}
'''


class PageHeaderTests(unittest.TestCase):
    def run_program(self, program):
        if not HOST_CXX:
            self.skipTest('No working host C++ compiler (set OPXY_TEST_HOST_CXX)')
        hidden = {'creationflags': subprocess.CREATE_NO_WINDOW} if os.name == 'nt' else {}
        with tempfile.TemporaryDirectory() as folder:
            source, binary = Path(folder) / 'header.cpp', Path(folder) / 'header.exe'
            source.write_text(program, encoding='utf-8')
            subprocess.run([HOST_CXX, '-std=c++11', '-O1', '-I', str(ROOT / 'sdk/include'),
                            str(source), '-o', str(binary)], check=True, capture_output=True, timeout=120, **hidden)
            result = subprocess.run([str(binary)], capture_output=True, timeout=30, **hidden)
            self.assertEqual(result.returncode, 0, result.stderr)

    def test_layout_shared_controls_and_motion(self):
        self.run_program(PROGRAM)

    def test_shift_shows_both_rows_and_changes_only_the_text(self):
        self.run_program(SHIFT_PROGRAM)
