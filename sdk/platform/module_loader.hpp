// Module files (.opx): one relocatable machine or extension per file, loaded at
// startup on each processor role into memory of its own.
//
// File layout, little-endian:
//   manifest JSON text, manifest_bytes from offset 0, zero-padded to image_offset
//   image    code, rodata, data, linked at 0; image_bytes at image_offset (4-aligned)
//   relocs   reloc_count Reloc entries at reloc_offset
//   names    the names imports refer to, 0-terminated, from the relocs' end to the trailer
//   trailer  Trailer, the last trailer_bytes of the file
// In memory a module takes image_bytes + bss_bytes, bss zeroed, at a base
// aligned to one cache line. A relocation writes base + target: Abs32 the word
// at its offset, Hi16 / Lo16 the 16-bit immediate of a Blackfin "reg.H/.L = imm16".
// An Import writes the firmware's address for a name (target: its offset in
// names) into the 4-aligned word at its offset; a name the firmware does not
// export keeps the module out (module.hpp, opxy::core).
// crc is CRC-32 (IEEE) over the image as stored, then the relocs, then the names.
//
// Trailer: manifest_bytes, image_offset, image_bytes, bss_bytes, reloc_offset,
// reloc_count, init_offset, crc, os (u32 each); format, trailer_bytes (u16);
// magic (8 bytes).
//
// The manifest says what the module is, for people and tools (the builder,
// a catalog, a web installer); the loader requires that it ends before the
// image and that its name is the file's. Manifest 1, as tooling/build_module.py
// writes it:
//   {"manifest": 1, "name": "karplus", "version": "1.0.0", "kind": "synth",
//    "os": "1.2.1", "api": 2}
//   name     the machine ID (tooling/projects.py MACHINE_ID): [a-z][a-z0-9_], at
//            most 15; also the file name without .opx
//   version  MAJOR.MINOR.PATCH; a catalog can refuse an update that does not raise it
//   kind     a machine kind (synth, ...), or "module" for a bare module with its own init
//   os       the stock OS the module is linked against (also the trailer's os)
//   api      the ModuleApi version it was built with (module_api_version)
// Room for more, none of it read yet; each would be checked before a module
// runs, by the loader or by the tool that installs it:
//   - the SDK version and the version of each contract it uses
//     (sdk/include/opxy/*.hpp), so an old module never reads a changed struct
//   - a tier: what the module may touch (audio only, screens, files, the sequencer)
//   - what it claims (keys, controls, pages), so overlaps show at boot
//   - modules it needs, and a hash or signature of its author or catalog
#pragma once
#include <opxy/types.hpp>
namespace opxy { namespace modules {
const u32 trailer_bytes = 48;
const u32 line_bytes = 32;
const u32 format = 0;
const u32 os_1_2_1 = 0x00010201;
const u32 abs32 = 1, hi16 = 2, lo16 = 3, import = 4;
const unsigned char magic[8] = {'O','P','X','Y','M','O','D',0};

struct Reloc {
    u32 where;   // offset in the image (low 28 bits) | type << 28
    u32 target;  // offset in the module (image or bss) the value points to
};

// Codes are part of the format (a log names them "m-<code>").
enum Status : u32 {
    loaded = 0,
    short_file = 101, bad_trailer = 102, wrong_os = 103, bad_layout = 104,
    no_space = 105, bad_crc = 106, bad_reloc = 107, init_failed = 108,
    too_many = 109, reserved = 110, io_error = 111, missing_import = 112,
    wrong_name = 113,
};

// A firmware function a module may import, by the name after opxy_import_.
struct Export {
    const char* name;
    u32 address;
};
struct Exports {
    const Export* table;
    u32 count;
};

struct Memory {
    void* context;
    // Returns writable, executable memory or nullptr.
    unsigned char* (*allocate)(void*, u32 bytes);
    void (*release)(void*, unsigned char*);
    // Writes back data lines and drops instruction lines; address and bytes
    // are cache-line aligned.
    bool (*synchronize)(void*, u32 address, u32 bytes);
};

struct Module {
    unsigned char* block;  // as allocated; released through Memory
    unsigned char* start;  // cache-line aligned start of the module
    u32 base;              // start as a 32-bit target address
    u32 bytes;             // image + bss, rounded up to a cache line
    u32 init;              // address of the init function
};

u32 crc32(u32 crc, const unsigned char* data, u32 bytes);

// The offset of the ']' closing the array under key (a quoted JSON key such as
// "\"engine_types\""), or 0: the key, its '[', then brackets counted with
// strings skipped, since entries hold arrays of their own. For splicing module
// entries into stock's catalog text.
u32 array_end(const unsigned char* text, u32 bytes, const char* key);

// Whether the manifest text names the module name, ignoring case (FAT32 does):
// the top-level "name" key's value, a plain string.
bool manifest_names(const unsigned char* text, u32 bytes, const char* name);

// Checks the whole file in data, copies and relocates it into fresh memory,
// resolves its imports from exports, zeroes bss and synchronizes caches. With
// a name (the file's, without .opx), the manifest must carry it. On success
// the module is ready for its init call; on failure nothing stays allocated.
Status stage(const unsigned char* data, u32 bytes, u32 os, const Memory& memory, const Exports& exports, Module& out,
             const char* name = nullptr);
}}
