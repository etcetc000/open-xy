// Step machines on stock's three condition keys (docs/abi-1.2.1.md#step-components).
// Stock decides those keys inside its component module rather than through
// objects, so the profile's `steps` service hooks two calls there: the one that
// registers stock's eleven component objects, after which the SDK registers the
// object of each step machine on a condition key, and the condition decision,
// which asks that machine's plays and otherwise keeps stock's rule. A build
// points a replaced key's picker name at its machine's descriptor, so which
// machine sits on which key is read at run time: any layout, one module.
#include "step_machines.hpp"
#include "stock.hpp"

using opxy::u32;
using opxy::platform::StepDescriptor;
using opxy::platform::stock;

extern "C" {
void sdk_step_register(void* manager) __asm__("sdk_step_register");
int sdk_step_condition(void* manager, u32 track, u32 step, u32 value, u32 kind) __asm__("sdk_step_condition");
}

namespace {
// Stock's picker names by component, its register-all (0x8d5835c0), register
// (0x8d583f54) and condition decision (0x8d583798): nonzero skips.
const u32 kNames = 0x8400d4a4;
const u32 kFirstCondition = 11, kConditions = 3;
// Stock's own names of the condition keys: "parameter", "component", "trigger".
const u32 kStockNames[kConditions] = {0x8400d9e8, 0x8400d9f4, 0x8400da00};

// The descriptor of the step machine replacing this condition key, or null: a
// build points a replaced key's name only at its machine's descriptor.
StepDescriptor* replaced(u32 component) {
    const u32 name = opxy::platform::word(kNames + 4 * component);
    return name == kStockNames[component - kFirstCondition] ? nullptr : reinterpret_cast<StepDescriptor*>(name);
}
}

// In place of stock's register-all call in the component manager's start (0x8d583828).
void sdk_step_register(void* manager) {
    stock<void(void*)>(0x8d5835c0)(manager);
    for (u32 component = kFirstCondition; component < kFirstCondition + kConditions; ++component) {
        if (StepDescriptor* machine = replaced(component)) {
            machine->tracks = ~0u;
            machine->component = component;
            stock<void(void*, const void*)>(0x8d583f54)(manager, &machine->methods);
        }
    }
}

// In place of the three calls to stock's condition decision: kind 0 is the
// parameter-lock key, 1 the component key and 2 the trig key.
int sdk_step_condition(void* manager, u32 track, u32 step, u32 value, u32 kind) {
    StepDescriptor* machine = kind < kConditions ? replaced(kFirstCondition + kind) : nullptr;
    if (!machine)
        return stock<int(void*, u32, u32, u32, u32)>(0x8d583798)(manager, track, step, value, kind);
    return machine->plays && !machine->plays(&machine->methods, track, step, value & 15) ? 1 : 0;
}
