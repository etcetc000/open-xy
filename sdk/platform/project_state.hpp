#pragma once
#include <opxy/project_state.hpp>
namespace opxy { namespace platform {
struct ProjectStateSpec { const char* name; u32 version, capacity; };
const char* project_state_status();
// Native I/O never invokes contributor code. Called only by controller sync.
void project_state_observe(const ProjectStateSpec&, void (*)(const extensions::ProjectState&));
bool project_state_publish(const ProjectStateSpec&, const extensions::ProjectToken&, const u8*, u32);
}}
