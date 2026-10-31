#include <opxy/extension.hpp>
#include <opxy/arrangement.hpp>
namespace opxy { namespace extensions { namespace arrangement_probe {
u32 cursor, ticks, transitions;
bool active = true;
bool enabled(const ArrangementContext&) { return active; }
void context(const ArrangementContext&) { ++transitions; }
void key(const ArrangementContext&, const ArrangementKeyEvent& e, const ArrangementActions& actions) {
    if (e.key != ArrangementKey::plus) return;
    if (e.pressed) { ++cursor; const u32 values[2] = {0, 0}; actions.submit(1, values, 2); }
}
void turn(const ArrangementContext&, const ArrangementTurn& e, const ArrangementActions&) {
    if (e.encoder) return;
    if (e.delta > 0) ++cursor;
    else if (cursor) --cursor;
}
void tick(const ArrangementContext&, const ArrangementActions&) { ++ticks; }
void render(const ArrangementContext&, i32, ArrangementFrame& f) {
    for (u32 p = 0; p < 128; ++p) f.paths[p].key = 1;
    for (u32 n = 0; n < 128; ++n) draw::hide(f, n);
    for (u32 t = 0; t < 80; ++t) { f.texts[t].scale = draw::unit; f.texts[t].chars[0] = 0; }
    f.paths[0].key = cursor + 1;
    f.paths[0].points[0] = {0, 0}; f.paths[0].points[1] = {32 * draw::unit, 0};
    f.paths[0].points[2] = {32 * draw::unit, 8 * draw::unit}; f.paths[0].points[3] = {0, 8 * draw::unit};
    f.nodes[0] = {static_cast<i32>(cursor % 400) * draw::unit, 64 * draw::unit,
                  draw::unit, draw::unit, 0, 15, draw::fill_and_stroke};
}
ArrangementContext controller_origin;
u32 reply_notes;
void intent(const Context&, const ArrangementIntent& event, const Actions& actions) {
    PatternInfo info{};
    if (event.code == 1 && actions.discover_pattern(event.values[0], event.values[1], info) &&
        actions.request_pattern_read(info.ref)) controller_origin = event.origin;
}
void pattern_result(const PatternReadResult& result, const Actions& actions) {
    if (result.success) actions.post_arrangement_reply(controller_origin,
        reinterpret_cast<const u8*>(result.notes), result.count * sizeof(Note));
}
void reply(const ArrangementContext&, const u8*, u32 size) { reply_notes = size / sizeof(Note); }
const ArrangementSurface arrangement = {enabled, context, {0, 1u << (54 - 32), 0}, 1, key, turn, tick, render, intent, reply, pattern_result};
extern constexpr Hooks hooks = Hooks().surface(&arrangement);
}}}
