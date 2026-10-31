// Stock OS 1.2.1 project companions (docs/projects.md). Stock saves every project,
// the workspace included, through one call of save(project, path) (8d599074, from
// 8d585eda). After a save that succeeds, the hook writes the .oxy beside the .xy: the
// saved file's size and CRC-32, then this firmware's machine numbers, in the canonical
// form tooling/projects.py writes. It runs where stock saves, never on the audio role.
// Publication uses a checked temporary file; the previous companion survives write
// failures. Readers can recover a complete temporary file bound to the saved project.
//
// Stock loads every project, the workspace included, through load_project (8d5890b0),
// which deserializes the file into the inactive project buffer with one call of
// deserialize(project, path) (8d599b10, from 8d58913e), then validates it and makes it
// pending. That call is hooked too: after a load that succeeds, the reader opens the
// .oxy beside the .xy, requires its binding to match the file's size and CRC-32, and
// maps each number its open-xy.machines section lists, by machine ID, to this
// firmware's number in every pattern of the instrument tracks (engines) and send
// tracks (effects), and a machine this firmware lacks to Prism or no effect. Any
// problem leaves the project as saved; the load never fails because of its companion.
#include "project_companion.hpp"
#include "stock.hpp"
#include "project_sections.hpp"
#include "project_state_lock.hpp"
#include "../project_state.hpp"

using opxy::u32;
using opxy::platform::CompanionMachine;
using opxy::platform::stock;

extern "C" u32 sdk_project_save(void* project, const char* path) __asm__("sdk_project_save");
extern "C" u32 sdk_project_load(void* project, const char* path) __asm__("sdk_project_load");

namespace {
typedef void File;
// newlib, as stock's project reader and writer call it (8d590d7c, 8d590e28, 8d591304, 8d590da2).
File* file_open(const char* path, const char* mode) {
    return stock<File*(const char*, const char*)>(0x8fed0348)(path, mode);
}
u32 file_read(void* data, u32 size, u32 count, File* file) {
    return stock<u32(void*, u32, u32, File*)>(0x8fed05bc)(data, size, count, file);
}
u32 file_write(const void* data, u32 size, u32 count, File* file) {
    return stock<u32(const void*, u32, u32, File*)>(0x8fed0780)(data, size, count, file);
}
int file_close(File* file) { return stock<int(File*)>(0x8fed00ae)(file); }
// OS FILE flags: fread's error branches (8fed06ae/8fed0722) set bit 9;
// its EOF branch sets bit 8. A zero read alone does not prove a complete file.
bool file_eof(File* file) {
    return (*static_cast<const opxy::u16*>(file) & 0x300u) == 0x100u;
}
bool file_publish(const char* from, const char* to) {
    // Stock removes an existing destination before rename. If replacement fails,
    // the complete temporary remains a bound recovery candidate.
    return stock<u32(const char*, const char*)>(0x8c040c78)(from, to) != 0;
}
u32 save(void* project, const char* path) { return stock<u32(void*, const char*)>(0x8d599074)(project, path); }
u32 deserialize(void* project, const char* path) {
    return stock<u32(void*, const char*)>(0x8d599b10)(project, path);
}

const u32 kPathBytes = 256;
const u32 kTextBytes = 1024;  // the writer streams through it (Writer)
const u32 kChunkBytes = 4096;
// The writer's and the reader's buffers stay apart, so a save and a load never share one.
unsigned char chunk[kChunkBytes];
char text[kTextBytes];
char companion[kPathBytes];
char temporary[kPathBytes];
unsigned char read_chunk[kChunkBytes];
char read_companion_path[kPathBytes];
char read_temporary_path[kPathBytes];
u32 crc_table[256];

u32 value(const char* symbol) { return static_cast<u32>(reinterpret_cast<__UINTPTR_TYPE__>(symbol)); }

// zlib's CRC-32.
u32 crc32(u32 crc, const unsigned char* data, u32 count) {
    if (!crc_table[1]) {
        for (u32 i = 0; i < 256; ++i) {
            u32 c = i;
            for (int bit = 0; bit < 8; ++bit) c = c & 1 ? 0xedb88320u ^ (c >> 1) : c >> 1;
            crc_table[i] = c;
        }
    }
    crc = ~crc;
    while (count--) crc = crc_table[(crc ^ *data++) & 0xff] ^ (crc >> 8);
    return ~crc;
}

// The project's bytes as saved: false when it cannot be read back.
bool measure(const char* path, unsigned char* buffer, u32& bytes, u32& crc) {
    File* file = file_open(path, "rb");
    if (!file) return false;
    bytes = crc = 0;
    for (u32 count; (count = file_read(buffer, 1, kChunkBytes, file)) != 0;) {
        if (count > 16 * 1024 * 1024u - bytes) { file_close(file); return false; }
        crc = crc32(crc, buffer, count);
        bytes += count;
    }
    const bool complete = file_eof(file);
    return file_close(file) == 0 && complete;
}

// song.xy -> song.oxy; false for any other name.
bool companion_path(const char* path, char* out) {
    u32 length = 0;
    while (length < kPathBytes && path[length]) ++length;
    if (length < 4 || length + 6 >= kPathBytes || path[length - 3] != '.' || path[length - 2] != 'x' ||
        path[length - 1] != 'y')
        return false;
    for (u32 i = 0; i < length - 2; ++i) out[i] = path[i];
    out[length - 2] = 'o';
    out[length - 1] = 'x';
    out[length] = 'y';
    out[length + 1] = 0;
    return true;
}

void temporary_path(const char* path, char* out) {
    u32 n = 0;
    while (path[n]) { out[n] = path[n]; ++n; }
    const char suffix[] = ".tmp";
    for (u32 i = 0; i < sizeof(suffix); ++i) out[n + i] = suffix[i];
}

// Stream through a bounded buffer; publish only after all writes and close succeed.
class Writer {
public:
    explicit Writer(File* file) : file_(file) {}
    void put(const char* s) { while (*s) byte(*s++); }
    void byte(char c) {
        if (!ok_) return;
        if (++total_ > 64 * 1024u) { ok_ = false; return; }
        if (length_ == kTextBytes) flush();
        if (ok_) text[length_++] = c;
    }
    void flush() {
        if (ok_ && length_ && file_ && file_write(text, 1, length_, file_) != length_) ok_ = false;
        length_ = 0;
    }
    bool ok() const { return ok_; }
    void number(u32 v) {
        char digits[10];
        u32 count = 0;
        do { digits[count++] = static_cast<char>('0' + v % 10); v /= 10; } while (v);
        while (count) byte(digits[--count]);
    }
    void hex(u32 v) {
        for (int shift = 28; shift >= 0; shift -= 4) byte("0123456789abcdef"[(v >> shift) & 15]);
    }

private:
    File* file_;
    u32 length_ = 0, total_ = 0;
    bool ok_ = true;
};

void write_project_sections(Writer& writer);
bool project_save_current();
void emit_companion(Writer& w, u32 bytes, u32 crc) {
    w.put("{\n  \"format\": \"open-xy-project\",\n  \"project\": {\n    \"bytes\": ");
    w.number(bytes);
    w.put(",\n    \"crc32\": \"");
    w.hex(crc);
    w.put("\"\n  },\n  \"sections\": {\n    \"open-xy.machines\": {\n      \"machines\": {");
    u32 listed = 0;
    for (const CompanionMachine* m = sdk_companion_machines; m->id; ++m) {
        const u32 number = value(m->number);
        if (!number) continue;  // a kit machine this selection left out
        w.put(listed++ ? ",\n        \"" : "\n        \"");
        w.put(m->id);
        w.put(m->effect ? "\": {\n          \"effect\": " : "\": {\n          \"engine\": ");
        w.number(number);
        w.put(",\n          \"page\": ");
        w.number(value(m->page));
        w.put("\n        }");
    }
    w.put(listed ? "\n      },\n" : "},\n");
    w.put("      \"profile\": \"opxy-1.2.1\",\n      \"version\": 1\n    }");
    write_project_sections(w);
    w.put("\n  },\n  \"version\": 1\n}\n");
}
bool companion_size_fits() {
    Writer writer(nullptr);
    emit_companion(writer, 0xffffffffu, 0);
    writer.flush();
    return writer.ok();
}
bool write_companion(u32 bytes, u32 crc) {
    temporary_path(companion, temporary);
    File* file = file_open(temporary, "wb");
    if (!file) return false;
    Writer w(file);
    emit_companion(w, bytes, crc);
    w.flush();
    const bool complete = w.ok();
    return file_close(file) == 0 && complete && project_save_current() && file_publish(temporary, companion);
}

// Reading, as tooling/projects.py parse() and translation() do. The document streams
// through read_chunk, so a companion of up to 64 KiB needs no buffer of its size. A
// repeated key counts once, its last value deciding, as Python's json reads it.
const u32 kCompanionLimit = 64 * 1024;
// The relocated engine and effect records (docs/abi-1.2.1.md, tooling/stock_tables.py):
// no registered engine or effect number lies outside its table.
const u32 kNumbers = 256;
const u32 kEffectNumbers = 63;
// A number the companion does not list stays as saved (a stock machine).
const u32 kUnlisted = 0xfffffffeu;
// What a listed machine this firmware lacks becomes, written into every pattern itself:
// Prism on an instrument track, no effect on a send, stock's own fallbacks. No pattern
// keeps a number stock would have to correct, and none opens whatever this firmware's
// layout gave the old number. Follow-up: nothing yet tells the user which machine was
// missing (docs/projects.md).
const u32 kPrism = 18;
const u32 kNoEffect = 0;
const u32 kDepth = 8;      // nesting a skipped value may use; deeper is refused (stack)
const u32 kKeyBytes = 64;  // longer strings match nothing we read
const u32 kMachineBytes = 15;
// Distinct machine IDs one section may list (tooling/projects.py MACHINE_LIMIT): more than
// one firmware can number (test_stock_tables), so it reads every companion it writes.
const u32 kMachines = 256;
// Other owners' sections invalid at once, each awaiting a repeat of its name that
// replaces it; more refuse the companion.
const u32 kInvalidSections = 4;

// Old number -> this firmware's number, kUnlisted, or the fallback, for engines (tracks
// 0-7) and effects (send tracks 14 and 15), which share the pattern field.
u32 engine_map[kNumbers];
u32 effect_map[kEffectNumbers];

// Each machine the section lists, by ID, as its last entry gives it.
struct Listed {
    char id[kMachineBytes];
    unsigned char length;
    unsigned short old;  // kNumbers when the entry names no number a pattern can hold
    bool effect, valid;
};
Listed listed[kMachines];
u32 listed_count;

class Source {
public:
    explicit Source(File* file, opxy::platform::project_sections::Document* capture = nullptr)
        : file_(file), capture_(capture) {}
    Source(const unsigned char* data, u32 length) : file_(nullptr), memory_(data), memory_size_(length) {}
    u32 position() const { return position_; }
    // The next byte, or -1 at the end or past the 64 KiB limit.
    int peek() {
        if (memory_) return position_ < memory_size_ ? memory_[position_] : -1;
        if (at_ == have_) {
            if (ended_) return -1;
            at_ = 0;
            have_ = file_read(read_chunk, 1, kChunkBytes, file_);
            total_ += have_;
            if (!have_ || total_ > kCompanionLimit) {
                ended_ = true;
                over_ = total_ > kCompanionLimit || !file_eof(file_);
                have_ = 0;
                return -1;
            }
        }
        return read_chunk[at_];
    }
    int next() {
        const int c = peek();
        if (c >= 0) {
            if (capture_) capture_->bytes[position_] = static_cast<unsigned char>(c);
            ++position_; if (!memory_) ++at_;
        }
        return c;
    }
    bool over() const { return over_; }

private:
    File* file_;
    opxy::platform::project_sections::Document* capture_ = nullptr;
    const unsigned char* memory_ = nullptr;
    u32 memory_size_ = 0, position_ = 0;
    u32 at_ = 0, have_ = 0, total_ = 0;
    bool ended_ = false, over_ = false;
};

bool digit(int c) { return c >= '0' && c <= '9'; }

// A string's decoded UTF-8: its length, and its first kKeyBytes bytes.
struct Text {
    char bytes[kKeyBytes];
    u32 length;
    bool is(const char* s) const {
        u32 n = 0;
        while (s[n]) ++n;
        if (n != length) return false;
        for (u32 i = 0; i < n; ++i)
            if (bytes[i] != s[i]) return false;
        return true;
    }
};

// A JSON number: integer when it has no fraction or exponent; the magnitude saturates.
struct Number {
    bool integer, negative;
    u32 magnitude;
};

// A strict JSON (RFC 8259) reader over the stream, building no document tree.
class Parser {
public:
    explicit Parser(File* file, opxy::platform::project_sections::Document* capture = nullptr) : source_(file, capture) {}
    Parser(const unsigned char* data, u32 length) : source_(data, length) {}
    u32 position() { peek(); return source_.position(); }

    int peek() {
        for (int c = source_.peek(); c == ' ' || c == '\t' || c == '\n' || c == '\r'; c = source_.peek())
            source_.next();
        return source_.peek();
    }
    bool at_number() {
        const int c = peek();
        return c == '-' || digit(c);
    }
    // Only whitespace remains, within the limit.
    bool end() { return peek() < 0 && !source_.over(); }

    bool string(Text& text) {
        text.length = 0;
        if (peek() != '"') return false;
        source_.next();
        for (;;) {
            const int c = source_.next();
            if (c < 0x20) return false;  // a control character, or the end
            if (c == '"') return true;
            u32 code = static_cast<u32>(c);
            if (c == '\\') {
                const int e = source_.next();
                if (e != 'u') {
                    if (!escape(e, code)) return false;
                } else {
                    if (!hex4(code)) return false;
                    // A high half pairs with a low one escaped right after it. Any other
                    // half stays alone, as Python keeps it, and what follows is read anew.
                    while (code >= 0xd800 && code < 0xdc00 && source_.peek() == '\\') {
                        source_.next();
                        const int f = source_.next();
                        if (f != 'u') {
                            put(text, code);
                            if (!escape(f, code)) return false;
                            break;
                        }
                        u32 low;
                        if (!hex4(low)) return false;
                        if (low >= 0xdc00 && low < 0xe000) {
                            code = 0x10000 + ((code - 0xd800) << 10) + (low - 0xdc00);
                            break;
                        }
                        put(text, code);
                        code = low;
                    }
                }
            } else if (c >= 0x80) {
                u32 follow, least;
                if (c >= 0xc2 && c <= 0xdf) follow = 1, least = 0x80, code = c & 0x1f;
                else if (c >= 0xe0 && c <= 0xef) follow = 2, least = 0x800, code = c & 0x0f;
                else if (c >= 0xf0 && c <= 0xf4) follow = 3, least = 0x10000, code = c & 0x07;
                else return false;
                while (follow--) {
                    const int d = source_.next();
                    if (d < 0 || (d & 0xc0) != 0x80) return false;
                    code = code << 6 | (d & 0x3f);
                }
                if (code < least || code > 0x10ffff || (code >= 0xd800 && code < 0xe000)) return false;
            }
            put(text, code);
        }
    }

    bool number(Number& n) {
        n.integer = true;
        n.negative = false;
        n.magnitude = 0;
        if (peek() == '-') {
            source_.next();
            n.negative = true;
        }
        const int first = source_.next();
        if (!digit(first)) return false;
        n.magnitude = static_cast<u32>(first - '0');
        if (first != '0') {
            while (digit(source_.peek())) {
                const u32 d = static_cast<u32>(source_.next() - '0');
                n.magnitude = n.magnitude > (0xffffffffu - d) / 10 ? 0xffffffffu : n.magnitude * 10 + d;
            }
        }
        if (source_.peek() == '.') {
            source_.next();
            n.integer = false;
            if (!digit(source_.peek())) return false;
            while (digit(source_.peek())) source_.next();
        }
        if (source_.peek() == 'e' || source_.peek() == 'E') {
            source_.next();
            n.integer = false;
            if (source_.peek() == '+' || source_.peek() == '-') source_.next();
            if (!digit(source_.peek())) return false;
            while (digit(source_.peek())) source_.next();
        }
        return true;
    }

    // An integer member: false only for malformed JSON; `ok` says it is a non-negative
    // integer (Python's `type(n) is int`, booleans excluded; -0 is 0).
    bool natural(u32& value, bool& ok) {
        ok = false;
        if (!at_number()) return skip();
        Number n;
        if (!number(n)) return false;
        ok = n.integer && (!n.negative || !n.magnitude);
        value = n.magnitude;
        return true;
    }

    // Any value, checked and discarded.
    bool skip(u32 depth = kDepth) {
        const int c = peek();
        if (c == '"') {
            Text text;
            return string(text);
        }
        if (c == '-' || digit(c)) {
            Number n;
            return number(n);
        }
        if (c == 't') return word("true");
        if (c == 'f') return word("false");
        if (c == 'n') return word("null");
        if (!depth) return false;
        if (c == '{') return object([&](const Text&) { return skip(depth - 1); });
        if (c != '[') return false;
        source_.next();
        if (peek() == ']') {
            source_.next();
            return true;
        }
        for (;;) {
            if (!skip(depth - 1)) return false;
            const int s = peek();
            source_.next();
            if (s == ']') return true;
            if (s != ',') return false;
        }
    }

    // An object, calling member(key) with the reader at each member's value, which it
    // must read. A repeated key is read again: each member keeps its last value's state.
    template <class Member> bool object(Member member) {
        if (peek() != '{') return false;
        source_.next();
        if (peek() == '}') {
            source_.next();
            return true;
        }
        for (;;) {
            Text key;
            if (!string(key) || peek() != ':') return false;
            source_.next();
            if (!member(key)) return false;
            const int s = peek();
            source_.next();
            if (s == '}') return true;
            if (s != ',') return false;
        }
    }

private:
    Source source_;

    // A backslash escape other than \u.
    static bool escape(int e, u32& code) {
        switch (e) {
        case '"': code = '"'; return true;
        case '\\': code = '\\'; return true;
        case '/': code = '/'; return true;
        case 'b': code = 8; return true;
        case 'f': code = 12; return true;
        case 'n': code = 10; return true;
        case 'r': code = 13; return true;
        case 't': code = 9; return true;
        }
        return false;
    }
    bool word(const char* s) {
        while (*s)
            if (source_.next() != *s++) return false;
        return true;
    }
    bool hex4(u32& code) {
        code = 0;
        for (int i = 0; i < 4; ++i) {
            const int c = source_.next();
            const int v = c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'f' ? c - 'a' + 10 :
                          c >= 'A' && c <= 'F' ? c - 'A' + 10 : -1;
            if (v < 0) return false;
            code = code << 4 | static_cast<u32>(v);
        }
        return true;
    }
    static void put(Text& text, u32 code) {
        unsigned char utf8[4];
        u32 n = 0;
        if (code < 0x80) {
            utf8[n++] = static_cast<unsigned char>(code);
        } else {
            const u32 follow = code < 0x800 ? 1 : code < 0x10000 ? 2 : 3;
            utf8[n++] = static_cast<unsigned char>((0xff00u >> (follow + 1)) | code >> (6 * follow));
            for (u32 i = follow; i--;) utf8[n++] = static_cast<unsigned char>(0x80 | (code >> (6 * i) & 0x3f));
        }
        for (u32 i = 0; i < n; ++i, ++text.length)
            if (text.length < kKeyBytes) text.bytes[text.length] = static_cast<char>(utf8[i]);
    }
};

bool lower_or_digit(char c) { return (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9'); }

bool equal(const char* a, const char* b, u32 count) {
    for (u32 i = 0; i < count; ++i)
        if (a[i] != b[i]) return false;
    return true;
}

// owner.name (tooling/projects.py SECTION_NAME); bounded: longer names are refused.
bool section_name(const Text& name) {
    if (name.length > kKeyBytes) return false;
    u32 parts = 0;
    for (u32 i = 0; i < name.length;) {
        if (!lower_or_digit(name.bytes[i])) return false;
        while (i < name.length && (lower_or_digit(name.bytes[i]) || name.bytes[i] == '-')) ++i;
        ++parts;
        if (i < name.length && (name.bytes[i++] != '.' || i == name.length)) return false;
    }
    return parts >= 2;
}

// A machine ID (tooling/projects.py MACHINE_ID): a lowercase letter, then lowercase
// letters, digits or _, at most 15 characters.
bool machine_name(const Text& name) {
    if (name.length < 1 || name.length > kMachineBytes || name.bytes[0] < 'a' || name.bytes[0] > 'z') return false;
    for (u32 i = 1; i < name.length; ++i)
        if (!lower_or_digit(name.bytes[i]) && name.bytes[i] != '_') return false;
    return true;
}

// This firmware's number for the machine, or the fallback when it lacks it (a kit
// machine left out has number 0) or holds it as the other kind.
u32 number_here(const Listed& entry) {
    for (const CompanionMachine* m = sdk_companion_machines; m->id; ++m)
        if (equal(entry.id, m->id, entry.length) && !m->id[entry.length] && m->effect == entry.effect &&
            value(m->number))
            return value(m->number);
    return entry.effect ? kNoEffect : kPrism;
}

void clear_maps() {
    for (u32 i = 0; i < kNumbers; ++i) engine_map[i] = kUnlisted;
    for (u32 i = 0; i < kEffectNumbers; ++i) effect_map[i] = kUnlisted;
}

// The maps from the listed machines, as tooling/projects.py translation() gives them.
// A number two listed machines claim is trusted for neither: it falls back.
void fill_maps() {
    clear_maps();
    for (u32 i = 0; i < listed_count; ++i) {
        const Listed& entry = listed[i];
        if (entry.old >= (entry.effect ? kEffectNumbers : kNumbers)) continue;  // no pattern holds such a number registered
        u32& slot = (entry.effect ? effect_map : engine_map)[entry.old];
        slot = slot == kUnlisted ? number_here(entry) : entry.effect ? kNoEffect : kPrism;
    }
}

// The entry for a valid ID, replacing its earlier one: false when the section lists
// more distinct machines than it may.
bool record(const Text& id, bool valid, bool effect, u32 old) {
    u32 i = 0;
    while (i < listed_count && !(listed[i].length == id.length && equal(listed[i].id, id.bytes, id.length))) ++i;
    if (i == listed_count) {
        if (listed_count == kMachines) return false;
        ++listed_count;
        for (u32 k = 0; k < id.length; ++k) listed[i].id[k] = id.bytes[k];
        listed[i].length = static_cast<unsigned char>(id.length);
    }
    listed[i].valid = valid;
    listed[i].effect = effect;
    listed[i].old = static_cast<unsigned short>(old < kNumbers ? old : kNumbers);
    return true;
}

// One machine of open-xy.machines: false only for malformed JSON. `valid` is cleared
// when the ID is invalid or one too many; the entry records whether it is an engine and
// page, or an effect and page, each an integer.
bool machine_entry(Parser& p, const Text& id, bool& valid) {
    if (!machine_name(id)) {
        valid = false;
        return p.skip();
    }
    bool engine = false, effect = false, page = false, other = false;
    bool engine_ok = false, effect_ok = false, page_ok = false;
    u32 engine_number = 0, effect_number = 0;
    const bool read = p.peek() != '{' ? p.skip() : p.object([&](const Text& field) {
        const bool is_page = field.is("page"), is_engine = field.is("engine");
        if (!is_page && !is_engine && !field.is("effect")) {
            other = true;
            return p.skip();
        }
        bool& present = is_page ? page : is_engine ? engine : effect;
        bool& ok = is_page ? page_ok : is_engine ? engine_ok : effect_ok;
        present = true;
        ok = false;
        if (!p.at_number()) return p.skip();
        Number n;
        if (!p.number(n)) return false;
        ok = n.integer;
        // A negative number is never a registered one (-0 is 0).
        if (!is_page) (is_engine ? engine_number : effect_number) = n.negative && n.magnitude ? kNumbers : n.magnitude;
        return true;
    });
    if (!read) return false;
    const bool entry = !other && page && page_ok && engine != effect && (engine ? engine_ok : effect_ok);
    if (!record(id, entry, effect, effect ? effect_number : engine_number)) valid = false;
    return true;
}

// open-xy.machines: false only for malformed JSON. `ok` says it is valid, else the
// whole companion is ignored; `applies` that it is version 1 for this firmware's
// profile, the maps then holding its numbers. A newer version is ignored and leaves
// them empty.
bool machines_section(Parser& p, bool& applies, bool& ok) {
    bool have_version = false, have_profile = false, same_profile = false, have_machines = false, valid = true;
    u32 version = 0;
    applies = ok = false;
    clear_maps();
    listed_count = 0;
    if (p.peek() != '{') return p.skip();
    const bool read = p.object([&](const Text& key) {
        if (key.is("version")) {
            if (!p.natural(version, have_version)) return false;
            have_version = have_version && version >= 1;
            return true;
        }
        if (key.is("profile")) {
            have_profile = p.peek() == '"';
            if (!have_profile) return p.skip();
            Text profile;
            if (!p.string(profile)) return false;
            same_profile = profile.is("opxy-1.2.1");
            return true;
        }
        if (!key.is("machines")) return p.skip();
        listed_count = 0;
        valid = true;
        have_machines = p.peek() == '{';
        if (!have_machines) return p.skip();
        return p.object([&](const Text& id) { return machine_entry(p, id, valid); });
    });
    if (!read) return false;
    if (!have_version) return true;
    if (version > 1) {  // a newer section: ignored, the rest of the companion used
        ok = true;
        return true;
    }
    for (u32 i = 0; valid && i < listed_count; ++i) valid = listed[i].valid;
    if (!valid || !have_profile || !have_machines) return true;
    ok = true;
    applies = same_profile;
    if (applies) fill_maps();
    return true;
}

// The project binding: false only for malformed JSON.
bool binding_member(Parser& p, u32& bytes, u32& crc, bool& valid) {
    bool have_bytes = false, have_crc = false;
    valid = false;
    if (p.peek() != '{') return p.skip();
    const bool read = p.object([&](const Text& field) {
        if (field.is("bytes")) return p.natural(bytes, have_bytes);
        if (!field.is("crc32")) return p.skip();
        have_crc = p.peek() == '"';
        if (!have_crc) return p.skip();
        Text text;
        if (!p.string(text)) return false;
        have_crc = text.length == 8;
        crc = 0;
        for (u32 i = 0; have_crc && i < 8; ++i) {
            const char c = text.bytes[i];
            have_crc = digit(c) || (c >= 'a' && c <= 'f');
            crc = crc << 4 | static_cast<u32>(digit(c) ? c - '0' : c - 'a' + 10);
        }
        return true;
    });
    valid = have_bytes && have_crc;
    return read;
}

// Any other owner's section: false only for malformed JSON; `ok` says it is an object
// with a positive integer version.
bool other_section(Parser& p, bool& ok) {
    ok = false;
    if (p.peek() != '{') return p.skip();
    bool positive = false;
    u32 version = 0;
    const bool read = p.object([&](const Text& field) {
        if (!field.is("version")) return p.skip();
        return p.natural(version, positive);
    });
    ok = positive && version >= 1;
    return read;
}

// Other owners' sections whose last value so far is invalid, by name (at most
// kKeyBytes, so held whole).
Text invalid_sections[kInvalidSections];
u32 invalid_count;

// Notes a section's validity under its name: false when too many are invalid at once.
bool note_section(const Text& name, bool ok) {
    u32 i = 0;
    while (i < invalid_count && !(invalid_sections[i].length == name.length &&
                                  equal(invalid_sections[i].bytes, name.bytes, name.length)))
        ++i;
    if (ok) {
        if (i < invalid_count) invalid_sections[i] = invalid_sections[--invalid_count];
        return true;
    }
    if (i < invalid_count) return true;
    if (invalid_count == kInvalidSections) return false;
    invalid_sections[invalid_count++] = name;
    return true;
}

// Reads the companion: true when it is valid and its machines section applies to this
// firmware, the maps then filled, with the binding it gives.
bool read_document(File* file, u32& bytes, u32& crc, bool& authoritative,
                   opxy::platform::project_sections::Document* capture = nullptr, u32* schema = nullptr) {
    if (capture) capture->clear();
    Parser p(file, capture);
    bool format = false, version = false, binding = false, sections = false, applies = false, valid = false;
    u32 envelope_version = 0;
    authoritative = false;
    clear_maps();
    const bool read = p.object([&](const Text& key) {
        if (key.is("format")) {
            format = p.peek() == '"';
            if (!format) return p.skip();
            Text text;
            if (!p.string(text)) return false;
            format = text.is("open-xy-project");
            return true;
        }
        if (key.is("version")) {
            if (!p.natural(envelope_version, version)) return false;
            version = version && envelope_version >= 1;
            return true;
        }
        if (key.is("project")) return binding_member(p, bytes, crc, binding);
        if (!key.is("sections")) return p.skip();
        // Only the last sections object counts.
        if (capture) capture->count = 0;
        applies = false;
        valid = true;
        invalid_count = 0;
        clear_maps();
        sections = p.peek() == '{';
        if (!sections) return p.skip();
        bool machines_ok = true;
        const bool members = p.object([&](const Text& name) {
            if (!section_name(name)) {
                valid = false;
                return p.skip();
            }
            const u32 begin = p.position();
            bool read_section;
            if (name.is("open-xy.machines")) read_section = machines_section(p, applies, machines_ok);
            else {
                bool ok;
                read_section = other_section(p, ok);
                if (read_section && !note_section(name, ok)) valid = false;
            }
            const u32 end = p.position();
            if (read_section && capture && !capture->record(name.bytes, name.length, begin, end - begin)) valid = false;
            return read_section;
        });
        valid = valid && machines_ok && !invalid_count;
        return members;
    });
    authoritative = read && p.end() && format && version && binding &&
        (envelope_version > 1 || (sections && valid));
    if (schema) *schema = envelope_version;
    if (capture && authoritative) capture->compact();
    return authoritative && envelope_version == 1 && applies;
}

// SDK-owned state: no contributor code is called by the file worker.
using StateDocument = opxy::platform::project_sections::Document;
using StateSpec = opxy::platform::ProjectStateSpec;
using StateStatus = opxy::extensions::ProjectStateStatus;
using StateToken = opxy::extensions::ProjectToken;
StateDocument project_live, project_staged, project_saved;
volatile opxy::u8 project_state_busy;
volatile u32 project_reset_serial;
bool project_state_initialized, project_loading, project_stage_ready;
bool project_protected, project_stage_protected, project_save_failed, project_retry;
u32 project_revision, project_stage_generation, project_stage_pointer, project_stage_reset;
u32 project_stage_bytes, project_stage_crc;
bool project_stage_bound;
StateToken project_token;
opxy::u8 project_decoded[8192];
struct StateObservation { const StateSpec* spec; u32 revision; };
StateObservation project_observations[16];
u32 project_observation_count;
u32 project_reported_status, project_reported_generation;

u32 current_generation() { return stock<u32()>(0x8d584224)(); }
u32 current_project() { return opxy::platform::word(0x840a0524); }
bool pending_project() { return opxy::platform::word(0x840a0528) != 0; }
bool same_token(const StateToken& a, const StateToken& b) {
    return a.generation == b.generation && a.identity == b.identity;
}
StateToken native_token() { return StateToken{current_generation(), current_project()}; }
bool project_save_current() { return !project_state_initialized || same_token(project_token, native_token()); }
void state_unlock() { opxy::platform::project_state_unlock(&project_state_busy); }
bool state_lock() { return opxy::platform::project_state_try_lock(&project_state_busy); }

bool resolve_state() {
    if (project_loading) return false;
    const StateToken now = native_token();
    if (!now.identity) return false;
    if (!project_state_initialized || !same_token(now, project_token)) {
        if (project_stage_ready && project_stage_generation == now.generation &&
            project_stage_pointer == now.identity && project_stage_reset == project_reset_serial) {
            project_live = project_staged;
            project_protected = project_stage_protected;
        } else {
            project_live.clear();
            project_protected = false;
        }
        project_stage_ready = false;
        project_token = now;
        project_state_initialized = true;
        project_save_failed = false;
        project_retry = false;
        ++project_revision;
    }
    return true;
}

// Opaque extension payload format: {"version": positive integer, "data": "hex"}.
// All unknown fields remain in the retained raw section until its owner publishes.
bool decode_state(const StateDocument& document, const StateSpec& spec,
                  u32& version, u32& size, StateStatus& status) {
    version = size = 0;
    const opxy::i32 index = document.find(spec.name);
    if (index < 0) { status = StateStatus::empty; return true; }
    const auto& section = document.sections[index];
    Parser parser(document.bytes + section.offset, section.size);
    bool have_version = false, have_data = false;
    u32 begin = 0, end = 0;
    const bool parsed = parser.object([&](const Text& key) {
        if (key.is("version")) return parser.natural(version, have_version);
        if (!key.is("data")) return parser.skip();
        begin = parser.position();
        Text ignored;
        have_data = parser.peek() == '"';
        if (!have_data) return parser.skip();
        const bool ok = parser.string(ignored);
        end = parser.position();
        return ok;
    }) && parser.end();
    if (!parsed || !have_version || !version) { status = StateStatus::unavailable; return false; }
    if (version != spec.version) { status = StateStatus::unsupported; return false; }
    while (end > begin && document.bytes[section.offset + end - 1] != '"') --end;
    if (!have_data || end < begin + 2 || (end - begin - 2) % 2 ||
        (end - begin - 2) / 2 > spec.capacity || (end - begin - 2) / 2 > sizeof(project_decoded)) {
        status = StateStatus::unavailable; return false;
    }
    size = (end - begin - 2) / 2;
    for (u32 i = 0; i < size; ++i) {
        u32 value = 0;
        for (u32 n = 0; n < 2; ++n) {
            const unsigned char c = document.bytes[section.offset + begin + 1 + i * 2 + n];
            const u32 digit = c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'f' ? c - 'a' + 10 : 16;
            if (digit == 16) { size = 0; status = StateStatus::unavailable; return false; }
            value = value * 16 + digit;
        }
        project_decoded[i] = static_cast<opxy::u8>(value);
    }
    status = StateStatus::ready;
    return true;
}

void write_project_sections(Writer& writer) {
    if (!project_state_initialized) return;
    for (u32 i = 0; i < project_saved.count; ++i) {
        const auto& section = project_saved.sections[i];
        if (project_saved.find("open-xy.machines") == static_cast<opxy::i32>(i)) continue;
        writer.put(",\n    \""); writer.put(section.name); writer.put("\": ");
        for (u32 n = 0; n < section.size; ++n) writer.byte(static_cast<char>(project_saved.bytes[section.offset + n]));
    }
}

// Rewrites every pattern's machine number on the instrument and send tracks
// (docs/abi-1.2.1.md, project companions).
void remap(unsigned char* project) {
    for (u32 track = 0; track < 16; ++track) {
        if (track >= 8 && track < 14) continue;  // tracks 8-13 hold no machine number
        const u32* map = track < 8 ? engine_map : effect_map;
        const u32 limit = track < 8 ? kNumbers : kEffectNumbers;
        unsigned char* base = project + 0xd7c + track * 0x53f44;
        u32 patterns = *reinterpret_cast<u32*>(base + 0x53f40);
        if (patterns > 16) patterns = 16;
        for (u32 k = 0; k < patterns; ++k) {
            u32& number = *reinterpret_cast<u32*>(base + k * 0x53f4 + 0x18);
            if (number < limit && map[number] != kUnlisted) number = map[number];
        }
    }
}

enum class Document { unusable, unbound, ignored, applies };
Document read_bound(const char* path, u32 measured_bytes, u32 measured_crc, StateDocument* capture = nullptr, u32* schema = nullptr, bool* present = nullptr) {
    File* file = file_open(path, "rb");
    if (!file) return Document::unusable;
    if (present) *present = true;
    u32 bytes = 0, crc = 0;
    bool authoritative = false;
    const bool applies = read_document(file, bytes, crc, authoritative, capture, schema);
    const bool closed = file_close(file) == 0;
    if (!authoritative || !closed) return Document::unusable;
    if (bytes != measured_bytes || crc != measured_crc) return Document::unbound;
    return applies ? Document::applies : Document::ignored;
}

void apply_companion(void* project, const char* path) {
    if (!companion_path(path, read_companion_path)) return;
    u32 bytes, crc;
    if (!measure(path, read_chunk, bytes, crc)) {
        if (project_loading) project_stage_protected = true;
        return;
    }
    temporary_path(read_companion_path, read_temporary_path);
    // A bound published document is authoritative even if its version/profile
    // is unsupported or it intentionally contains no machine mappings.
    bool present = false; u32 schema = 0;
    StateDocument* capture = project_loading ? &project_staged : nullptr;
    Document result = read_bound(read_companion_path, bytes, crc, capture, &schema, &present);
    if (result == Document::unusable || result == Document::unbound) {
        const bool primary_present = present;
        const Document fallback = read_bound(read_temporary_path, bytes, crc, capture, &schema, &present);
        if (fallback == Document::applies || fallback == Document::ignored ||
            (fallback == Document::unbound && !primary_present)) result = fallback;
    }
    if (project_loading) {
        project_stage_bound = result == Document::applies || result == Document::ignored;
        project_stage_bytes = bytes; project_stage_crc = crc;
        project_stage_protected = (present && result == Document::unusable) || (project_stage_bound && schema != 1);
        if (!project_stage_bound) project_staged.clear();
    }
    if (result == Document::applies) remap(static_cast<unsigned char*>(project));
}
}

u32 sdk_project_save(void* project, const char* path) {
    if (!project) {
        const u32 saved = save(project, path);
        u32 bytes, crc;
        if (saved && path && companion_path(path, companion) && measure(path, chunk, bytes, crc)) write_companion(bytes, crc);
        return saved;
    }
    if (!state_lock()) return 0;
    if (!resolve_state() || project_protected || current_project() != static_cast<u32>(reinterpret_cast<opxy::platform::Address>(project))) {
        state_unlock(); return 0;
    }
    const StateToken token = project_token;
    project_saved = project_live;
    if (!companion_size_fits()) {
        project_save_failed = true; ++project_revision;
        state_unlock(); return 0;
    }
    const u32 saved = save(project, path);
    u32 bytes, crc;
    const bool complete = saved && same_token(token, native_token()) && path && companion_path(path, companion) &&
        measure(path, chunk, bytes, crc) && write_companion(bytes, crc);
    if (!complete) { project_save_failed = true; project_retry = true; ++project_revision; }
    else if (project_save_failed) { project_save_failed = false; ++project_revision; }
    state_unlock();
    return saved;
}

// Stock's deserialize, then the companion's numbers. The project is the inactive buffer,
// which nothing reads until stock validates it and makes it pending.
u32 sdk_project_load(void* project, const char* path) {
    const u32 loaded = deserialize(project, path);
    if (loaded && project && path) {
        if (project_loading) project_stage_pointer = static_cast<u32>(reinterpret_cast<opxy::platform::Address>(project));
        apply_companion(project, path);
    }
    return loaded;
}


namespace opxy { namespace platform {
const char* project_state_status() {
    if (!state_lock()) return nullptr;
    if (!resolve_state() || pending_project()) { state_unlock(); return nullptr; }
    const u32 status = project_protected ? 2 : project_save_failed ? 1 : 0;
    const bool changed = status != project_reported_status || project_reported_generation != project_token.generation;
    project_reported_status = status; project_reported_generation = project_token.generation;
    const bool retry = project_retry && !project_protected;
    project_retry = false;
    state_unlock();
    if (retry) stock<void()>(0x8d585ee8)();
    return !changed || !status ? nullptr : status == 2 ? "project data: reload or New" : "project data save failed";
}
void project_state_observe(const ProjectStateSpec& spec, void (*callback)(const extensions::ProjectState&)) {
    if (!callback || !spec.name || !spec.version || !spec.capacity || spec.capacity > sizeof(project_decoded) || !state_lock()) return;
    if (!resolve_state() || pending_project()) { state_unlock(); return; }
    u32 index = 0;
    while (index < project_observation_count && project_observations[index].spec != &spec) ++index;
    if (index == project_observation_count) {
        if (index == 16) { state_unlock(); return; }
        project_observations[index] = StateObservation{&spec, project_revision - 1};
        ++project_observation_count;
    }
    if (project_observations[index].revision == project_revision) { state_unlock(); return; }
    u32 version = 0, size = 0;
    StateStatus status = StateStatus::unavailable;
    if (!project_protected) decode_state(project_live, spec, version, size, status);
    if (project_save_failed && !project_protected) status = StateStatus::save_failed;
    const extensions::ProjectState event = {project_token, status, version, size, size ? project_decoded : nullptr};
    project_observations[index].revision = project_revision;
    callback(event);
    state_unlock();
}

bool project_state_publish(const ProjectStateSpec& spec, const extensions::ProjectToken& token,
                           const u8* data, u32 size) {
    if (!spec.name || !spec.version || !spec.capacity || spec.capacity > sizeof(project_decoded) ||
        size > spec.capacity || (size && !data) || !state_lock()) return false;
    if (!resolve_state() || pending_project() || project_protected || !same_token(token, project_token)) {
        state_unlock(); return false;
    }
    u32 old_version, old_size;
    StateStatus old_status;
    if (!decode_state(project_live, spec, old_version, old_size, old_status)) { state_unlock(); return false; }
    char version[10]; u32 digits = 0, number = spec.version;
    do { version[digits++] = static_cast<char>('0' + number % 10); number /= 10; } while (number);
    const char prefix[] = "{\"version\":";
    const char middle[] = ",\"data\":\"";
    const u32 required = sizeof(prefix) - 1 + digits + sizeof(middle) - 1 + 2 * size + 2;
    const i32 previous = project_live.find(spec.name);
    const u32 previous_size = previous < 0 ? 0 : project_live.sections[previous].size;
    if ((previous < 0 && project_live.count == project_sections::section_limit) ||
        required > project_sections::byte_limit - project_live.size + previous_size) { state_unlock(); return false; }
    if (previous >= 0) project_live.erase(static_cast<u32>(previous));
    const u32 begin = project_live.size;
    for (u32 i = 0; i < sizeof(prefix) - 1; ++i) project_live.bytes[project_live.size++] = prefix[i];
    while (digits) project_live.bytes[project_live.size++] = version[--digits];
    for (u32 i = 0; i < sizeof(middle) - 1; ++i) project_live.bytes[project_live.size++] = middle[i];
    for (u32 i = 0; i < size; ++i) {
        project_live.bytes[project_live.size++] = "0123456789abcdef"[data[i] >> 4];
        project_live.bytes[project_live.size++] = "0123456789abcdef"[data[i] & 15];
    }
    project_live.bytes[project_live.size++] = '"'; project_live.bytes[project_live.size++] = '}';
    u32 length = 0; while (spec.name[length]) ++length;
    const bool stored = project_live.record(spec.name, length, begin, required) && same_token(token, native_token());
    ++project_revision;
    state_unlock();
    if (stored) stock<void()>(0x8d585ee8)();
    return stored;
}
}}

extern "C" u32 sdk_project_load_begin(const char* path, u32 commit) __asm__("sdk_project_load_begin");
extern "C" void sdk_project_new_commit() __asm__("sdk_project_new_commit");
extern "C" u32 sdk_project_rename(const char*, const char*) __asm__("sdk_project_rename");

u32 sdk_project_load_begin(const char* path, u32 commit) {
    if (!path || pending_project() || !state_lock()) return 0;
    // A previous successful commit may still await controller notification.
    // Adopt it before a new candidate can replace the staging buffer.
    resolve_state();
    const u32 before = current_generation(), reset = project_reset_serial;
    project_loading = true;
    project_stage_ready = false;
    project_staged.clear();
    project_stage_pointer = 0;
    project_stage_protected = false;
    project_stage_bound = false;
    const u32 loaded = stock<u32(const char*, u32)>(0x8d5890b0)(path, commit);
    if (loaded) {
        u32 bytes, crc;
        if (project_stage_bound && (!measure(path, read_chunk, bytes, crc) ||
            bytes != project_stage_bytes || crc != project_stage_crc)) project_stage_protected = true;
        project_stage_generation = before + 1;
        project_stage_reset = reset;
        const bool candidate = project_stage_pointer &&
            (project_stage_pointer == opxy::platform::word(0x840a0528) ||
             (project_stage_pointer == current_project() && current_generation() == before + 1));
        project_stage_ready = reset == project_reset_serial && candidate;
    }
    project_loading = false;
    state_unlock();
    return loaded;
}

void sdk_project_new_commit() {
    // New may reuse an abandoned load's pointer and next generation. Its explicit
    // commit invalidates that candidate without blocking the native controller.
    ++project_reset_serial;
    stock<void()>(0x8d585cdc)();
}

u32 sdk_project_rename(const char* from, const char* to) {
    if (!from || !to || !state_lock()) return 0;
    if (project_state_initialized && (!resolve_state() || project_protected || project_save_failed)) {
        state_unlock(); return 0;
    }
    char old_path[kPathBytes], new_path[kPathBytes];
    if (!companion_path(from, old_path) || !companion_path(to, new_path)) { state_unlock(); return 0; }
    const u32 result = stock<u32(const char*, const char*)>(0x8c040c78)(from, to);
    if (result) {
        for (u32 i = 0; i < 2; ++i) {
            File* file = file_open(old_path, "rb");
            if (file && (file_close(file) != 0 || !file_publish(old_path, new_path))) {
                project_protected = true; project_save_failed = true; ++project_revision;
            }
            if (!i) { temporary_path(old_path, old_path); temporary_path(new_path, new_path); }
        }
    }
    state_unlock();
    return result;
}
