// MTP admission for module files, in module builds only. Stock checks every
// path against two pattern lists, one for folders and one for files; every
// stock rule keeps its answer. Our rules ride on one stock rule per list, so
// they are checked once per path: the folder list's ^/projects$ admits the
// /open-xy and /open-xy/modules folders, the file list's project rule admits
// module files. The stock matcher has no alternation, so each extra rule is a
// pattern of its own.
#include "stock.hpp"
using namespace opxy;
using namespace opxy::platform;
namespace {
u32 match(const char* pattern, const void* path) { return stock<u32(const char*, const void*)>(0x8d5c1bb0)(pattern, path); }
const char root_pattern[] = "^/open-xy$";
const char modules_pattern[] = "^/open-xy/modules$";
const char module_pattern[] = "^/open-xy/modules/[\\w\\-]+\\.opx$";
// The probe reports (gui-module.oxc, audio-module.oxc), so they can be read back.
const char report_pattern[] = "^/open-xy/[\\w\\-]+\\.oxc$";
}

extern "C" u32 sdk_module_mtp_folder_match(const char* pattern, const void* path) __asm__("sdk_module_mtp_folder_match");
u32 sdk_module_mtp_folder_match(const char* pattern, const void* path) {
    if (match(pattern, path)) return 1;
    if (pattern != reinterpret_cast<const char*>(0x840ae0c4)) return 0;
    return match(root_pattern, path) || match(modules_pattern, path);
}

extern "C" u32 sdk_module_mtp_match(const char* pattern, const void* path) __asm__("sdk_module_mtp_match");
u32 sdk_module_mtp_match(const char* pattern, const void* path) {
    if (match(pattern, path)) return 1;
    if (pattern != reinterpret_cast<const char*>(0x840ae4b0)) return 0;
    return match(module_pattern, path) || match(report_pattern, path);
}
