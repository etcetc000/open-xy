// Host checks for the preset-scan hook (presets.cpp) with the stock type reader
// and the installed-engine check standing in.
#include <presets.cpp>
#include <cstdio>
#include <cstdlib>
#include <cstring>

using opxy::platform::Address;

static const char* next_type = "";
static char heap[64];
static int asked = 0;

// The stock reader: the preset's "type", inline below 16 characters, else on the heap.
static void read_type(StockString* out, const StockString*) {
    const unsigned length = static_cast<unsigned>(std::strlen(next_type));
    std::memset(out, 0, sizeof(*out));
    if (length < 16) {
        std::memcpy(out->text, next_type, length + 1);
        out->capacity = 15;
    } else {
        std::memcpy(heap, next_type, length + 1);
        char* storage = heap;
        std::memcpy(out->text, &storage, sizeof(storage));
        out->capacity = 63;
    }
    out->length = length;
}

static const char* text(const StockString* s) {
    if (s->capacity < 16) return s->text;
    char* storage;
    std::memcpy(&storage, s->text, sizeof(storage));
    return storage;
}

extern "C" int sdk_engine_installed(const void* name) {
    ++asked;
    const char* type = text(static_cast<const StockString*>(name));
    return !std::strcmp(type, "prism") || !std::strcmp(type, "karplus");
}

Address opxy::platform::host_stock_address(Address address) {
    if (address == 0x8d4eead6) return reinterpret_cast<Address>(&read_type);
    std::abort();
}

static void check(bool ok, const char* what) {
    if (!ok) { std::printf("FAIL %s\n", what); std::exit(1); }
}

static StockString scan(const char* type, u32 flags) {
    next_type = type;
    StockString out, path = {};
    sdk_preset_type(&out, &path, flags);
    return out;
}

int main() {
    StockString s = scan("prism", 1);
    check(s.length == 5 && !std::strcmp(s.text, "prism"), "a stock engine's factory preset stays listed");
    s = scan("karplus", 3);
    check(s.length == 7, "an installed machine's factory preset stays listed");
    s = scan("fm2op", 1);
    check(s.length == 0 && s.text[0] == 0 && s.capacity == 15, "a removed machine's factory preset is hidden");
    asked = 0;
    s = scan("fm2op", 0);
    check(s.length == 5 && !asked, "user presets are never hidden or checked");
    s = scan("", 1);
    check(s.length == 0 && !asked, "an untyped preset stays as stock leaves it");
    s = scan("averyveryverylongname", 1);
    check(s.length == 0 && heap[0] == 0 && s.capacity == 63 && text(&s) == heap,
          "a hidden heap name keeps its storage for stock to free");
    std::puts("presets ok");
    return 0;
}
