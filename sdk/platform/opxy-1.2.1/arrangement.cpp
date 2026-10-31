#include "arrangement.hpp"
#include "arrangement_descriptor.hpp"
#include "arrangement_input.hpp"
#include "arrangement_queue.hpp"
#include "arrangement_registration.hpp"
#include "barrier.hpp"
#include "screen.hpp"

// Use the shared retained renderer's geometry, transform and text application.
namespace opxy { namespace binding {
struct DisplayState { u32 unused; };
using Frame = extensions::ArrangementFrame;
inline void display_reset(DisplayState*, Frame* f) {
    *f = Frame{};
    for (u32 p = 0; p < Frame::path_count; ++p) f->paths[p].key = 1;
    for (u32 i = 0; i < Frame::node_count; ++i) draw::hide(*f, i);
    for (u32 i = 0; i < Frame::text_count; ++i) f->texts[i].scale = draw::unit;
}
inline void display_render(DisplayState*, const draw::Input*, Frame*) {}
inline bool display_valid(const Frame* f) { return draw::valid(*f); }
}}
#include "screen_drawing.hpp"
#include "arrangement_artwork.hpp"

extern "C" {
opxy::u32 stock_gui_role() __asm__("stock_gui_role");
extern const char arrangement_slots[] __asm__("__stock_screen_slots");
}
namespace {
using namespace opxy;
using namespace opxy::screen;
using platform::stock;
using extensions::ArrangementContext;
using extensions::ArrangementSurface;
struct Artwork { static const char* const ids[224]; };
const char* const Artwork::ids[224] = { OPXY_ARRANGEMENT_IDS };
using Renderer = Drawing<Artwork>;
Renderer::Object object;
u32 page, presented;
platform::ArrangementInput input;
u32 held_generation[72], held_identity[72], held_track[72];
platform::ArrangementQueue<256> events;
platform::ArrangementPublicationSlot publication;
ArrangementContext previous;
bool have_context, gui_callback, artwork_attempted;
ArrangementContext gui_context;
platform::ArrangementMailbox<extensions::ArrangementIntent> intents;
struct Reply { ArrangementContext origin; u32 size; u8 data[2048]; };
platform::ArrangementMailbox<Reply> replies;
Reply controller_reply, gui_reply;
bool submit(u32 code, const u32* values, u32 count) {
    if (!gui_callback || !gui_context.visible || count > 4 || (count && !values)) return false;
    extensions::ArrangementIntent intent = {gui_context, code, {0, 0, 0, 0}};
    for (u32 i = 0; i < count; ++i) intent.values[i] = values[i];
    return intents.put(intent, platform::barrier);
}
const extensions::ArrangementActions gui_actions = {submit};
const ArrangementSurface* owner() {
    const auto* o = sdk_arrangement_owner();
    return o && o->enabled && o->render && platform::arrangement_mask_valid(*o) ? o : nullptr;
}
bool registered() {
    return page && object.base.vtable &&
        reinterpret_cast<Base* const*>(arrangement_slots)[page] == &object.base;
}
ArrangementContext context(bool visible) {
    return {stock<u32()>(0x8d584224)(), platform::word(0x840a0524), platform::selected_track(), presented, visible};
}
bool enabled() {
    const auto* o = owner();
    return o && registered() && o->enabled(context(false));
}
bool artwork_ready() {
    if (!object.base.svg) return false;
    for (u32 i = 0; i < Renderer::path_count; ++i) if (!object.nodes[i] || !object.geometry[i]) return false;
    for (u32 i = 0; i < Renderer::text_count; ++i) if (!object.texts[i]) return false;
    for (u32 i = 0; i < Renderer::palette_count; ++i) if (!object.palette[i]) return false;
    return true;
}
void initialize_artwork(Renderer::Object* o) {
    if (!artwork_attempted) {
        artwork_attempted = true;
        // Fixed validated markup: use the stock in-memory parser, never the
        // file loader (missing files take a stock fatal-error path).
        o->base.svg = stock<Document*(const char*)>(0x8d5f2160)(arrangement_svg);
        if (o->base.svg) stock<void(const Document*)>(0x8d521028)(o->base.svg);
    }
    Renderer::init(o);
}
bool visible() {
    if (!enabled() || !artwork_ready()) return false;
    const u32* controller = stock<const u32*()>(0x8c0944b0)();
    return controller && controller[1] == 40;
}
ArrangementContext notify(bool shown) {
    const auto now = context(shown);
    if (!have_context || now.visible != previous.visible || now.project != previous.project || now.identity != previous.identity || now.track != previous.track) {
        previous = now; have_context = true;
        if (const auto* o = owner()) if (o->context) o->context(now);
    }
    return now;
}
void frame(Renderer::Object* o, i32 elapsed) {
    if (!visible() || !o->base.svg) return;
    const auto now = notify(true);
    owner()->render(now, elapsed, display_frame);
    if (!draw::valid(display_frame)) return;
    for (u32 p = 0; p < Renderer::path_count; ++p) Renderer::apply_path(o, p);
    for (u32 n = 0; n < Renderer::node_count; ++n) Renderer::apply_node(o, n);
    for (u32 t = 0; t < Renderer::text_count; ++t) Renderer::apply_text(o, t);
    for (u32 i = 0; i < 4; ++i) {
        if (Leaf* n = text_node(o->base.values[i])) set_text(n, display_frame.header[i]);
        if (Leaf* n = text_node(o->base.titles[i])) set_text(n, "");
    }
    opxy::screen::draw(&o->base);
    ++presented;
}
void destroy_artwork(Renderer::Object* o) {
    publication.publish({0, 0, 0, 0, 0}, platform::barrier);
    const Document* document = o->base.svg;
    o->base.svg = nullptr;
    if (document) {
        const u32* methods = *reinterpret_cast<const u32* const*>(document);
        stock<void(const Document*)>(methods[1])(document);
    }
    base_destroy(&o->base); clear(o); page = 0;
}
const VTable<Renderer> vtable = {&destroy_artwork, &destroy_artwork,
    reinterpret_cast<const void*>(0x8d51fcd4), &frame, &initialize_artwork,
    reinterpret_cast<const void*>(0x8c0867f0)};
}
void arrangement_native_startup() {
    if (!stock_gui_role() || !owner() || page || object.base.vtable) return;
    Base** slots = stock<Base**()>(0x8d52077c)();
    if (reinterpret_cast<const void*>(slots) != reinterpret_cast<const void*>(arrangement_slots)) return;
    const u32 available = platform::arrangement_free_page(slots, OPXY_ARRANGEMENT_PAGE_LIMIT);
    if (!available) return;
    base_construct(&object.base, available, nullptr);
    object.base.vtable = &vtable;
    if (slots[available] == &object.base) page = available;
}
int arrangement_native_owned(u32 p) { return p == page && registered(); }
bool published(platform::ArrangementPublication& out) {
    return publication.read(out, platform::barrier) && out.generation == stock<u32()>(0x8d584224)() &&
        out.identity == platform::word(0x840a0524);
}
u32 arrangement_native_current_page(void*) {
    platform::ArrangementPublication now{};
    return published(now) && now.enabled ? now.page : 47;
}
void arrangement_native_poll() {
    if (!stock_gui_role()) return;
    if (registered() && !artwork_attempted) initialize_artwork(&object);
    const auto now = notify(visible());
    publication.publish({now.project, now.identity, page,
        enabled() && (!artwork_attempted || artwork_ready()) ? 1u : 0u, now.visible ? 1u : 0u}, platform::barrier);
    platform::ArrangementEvent event;
    for (u32 n = 0; n < 32 && events.pop(event, platform::barrier); ++n) {
        if (event.generation != now.project || event.identity != now.identity ||
            (!now.visible && (event.turn || event.value))) continue;
        const auto* o = owner();
        if (!o) continue;
        const ArrangementContext at = {event.generation, event.identity, event.track, presented, now.visible};
        gui_context = at; gui_callback = true;
        if (event.turn) { if (o->turn) o->turn(at, {event.key, event.value, event.shifted}, gui_actions); }
        else if (o->key) o->key(at, {static_cast<extensions::ArrangementKey>(event.key), event.value != 0, event.shifted}, gui_actions);
        gui_callback = false;
    }
    if (const auto* o = owner()) {
        if (replies.take(gui_reply, platform::barrier) && gui_reply.origin.project == now.project &&
            gui_reply.origin.identity == now.identity && o->reply) o->reply(now, gui_reply.data, gui_reply.size);
        gui_context = now; gui_callback = true;
        if (o->tick) o->tick(now, gui_actions);
        gui_callback = false;
    }
}
// These two functions run on the controller task. Never call contributor code
// or access its mutable GUI state here, even though this is the master CPU.
bool controller_visible(platform::ArrangementPublication& now) {
    if (!published(now) || !now.visible) return false;
    const u32* controller = stock<const u32*()>(0x8c0944b0)();
    return controller && controller[1] == 40;
}
bool arrangement_native_consume_raw(const void* raw) {
    if (!stock_gui_role() || !raw) return false;
    const u32* words = static_cast<const u32*>(raw);
    const u32 key = words[0];
    if (key >= 26 && key <= 49) return false; // immediate keyboard path is not this queue's producer
    const auto* o = owner();
    const bool captured = key < 72 && o && (o->keys[key / 32] & (1u << (key % 32)));
    platform::ArrangementPublication observed{};
    return input.key(key, words[1] != 0, captured && controller_visible(observed),
        [&](bool down, bool shift) {
            const platform::ArrangementEvent event = {
                down ? observed.generation : held_generation[key],
                down ? observed.identity : held_identity[key],
                down ? platform::selected_track() : held_track[key], key, down ? 1 : 0, false, shift};
            if (!events.push(event, !down, platform::barrier)) return false;
            if (down) { held_generation[key] = event.generation; held_identity[key] = event.identity; held_track[key] = event.track; }
            return true;
        });
}
bool arrangement_native_consume_encoder(u32 encoder, const void* raw) {
    platform::ArrangementPublication observed{};
    if (!stock_gui_role() || !raw || encoder >= 8 || !controller_visible(observed)) return false;
    const auto* o = owner();
    if (!o || !(o->encoders & (1u << (encoder % 4)))) return false;
    const platform::ArrangementEvent event = {observed.generation, observed.identity,
        platform::selected_track(), encoder % 4, static_cast<const i32*>(raw)[1], true, encoder >= 4 || input.shifted()};
    return events.push(event, false, platform::barrier);
}
bool arrangement_native_take_intent(extensions::ArrangementIntent* out) {
    if (!out || !intents.take(*out, platform::barrier)) return false;
    return out->origin.project == stock<u32()>(0x8d584224)() && out->origin.identity == platform::word(0x840a0524);
}
bool arrangement_native_reply(const ArrangementContext& expected, const u8* bytes, u32 size) {
    if (size > 2048 || (size && !bytes) || replies.pending ||
        expected.project != stock<u32()>(0x8d584224)() || expected.identity != platform::word(0x840a0524)) return false;
    controller_reply.origin = expected; controller_reply.size = size;
    for (u32 i = 0; i < size; ++i) controller_reply.data[i] = bytes[i];
    return replies.put(controller_reply, platform::barrier);
}
namespace { const opxy::platform::ArrangementDescriptor arrangement_descriptor = {
    arrangement_native_startup, arrangement_native_owned, arrangement_native_current_page,
    arrangement_native_consume_raw, arrangement_native_consume_encoder, arrangement_native_poll,
    arrangement_native_take_intent, arrangement_native_reply}; }
