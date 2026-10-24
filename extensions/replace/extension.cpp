#include <opxy/extension.hpp>
#include "policy.hpp"

namespace opxy { namespace extensions { namespace replace {
namespace {
Policy state;
void reset() { state.reset(); }
void recorded(const RecordedSlot& note) { state.recorded(note); }
void select(const RecordingFrame& frame, RecordingSelection& out) { state.select(frame, out); }
const RecordingPolicy policy = {"replace", reset, recorded, select};
}
extern const Hooks hooks = {nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, &policy};
}}}
