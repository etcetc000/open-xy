#include <cstdlib>
#include "drum_machines.hpp"

namespace {
int record_state = 1, screen_state = 0;
unsigned screen_reads = 0;
void require(bool ok) { if (!ok) std::abort(); }
}

extern "C" {
extern const char probe_engine_id[] __asm__("probe_engine_id");
int probe_record_ready() __asm__("probe_record_ready");
int probe_ready() __asm__("probe_ready");
void probe_drum_defaults(opxy::u32, opxy::u8*, opxy::u8*) __asm__("probe_drum_defaults");
const char* probe_drum_label(opxy::u32, opxy::u32) __asm__("probe_drum_label");
const char* probe_drum_model_name(opxy::u32) __asm__("probe_drum_model_name");
opxy::u32 probe_drum_count(opxy::u32) __asm__("probe_drum_count");
extern const char probe_engine_id[] = "probe";
int probe_record_ready() { return record_state; }
int probe_ready() { ++screen_reads; return record_state == 1 && screen_state; }
void probe_drum_defaults(opxy::u32, opxy::u8*, opxy::u8*) {}
const char* probe_drum_label(opxy::u32, opxy::u32) { return ""; }
const char* probe_drum_model_name(opxy::u32) { return ""; }
opxy::u32 probe_drum_count(opxy::u32) { return 8; }
}

int main() {
    const opxy::u32 engine = sdk_drum_machines[0].engine();
    require(engine != 4);
    // Audio has the registered engine, but never constructs the GUI screen.
    require(sdk_drum_alias(engine) == 4 && sdk_drum_machine(engine));
    require(screen_reads == 0);
    screen_state = 1;
    const int rejected[] = {0, -2, 2};
    for (int state : rejected) {
        record_state = state;
        require(sdk_drum_alias(engine) == engine && !sdk_drum_machine(engine));
    }
    record_state = 1;
    require(sdk_drum_alias(engine) == 4 && screen_reads == 0);
    return 0;
}
