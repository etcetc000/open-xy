// A machine's record: everything the SDK's shared code needs from one machine,
// defined by that machine's own generated object (record.cpp, tooling/records.py)
// as <prefix>_record. A build lists its selected machines' records, by kind then
// ID, in sdk_machine_records: machine_records.o, the one object made for the
// selection (sdk.py assembles it; the browser builder writes it). Startup
// registers the machines from their records (startup.cpp.in) and builds the
// shared tables from them (machine_tables.cpp), so no machine's objects and no
// other shared object depend on which machines a build holds.
#pragma once
#include <opxy/types.hpp>

namespace opxy {
namespace benchmark { struct Run; struct Setting; }
namespace platform {
struct SampleMachine;
struct SynthControls;
struct LfoMachine;
struct CompanionMachine;
struct ControlMachine;
struct PlayerEntry;
struct DrumMachine;
struct DrumModelEdit;
struct DrumKit;
struct DrumSendKit;
namespace track_stage { struct Stage; }

// The kinds, in the order sdk_machine_records lists them (tooling/records.py KINDS).
namespace machine_kind {
const u32 kSynth = 0, kDrum = 1, kSample = 2, kEffect = 3, kPlayer = 4;
}

struct MachineRecord {
    u32 kind;
    // Startup and the menu (startup.cpp.in). An engine's _register or an effect's
    // _fx_register; its own screen's _screen_register (a synth's or kit's page, an
    // effect's own page, a sample engine's own screen, a player's page); its
    // readiness (_ready); an engine's name check; a sample engine's own screen's
    // readiness. Null where the machine has none.
    int (*register_machine)();
    int (*register_screen)();
    int (*ready)();
    int (*name_matches)(const void* name);
    int (*screen_ready)();
    // Link-time numbers, as addresses: the engine or effect number; the page the
    // machine owns, null for one on a stock page (a sample engine, an effect
    // without its own page, a player); a sample engine's own screen page, or null.
    const char* number;
    const char* page;
    const char* screen_page;
    // Its row in each shared table (machine_tables.cpp), or null.
    const SampleMachine* sample;
    const SynthControls* controls;
    const LfoMachine* lfo;
    const CompanionMachine* companion;
    const ControlMachine* control;
    const PlayerEntry* player;
    const DrumMachine* drum;
    const DrumModelEdit* model_edit;
    const DrumKit* kit;
    const DrumSendKit* send;
    const track_stage::Stage* stage;
    // Its diagnostic runs (benchmark.cpp, tooling/benchmark.py): a row per run; in a
    // sweep build, a synth's swept settings and its controls' defaults beside them.
    const benchmark::Run* runs;
    u32 run_count;
    const benchmark::Setting* settings;
    u32 setting_count;
    const i32* defaults;
};

// The bytes of table rows the shared tables may copy for one record, at most: a
// synth's or a kit's (machine_tables.cpp checks it). machine_records.o reserves
// this much per selected machine, and once more for the companion table's end
// (tooling/records.py and builder/core.mjs RECORD_TABLE_BYTES, 192 on the device). Rows
// hold words and pointers, so a host test's are larger.
const u32 kRecordTableBytes = 48 * sizeof(void*);
}}

extern "C" {
// machine_records.o: the selected machines' records and the space their tables take.
extern const opxy::platform::MachineRecord* const sdk_machine_records[] __asm__("sdk_machine_records");
extern const opxy::u32 sdk_machine_record_count __asm__("sdk_machine_record_count");
extern opxy::u8 sdk_machine_record_space[] __asm__("sdk_machine_record_space");
extern const opxy::u32 sdk_machine_record_space_bytes __asm__("sdk_machine_record_space_bytes");
// Builds the shared tables from the records (machine_tables.cpp). Startup calls it on
// both roles before registering anything; it writes the same bytes every time.
void sdk_machine_tables() __asm__("sdk_machine_tables");
}
