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


class PageHeaderTests(unittest.TestCase):
    def test_layout_shared_controls_and_motion(self):
        if not HOST_CXX:
            self.skipTest('No working host C++ compiler (set OPXY_TEST_HOST_CXX)')
        hidden = {'creationflags': subprocess.CREATE_NO_WINDOW} if os.name == 'nt' else {}
        with tempfile.TemporaryDirectory() as folder:
            source, binary = Path(folder) / 'header.cpp', Path(folder) / 'header.exe'
            source.write_text(PROGRAM, encoding='utf-8')
            subprocess.run([HOST_CXX, '-std=c++11', '-O1', '-I', str(ROOT / 'sdk/include'),
                            str(source), '-o', str(binary)], check=True, capture_output=True, timeout=120, **hidden)
            result = subprocess.run([str(binary)], capture_output=True, timeout=30, **hidden)
            self.assertEqual(result.returncode, 0, result.stderr)
