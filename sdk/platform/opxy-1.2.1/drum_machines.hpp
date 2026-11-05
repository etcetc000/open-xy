// Owned synthesis kits using stock's per-key Drumline region transport.
#pragma once
#include <opxy/types.hpp>
#include "drum_storage.hpp"

namespace opxy { namespace platform {
struct DrumMachine {
    const char* id;
    int (*ready)();
    u32 model_count;
    void (*defaults)(u32 key, u8* model, u8* controls);
    const char* (*label)(u32 model, u32 slot);
    const char* (*model_name)(u32 model);
    u32 (*count)(u32 model);
    // Optional named groups of consecutive models (resources.modelGroups):
    // Shift and the first encoder choose a group, the second a model in it.
    u32 group_count;
    const u8* group_sizes;
    const char* const* group_names;
    // resources.padMix (SDK 1.10): Shift's first row is the model (all of
    // them, groups or not), the choke group and the key's stock pan and
    // level; its second, every key's, the key's pad fields.
    u32 pad_mix;
    // resources.padControls (SDK 1.10): up to two of the kit's own pad fields,
    // first on Shift's second row, each with its bits, a signed mask, a label
    // and the kit's caption for a value (null: the number).
    u32 pad_count;
    const u8* pad_bits;
    u32 pad_signs;
    const char* const* pad_labels;
    bool (*pad_text)(u32 field, i32 value, char* text);
    u32 engine() const { return static_cast<u32>(reinterpret_cast<__UINTPTR_TYPE__>(id)); }
    // A model's group (0 without groups) and the group's first model.
    u32 group(u32 model) const {
        u32 g = 0;
        for (u32 first = 0; g + 1 < group_count && model >= first + group_sizes[g]; ++g) first += group_sizes[g];
        return g;
    }
    u32 group_first(u32 g) const {
        u32 first = 0;
        for (u32 i = 0; i < g && i < group_count; ++i) first += group_sizes[i];
        return first;
    }
    // The pad field at a second-row position (0..3) of a padMix kit: the
    // kit's pad controls first, then FX I and FX II on positions 2 and 3.
    // False when the position holds none.
    bool pad_field(u32 position, u32& low, u32& bits, bool& sign) const {
        if (!pad_mix || position >= kDrumPadFields) return false;
        if (position >= kDrumKitPadFields) {
            low = kDrumSendLow + kDrumSendBits * (position - kDrumKitPadFields);
            bits = kDrumSendBits;
            sign = false;
            return true;
        }
        if (position >= pad_count) return false;
        low = drum_pad_low(position, pad_bits);
        bits = pad_bits[position];
        sign = (pad_signs >> position & 1u) != 0;
        return true;
    }
    // What Shift and the encoder at a position (0..3) choose for the selected
    // key, on the encoders' first row or (second) their second. A kit with
    // groups: the group, the model in it and the choke group, then nothing; one
    // without: the model and the choke group. A padMix kit: the model, the
    // choke group, pan and level on the first row, and its pad fields on the
    // second.
    enum Shift { kShiftNone, kShiftGroup, kShiftModel, kShiftChoke, kShiftPan, kShiftLevel,
                 kShiftPad, kShiftPad1, kShiftPad2, kShiftPad3 };   // kShiftPad + i: pad field i
    // Every key of a padMix kit has a second row for Shift's pad fields.
    bool second_shift_row() const { return pad_mix != 0; }
    // No tables: an inline function's statics would be defined in every
    // translation unit that uses it.
    Shift shift(u32 position, bool second) const {
        if (position >= 4) return kShiftNone;
        if (pad_mix && second) {
            u32 low, bits;
            bool sign;
            return pad_field(position, low, bits, sign) ? static_cast<Shift>(kShiftPad + position) : kShiftNone;
        }
        if (pad_mix) return position == 0 ? kShiftModel : position == 1 ? kShiftChoke : position == 2 ? kShiftPan : kShiftLevel;
        if (group_count && position < 2) return position == 0 ? kShiftGroup : kShiftModel;
        if (position < 2) return position == 0 ? kShiftModel : kShiftChoke;
        return group_count && position == 2 ? kShiftChoke : kShiftNone;
    }
    // A pad field's label: the kit's, or FX I and FX II.
    const char* pad_label(u32 position) const {
        return position < kDrumKitPadFields ? (position < pad_count ? pad_labels[position] : "")
             : position == kDrumKitPadFields ? "FX I" : "FX II";
    }
    // A slot the model uses: below its count and named. An unnamed slot inside
    // the count is a gap in the machine's own layout: blank and inert.
    bool used(u32 model, u32 slot) const {
        const char* name = slot < count(model) ? label(model, slot) : nullptr;
        return name && name[0];
    }
};
}}

extern "C" {
extern const opxy::platform::DrumMachine sdk_drum_machines[] __asm__("sdk_drum_machines");
extern const opxy::u32 sdk_drum_machine_count __asm__("sdk_drum_machine_count");
const opxy::platform::DrumMachine* sdk_drum_machine(opxy::u32 engine) __asm__("sdk_drum_machine");
opxy::u32 sdk_drum_alias(opxy::u32 engine) __asm__("sdk_drum_alias");
// Project load's check of a pattern's track object: the alias, after a padMix
// kit's per-hit lock words get their base with no lock there.
opxy::u32 sdk_drum_load_engine(opxy::u32 engine, opxy::u32 object) __asm__("sdk_drum_load_engine");
// GUI projection: the selected key's model and stored controls; track offsets
// (step locks, modulation) remain separate.
bool sdk_drum_values(opxy::u32 track, opxy::u8* model, opxy::u8* controls) __asm__("sdk_drum_values");
const char* sdk_drum_title(opxy::u32 engine, opxy::u32 slot) __asm__("sdk_drum_title");
// The selected key's canonical pad, 53..76, or 0 when the track is not an owned kit.
opxy::u32 sdk_drum_selected_pad(opxy::u32 track) __asm__("sdk_drum_selected_pad");
// The canonical pad a played note sounds on a track, 53..76, or 0. GUI only.
opxy::u32 sdk_drum_pad(opxy::u32 track, opxy::u32 note) __asm__("sdk_drum_pad");
// The selected key's choke group, as the Shift page names it.
const char* sdk_drum_choke(opxy::u32 track) __asm__("sdk_drum_choke");
// The same as a choice, 0 (none) to 3.
opxy::u32 sdk_drum_choke_choice(opxy::u32 track) __asm__("sdk_drum_choke_choice");
// The selected key's stock pan, -100..100 (0 centre), and its level in whole
// dB as a padMix kit plays it, -30..0; 0 when the track is not an owned kit
// (and the level 0 when the kit has not set the key up, drum_storage.hpp).
opxy::i32 sdk_drum_pan(opxy::u32 track) __asm__("sdk_drum_pan");
opxy::i32 sdk_drum_level(opxy::u32 track) __asm__("sdk_drum_level");
// The selected key's four pad fields (the kit's pad controls, FX I, FX II),
// each 0 when the kit has not set the key up or the field is not there.
void sdk_drum_pad_values(opxy::u32 track, opxy::i32* values) __asm__("sdk_drum_pad_values");
opxy::u32 sdk_drum_edit(opxy::u32 key, opxy::u32 control, const opxy::u32* event, opxy::u32 fine)
    __asm__("sdk_drum_edit");
// The instrument turn's key when it holds none (8c0b14b4) and the key it latches
// then (8c0b167a): on an owned kit's track the selected key and none (-1), so
// the SDK never holds a key itself (stock.hpp drum_edit_key); else stock's.
opxy::u32 sdk_drum_unheld_key(opxy::u32 track) __asm__("sdk_drum_unheld_key");
opxy::u32 sdk_drum_latch_key(opxy::u32 track) __asm__("sdk_drum_latch_key");
// Stock's preset import (8d4e35a8, its three calls hooked), then a padMix kit's
// loaded keys set up as the preset holds them (drum_values_stock.cpp).
opxy::u32 sdk_drum_preset_load(opxy::u32 preset, opxy::u32 flag) __asm__("sdk_drum_preset_load");
// A Shift press of encoder 0..3 on a kit with a second Shift row
// (synth_controls.cpp): moves the encoders, and Shift with them, to the other row.
// True when it did.
bool sdk_drum_shift_press(opxy::u32 track, opxy::u32 encoder) __asm__("sdk_drum_shift_press");
}
