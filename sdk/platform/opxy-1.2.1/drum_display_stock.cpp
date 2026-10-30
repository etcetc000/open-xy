// Stock OS 1.2.1 drum-page projection. The machine's panel callback supplies
// values and captions; this file shows them on the stock page and restores the
// page before every repaint. GUI role only. Hooks replace CALLs to the stock
// functions they forward to; the fade hook arrives through drum_page.s.
#include "sample_machines.hpp"
#include "stock_sample.hpp"
#include "stock.hpp"

extern "C" {
opxy::u32 sdk_sample_selected_engine() __asm__("sdk_sample_selected_engine");
const opxy::DrumPanel* sdk_sample_drum_view(const void*, void*, const opxy::i32*) __asm__("sdk_sample_drum_view");
int sdk_sample_drum_owned() __asm__("sdk_sample_drum_owned");
opxy::i32 sdk_sample_drum_fade_word(const void*) __asm__("sdk_sample_drum_fade_word");
opxy::u32 sdk_sample_drum_range(const opxy::i32*, void*, void*) __asm__("sdk_sample_drum_range");
void sdk_sample_drum_display(unsigned char*, const void*, opxy::u32, opxy::u32, opxy::u32) __asm__("sdk_sample_drum_display");
}

namespace {
using opxy::i32;
using opxy::u32;
using namespace opxy::platform;
typedef unsigned char Node;
u32& field(Node* node, u32 offset) { return *reinterpret_cast<u32*>(node + offset); }
Node* child(Node* node, u32 offset) { return reinterpret_cast<Node*>(field(node, offset)); }
const u32 kTextVtable = 0x840c5158;
bool is_text(Node* node) { return node && field(node, 0) == kTextVtable; }
i32 spacing(Node* node) { return static_cast<signed char>(node[156]); }

// Changes made to the last painted page, undone before its next repaint.
// Bounded to the reviewed page's six hidden nodes and two captions.
struct Visibility { Node* node; u32 shown; };
struct Caption { Node* node; u32 x, style, mode, spacing; };
Node* saved_page;
Visibility flags[8];
u32 flag_count;
Caption captions[2];
u32 caption_count;

// Stock GUI vector: allocator, begin, end, capacity. Audio uses Q31; the
// GUI getter supplies the same eight live values as IEEE float words.
struct Parameters { u32 allocator; const u32 *begin, *end, *capacity; };
static_assert(sizeof(Parameters) == 16, "Stock GUI parameter vector layout changed");

void visible(Node* node, u32 shown) {
    if (!node || flag_count >= 8) return;
    flags[flag_count++] = Visibility{node, node[92]};
    node[92] = static_cast<unsigned char>(shown);
}
// Center a caption on an encoder column, keeping its baseline and font, and
// tighten its spacing until stock's own width measure fits 110 px.
void caption(Node* node, u32 center, const char* text) {
    visible(node, 1);
    if (caption_count >= 2) return;
    captions[caption_count++] = Caption{node, field(node, 28), field(node, 88), field(node, 96), node[156]};
    field(node, 88) &= 0xffff0000u;
    field(node, 96) = 1;
    field(node, 28) = center << 10;
    set_text(node, text);
    while (stock<i32(u32, Node*, i32)>(0x8d5ffe74)(field(node, 100), node + 112, spacing(node)) > 110 &&
           spacing(node) > -6)
        node[156] = static_cast<unsigned char>(spacing(node) - 1);
}
void restore(Node* page) {
    if (page == saved_page) {
        for (u32 i = 0; i < flag_count; ++i) flags[i].node[92] = static_cast<unsigned char>(flags[i].shown);
        for (u32 i = 0; i < caption_count; ++i) {
            Node* node = captions[i].node;
            field(node, 28) = captions[i].x;
            field(node, 88) = captions[i].style;
            field(node, 96) = captions[i].mode;
            node[156] = static_cast<unsigned char>(captions[i].spacing);
        }
        // Stock never rewrites its two literal pan letters.
        set_text(child(page, 104), "L");
        set_text(child(page, 108), "R");
    }
    saved_page = nullptr;
    flag_count = caption_count = 0;
}
}

int sdk_sample_drum_owned() { return drum_controls(sdk_sample_selected_engine()); }

// 8d5355ac (through drum_page.s): the painter's fade, the record's fade word
// >> 8. A machine with pad settings keeps them there, so it paints no fade.
i32 sdk_sample_drum_fade_word(const void* region) {
    return pad_settings(sdk_sample_selected_engine()) ? 0 : static_cast<i32>(pad_word(region, pad_record::kFade)) >> 8;
}

// 8d535222: stock 8d4d6f34(region, &start, &end) normalizes the trim markers.
// An owned page shows the outer markers, which playback follows, instead of
// the inner loop markers.
u32 sdk_sample_drum_range(const i32* region, void* start, void* end) {
    u32 (*const range)(const i32*, void*, void*) = stock<u32(const i32*, void*, void*)>(0x8d4d6f34);
    if (!sdk_sample_drum_owned() || !region || region[pad_record::kFrames] <= 0) return range(region, start, end);
    const opxy::SampleRegion outer = sample_gui_region(region);
    const i32 header[3] = {region[pad_record::kFrames], i32(outer.begin), i32(outer.end)};
    return range(header, start, end);
}

// 8d5353a6: stock 8d534000 paints the drum page. An owned page paints the
// machine's panel; with Shift held, its two captions replace the pan row.
void sdk_sample_drum_display(Node* page, const void* region, u32 third, u32 fourth, u32 fifth) {
    restore(page);
    bool owned = sdk_sample_drum_owned() == 1;
    u32 shadow[8];
    u32 values[8];
    i32 parameters[8];
    Parameters projected;
    const opxy::DrumPanel* view = nullptr;
    const void* painted = region;
    if (owned && region) {
        const Parameters* live = reinterpret_cast<const Parameters*>(third);
        if (live && live->begin && reinterpret_cast<u32>(live->end) - reinterpret_cast<u32>(live->begin) == 32) {
            for (u32 i = 0; i < 8; ++i) {
                values[i] = live->begin[i];
                // Bound malformed float inputs before conversion (negative,
                // infinity and NaN cannot enter an authored callback).
                if (values[i] & 0x80000000u) values[i] = 0;
                else if (values[i] > 0x3f800000u) values[i] = 0x3f800000u;
                parameters[i] = values[i] == 0x3f800000u ? 0x7fffffff :
                    stock<i32(u32)>(0x040212cc)(values[i]);
            }
            view = sdk_sample_drum_view(region, shadow, parameters);
        }
        if (view) {
            painted = shadow;
            // The callback has already combined saved and live pitch/mode.
            // Avoid adding pitch twice or replacing its resolved play mode.
            // The caller's original vector still drives stock waveform trims.
            values[0] = 0x3f000000u;
            values[3] = view->repeat ? 0x3f000000u : 0x3e800000u;
            projected = Parameters{0, values, values + 8, values + 8};
            third = reinterpret_cast<u32>(&projected);
            // The shadow record already carries the panel's direction; scale
            // the saved volume (a float) by the panel's level.
            fourth = stock<u32(u32, u32)>(0x04021ac0)(fourth, q31_float_bits(view->level));
        }
        else owned = false;
    }
    stock<void(Node*, const void*, u32, u32, u32)>(0x8d534000)(page, painted, third, fourth, fifth);
    if (!owned || !region || !shift_held()) return;
    Node* shifted = static_cast<Node*>(find_node(page, "shifted"));
    if (!shifted || !is_text(child(page, 104)) || !is_text(child(page, 156))) return;
    Node* group = child(page, 96);
    if (!group) return;
    const u32 begin = field(group, 72), end = field(group, 76);
    if (end < begin || end - begin != 16) return;
    const u32* row = reinterpret_cast<const u32*>(begin);  // the pan row's four nodes
    if (row[1] != field(page, 104) || row[2] != field(page, 108)) return;
    saved_page = page;
    visible(shifted, 1);
    visible(reinterpret_cast<Node*>(row[0]), 0);
    visible(reinterpret_cast<Node*>(row[3]), 0);
    visible(child(page, 108), 0);
    visible(child(page, 148), 0);
    visible(child(page, 152), 0);
    caption(child(page, 104), 180, view->shifted[0]);
    caption(child(page, 156), 300, view->shifted[1]);
}
