// Host boundary double for the real hidden-pages check in synth_pages.cpp:
// stock's page words and setters (instrument and aux), the selected track,
// track engines and the machines holding aux tracks.
#include <cstdio>
#include <cstdlib>
#include <initializer_list>
#include <synth_pages.cpp>

namespace {
using opxy::u32;
using opxy::platform::Address;

void require(bool condition, const char* message) {
    if (!condition) { std::fprintf(stderr, "%s\n", message); std::exit(1); }
}
u32 page, sets, selected, engines[16];
void stock_set_page(u32 value) { page = value; ++sets; }
// The aux controller's tab word and its setter, and the machine holding each aux track.
u32 aux_tab, aux_sets, aux_engines[16];
void stock_set_aux_tab(u32 controller, u32 value) {
    require(controller == 0x08014db4, "Aux tab set on another controller");
    aux_tab = value;
    ++aux_sets;
}
u32 stock_selected() { return selected; }
u32 stock_engine(u32 track) { return track < 16 ? engines[track] : 0; }
int ready() { return 1; }
}

extern "C" {
// Engine 40 hides the envelope and filter pages; engine 41 hides nothing; 42
// hides the filter page only.
const opxy::platform::SynthControls sdk_synth_controls[] = {
    {reinterpret_cast<const char*>(40), ready, {0, 1, 2, 3, 4, 5, 6, 7}, 0, {0, 0, 0, 0, 0, 0, 0, 0}, nullptr, false, false, 6},
    {reinterpret_cast<const char*>(41), ready, {0, 1, 2, 3, 4, 5, 6, 7}, 0, {0, 0, 0, 0, 0, 0, 0, 0}, nullptr, false, false, 0},
    {reinterpret_cast<const char*>(42), ready, {0, 1, 2, 3, 4, 5, 6, 7}, 0, {0, 0, 0, 0, 0, 0, 0, 0}, nullptr, false, false, 4},
    {nullptr, nullptr, {0, 1, 2, 3, 4, 5, 6, 7}, 0, {0, 0, 0, 0, 0, 0, 0, 0}, nullptr, false, false, 0}};
const u32 sdk_synth_control_count = 3;
}

u32 opxy::platform::control_engine(u32 track) {
    return track < 8 ? engines[track] : track < 16 ? aux_engines[track] : 0;
}

Address opxy::platform::host_stock_address(Address address) {
    if (address == 0x080238bc) return reinterpret_cast<Address>(&page);
    if (address == 0x0801eae4) return reinterpret_cast<Address>(&aux_tab);
    if (address == 0x8c08f74c) return reinterpret_cast<Address>(&stock_set_aux_tab);
    if (address == 0x8c0b19c4) return reinterpret_cast<Address>(&stock_set_page);
    if (address == 0x8d4d6f14) return reinterpret_cast<Address>(&stock_selected);
    if (address == 0x8d58781c) return reinterpret_cast<Address>(&stock_engine);
    std::abort();
}

int main() {
    engines[2] = 40;
    engines[3] = 41;
    // A stock engine and a synth hiding nothing keep every page.
    for (u32 track : {0u, 3u})
        for (u32 p = 0; p < 4; ++p) {
            selected = track;
            page = p;
            sdk_synth_pages();
            require(page == p && sets == 0, "Page changed for a synth hiding nothing");
        }
    // The LFO page (M4) always stays; a synth hiding only the filter keeps the envelope.
    engines[4] = 42;
    selected = 4;
    for (u32 p : {3u, 1u}) {
        page = p;
        sdk_synth_pages();
        require(page == p && sets == 0, "LFO or envelope page hidden");
    }
    page = 2;
    sdk_synth_pages();
    require(page == 1 && sets == 1, "Filter page not hidden");
    sets = 0;
    // On the hiding synth, M4 then M2: back to M4; M3 from M1: back to M1.
    selected = 2;
    page = 3;
    sdk_synth_pages();
    page = 1;
    sdk_synth_pages();
    require(page == 3 && sets == 1, "Envelope page not replaced by the page before");
    page = 0;
    sdk_synth_pages();
    page = 2;
    sdk_synth_pages();
    require(page == 0 && sets == 2, "Filter page not replaced by the page before");
    require(sdk_synth_page_key(2) == 3 && sdk_synth_page_key(2) == 0, "M3 press not kept once");
    // Selecting the track while on another track's envelope page: the engine page.
    selected = 3;
    page = 1;
    sdk_synth_pages();
    selected = 2;
    sdk_synth_pages();
    require(page == 0 && sets == 3, "Hidden page kept after selecting the track");
    // Pressing a hidden page's key on the selected track is kept for its
    // machine, once; selecting the track while on that page is not a press.
    require(sdk_synth_page_key(2) == 0, "Track selection taken for a key press");
    sdk_synth_pages();  // the next tick, on the engine page
    page = 2;
    sdk_synth_pages();
    require(page == 0 && sdk_synth_page_key(3) == 0 && sdk_synth_page_key(2) == 3 && sdk_synth_page_key(2) == 0,
            "M3 press not kept once for its track");
    // An aux track nothing holds is left alone, whatever its stock engine word says.
    const u32 sets_before = sets;
    selected = 9;
    engines[9] = 40;
    aux_tab = 1;
    sdk_synth_pages();
    require(aux_tab == 1 && sets == sets_before && aux_sets == 0, "Auxiliary track's page changed");
    // Aux replacements do not pass through instrument-page hiding.
    selected = 11;
    aux_engines[11] = 42;
    for (u32 tab = 0; tab < 4; ++tab) {
        aux_tab = tab;
        sdk_synth_pages();
        require(aux_tab == tab && aux_sets == 0, "Aux page changed by instrument hiding");
    }
    sdk_synth_queue_page_key(11, 2);
    require(sdk_synth_page_key(0) == 0 && sdk_synth_page_key(11) == 2 && sdk_synth_page_key(11) == 0,
            "Direct aux M2 was not delivered once to its owner");

    return 0;
}
