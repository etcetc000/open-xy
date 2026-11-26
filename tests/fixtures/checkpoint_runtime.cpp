#include <extension_runtime.cpp>
#include <cstdio>
#include <cstdlib>
using namespace opxy;
using namespace opxy::platform;
using Command = extensions::CheckpointCommand;
using Result = extensions::CheckpointResult;
namespace {
void require(bool value, const char* message) {
    if (!value) { std::fprintf(stderr, "%s\n", message); std::exit(1); }
}
RecordingContext context = {1, 2, 0, 7680, 3, 255, true, true};
u32 calls, mode;
Result owner_result, observer_result, second_result;
extensions::Actions retained;
extensions::Context retained_context;
void observer(const extensions::Context& c, const extensions::Control&, const extensions::Actions& a) {
    observer_result = a.checkpoint(c, Command::restore);
}
void owner(const extensions::Context& c, const extensions::Control&, const extensions::Actions& a) {
    retained = a; retained_context = c;
    if (mode == 1) ++context.project;
    owner_result = a.checkpoint(c, Command::store);
    second_result = a.checkpoint(c, Command::restore);
}
const extensions::Hooks observers = extensions::Hooks().with_queued_control(observer);
const extensions::Hooks owners = extensions::Hooks().with_queued_control(owner);
const u32 keys[] = {50, 1}, permissions[] = {1, 0, 2};
const __UINT64_TYPE__ chords[] = {0, (1ull << 50) | (1ull << 53) | (1ull << 54)};
const ExtensionRegistry known_registry = {keys, 2, permissions, 3, chords};
const ExtensionEntry known_entries[] = {{"observer", 1, action_notify, &observers},
                                 {"checkpoint", 2, action_checkpoint, &owners}};
Result backend(Command) { ++calls; return Result::success; }
}
namespace opxy { namespace platform {
const ExtensionEntry* extension_collection(u32& count) { count = 2; return known_entries; }
const ExtensionRegistry& extension_registry() { return known_registry; }
const CheckpointBackend& extension_checkpoint_backend() { static const CheckpointBackend b = {nullptr, backend}; return b; }
RecordingContext extension_context(bool& playing) { playing = true; return context; }
TrackPattern extension_pattern(u32) { return {2, 7680, 3, true}; }
i32 extension_position(u32) { return 0; }
u32 extension_scaled_delta(u32, u32 n) { return n; }
i32 extension_collision_radius(u32) { return 180; }
bool extension_record_held() { return false; }
u32 extension_write(u32, const RecordedNote*, u32, bool) { return 0; }
void extension_notify(const char*) {}
}}
int main() {
    require(extensions_modifier(1) && !extensions_modifier(50) && extensions_chord(1, 54) &&
            !extensions_chord(1, 7), "only enabled registry chords recognized");
    extensions_queued_control({54, true, true, false, 0, 1});
    require(calls == 1 && owner_result == Result::success && observer_result == Result::unavailable &&
            second_result == Result::unavailable, "one checkpoint action for its exclusive owner");
    require(retained.checkpoint(retained_context, Command::restore) == Result::unavailable, "saved action cannot run later");
    extensions_queued_control({54, true, true, true, 0});
    extensions_queued_control({54, true, false, false, 0, 1});
    extensions_queued_control({50, true, true, true, 0});
    require(calls == 1, "plain or rejected controls never grant checkpoint");
    mode = 1; extensions_queued_control({53, true, true, false, 0, 1});
    require(calls == 1 && owner_result == Result::changed, "stale context cannot change scene state");
    return 0;
}
