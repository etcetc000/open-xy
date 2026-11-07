#include <live_input_menu.cpp>
#include <live_input_stock.cpp>
#include <cstring>

#define CHECK(value) do { if (!(value)) return __LINE__; } while (false)
namespace {
using namespace opxy;
using namespace opxy::platform;
u8 selected_track_byte, manual_flags[9], resolved_flags[9];
u32 page, shift, project_state[2] = {1, 0};
u32 manual_scale[3] = {0, 0, 0}, resolved_scale[3] = {2, 0, 1};
u32 gui_word, audio_word, writes, reset_count, process_count, audio_values[4];
i32 input_block[128];
bool bad;
u32 held() { return shift; }

void publish(u32 parameters, u32 track, u32 index, i32 value) {
    bad |= parameters != 0x86bd027c || track != 12 || index != 57;
    gui_word = static_cast<u32>(value);
    ++writes;
}
}

opxy::platform::Address opxy::platform::host_stock_address(Address address) {
    switch (address) {
    case kSelectedTrack: return reinterpret_cast<Address>(&selected_track_byte);
    case 0x0801eae4: return reinterpret_cast<Address>(&page);
    case 0x8d51f184: return reinterpret_cast<Address>(&held);
    case 0x840a0524: return reinterpret_cast<Address>(&project_state[0]);
    case 0x840a0528: return reinterpret_cast<Address>(&project_state[1]);
    case 0x84eb5584: return reinterpret_cast<Address>(&manual_flags[0]);
    case 0x84eb558c: return reinterpret_cast<Address>(&manual_flags[8]);
    case 0x84eb55f8: return reinterpret_cast<Address>(&manual_scale[0]);
    case 0x84eb5600: return reinterpret_cast<Address>(&manual_scale[2]);
    case 0x860bf520: return reinterpret_cast<Address>(&resolved_flags[0]);
    case 0x860bf528: return reinterpret_cast<Address>(&resolved_flags[8]);
    case 0x860bf594: return reinterpret_cast<Address>(&resolved_scale[0]);
    case 0x860bf59c: return reinterpret_cast<Address>(&resolved_scale[2]);
    case 0x86bd027c + 0x4080 + (12 * 58 + 57) * 4: return reinterpret_cast<Address>(&gui_word);
    case 0x84eab39c + (12 * 58 + 57) * 4: return reinterpret_cast<Address>(&audio_word);
    case 0x8d5acea8: return reinterpret_cast<Address>(&publish);
    case 0x11903600: return reinterpret_cast<Address>(input_block);
    default: bad = true; return 0;
    }
}

const opxy::live_input::Controls* sdk_live_input_controls() {
    static const char* modes[] = {"off", "auto", "chromatic"};
    static const char* speeds[] = {"smooth", "hard"};
    static const opxy::live_input::Controls controls = {{modes, 3}, {speeds, 2}};
    return &controls;
}
void sdk_live_input_reset() { ++reset_count; }
void sdk_live_input_process(opxy::i32* buffer) {
    bad |= buffer != input_block;
    ++process_count;
}
void sdk_live_input_prepare(u32 mode, u32 speed, u32 amount, u32 brain) {
    audio_values[0] = mode;
    audio_values[1] = speed;
    audio_values[2] = amount;
    audio_values[3] = brain;
}

int main() {
    const i32 up = 1, down = -1;
    sdk_live_input_publish();
    CHECK(!writes && !sdk_live_input_turn(1, &up));
    selected_track_byte = 12;
    CHECK(!sdk_live_input_turn(1, &up));
    shift = 1;
    page = 1;
    CHECK(!sdk_live_input_turn(1, &up));
    page = 0;
    CHECK(!sdk_live_input_turn(0, &up));
    CHECK(sdk_live_input_turn(5, &up) && brain(gui_word) == 0xab5u);
    CHECK(sdk_live_input_turn(2, &up) && sdk_live_input_turn(7, &down));
    CHECK(!std::strcmp(sdk_live_input_caption(1), "auto") &&
          !std::strcmp(sdk_live_input_caption(2), "hard") &&
          !std::strcmp(sdk_live_input_caption(3), "93%"));
    manual_flags[0] = 1;
    sdk_live_input_publish();
    CHECK(brain(gui_word) == 0xcd6u);
    gui_word = input_monitor::mark | 3u | sdk_live_input_option_bits();
    const u32 write_count = writes;
    sdk_live_input_publish();
    CHECK(writes == write_count && (gui_word & ~tuner_mask) == (input_monitor::mark | 3u));
    audio_word = gui_word;

    sdk_live_input_rendered(0);
    CHECK(reset_count == 1 && process_count == 1 && audio_values[0] == 1 &&
          audio_values[1] == 1 && audio_values[2] == 14 && audio_values[3] == 0xcd6u);
    sdk_live_input_finish();
    audio_word = gui_word = 0;
    sdk_live_input_rendered(0);
    CHECK(reset_count == 1 && process_count == 2 && audio_values[3] == 0xcd6u);
    sdk_live_input_finish();
    sdk_live_input_finish();
    sdk_live_input_rendered(0);
    CHECK(reset_count == 2 && process_count == 3);
    sdk_live_input_finish();
    sdk_live_input_rendered(0x20000000u); // Headset mic.
    CHECK(reset_count == 3 && process_count == 4 && audio_values[1] == 1);
    sdk_live_input_finish();
    sdk_live_input_rendered(0x40000000u); // Line input.
    CHECK(reset_count == 4 && process_count == 5 && audio_values[1] == 1);
    sdk_live_input_finish();
    sdk_live_input_rendered(0x60000000u); // USB audio.
    CHECK(reset_count == 4 && process_count == 5);
    sdk_live_input_finish();
    sdk_live_input_publish();
    CHECK(target(gui_word) == 1 && brain(gui_word) == 0xcd6u && writes == write_count + 1);
    CHECK(sdk_live_input_turn(1, &down) && target(gui_word) == 0 && brain(gui_word) == 0 && !bad);
}
