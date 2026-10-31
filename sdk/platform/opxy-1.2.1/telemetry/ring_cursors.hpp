#pragma once

namespace opxy_transport { namespace telemetry_ring_cursors {
// The stock TX completion callback accumulates completed bytes without masking.
// Its occupancy consumers mask this cursor. The enqueue writer uses a circular
// DAG and remains bounded; retain its original validation and comparison.
template<class State> inline void normalize_completed_read(State& state) {
    if (state.capacity == 0x8000u) state.read &= 0x7fffu;
}
}}
