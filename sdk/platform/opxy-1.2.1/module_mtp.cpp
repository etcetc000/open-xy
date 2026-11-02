// MTP admission for module files, in module builds only. Stock's file
// allowlist scan asks its matcher about each path with its own project rule;
// every stock rule keeps its answer, and only that scan also admits module
// files in /projects/openxy. The stock matcher has no alternation, so each
// extra rule is a pattern of its own.
#include "stock.hpp"
using namespace opxy;
using namespace opxy::platform;
namespace {
const char module_pattern[] = "^/projects/openxy/[\\w\\-]+\\.opx$";
// The probe reports (gui-module.oxc, audio-module.oxc), so they can be read back.
const char report_pattern[] = "^/projects/openxy/[\\w\\-]+\\.oxc$";
}

extern "C" u32 sdk_module_mtp_match(const char* pattern, const void* path) __asm__("sdk_module_mtp_match");
u32 sdk_module_mtp_match(const char* pattern, const void* path) {
    const auto match = stock<u32(const char*, const void*)>(0x8d5c1bb0);
    if (match(pattern, path)) return 1;
    if (pattern != reinterpret_cast<const char*>(0x840ae4b0)) return 0;
    return match(module_pattern, path) || match(report_pattern, path);
}
