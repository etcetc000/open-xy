#pragma once
#include <opxy/extension.hpp>

namespace opxy { namespace platform {
struct ExtensionEntry;

// A validated, currently loaded SVG document supplied by the version adapter.
// `stride` is bytes between colours: fills are four bytes and stock strokes
// are twenty. It deliberately exposes neither an SVG object nor stock ABI to
// extension authors.
struct PaletteRange { extensions::Color* begin; u32 count, stride; };
struct PaletteDocument {
    const void* identity;
    PaletteRange fills, strokes;
};

// Version adapter: enumerate only documents live in the current GUI screen
// table. It never allocates or calls into an extension.
u32 extension_palette_documents(PaletteDocument* out, u32 capacity);
// Runtime: retain source colours, compose selected extensions in collection
// order, and write only a changed loaded palette entry.
void extension_palette_sync(const ExtensionEntry* entries, u32 count);
}}
