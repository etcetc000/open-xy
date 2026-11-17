// Experimental Morph HUD command types; no callback authorizes itself.
#pragma once
#include <opxy/types.hpp>
namespace morphcapture { namespace hud {
enum Action : opxy::u32 {
 None=0u, CaptureTrackA=1u, CaptureTrackB=2u,
 ToggleEdit=3u, JumpA=4u, JumpB=5u, LiveBypass=6u, SetS=7u
};
struct Event { opxy::u32 action, track; };
inline bool action_valid(opxy::u32 a) { return a>=CaptureTrackA && a<=SetS; }
inline bool event_valid(const Event& e) { return action_valid(e.action) && e.track<8u; }
}}
extern "C" {
// A HUD compositor calls this only after its physical ownership gate.
// A posted event is NOT automatically authorized: the consumer rechecks.
bool sdk_morph_hud_event_offer(opxy::u32 action,opxy::u32 focused_track)
    __asm__("sdk_morph_hud_event_offer");
// The machine must recheck the actual compositor witness and current context.
opxy::u32 sdk_morph_hud_frame_authorized(opxy::u32 track)
    __asm__("sdk_morph_hud_frame_authorized");
}
