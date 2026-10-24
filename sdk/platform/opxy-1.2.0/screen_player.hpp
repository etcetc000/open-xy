// A note player's own page: the stock four-control header with the player's
// captions and values, 24 dots that show or hide, and one centred line of
// text. The player draws it from its State (opxy/player.hpp: display). It
// paints in place of the stock Maestro page while the selected track plays
// the player (players.cpp). Include once, from the player's generated unit.
#pragma once
#include <opxy/player.hpp>
#include "players.hpp"
#include "screen.hpp"

namespace opxy { namespace screen { namespace {
// Machine: ids[] names the 24 dots then the text line; titles[] the header
// captions; display() fills the page from the selected track's player, false
// when that track plays no such player.
template <class Machine> struct PlayerPage {
    enum { kDots = player::display_dots };
    static_assert(sizeof(Machine::ids) / sizeof(Machine::ids[0]) == kDots + 1, "Player page artwork IDs");
    // A hidden dot moves this far right, outside the display; Q10 pixels.
    static const i32 kHidden = 1024 << 10;
    // Approximate advance of the line's 22 px stock font; Q10 pixels.
    static const i32 kCharacter = 11 << 10;
    static const i32 kCentre = 240 << 10;

    struct Object {
        Base base;
        PathNode* dots[kDots];
        i32 home[kDots];        // each dot's authored x translation
        Leaf* text;
        u32 lit;               // the dots shown
        u32 titled;            // captions written
        char line[sizeof(player::Display::text)];
        char values[4][sizeof(player::Display::values[0])];
    };

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

    static void init(Object* o) {
        clear(o);
        if (!o->base.svg) return;
        bind_controls(&o->base);
        for (u32 i = 0; i < kDots; ++i) {
            o->dots[i] = path_node(find(&o->base, Machine::ids[i]));
            if (o->dots[i]) o->home[i] = o->dots[i]->leaf.matrix[6];
        }
        o->text = text_node(find(&o->base, Machine::ids[kDots]));
        o->lit = ~0u;  // apply every dot on the first frame
        o->line[0] = 1;
    }

    static void frame(Object* o, i32) {
        if (!o->base.svg) return;
        for (u32 i = 0; i < 4; ++i)
            if (!o->base.values[i] || !o->base.titles[i]) return;
        if (!o->titled) {
            for (u32 i = 0; i < 4; ++i) set_text(o->base.titles[i], Machine::titles[i]);
            o->titled = 1;
        }
        player::Display shown = {};
        if (!Machine::display(selected_track(), shown)) shown.lit = 0;
        for (u32 i = 0; i < 4; ++i) show(o->base.values[i], o->values[i], shown.values[i], sizeof(o->values[i]));
        if (o->text && !same(o->line, shown.text)) {
            copy(o->line, shown.text, sizeof(o->line));
            i32 length = 0;
            while (o->line[length]) ++length;
            const u32 at[4] = {0, 4, 6, 7};
            const i32 value[4] = {1024, 1024, kCentre - length * kCharacter / 2, o->text->matrix[7]};
            set_words(o->text, at, value);
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
        draw(&o->base);
    }
};
}}} // namespace opxy::screen::<unnamed>
