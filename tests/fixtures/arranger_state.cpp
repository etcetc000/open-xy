extern "C" int puts(const char*);
extern "C" void exit(int);
void host_barrier();
#include <arranger_state.cpp>

using namespace opxy;
using namespace opxy::arranger;

void require(bool condition, const char* message) {
    if (!condition) { puts(message); exit(1); }
}

void (*at_barrier)();
bool inside_barrier;
unsigned barrier_calls;
void host_barrier() {
    ++barrier_calls;
    if (at_barrier && !inside_barrier) {
        inside_barrier = true;
        at_barrier();
        inside_barrier = false;
    }
}

void producer_during_consume() {
    sdk_arranger_model().move(7, 1, 500);
    sdk_arranger_publish(false);
}

void consumer_during_publish() {
    require(!sdk_arranger_consume(), "consumer saw unpublished mailbox");
}

int main() {
    require(!sdk_arranger_audio_active() && !sdk_arranger_consume(), "zero initial state");
    require(!sdk_arranger_audio_project_matches(0), "initial model unbound");
    sdk_arranger_bind_project(42);
    Model& gui = sdk_arranger_model();
    const Clip a = {0, 3, 100, 200, 0, 96};
    require(gui.insertAt(7, a), "GUI insert");
    gui.seek(123);
    require(!sdk_arranger_audio_model().clip(7), "GUI state is private");
    at_barrier = consumer_during_publish;
    sdk_arranger_publish(true, true);
    at_barrier = nullptr;
    require(!sdk_arranger_audio_active(), "publication waits for audio boundary");
    require(sdk_arranger_consume(), "initial consume");
    require(sdk_arranger_audio_project_matches(42) && !sdk_arranger_audio_project_matches(43), "audio bound generation");
    require(sdk_arranger_audio_active() && sdk_arranger_audio_model().position() == 123, "activation and seek");
    require(sdk_arranger_audio_model().clip(7)->start == 100, "snapshot data");
    require(!sdk_arranger_consume(), "single consume per publication");
    require(gui.setMuted(7,true), "GUI instance mute");
    sdk_arranger_publish(true);
    require(sdk_arranger_consume() && sdk_arranger_audio_model().clip(7)->muted,
        "instance mute crosses acknowledged immutable publication");
    sdk_arranger_audio_advance(10);
    gui.seek(9999);
    require(gui.move(7, 1, 400), "GUI move");
    sdk_arranger_publish(true);
    require(sdk_arranger_consume() && sdk_arranger_audio_model().position() == 133, "nonseek preserves live position");

    sdk_arranger_publish(true);
    gui.seek(2000); sdk_arranger_publish(false, true);
    gui.seek(3000); sdk_arranger_publish(true, true);
    gui.seek(9000); sdk_arranger_publish(false);
    require(sdk_arranger_consume() && sdk_arranger_audio_active(), "ready mailbox cannot be overwritten");
    require(sdk_arranger_audio_model().position() == 133, "queued explicit seek not prematurely visible");
    sdk_arranger_publish_poll();
    require(sdk_arranger_consume() && !sdk_arranger_audio_active(), "latest pending active state");
    require(sdk_arranger_audio_model().position() == 3000, "latest explicit seek retained across nonseek edits");

    gui.move(7, 0, 100); sdk_arranger_publish(true);
    at_barrier = producer_during_consume;
    require(sdk_arranger_consume(), "consume with producer interleave");
    at_barrier = nullptr;
    require(sdk_arranger_audio_model().clip(7)->start == 100 && sdk_arranger_audio_active(), "producer cannot lap in-flight consumer");
    sdk_arranger_publish_poll();
    require(sdk_arranger_consume() && sdk_arranger_audio_model().clip(7)->start == 500 &&
        !sdk_arranger_audio_active(), "retry after acknowledgement");
    require(sdk_arranger_audio_model().position() == 3000, "retry edit preserves time");

    require(gui.setLoop(true, 100, 300), "GUI loop");
    gui.seek(280); sdk_arranger_publish(true, true); require(sdk_arranger_consume(), "loop snapshot");
    require(sdk_arranger_audio_advance(45) == 125, "audio owns loop advance");
    require(gui.position() == 280, "audio never writes GUI model");
    require(sdk_arranger_playback_position() == 125, "GUI playback telemetry");
    sdk_arranger_audio_seek(175);
    require(sdk_arranger_audio_model().position() == 175, "audio seek");
    sdk_arranger_publish(false); require(sdk_arranger_consume(), "disable");
    require(!sdk_arranger_audio_active() && sdk_arranger_audio_model().position() == 175, "disable preserves position");
    require(barrier_calls >= 20, "publication barriers exercised");
    return 0;
}
