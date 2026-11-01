#include "stock.hpp"
using namespace opxy;
using namespace opxy::platform;
extern "C" u32 sdk_collection_mtp_match(const char*,const void*) __asm__("sdk_collection_mtp_match");
u32 sdk_collection_mtp_match(const char* pattern,const void* path) {
    const auto match=stock<u32(const char*,const void*)>(0x8d5c1bb0);
    if (match(pattern,path)) return 1;
    return pattern==reinterpret_cast<const char*>(0x840ae4b0) ? match("^/projects/openxy/[\\w\\-]+\\.oxc$",path) : 0;
}
