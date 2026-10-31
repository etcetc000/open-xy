// Track control's data kept with each project (docs/projects.md, open-xy.control):
// part of the control service, so only builds with a track control machine link it.
// The companion writer calls sdk_control_kept_write, through startup.cpp, with the
// companion open, at the start of its sections.
//
// Stock loads every project through one call of read(project, path) (8d599b10, from
// 8d58913e in 8d5890b0), which the project companion hooks (project_companion.cpp);
// after a read that succeeds, it calls sdk_control_kept_load, through startup.cpp, and
// track control machines get back what they kept. A new project reads no file: stock clears the project (8d588114,
// from 8d572f70) or clears it at random (8d588988, from 8d572ff8). Both do nothing
// while a project is loading (the word at 840a0528); otherwise machines then get
// nothing back, as after loading a project without their data.
//
// Stock loads on its background file task, not the GUI task that runs control():
// bg_file_bfm.cpp queues a request (8d4df754, vtable 84039cdc) whose run method
// (8d4de07c) calls the load (8d5890b0). So the load only finds the machines' data;
// the next GUI tick ends the old project's capture and overlays, resets each unique
// machine and gives it its data (sdk_control_kept_apply), on the GUI task.
#include "project_files.hpp"
#include "project_companion.hpp"
#include "track_control.hpp"

extern "C" opxy::u32 sdk_control_kept_write(void* file) __asm__("sdk_control_kept_write");
extern "C" void sdk_control_kept_load(const char* path) __asm__("sdk_control_kept_load");
extern "C" opxy::u32 sdk_project_clear(opxy::u32, opxy::u32, opxy::u32) __asm__("sdk_project_clear");
extern "C" opxy::u32 sdk_project_clear_random(opxy::u32, opxy::u32, opxy::u32) __asm__("sdk_project_clear_random");

namespace {
using opxy::u32;
using opxy::platform::stock;

const u32 kProjectLoading = 0x840a0528;

// The format's 64 KiB, less what a reader never needs.
const u32 kReadBytes = 32768;
char project_text[kReadBytes];
// One machine's kept bytes, as it saved them or as read back.
unsigned char kept[opxy::control::saved_capacity];
const char kBase64[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

// Writes through a small buffer straight to the open companion.
class Stream {
public:
    explicit Stream(kept_files::File* file) : file(file) {}
    ~Stream() { flush(); }
    void put(const char* s) { while (*s) byte(*s++); }
    void byte(char c) {
        if (count == sizeof buffer) flush();
        buffer[count++] = c;
    }
    void base64(const unsigned char* data, u32 bytes) {
        for (u32 i = 0; i < bytes; i += 3) {
            const u32 n = bytes - i, v = data[i] << 16 | (n > 1 ? data[i + 1] << 8 : 0) | (n > 2 ? data[i + 2] : 0);
            byte(kBase64[v >> 18]);
            byte(kBase64[v >> 12 & 63]);
            byte(n > 1 ? kBase64[v >> 6 & 63] : '=');
            byte(n > 2 ? kBase64[v & 63] : '=');
        }
    }
private:
    void flush() {
        if (count) kept_files::file_write(buffer, count, 1, file);
        count = 0;
    }
    kept_files::File* file;
    char buffer[256];
    u32 count = 0;
};

// A small reader for companions any writer made: the span [at, to) of a value.
struct Reader {
    u32 length;
    u32 space(u32 i) const {
        while (i < length && (project_text[i] == ' ' || project_text[i] == '\n' || project_text[i] == '\r' || project_text[i] == '\t')) ++i;
        return i;
    }
    // The end of the value at i, or 0 when it is malformed.
    u32 end(u32 i) const {
        if (i >= length) return 0;
        if (project_text[i] == '"') {
            for (++i; i < length; ++i) {
                if (project_text[i] == '\\') ++i;
                else if (project_text[i] == '"') return i + 1;
            }
            return 0;
        }
        if (project_text[i] == '{' || project_text[i] == '[') {
            u32 depth = 0;
            for (; i < length; ++i) {
                if (project_text[i] == '"') {
                    const u32 after = end(i);
                    if (!after) return 0;
                    i = after - 1;
                } else if (project_text[i] == '{' || project_text[i] == '[') {
                    ++depth;
                } else if (project_text[i] == '}' || project_text[i] == ']') {
                    if (!--depth) return i + 1;
                }
            }
            return 0;
        }
        while (i < length && project_text[i] != ',' && project_text[i] != '}' && project_text[i] != ']' && project_text[i] != ' ' &&
               project_text[i] != '\n' && project_text[i] != '\r' && project_text[i] != '\t')
            ++i;
        return i;
    }
    bool is(u32 at, u32 to, const char* key) const {
        if (to - at < 2 || project_text[at] != '"') return false;
        u32 i = at + 1;
        for (; *key && i < to - 1; ++i, ++key)
            if (project_text[i] != *key) return false;
        return !*key && i == to - 1;
    }
    // The member `key` of the object [object, object_end): its value's span.
    bool member(u32 object, u32 object_end, const char* key, u32& at, u32& to) const {
        if (object >= object_end || project_text[object] != '{') return false;
        for (u32 i = space(object + 1); i < object_end && project_text[i] == '"';) {
            const u32 key_end = end(i);
            const u32 colon = key_end ? space(key_end) : object_end;
            if (colon >= object_end || project_text[colon] != ':') return false;
            const u32 value = space(colon + 1), value_end = end(value);
            if (!value_end || value_end > object_end) return false;
            if (is(i, key_end, key)) {
                at = value;
                to = value_end;
                return true;
            }
            const u32 next = space(value_end);
            if (next >= object_end || project_text[next] != ',') return false;
            i = space(next + 1);
        }
        return false;
    }
    bool number(u32 at, u32 to, u32& v) const {
        v = 0;
        if (at >= to) return false;
        for (u32 i = at; i < to; ++i) {
            if (project_text[i] < '0' || project_text[i] > '9' || v > 400000000) return false;
            v = v * 10 + static_cast<u32>(project_text[i] - '0');
        }
        return true;
    }
    bool hex(u32 at, u32 to, u32& v) const {
        v = 0;
        if (to - at != 10 || project_text[at] != '"') return false;
        for (u32 i = at + 1; i < to - 1; ++i) {
            const char c = project_text[i];
            const u32 d = c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'f' ? c - 'a' + 10 : 16;
            if (d > 15) return false;
            v = v << 4 | d;
        }
        return true;
    }
    // A base64 string into kept: its byte count, or 0.
    u32 base64(u32 at, u32 to, u32 capacity) const {
        if (to - at < 2 || project_text[at] != '"' || (to - at - 2) % 4) return 0;
        u32 count = 0;
        for (u32 i = at + 1; i + 4 <= to - 1; i += 4) {
            u32 v = 0, pad = 0;
            for (u32 j = 0; j < 4; ++j) {
                const char c = project_text[i + j];
                u32 d = 64;
                for (u32 k = 0; k < 64; ++k)
                    if (kBase64[k] == c) d = k;
                if (c == '=' && i + 4 == to - 1 && j >= 2) d = 0, ++pad;
                if (d > 63 || (pad && c != '=')) return 0;
                v = v << 6 | d;
            }
            for (u32 j = 0; j < 3 - pad; ++j) {
                if (count >= capacity) return 0;
                kept[count++] = static_cast<unsigned char>(v >> (16 - 8 * j));
            }
        }
        return count;
    }
};

// The companion's "open-xy.control" machines object, when the companion
// matches the project's bytes.
bool kept_machines(const Reader& r, u32 bytes, u32 crc, u32& at, u32& to) {
    const u32 root = r.space(0), root_end = r.end(root);
    u32 a, b, c, d, v;
    if (!root_end || !r.member(root, root_end, "format", a, b) || !r.is(a, b, "open-xy-project")) return false;
    if (!r.member(root, root_end, "version", a, b) || !r.number(a, b, v) || v != 1) return false;
    if (!r.member(root, root_end, "project", a, b)) return false;
    if (!r.member(a, b, "bytes", c, d) || !r.number(c, d, v) || v != bytes) return false;
    if (!r.member(a, b, "crc32", c, d) || !r.hex(c, d, v) || v != crc) return false;
    if (!r.member(root, root_end, "sections", a, b) || !r.member(a, b, "open-xy.control", c, d)) return false;
    if (!r.member(c, d, "version", a, b) || !r.number(a, b, v) || v != 1) return false;
    return r.member(c, d, "machines", at, to);
}

// What the last load or new project left for the next GUI tick: whether it
// waits, and the companion's machines object, or a length of 0 for nothing.
volatile u32 pending;
u32 found_length, found_at, found_to;

void load_kept(const char* path) {
    pending = 0;  // project_text changes below
    u32 bytes, crc, length = 0, at = 0, to = 0;
    bool keeping = false, found = false;
    for (u32 i = 0; i < sdk_control_machine_count; ++i) keeping = keeping || sdk_control_machines[i].load;
    // Without a machine that keeps data, nothing to give back: no project I/O.
    if (keeping && kept_files::companion_path(path) && kept_files::measure(path, bytes, crc)) {
        if (kept_files::File* file = kept_files::file_open(kept_files::companion, "rb")) {
            length = kept_files::file_read(project_text, 1, kReadBytes, file);
            kept_files::file_close(file);
        }
        const Reader r = {length < kReadBytes ? length : 0};
        found = r.length && kept_machines(r, bytes, crc, at, to);
    }
    found_length = found ? length : 0;
    found_at = at;
    found_to = to;
    pending = 1;
}

// After a new project: every machine that keeps data gets nothing back.
void forget_kept() {
    pending = 0;
    found_length = 0;
    pending = 1;
}
}

// From the GUI tick, before the machines run: after a load or a new project, each
// ready unique machine is reset, then one that keeps data gets back what it kept.
extern "C" void sdk_control_kept_apply() {
    if (!pending) return;
    pending = 0;
    sdk_control_forget();
    const Reader r = {found_length};
    for (u32 i = 0; i < sdk_control_machine_count; ++i) {
        const opxy::platform::ControlMachine& m = sdk_control_machines[i];
        if (!m.unique || m.ready() != 1) continue;
        m.reset(0);
        sdk_control_prepared[i] = true;
        if (!m.load) continue;
        u32 a, b, count = 0;
        if (r.length && r.member(found_at, found_to, m.name, a, b)) count = r.base64(a, b, m.saved_bytes);
        m.load(kept, count);
    }
}

// "open-xy.control": what each track control machine keeps, by machine ID.
extern "C" u32 sdk_control_kept_write(void* file) {
    Stream out(static_cast<kept_files::File*>(file));
    u32 listed = 0;
    for (u32 i = 0; i < sdk_control_machine_count; ++i) {
        const opxy::platform::ControlMachine& m = sdk_control_machines[i];
        if (!m.save || m.ready() != 1) continue;
        const u32 count = m.save(kept);
        if (!count || count > m.saved_bytes) continue;
        out.put(listed++ ? ",\n        \"" : "    \"open-xy.control\": {\n      \"machines\": {\n        \"");
        out.put(m.name);
        out.put("\": \"");
        out.base64(kept, count);
        out.byte('"');
    }
    if (listed) out.put("\n      },\n      \"version\": 1\n    },\n");
    return listed;
}

// From the companion's load hook, after a read that succeeded, before stock makes the
// project pending.
void sdk_control_kept_load(const char* path) { load_kept(path); }

u32 sdk_project_clear(u32 a, u32 b, u32 c) {
    const bool cleared = !opxy::platform::word(kProjectLoading);
    const u32 result = stock<u32(u32, u32, u32)>(0x8d588114)(a, b, c);
    if (cleared) forget_kept();
    return result;
}

u32 sdk_project_clear_random(u32 a, u32 b, u32 c) {
    const bool cleared = !opxy::platform::word(kProjectLoading);
    const u32 result = stock<u32(u32, u32, u32)>(0x8d588988)(a, b, c);
    if (cleared) forget_kept();
    return result;
}
