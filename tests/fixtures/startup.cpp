// Host contract checks for generated startup glue: two own-page synths
// (first, second), two stock-page sample engines, one of them (drawn) with its
// own screen, an own-page effect and a player. Both screen registries must
// coexist after SDK integration.
#include <startup.cpp>
#include <cstdio>
#include <cstdlib>
#include <initializer_list>
#include <string>

#define require(value) do { if (!(value)) { std::fprintf(stderr, "line %d\n", __LINE__); std::abort(); } } while (0)

static std::string calls;
static bool gui, screens = true;
static int ready_state = 1;
static const char* named;
static u32 inserted;

#define ENGINE(prefix)                                                                        \
    const char prefix##_engine_id[1] = {};                                                    \
    int prefix##_register() { calls += #prefix " "; return 1; }                               \
    int prefix##_name_matches(const void* name) { return named && std::string(named) == #prefix; } \
    int prefix##_ready() { calls += #prefix "? "; return ready_state; }
ENGINE(first)
ENGINE(second)
ENGINE(sampled)
ENGINE(drawn)
const char first_page_id[1] = {}, second_page_id[1] = {}, drawn_screen_page_id[1] = {};
static int drawn_screen = 1;
int drawn_screen_register() { calls += "drawn-page "; return drawn_screen; }
int drawn_screen_ready() { calls += "drawn-page? "; return drawn_screen; }
// The selected track's engine, through stock's getters (host addresses).
static u32 track = 0, engine = 0;
static u32 selected() { return track; }
static u32 engine_of(u32 t) { require(t == track); return engine; }
opxy::platform::Address opxy::platform::host_stock_address(Address address) {
    if (address == 0x8d4d6f14) return reinterpret_cast<Address>(&selected);
    if (address == 0x8d58781c) return reinterpret_cast<Address>(&engine_of);
    std::abort();
}
int first_screen_register() { calls += "first-page "; return screens; }
int second_screen_register() { calls += "second-page "; return 1; }
int effect_fx_register() { calls += "effect "; return 1; }
const char effect_fx_page_id[1] = {};
int effect_screen_register() { calls += "effect-page "; return 1; }
int effect_ready() { calls += "effect? "; return ready_state; }
int player_screen_register() { calls += "player-page "; return 1; }
u32 stock_gui_role() { calls += "role "; return gui; }
u32 stock_catalog_insert(void*, const void*, u32 id) { inserted = id; return 7; }
int sdk_stock_engine_known(const void*) { return named && std::string(named) == "prism"; }

static u32 catalog(const char* name, u32 id) {
    named = name; inserted = 0; calls.clear();
    return extensions_catalog_checked(nullptr, name, id);
}

static int installed(const char* name) {
    named = name;
    return sdk_engine_installed(name);
}

int main() {
    // The preset scan's engine check: stock engines, then this build's machines by name.
    require(installed("prism") == 1 && installed("first") == 1 && installed("sampled") == 1);
    require(installed("gone") == 0 && installed("effect") == 0 && installed("player") == 0);
    // Effects register on both roles, before the role check; engines in ledger order on audio.
    sdk_startup_synth();
    require(calls == "effect role first second sampled drawn ");
    calls.clear(); gui = true;
    sdk_startup_synth();
    require(calls == "effect role ");
    // GUI role: each own page guards only its own engine; stock pages need none.
    calls.clear();
    sdk_startup_screen();
    require(calls == "effect-page first-page first second-page second sampled drawn-page drawn player-page ");
    // A sample engine plays without its own screen: the screen never holds it back.
    calls.clear(); screens = false; drawn_screen = 0;
    sdk_startup_screen();
    require(calls == "effect-page first-page second-page second sampled drawn-page drawn player-page ");
    drawn_screen = 1;

    const u32 first = value(first_engine_id), second = value(second_engine_id);
    // Own names need their expected ID and current readiness.
    require(catalog("second", second) == 7 && inserted == second && calls == "second? ");
    require(catalog("second", first) == 0 && inserted == 0 && calls.empty());
    ready_state = false;
    require(catalog("first", first) == 0 && inserted == 0);
    ready_state = true;
    require(catalog("first", first) == 7 && inserted == first);
    // Every other name reaches stock unchanged, even with an own ID; the
    // missing-engine ID 33 never does.
    require(catalog("stock", second) == 7 && inserted == second && calls.empty());
    require(catalog("stock", 33) == 0 && inserted == 0);
    require(catalog("first", 33) == 0 && inserted == 0);
    require(kit_absent() == 0);

    // Only a ready machine's own page is owned; readiness must be exactly 1.
    calls.clear();
    require(sdk_screen_owned(value(second_page_id)) == 1 && calls == "second? ");
    require(sdk_screen_owned(value(first_page_id)) == 1);
    require(sdk_screen_owned(value(effect_fx_page_id)) == 1);
    require(sdk_screen_owned(0) == 0 && sdk_screen_owned(value(first_engine_id)) == 0);
    for (int state : {0, 2, -1}) {
        ready_state = state;
        require(sdk_screen_owned(value(first_page_id)) == 0);
        require(sdk_screen_owned(value(effect_fx_page_id)) == 0);
    }
    ready_state = 1;

    // The sampler's page (2) of a track using drawn shows drawn's screen while it is ready.
    const u32 drawn = value(drawn_engine_id), screen = value(drawn_screen_page_id);
    engine = drawn;
    for (u32 t : {0u, 7u}) { track = t; require(sdk_sample_screen(2) == screen); }
    require(sdk_sample_screen(7) == 0 && sdk_sample_screen(0) == 0 && sdk_sample_screen(screen) == 0);
    drawn_screen = 0;
    require(sdk_sample_screen(2) == 0);
    drawn_screen = 1;
    // A ready screen isn't enough: the engine's record must be ours and ready too.
    for (int state : {0, 2, -1}) {
        ready_state = state;
        require(sdk_sample_screen(2) == 0);
    }
    ready_state = 1;
    require(sdk_sample_screen(2) == screen);
    engine = value(sampled_engine_id);
    require(sdk_sample_screen(2) == 0);
    track = 8;   // no track selected
    require(sdk_sample_screen(2) == 0);
    std::puts("startup ok");
    return 0;
}
