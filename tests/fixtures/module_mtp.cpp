// Runs the module MTP matcher against a model of stock's matcher.
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <regex>
#include <stdexcept>
#include "stock.hpp"

using opxy::u32;
namespace {
u32 match(const char* pattern, const void* path) {
    // The firmware's small matcher has no group/alternation operators. Model
    // that boundary before using the host matcher for the supported syntax.
    if (pattern == reinterpret_cast<const char*>(0x840ae4b0))
        pattern = R"(^/projects/[\w\!\+\-\@\[\] #-)]+/[\w\d\!\+\-\@\[\] #-)]+\.o?xy$)";
    else if (pattern == reinterpret_cast<const char*>(0x840ae494))
        pattern = R"(^/projects/workspace\.o?xy$)";
    else if (pattern == reinterpret_cast<const char*>(0x840ae0c4))
        pattern = R"(^/projects$)";
    else if (pattern == reinterpret_cast<const char*>(0x840ae0d0))
        pattern = R"(^/samples$)";
    bool character_class = false, escaped = false;
    for (const char* p = pattern; *p; ++p) {
        if (escaped) { escaped = false; continue; }
        if (*p == '\\') { escaped = true; continue; }
        if (*p == '[') character_class = true;
        if (*p == ']') character_class = false;
        if (!character_class && (*p == '(' || *p == ')' || *p == '|'))
            throw std::runtime_error("unsupported stock regex operator");
    }
    return std::regex_match(static_cast<const char*>(path), std::regex(pattern));
}
}

opxy::platform::Address opxy::platform::host_stock_address(u32 address) {
    if (address == 0x8d5c1bb0) return reinterpret_cast<Address>(match);
    throw std::runtime_error("unexpected stock call");
}

extern "C" u32 sdk_module_mtp_match(const char*, const void*) __asm__("sdk_module_mtp_match");
extern "C" u32 sdk_module_mtp_folder_match(const char*, const void*) __asm__("sdk_module_mtp_folder_match");
// matcher <path> <file|folder> <0: the stock rule our rules ride on | 1: another stock rule>
int main(int argc, char** argv) {
    if (argc != 4) return 2;
    const bool folder = !std::strcmp(argv[2], "folder"), other = std::atoi(argv[3]) != 0;
    const char* pattern = reinterpret_cast<const char*>(
        folder ? (other ? 0x840ae0d0 : 0x840ae0c4) : (other ? 0x840ae494 : 0x840ae4b0));
    std::printf("%u\n", (folder ? sdk_module_mtp_folder_match : sdk_module_mtp_match)(pattern, argv[1]));
    return 0;
}
