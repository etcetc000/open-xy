// A note player's own page: a header with the player's captions and values,
// 24 input dots, a centered chord line, and an output keyboard showing emitted
// notes. The player supplies its input display. It paints in place of the stock
// Maestro page while the selected track plays the player (players.cpp). Include
// once, from the player's generated unit.
#pragma once
#include <opxy/page_header.hpp>
#include <opxy/player.hpp>
#include "players.hpp"
#include "player_activity.hpp"
#include "screen.hpp"

namespace opxy { namespace screen { namespace {
// Text the page has written, kept so an unchanged frame writes nothing.
struct PlayerText {
    static bool same(const char* a, const char* b) {
        while (*a && *a == *b) ++a, ++b;
        return *a == *b;
    }
    static void copy(char* to, const char* from, u32 size) {
        u32 i = 0;
        for (; i + 1 < size && from[i]; ++i) to[i] = from[i];
        to[i] = 0;
    }
    static void show(Leaf* node, char* shown, const char* text, u32 size) {
        if (!node || same(shown, text)) return;
        copy(shown, text, size);
        set_text(node, shown);
    }
};

// A one-page player's header: the stock four controls.
template <class Machine, bool Paged = Machine::paged> struct PlayerHeader {
    static const u32 kIds = 0;
    struct State {
        u32 titled;  // captions written
        char values[4][sizeof(player::Display::values[0])];
    };
    static bool bound(const Base* base) {
        for (u32 i = 0; i < 4; ++i)
            if (!base->values[i] || !base->titles[i]) return false;
        return true;
    }
    static void bind(Base*, State& state, const char* const*) {
        // The stock header starts with "0" in each value; an empty first value must still overwrite it.
        for (u32 i = 0; i < 4; ++i) state.values[i][0] = 1;
    }
    static void frame(Base* base, State& state, const player::Display& shown, i32) {
        if (!state.titled) {
            for (u32 i = 0; i < 4; ++i) set_text(base->titles[i], Machine::titles[i]);
            state.titled = 1;
        }
        for (u32 i = 0; i < 4; ++i)
            PlayerText::show(base->values[i], state.values[i], shown.values[i], sizeof(state.values[i]));
    }
};

// A paged player's header: both pages in place (opxy/page_header.hpp), on the
// bars and texts the SDK adds to the player's artwork over the stock header.
template <class Machine> struct PlayerHeader<Machine, true> {
    static const u32 kBars = draw::page_header_nodes, kTexts = draw::page_header_texts;
    static const u32 kIds = kBars + kTexts;
    typedef draw::Frame<0, 1, draw::page_header_nodes, draw::page_header_texts, 1> Frame;
    struct State {
        PathNode* bars[kBars];
        Leaf* texts[kTexts];
        draw::PageHeader header;
        draw::Input input;
        Frame frame;
        char shown[kTexts][draw::text_capacity];
    };
    static bool bound(const Base*) { return true; }
    static void bind(Base* base, State& state, const char* const* ids) {
        for (u32 i = 0; i < kBars; ++i) state.bars[i] = path_node(find(base, ids[i]));
        for (u32 i = 0; i < kTexts; ++i) {
            state.texts[i] = text_node(find(base, ids[kBars + i]));
            state.shown[i][0] = 1;  // apply every text on the first frame
        }
    }
    static void frame(Base*, State& state, const player::Display& shown, i32 elapsed) {
        draw::Input& input = state.input;
        input.elapsed = elapsed;
        input.track = selected_track();
        // The page a press has switched the selected track's player to.
        const u32 row = platform::player_row(input.track);
        for (u32 i = 0; i < 4; ++i) input.shown[i] = static_cast<u8>(row + i);
        draw::page_header(state.header, input, state.frame, Machine::titles, shown.values, 0);
        const u32 at[4] = {0, 4, 6, 7};
        for (u32 i = 0; i < kBars; ++i) {
            if (!state.bars[i]) continue;
            const draw::Node& node = state.frame.nodes[i];
            const i32 pose[4] = {node.scale, node.scale, node.x, node.y};
            set_words(&state.bars[i]->leaf, at, pose);
        }
        for (u32 i = 0; i < kTexts; ++i) {
            if (!state.texts[i]) continue;
            const draw::Text& text = state.frame.texts[i];
            const i32 pose[4] = {text.scale, text.scale, text.x, text.y};
            set_words(state.texts[i], at, pose);
            PlayerText::show(state.texts[i], state.shown[i], text.chars, sizeof(state.shown[i]));
        }
    }
};

// Machine: ids[] names the input dots, text, output keys and output dots, then
// a paged player's header bars and texts; titles[] names the header captions,
// four to a page; paged tells whether a press switches between two pages;
// display() fills the page from the selected track's player, false when that
// track plays no such player.
template <class Machine> struct PlayerPage {
    typedef PlayerHeader<Machine> Header;
    enum { kDots = player::display_dots };
    static const u32 kArtwork = kDots + 257;  // the IDs before the header's
    static_assert(sizeof(Machine::ids) / sizeof(Machine::ids[0]) == kArtwork + Header::kIds,
                  "Player page artwork IDs");
    // A hidden dot moves this far right, outside the display; Q10 pixels.
    static const i32 kHidden = 1024 << 10;
    static const i32 kCentre = 240 << 10;

    struct Object {
        Base base;
        PathNode* dots[kDots];
        PathNode* keys[128];
        PathNode* played[128];
        i32 home[kDots];        // each dot's authored x translation
        Leaf* text;
        u32 lit;               // the dots shown
        char line[sizeof(player::Display::text)];
        typename Header::State header;
    };

    // Centres a text leaf on kCentre; stock measures the shown text when it draws.
    static void centre(Leaf* text) {
        anchor_middle(text);
        const u32 at[3] = {0, 4, 6};
        const i32 value[3] = {1024, 1024, kCentre};
        set_words(text, at, value);
    }

    static void init(Object* o) {
        clear(o);
        if (!o->base.svg) return;
        bind_controls(&o->base);
        Header::bind(&o->base, o->header, Machine::ids + kArtwork);
        for (u32 i = 0; i < kDots; ++i) {
            o->dots[i] = path_node(find(&o->base, Machine::ids[i]));
            if (o->dots[i]) o->home[i] = o->dots[i]->leaf.matrix[6];
        }
        o->text = text_node(find(&o->base, Machine::ids[kDots]));
        if (o->text) centre(o->text);
        for (u32 i = 0; i < 128; ++i) {
            o->keys[i] = path_node(find(&o->base, Machine::ids[kDots + 1 + i]));
            o->played[i] = path_node(find(&o->base, Machine::ids[kDots + 129 + i]));
        }
        o->lit = ~0u;  // apply every dot on the first frame
        o->line[0] = 1;
    }

    // Incoming elapsed time is the dispatcher's, in Q10 seconds.
    static void frame(Object* o, i32 elapsed) {
        if (!o->base.svg || !Header::bound(&o->base)) return;
        player::Display shown = {};
        if (!Machine::display(selected_track(), shown)) shown.lit = 0;
        Header::frame(&o->base, o->header, shown, elapsed);
        if (o->text && !PlayerText::same(o->line, shown.text)) {
            PlayerText::copy(o->line, shown.text, sizeof(o->line));
            set_text(o->text, o->line);
        }
        if (shown.lit != o->lit) {
            for (u32 i = 0; i < kDots; ++i) {
                if (!o->dots[i] || ((shown.lit ^ o->lit) >> i & 1) == 0) continue;
                const u32 at[4] = {0, 4, 6, 7};
                const i32 value[4] = {1024, 1024, o->home[i] + (shown.lit >> i & 1 ? 0 : kHidden),
                                      o->dots[i]->leaf.matrix[7]};
                set_words(&o->dots[i]->leaf, at, value);
            }
            o->lit = shown.lit;
        }
        // Fit the full emitted range, retaining at least the keyboard's two
        // octaves. Notes at different octaves never fold onto the same key.
        const platform::PlayerRange range = platform::player_range(shown.played, shown.lowest_key);
        const i32 width = (444 << 10) / range.whites;
        const u32 at[4] = {0, 4, 6, 7};
        for (u32 n = 0; n < 128; ++n) {
            const bool visible = n >= range.low && n <= range.high;
            const bool black = platform::player_black(n);
            const i32 x = (18 << 10) + (static_cast<i32>(platform::player_white(n)) -
                          static_cast<i32>(platform::player_white(range.low))) * width;
            const i32 key_x = x + (black ? width * 7 / 10 : 0);
            if (o->keys[n]) {
                const i32 pose[4] = {width / 10, 1024, visible ? key_x : kHidden, 174 << 10};
                set_words(&o->keys[n]->leaf, at, pose);
            }
            if (o->played[n]) {
                const bool lit = visible && (shown.played[n / 32] & (1u << (n % 32)));
                const i32 pose[4] = {1024, 1024, lit ? x + (black ? width : width / 2) : kHidden,
                                    (black ? 195 : 211) << 10};
                set_words(&o->played[n]->leaf, at, pose);
            }
        }
        draw(&o->base);
    }
};
}}} // namespace opxy::screen::<unnamed>
