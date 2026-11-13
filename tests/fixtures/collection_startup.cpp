// Executes the generated startup probe with a FILE/role boundary double.
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <stdexcept>
#include <string>
#include <regex>
#include "stock.hpp"

using opxy::u32;
namespace {
struct Handle { opxy::u16 flags; FILE* file; } input = {};
bool gui, missing, report_failure;
unsigned opened_input, opened_report, writes, closes;
const char* source;
std::string report;

void* open(const char* path, const char* mode) {
    if (!std::strcmp(mode,"rb")) {
        if (std::strcmp(path,"/fat32/projects/openxy/collection.oxc")) throw std::runtime_error("wrong native input path");
        ++opened_input;
        if (missing) return nullptr;
        input.file=std::fopen(source,"rb"); input.flags=0;
        return input.file ? &input : nullptr;
    }
    const char* expected=gui ? "/fat32/projects/openxy/gui-status.oxc" : "/fat32/projects/openxy/audio-status.oxc";
    if (std::strcmp(path,expected) || std::strcmp(mode,"wb")) throw std::runtime_error("wrong role report path");
    ++opened_report;
    return report_failure ? nullptr : &report;
}
u32 read(void* out,u32 size,u32 count,void* file) {
    if (file!=&input || size!=1) throw std::runtime_error("wrong reader");
    const u32 n=static_cast<u32>(std::fread(out,size,count,input.file));
    if (std::feof(input.file)) input.flags|=0x100;
    if (std::ferror(input.file)) input.flags|=0x200;
    return n;
}
u32 write(const void* data,u32 size,u32 count,void* file) {
    if (file!=&report || size!=1) throw std::runtime_error("wrong writer");
    ++writes; report.append(static_cast<const char*>(data),count);
    return count;
}
int close(void* file) {
    ++closes;
    if (file==&input) return std::fclose(input.file);
    if (file!=&report) throw std::runtime_error("wrong close");
    return 0;
}
u32 role() { return gui; }
u32 match(const char* pattern, const void* path) {
    // The firmware's small matcher has no group/alternation operators. Model
    // that boundary before using the host matcher for the supported syntax.
    if (pattern == reinterpret_cast<const char*>(0x840ae4b0))
        pattern=R"(^/projects/[\w\!\+\-\@\[\] #-)]+/[\w\d\!\+\-\@\[\] #-)]+\.o?xy$)";
    else if (pattern == reinterpret_cast<const char*>(0x840ae494))
        pattern=R"(^/projects/workspace\.o?xy$)";
    bool character_class=false, escaped=false;
    for (const char* p=pattern; *p; ++p) {
        if (escaped) { escaped=false; continue; }
        if (*p=='\\') { escaped=true; continue; }
        if (*p=='[') character_class=true;
        if (*p==']') character_class=false;
        if (!character_class && (*p=='(' || *p==')' || *p=='|'))
            throw std::runtime_error("unsupported stock regex operator");
    }
    return std::regex_match(static_cast<const char*>(path),std::regex(pattern));
}
}

opxy::platform::Address opxy::platform::host_stock_address(u32 address) {
    switch(address) {
    case 0x8fed0348: return reinterpret_cast<Address>(open);
    case 0x8fed05bc: return reinterpret_cast<Address>(read);
    case 0x8fed0780: return reinterpret_cast<Address>(write);
    case 0x8fed00ae: return reinterpret_cast<Address>(close);
    case 0x8d5c3eac: return reinterpret_cast<Address>(role);
    case 0x8d5c1bb0: return reinterpret_cast<Address>(match);
    default: throw std::runtime_error("unexpected stock call");
    }
}

extern "C" void sdk_collection_probe() __asm__("sdk_collection_probe");
extern "C" u32 sdk_collection_mtp_match(const char*,const void*) __asm__("sdk_collection_mtp_match");
int main(int argc,char** argv) {
    if (argc!=4) return 2;
    if (!std::strcmp(argv[3],"mtp")) {
        const char* pattern=reinterpret_cast<const char*>(std::atoi(argv[2]) ? 0x840ae494 : 0x840ae4b0);
        std::printf("%u\n",sdk_collection_mtp_match(pattern,argv[1]));
        return 0;
    }
    source=argv[1]; gui=std::atoi(argv[2])!=0;
    missing=!std::strcmp(argv[3],"missing");
    report_failure=!std::strcmp(argv[3],"report-failure");
    sdk_collection_probe();
    sdk_collection_probe();  // Reinitializing engines must not repeat startup I/O.
    if (opened_input!=1 || opened_report!=1 || writes!=(report_failure?0u:1u) ||
        closes!=(missing?1u:(report_failure?1u:2u))) return 3;
    if (report_failure) std::puts("{\"report\":\"unavailable\"}");
    else std::fwrite(report.data(),1,report.size(),stdout);
    return 0;
}
