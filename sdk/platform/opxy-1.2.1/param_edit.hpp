// The encoder-edit hook (param_edit.cpp), its own service: track control and
// extensions each link it and register what they do with an edit.
#pragma once
#include <opxy/types.hpp>

extern "C" {
// Stock's encoder edit calls its setter 8d5acf28 at 8d5ad5ae through this.
void sdk_param_edit_set(opxy::u32 project, opxy::u32 track, opxy::u32 index, opxy::i32 value)
    __asm__("sdk_param_edit_set");
// Before stock's setter: track control notes the parameter turned. Null without it.
extern void (*sdk_param_turned)(opxy::u32 track, opxy::u32 index) __asm__("sdk_param_turned");
// After it: the extensions' parameter_edit event. Null without extensions.
extern void (*sdk_parameter_edits)(opxy::u32 track, opxy::u32 index, opxy::i32 value) __asm__("sdk_parameter_edits");
}
