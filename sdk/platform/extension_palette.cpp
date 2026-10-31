#include "extension_palette.hpp"
#include "extension_runtime.hpp"

namespace opxy { namespace platform {
namespace {
const u32 kDocuments = 256, kCells = 2048, kExtensions = 16;

struct PaletteState { const extensions::Palette* palette; u32 revision; };
struct CachedDocument {
    const void* identity;
    PaletteRange fills, strokes;
    u32 fill_source, stroke_source, applied;
};

PaletteState active[kExtensions];
CachedDocument documents[kDocuments];
PaletteDocument current_documents[kDocuments];
extensions::Color source[kCells];
u32 source_count, active_count, epoch = 1;

bool same(const extensions::Color& a, const extensions::Color& b) {
    return a.red == b.red && a.green == b.green && a.blue == b.blue && a.alpha == b.alpha;
}
bool same(const PaletteRange& a, const PaletteRange& b) {
    return a.begin == b.begin && a.count == b.count && a.stride == b.stride;
}
bool same(const CachedDocument& item, const PaletteDocument& document) {
    return item.identity == document.identity && same(item.fills, document.fills) && same(item.strokes, document.strokes);
}
u32 revision(const extensions::Palette* palette) {
    return palette && palette->revision ? palette->revision() : 0;
}

void refresh(const ExtensionEntry* entries, u32 count) {
    PaletteState next[kExtensions];
    u32 next_count = 0;
    for (u32 i = 0; i < count && next_count < kExtensions; ++i) {
        const extensions::Palette* palette = entries[i].hooks ? entries[i].hooks->screen_palette : nullptr;
        if (palette && palette->map) next[next_count++] = {palette, revision(palette)};
    }
    bool changed = next_count != active_count;
    for (u32 i = 0; i < next_count; ++i)
        changed = changed || next[i].palette != active[i].palette || next[i].revision != active[i].revision;
    if (!changed) return;
    for (u32 i = 0; i < next_count; ++i) active[i] = next[i];
    active_count = next_count;
    if (!++epoch) ++epoch;
}

CachedDocument* cache(const PaletteDocument& document) {
    if (!document.identity || (!document.fills.count && !document.strokes.count)) return nullptr;
    CachedDocument* available = nullptr;
    for (u32 i = 0; i < kDocuments; ++i) {
        CachedDocument& item = documents[i];
        if (same(item, document)) return &item;
        if (item.identity == document.identity) available = &item;
        if (!item.identity && !available) available = &item;
    }
    const u32 cells = document.fills.count + document.strokes.count;
    if (!available || cells > kCells - source_count) return nullptr;
    *available = {document.identity, document.fills, document.strokes, source_count,
                  source_count + document.fills.count, 0};
    for (u32 i = 0; i < document.fills.count; ++i)
        source[source_count + i] = *reinterpret_cast<extensions::Color*>(
            reinterpret_cast<unsigned char*>(document.fills.begin) + i * document.fills.stride);
    source_count += document.fills.count;
    for (u32 i = 0; i < document.strokes.count; ++i)
        source[source_count + i] = *reinterpret_cast<extensions::Color*>(
            reinterpret_cast<unsigned char*>(document.strokes.begin) + i * document.strokes.stride);
    source_count += document.strokes.count;
    return available;
}

void apply(const PaletteRange& range, u32 first) {
    for (u32 i = 0; i < range.count; ++i) {
        extensions::Color value = source[first + i];
        for (u32 palette = 0; palette < active_count; ++palette) active[palette].palette->map(value);
        extensions::Color* target = reinterpret_cast<extensions::Color*>(
            reinterpret_cast<unsigned char*>(range.begin) + i * range.stride);
        if (!same(*target, value)) *target = value;
    }
}
}

void extension_palette_sync(const ExtensionEntry* entries, u32 count) {
    if (!entries || count > kExtensions) return;
    refresh(entries, count);
    const u32 live_count = extension_palette_documents(current_documents, kDocuments);
    if (live_count > kDocuments) return;
    for (u32 i = 0; i < live_count; ++i) {
        CachedDocument* document = cache(current_documents[i]);
        if (!document || document->applied == epoch) continue;
        apply(document->fills, document->fill_source);
        apply(document->strokes, document->stroke_source);
        document->applied = epoch;
    }
}
}}
