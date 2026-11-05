// Module presets as the factory preset walk sees them (sdk/platform/module_presets.cpp).
#include "module_presets.hpp"
#include <cstdio>
#include <cstdlib>
#include <cstring>
using namespace opxy;
namespace m = opxy::modules;
namespace {
void require(bool condition, const char* message) {
    if (!condition) { std::fprintf(stderr, "%s\n", message); std::exit(1); }
}
const char* name(const unsigned char* entry) { return reinterpret_cast<const char*>(entry + m::entry_name); }

// Registration order, not ID order; one module ships no presets.
const char* const karplus_names[] = {"default", "chorus", "harp"};
const char* const lute_names[] = {"a-very-long-preset-name1"};  // 24 characters, the longest
const m::PresetModule list[] = {{"karplus", 3, karplus_names}, {"silent", 0, nullptr}, {"lute", 1, lute_names}};
const m::PresetModules all = {list, 3};
}

int main() {
    u32 module = 99, preset = 99;
    require(m::preset_file(all, "content/presets/karplus/harp.json", module, preset) && module == 0 && preset == 2,
            "module preset not found");
    require(m::preset_file(all, "/yaffs2/content/presets/lute/a-very-long-preset-name1.json", module, preset) &&
            module == 2 && preset == 0, "module preset behind /yaffs2/ not found");
    const char* const not_ours[] = {
        "content/presets/karplus/harp", "content/presets/karplus/harp.jsonx", "content/presets/karplus/har.json",
        "content/presets/karplus/harp.json/x", "content/presets/karplusx/harp.json", "content/presets/karplu/harp.json",
        "content/presets/silent/default.json", "content/presets/prism/default.json", "content/presetsx/karplus/harp.json",
        "/fat32/presets/karplus/harp.json", "presets/karplus/harp.json", "content/presets/karplus", "", nullptr};
    for (const char* path : not_ours) require(!m::preset_file(all, path, module, preset), path ? path : "null path");

    require(m::preset_folder(all, "content/presets", module) == m::folder_root, "root not seen");
    require(m::preset_folder(all, "content/presets/lute", module) == m::folder_module && module == 2, "module folder");
    const char* const stock_folders[] = {"content/presets/prism", "content/presets/silent", "content/presets/lute/x",
                                         "content/presetsx", "/presets", "content/presets/", "content/presets/lutex"};
    for (const char* path : stock_folders) require(m::preset_folder(all, path, module) == m::folder_stock, path);

    // content/presets: after stock's, a folder per module with presets, in registration order.
    unsigned char entry[m::entry_bytes];
    m::PresetFolder root = {m::folder_root, 0, 0};
    require(m::next_entry(all, root, entry) && entry[m::entry_type] == m::entry_folder && !std::strcmp(name(entry), "karplus"),
            "first module folder");
    require(m::next_entry(all, root, entry) && !std::strcmp(name(entry), "lute"), "module without presets listed");
    require(!m::next_entry(all, root, entry), "root does not end");

    // A module's folder: its presets as .json files, nothing else.
    m::PresetFolder karplus = {m::folder_module, 0, 0};
    const char* const files[] = {"default.json", "chorus.json", "harp.json"};
    for (const char* file : files)
        require(m::next_entry(all, karplus, entry) && entry[m::entry_type] == m::entry_file && !std::strcmp(name(entry), file),
                file);
    require(!m::next_entry(all, karplus, entry), "module folder does not end");
    m::PresetFolder lute = {m::folder_module, 2, 0};
    require(m::next_entry(all, lute, entry) && !std::strcmp(name(entry), "a-very-long-preset-name1.json"),
            "longest preset name cut");
    m::PresetFolder stray = {m::folder_module, 7, 0};
    require(!m::next_entry(all, stray, entry), "folder of no module listed");

    // A stock folder a module's replaces, and the ones it leaves.
    unsigned char stock_entry[m::entry_bytes] = {};
    stock_entry[m::entry_type] = m::entry_folder;
    std::strcpy(reinterpret_cast<char*>(stock_entry + m::entry_name), "karplus");
    require(m::replaced(all, stock_entry), "stock karplus folder kept beside the module's");
    std::strcpy(reinterpret_cast<char*>(stock_entry + m::entry_name), "silent");
    require(!m::replaced(all, stock_entry), "folder of a module without presets replaced");
    std::strcpy(reinterpret_cast<char*>(stock_entry + m::entry_name), "prism");
    require(!m::replaced(all, stock_entry), "stock folder replaced");
    stock_entry[m::entry_type] = m::entry_file;
    std::strcpy(reinterpret_cast<char*>(stock_entry + m::entry_name), "karplus");
    require(!m::replaced(all, stock_entry), "a file replaced as a folder");
    require(!m::replaced(all, nullptr), "no entry replaced");
    return 0;
}
