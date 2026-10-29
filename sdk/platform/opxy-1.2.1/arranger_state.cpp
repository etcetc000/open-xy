#include "arranger_state.hpp"
#include "barrier.hpp"

namespace {
using opxy::u32;
using opxy::platform::barrier;
opxy::arranger::Model gui_model;
opxy::arranger::Model mailbox_model;
opxy::arranger::Model audio_model;

// Single producer/consumer ownership: GUI writes only while zero, audio reads
// only while one and acknowledges AFTER copying. Neither role can lap the other.
alignas(4) volatile u32 ready;
alignas(4) volatile u32 playback_position;
bool mailbox_active, mailbox_seek;
u32 mailbox_position;
bool audio_active;
u32 gui_generation, mailbox_generation, audio_generation;
bool gui_bound, mailbox_bound, audio_bound;

// GUI-private retry state coalesces edits; the latest explicit seek survives edits.
bool pending, requested_active, requested_seek;
u32 requested_position;
}

opxy::arranger::Model& sdk_arranger_model() { return gui_model; }

void sdk_arranger_bind_project(u32 generation) { gui_generation = generation; gui_bound = true; }
bool sdk_arranger_audio_project_matches(u32 generation) {
    return audio_bound && audio_generation == generation;
}

void sdk_arranger_publish(bool active, bool seek) {
    requested_active = active;
    pending = true;
    if (seek) {
        requested_seek = true;
        requested_position = gui_model.position();
    }
    sdk_arranger_publish_poll();
}

void sdk_arranger_publish_poll() {
    if (!pending || ready) return;
    barrier();
    mailbox_model = gui_model;
    mailbox_generation = gui_generation;
    mailbox_bound = gui_bound;
    mailbox_active = requested_active;
    mailbox_seek = requested_seek;
    mailbox_position = requested_position;
    pending = requested_seek = false;
    barrier();
    ready = 1;
}

bool sdk_arranger_consume() {
    if (!ready) return false;
    barrier();
    const u32 position = audio_model.position();
    audio_model = mailbox_model;
    audio_model.seek(mailbox_seek ? mailbox_position : position);
    audio_generation = mailbox_generation;
    audio_bound = mailbox_bound;
    audio_active = mailbox_active;
    playback_position = audio_model.position();
    barrier();
    ready = 0;
    return true;
}

const opxy::arranger::Model& sdk_arranger_audio_model() { return audio_model; }
bool sdk_arranger_audio_active() { return audio_active; }
u32 sdk_arranger_playback_position() {
    barrier();
    return playback_position;
}
void sdk_arranger_audio_seek(u32 time) {
    audio_model.seek(time);
    playback_position = time;
    barrier();
}
u32 sdk_arranger_audio_advance(u32 delta) {
    const u32 position = audio_model.advance(delta);
    playback_position = position;
    barrier();
    return position;
}
