#include "arranger_bounce_destination.hpp"
#include "arranger_bounce_job.hpp"
#include "arranger_project.hpp"
#include "arranger_state.hpp"
#include "arranger_resolver.hpp"
#include "stock.hpp"
#include "barrier.hpp"
extern "C" {
bool sdk_arranger_bounce_job_begin(opxy::u32, opxy::u32) __asm__("sdk_arranger_bounce_job_begin");
bool sdk_arranger_bounce_job_busy() __asm__("sdk_arranger_bounce_job_busy");
opxy::u32 stock_gui_role() __asm__("stock_gui_role");
}
namespace {
using namespace opxy;
using namespace opxy::platform;
arranger::Model candidate, original_model;
u32 project_address, generation, track, pattern, engine, frames, result;
u32 length, steps, scale, source_steps;
bool pending, scope;
char sample_path[97];
u8* project() { return reinterpret_cast<u8*>(project_address); }
bool bound() {
    return project_address && word(0x840a0524) == project_address &&
        stock<u32()>(0x8d584224)() == generation && sdk_arranger_project_linear() && sdk_arranger_project_edit_ready();
}
bool prepare(u32 slot, u32 type, bool replace) {
    if (!sdk_arranger_project_edit_ready() || !sdk_arranger_project_linear() || (type != 2 && type != 3)) return false;
    const arranger::Clip* original = sdk_arranger_model().clip(slot);
    if (!original || original->track >= 7 || !original->length || original->length > 244800) return false;
    u8* p = reinterpret_cast<u8*>(word(0x840a0524));
    const u32 lane = original->track + 1;
    if (!p || ((p[12] | (u32(p[13]) << 8)) & (1u << lane))) return false;
    const u8* pool = p + 0xd7c + lane * 0x53f44;
    const u32 count = *reinterpret_cast<const u32*>(pool + 0x53f40);
    if (!count || count >= 16) return false;
    arranger::Clip value;
    if (!arranger_bounce_layout(sdk_arranger_model(), slot, count, replace, candidate, value, steps, scale)) return false;
    track = lane; pattern = count; engine = type; length = original->length;
    source_steps = scale == 3 ? length : (length + 1) / 2;
    return true;
}
}
bool sdk_arranger_bounce_destination_ready(opxy::u32 slot, opxy::u32 engine, bool replace) {
    return !sdk_arranger_bounce_job_busy() && prepare(slot, engine, replace);
}
bool sdk_arranger_bounce_destination_begin(opxy::u32 slot, opxy::u32 type, bool replace,
                                           const char* path, opxy::u32 count) {
    if (sdk_arranger_bounce_job_busy() || !path || count <= 6000 || count > 882000 || !prepare(slot, type, replace)) return false;
    u32 n = 0; while (path[n] && n < sizeof(sample_path) - 1) { sample_path[n] = path[n]; ++n; }
    if (!n || path[n]) return false;
    sample_path[n] = 0;
    project_address = word(0x840a0524); generation = stock<u32()>(0x8d584224)();
    frames = count; pending = true; result = 0;
    if (sdk_arranger_bounce_job_begin(engine, track)) return true;
    pending = false; result = 2; return false;
}
opxy::u32 sdk_arranger_bounce_destination_status() { return result; }
bool sdk_arranger_bounce_destination_busy() { return sdk_arranger_bounce_job_busy(); }
void sdk_arranger_bounce_destination_cancel() { pending = false; result = 2; sdk_arranger_bounce_job_cancel(); }
const opxy::u8* sdk_arranger_bounce_destination_source(opxy::u32 lane) {
    return scope && stock_gui_role() && lane == track && bound() ?
        project() + 0xd7c + track * 0x53f44 + pattern * 0x53f4 : nullptr;
}
opxy::i32 sdk_arranger_bounce_destination_index(const opxy::u8* pointer) {
    if (!sdk_arranger_bounce_destination_source(track)) return -1;
    return pointer == project() + 0x98 + 33u * project()[6] + track ? static_cast<i32>(pattern) : -1;
}
void sdk_arranger_bounce_destination_apply(void* raw) {
    // Native queue invokes this on GUI after worker preparation; never redirect
    // worker/audio lookups, and never modify a scene reference or instrument lock.
    if (!pending || !stock_gui_role() || !bound() || !static_cast<u8*>(raw)[0xdcc]) { result = 2; pending = false; return; }
    u8* pool = project() + 0xd7c + track * 0x53f44;
    u32* count = reinterpret_cast<u32*>(pool + 0x53f40);
    if (*count != pattern || ((project()[12] | (u32(project()[13]) << 8)) & (1u << track))) {
        result = 2; pending = false; return;
    }
    u8* target = pool + pattern * 0x53f4;
    for (u32 i = 0; i < 0x53f4; ++i) target[i] = pool[i];
    stock<void(u8*)>(0x8d5844d0)(target);
    barrier(); ++*count; barrier(); scope = true;
    const i32 original_octave = static_cast<signed char>(project()[0x40 + track]);
    stock<void(void*)>(0x8d4dda6c)(raw);
    // Preset application changes the per-track octave, not a source field.
    // Preserve all existing destination clips' keyboard mapping.
    stock<void(u32,i32)>(0x8d58573c)(track, original_octave);
    stock<void(u8*)>(0x8d5844d0)(target);
    target[0] = static_cast<u8>(steps); target[8] = static_cast<u8>(scale);
    if (engine == 3) {
        stock<void(u32,u32,const char*)>(0x8d5893bc)(track, 60, sample_path);
        stock<void(u32,u32,u32,u32)>(0x8d589ac0)(track, 60, frames, 0);
    } else {
        stock<void(u32,u32,const char*)>(0x8d5895dc)(track, 0, sample_path);
        // Exact sampler completion path8c0a44ae: native parameter52 assignment.
        stock<void(u32,u32,u32,u32)>(0x8d5acf28)(0x86bd027c, track, 52, 0x3ffffff8);
        stock<void(u32,u32,u32,i32,i32,u32,u32)>(0x8d589b8c)(track, 60, 60, -1, -1, frames, 0);
    }
    struct Note { i32 position; u32 duration; u8 pitch, velocity, flags, padding; };
    const Note note = {0, source_steps, 60, 100, 0, 0};
    stock<i64(void*,const Note*,i32)>(0x8d58f24c)(target + 0x457c, &note, 0);
    const u8* region = stock<const u8*(u32,u32)>(0x8d589370)(track, 60);
    const bool assigned = region && region >= target + 0x394c && region < target + 0x454c &&
        *reinterpret_cast<const u32*>(region) == frames && region[0x20] != 0 &&
        *reinterpret_cast<const u32*>(target + 0x18) == engine;
    const bool inserted = assigned && *reinterpret_cast<const u16*>(target + 0x457c + 0x786) == 1;
    scope = false; pending = false;
    if (!inserted) { --*count; barrier(); result = 2; return; }
    const auto same_project = []() { return word(0x840a0524) == project_address &&
        stock<u32()>(0x8d584224)() == generation; };
    if (!arranger_bounce_publish(sdk_arranger_model(), candidate, original_model,
        []() { return sdk_arranger_project_changed(true, byte(kSelectedTrack)); }, same_project)) {
        // A rejected publication can restore another project; never touch its
        // predecessor's pool or overwrite the new model in that case.
        if (same_project()) { barrier(); --*count; barrier(); }
        result = 2; return;
    }
    sdk_arranger_publish(true, false); result = 1;
}

void sdk_arranger_bounce_sort_regions(opxy::u32 supplied_track) {
    // Stock5893bc creates a region for its argument track but sorts the GUI's
    // selected track at589504. Bounce keeps that selection on the source lane.
    const u32 destination = scope && stock_gui_role() && bound() ? track : supplied_track;
    stock<void(u32)>(0x8d58938c)(destination);
}
