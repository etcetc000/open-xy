#pragma once
#include <opxy/page_header.hpp>
#include <opxy/synth_shift.hpp>

namespace probeshift {
// Matches resources.display in definition.json: paths, points, nodes, texts, palette.
using Frame = opxy::draw::Frame<2, 64, 10, 16, 16>;
struct DisplayState { opxy::u32 phase; opxy::draw::PageHeader header; };
}
