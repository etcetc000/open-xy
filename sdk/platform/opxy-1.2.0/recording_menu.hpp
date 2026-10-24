#pragma once

namespace opxy { namespace platform {
// GUI-loop service. Adds one session-only recording setting after stock startup.
void recording_menu_sync();

// Supplied by the extension runtime. A null name means there is no valid policy.
const char* recording_policy_name();
void recording_mode_set(bool enabled);
bool recording_mode_enabled();
}}
