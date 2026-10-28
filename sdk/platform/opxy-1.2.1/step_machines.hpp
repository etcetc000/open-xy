// A step machine's descriptor, shared by its adapter (step_object.cpp.in) and the
// condition runtime (step_conditions.cpp). A build points the replaced stock
// component's picker name at it (tooling/steps.py), so it starts with the name
// the picker shows. On the three condition keys stock has no object: the SDK
// registers the descriptor's own object, numbered as that key, and asks plays
// whether the step's trig, locks or other components happen.
#pragma once
#include <opxy/types.hpp>

namespace opxy { namespace platform {
struct StepDescriptor {
    char name[16];
    // A stock component object: method table, track mask, component number.
    const void* const* methods;
    u32 tracks;
    u32 component;
    // Nonzero lets the step's trig, locks or other components happen; null always does.
    int (*plays)(const void* object, u32 track, u32 step, u32 setting);
};
static_assert(__builtin_offsetof(StepDescriptor, methods) == 16, "The picker name comes first");
}}
