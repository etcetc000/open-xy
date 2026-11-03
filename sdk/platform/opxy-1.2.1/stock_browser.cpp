#include "stock_browser.hpp"

namespace opxy { namespace platform { namespace stock_browser { namespace detail {
namespace {
Session* owner;
Session* current(Delegate* delegate) {
    Session* session = delegate ? delegate->session : nullptr;
    return owner == session && session && !session->closing ? session : nullptr;
}
void noop(Delegate*) {}
u32 no(Delegate*) { return 0; }
u32 key(Delegate*, const u32* event) { return event && event[0] == 7 ? 1u : 0u; }
void highlight(Delegate* delegate, u32 mode, i32 id) {
    Session* session = current(delegate);
    if (session && !session->accepted && session->callbacks.highlight)
        session->callbacks.highlight(session->callbacks.context, mode, id);
    if (session && !session->accepted && mode == 0 && id >= 0 && static_cast<u32>(id) < session->count)
        session->candidate = static_cast<u32>(id);
}
u32 click(Delegate* delegate, u32 encoder) {
    Session* session = current(delegate);
    // A flat list confirms on the left or white click; two columns on knobs 2-4, as
    // stock's preset browser does (its knob 1 sorts, which this adapter has none of).
    const bool confirms = session && (session->columns ? encoder >= 1 && encoder <= 3 : encoder == 0 || encoder == 3);
    if (!confirms) return 1;
    if (!session->accepted) {
        session->accepted = true;
        session->callbacks.confirm(session->callbacks.context, session->candidate);
    }
    return 0; // Stock pops controller 9 after either left or white confirmation.
}
void leave(Delegate* delegate) {
    Session* session = current(delegate);
    if (!session) return;
    session->closing = true; // Keep ownership during callbacks; reject reentry.
    if (!session->accepted && session->callbacks.cancel)
        session->callbacks.cancel(session->callbacks.context);
    delegate->session = nullptr;
    owner = nullptr;
}
void audition(Delegate*, i32) {}
void options(Vector<String>* result, Delegate*) { *result = Vector<String>{}; }
void set_option(Delegate*, u32) {}
const Methods methods = {
    noop, no, key, key, highlight, no, no, click,
    noop, leave, audition, no, options, no, set_option, no
};
u32 length(const char* text) {
    if (!text) return 0;
    for (u32 i = 0; i < 16; ++i) {
        const unsigned char c = static_cast<unsigned char>(text[i]);
        if (!c) return i;
        if (c < 32 || c > 126) return 0;
    }
    return 0;
}
void copy(char* target, const char* source, u32 size) {
    for (u32 i = 0; i < 16; ++i) target[i] = i < size ? source[i] : 0;
}
}
bool name(String& target, const char* text) {
    const u32 size = length(text);
    target = String{};
    target.length = size; target.capacity = 15;
    copy(target.text, text, size);
    return size != 0;
}

bool open(Session& session, Row* rows, u32 count, const char* title,
          const char* const* labels, u32 initial, Callbacks callbacks, i32 track) {
    // Validate everything first: rows an open browser may show, or invalid input,
    // leave the caller's storage untouched.
    if (owner || word(kScreen) == 9 || !rows || !labels || !count || initial >= count || !callbacks.confirm ||
        !length(title))
        return false;
    for (u32 i = 0; i < count; ++i) if (!length(labels[i])) return false;
    for (u32 i = 0; i < count; ++i) {
        rows[i] = Row{};
        name(rows[i].name, labels[i]);
        rows[i].id = static_cast<i32>(i);
    }
    return open_rows(session, rows, count, title, initial, callbacks, track, false);
}

bool open_rows(Session& session, Row* rows, u32 count, const char* title, u32 initial, Callbacks callbacks,
               i32 track, bool columns) {
    if (owner || word(kScreen) == 9 || !rows || !count || initial >= count || !callbacks.confirm) return false;
    const u32 title_size = length(title);
    if (!title_size) return false;
    // The getters are the same ones the stock send-effect selector uses.
    void* browser = stock<void*()>(0x8c09dc28)();
    void* screen = stock<void*()>(0x8d550e70)();
    if (!browser || !screen) return false;
    session.catalog = Catalog{{0, rows, rows + count, rows + count}, 0};
    session.delegate = Delegate{&methods, &session};
    session.callbacks = callbacks;
    copy(session.title, title, title_size);
    session.count = count; session.candidate = initial;
    session.accepted = session.closing = false;
    session.columns = columns;
    owner = &session;
    // Stock's send-effect selector passes its track here (8c08f638).
    stock<void(void*, i32, const char*)>(0x8d551c48)(screen, track < 16 ? track : -1, session.title);
    stock<void(void*, Catalog*, i32, Delegate*, u32)>(0x8c0a0b90)(
        browser, &session.catalog, static_cast<i32>(initial), &session.delegate, 0);
    // The preset browser (8c0b174c) shows its bank column so; flat lists hide it.
    stock<void(void*, u32)>(0x8d5501ec)(screen, columns ? 1 : 0);
    return true;
}
}}}}
