#include "collection_probe.hpp"
#include <cstdio>
#include <cstdlib>
#include <cstring>

using namespace opxy;
struct Input { FILE* file; u32 chunk; bool fail_at_end; };
static u32 read(void* context, void* output, u32 count) {
    Input& input = *static_cast<Input*>(context);
    if (input.chunk && input.chunk < count) count = input.chunk;
    return static_cast<u32>(std::fread(output, 1, count, input.file));
}
static bool failed(void* context) {
    Input& input = *static_cast<Input*>(context);
    return std::ferror(input.file) || (input.fail_at_end && std::feof(input.file));
}
static bool ended(void* context) { return std::feof(static_cast<Input*>(context)->file) != 0; }
int main(int argc, char** argv) {
    if (argc != 4) return 2;
    FILE* file = std::fopen(argv[2], "rb");
    if (!file) return 3;
    Input input = {file, static_cast<u32>(std::strtoul(argv[3], nullptr, 10)), !std::strcmp(argv[1], "fail-at-end")};
    if (!std::strcmp(argv[1], "sha")) {
        collection::Sha256 hash; hash.begin();
        unsigned char buffer[997], digest[32]; u32 count;
        while ((count = read(&input, buffer, sizeof buffer))) hash.add(buffer, count);
        hash.finish(digest);
        for (u32 i=0; i<32; ++i) std::printf("%02x", digest[i]);
        std::puts("");
    } else {
        collection::Limits limits = {0x8d2d0380, 0x1fc000, {0}};
        for (u32 i=0; i<32; ++i) limits.platform[i] = i;
        collection::Workspace workspace;
        const collection::Inspection result = collection::inspect(collection::Reader{&input,read,failed,ended}, limits, workspace);
        std::printf("%u\n", static_cast<unsigned>(result.status));
    }
    std::fclose(file);
    return 0;
}
