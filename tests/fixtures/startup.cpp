// Host contract checks for generated startup glue: two own-page synths
// (first, second), one stock-page sample engine (sampled) and one effect.
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
const char first_page_id[1] = {}, second_page_id[1] = {};
int first_screen_register() { calls += "first-page "; return screens; }
int second_screen_register() { calls += "second-page "; return 1; }
int effect_fx_register() { calls += "effect "; return 1; }
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
    require(installed("gone") == 0 && installed("effect") == 0);
    // Effects register on both roles, before the role check; engines in ledger order on audio.
    sdk_startup_synth();
    require(calls == "effect role first second sampled ");
    calls.clear(); gui = true;
    sdk_startup_synth();
    require(calls == "effect role ");
    // GUI role: each own page guards only its own engine; stock pages need none.
    calls.clear();
    sdk_startup_screen();
    require(calls == "first-page first second-page second sampled ");
    calls.clear(); screens = false;
    sdk_startup_screen();
    require(calls == "first-page second-page second sampled ");

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
    require(sdk_screen_owned(0) == 0 && sdk_screen_owned(value(first_engine_id)) == 0);
    for (int state : {0, 2, -1}) {
        ready_state = state;
        require(sdk_screen_owned(value(first_page_id)) == 0);
    }
    ready_state = 1;
    std::puts("startup ok");
    return 0;
}
