// The shared tables, built from the selected machines' records (machine_record.hpp)
// into the space machine_records.o reserves for them. Each table holds a copy of
// every record's row for it, in record order (by kind, then ID); the companion
// table is sorted by ID and ends with an empty row, as companions list machines.
// Startup builds them on each role before it registers a machine, and a project load
// or save and stock's player lookups before they read them, which may come first;
// the other readers each check their machine is registered and ready, so
// they find a row only after startup. The first build sets them; a build that
// overlaps it writes the same bytes at the same places, and each table's address
// is published before its count: a reader that sees the count sees the rows. Every
// machine object and every other shared object is the same whichever machines a
// build holds.
#include "machine_record.hpp"
#include "sample_machines.hpp"
#include "synth_controls.hpp"
#include "lfo.hpp"
#include "project_companion.hpp"
#include "track_control.hpp"
#include "players.hpp"
#include "drum_machines.hpp"
#include "drum_model_edit.hpp"
#include "drum_kit_controls.hpp"
#include "drum_send.hpp"
#include "track_stage.hpp"
#include "barrier.hpp"

static_assert(sizeof(void*) != 4 || opxy::platform::kRecordTableBytes == 192,
              "tooling/records.py RECORD_TABLE_BYTES differs");

using opxy::u8;
using opxy::u32;
using namespace opxy::platform;

// What a synth's or a kit's record can hold, the most of any kind, fits the space
// each record reserves (a sample engine's or an effect's rows are fewer).
static_assert(sizeof(SynthControls) + sizeof(LfoMachine) + sizeof(CompanionMachine) + sizeof(ControlMachine) +
              sizeof(track_stage::Stage) <= kRecordTableBytes, "A synth's rows exceed kRecordTableBytes");
static_assert(sizeof(SynthControls) + sizeof(LfoMachine) + sizeof(CompanionMachine) + sizeof(DrumMachine) +
              sizeof(DrumModelEdit) + sizeof(DrumKit) + sizeof(DrumSendKit) <= kRecordTableBytes,
              "A kit's rows exceed kRecordTableBytes");
static_assert(sizeof(SampleMachine) + sizeof(SynthControls) + sizeof(LfoMachine) + sizeof(CompanionMachine)
              <= kRecordTableBytes, "A sample engine's rows exceed kRecordTableBytes");

const SampleMachine* sdk_sample_machines = nullptr;
u32 sdk_sample_machine_count = 0;
const SynthControls* sdk_synth_controls = nullptr;
u32 sdk_synth_control_count = 0;
const LfoMachine* sdk_lfo_machines = nullptr;
u32 sdk_lfo_machine_count = 0;
const ControlMachine* sdk_control_machines = nullptr;
u32 sdk_control_machine_count = 0;
const PlayerEntry* sdk_players = nullptr;
u32 sdk_player_count = 0;
const DrumMachine* sdk_drum_machines = nullptr;
u32 sdk_drum_machine_count = 0;
const DrumModelEdit* sdk_drum_model_edits = nullptr;
u32 sdk_drum_model_edit_count = 0;
const DrumKit* sdk_drum_kits = nullptr;
u32 sdk_drum_kit_count = 0;
const DrumSendKit* sdk_drum_send_kits = nullptr;
u32 sdk_drum_send_kit_count = 0;
const track_stage::Stage* sdk_track_stages = nullptr;
u32 sdk_track_stage_count = 0;
namespace {
// The companion table before its first build: no machines.
const CompanionMachine kNoCompanions[1] = {};
}
const CompanionMachine* sdk_companion_machines = kNoCompanions;

namespace {
struct Space {
    u8* at;
    u8* end;
    // `bytes` more of the reserved space, or null when it has run out (never, with
    // the rows records hold: the table is then left as it was).
    u8* take(u32 bytes) {
        if (bytes > static_cast<u32>(end - at)) return nullptr;
        u8* taken = at;
        at += bytes;
        return taken;
    }
};

void copy(u8* to, const void* from, u32 bytes) {
    const u8* source = static_cast<const u8*>(from);
    for (u32 i = 0; i < bytes; ++i) to[i] = source[i];
}

template <class T>
u32 rows(const T* const MachineRecord::*row) {
    u32 count = 0;
    for (u32 i = 0; i < sdk_machine_record_count; ++i)
        if (sdk_machine_records[i]->*row) ++count;
    return count;
}

template <class T>
void build(Space& space, const T* const MachineRecord::*row, const T*& table, u32& count) {
    const u32 total = rows(row);
    u8* to = space.take(total * sizeof(T));
    if (!to) return;
    u32 at = 0;
    for (u32 i = 0; i < sdk_machine_record_count; ++i)
        if (const T* entry = sdk_machine_records[i]->*row) copy(to + sizeof(T) * at++, entry, sizeof(T));
    barrier();
    table = reinterpret_cast<const T*>(to);
    barrier();
    count = total;
}

// Machine IDs in byte order, as tooling/adapters.py companion_table sorts them.
bool before(const char* a, const char* b) {
    while (*a && *a == *b) ++a, ++b;
    return static_cast<u8>(*a) < static_cast<u8>(*b);
}

// Each row goes straight to its place in ID order, so a later build never moves one.
void build_companions(Space& space) {
    const u32 total = rows(&MachineRecord::companion);
    u8* to = space.take((total + 1) * sizeof(CompanionMachine));
    if (!to) return;
    for (u32 i = 0; i < sdk_machine_record_count; ++i) {
        const CompanionMachine* entry = sdk_machine_records[i]->companion;
        if (!entry) continue;
        u32 place = 0;
        for (u32 j = 0; j < sdk_machine_record_count; ++j) {
            const CompanionMachine* other = sdk_machine_records[j]->companion;
            if (other && before(other->id, entry->id)) ++place;
        }
        copy(to + sizeof(CompanionMachine) * place, entry, sizeof(CompanionMachine));
    }
    copy(to + sizeof(CompanionMachine) * total, &kNoCompanions[0], sizeof(CompanionMachine));
    barrier();
    sdk_companion_machines = reinterpret_cast<const CompanionMachine*>(to);
}
}

// Set once the tables are built: later calls (the other role's startup, or a project
// load or save, which may come first) return at once.
static volatile u32 built = 0;

void sdk_machine_tables() {
    if (built) return;
    Space space{sdk_machine_record_space, sdk_machine_record_space + sdk_machine_record_space_bytes};
    build(space, &MachineRecord::sample, sdk_sample_machines, sdk_sample_machine_count);
    build(space, &MachineRecord::controls, sdk_synth_controls, sdk_synth_control_count);
    build(space, &MachineRecord::lfo, sdk_lfo_machines, sdk_lfo_machine_count);
    build(space, &MachineRecord::control, sdk_control_machines, sdk_control_machine_count);
    build(space, &MachineRecord::player, sdk_players, sdk_player_count);
    build(space, &MachineRecord::drum, sdk_drum_machines, sdk_drum_machine_count);
    build(space, &MachineRecord::model_edit, sdk_drum_model_edits, sdk_drum_model_edit_count);
    build(space, &MachineRecord::kit, sdk_drum_kits, sdk_drum_kit_count);
    build(space, &MachineRecord::send, sdk_drum_send_kits, sdk_drum_send_kit_count);
    build(space, &MachineRecord::stage, sdk_track_stages, sdk_track_stage_count);
    build_companions(space);
    barrier();
    built = 1;
}
