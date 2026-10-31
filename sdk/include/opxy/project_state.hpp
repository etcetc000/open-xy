#pragma once
#include <opxy/types.hpp>

namespace opxy { namespace extensions {
// Tokens are borrowed project identities, never pointers or persistent IDs.
struct ProjectToken { u32 generation, identity; };
enum class ProjectStateStatus : u32 { empty, ready, unsupported, unavailable, save_failed };
struct ProjectState {
    ProjectToken token;
    ProjectStateStatus status;
    u32 version, size;
    // Borrowed only for on_project_state; copy before returning. May be null for empty/error.
    const u8* data;
};
}}
