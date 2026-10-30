// GUI-thread status consumer. The cache integration supplies a coherent
// same-role snapshot; this file does not read worker/audio shared storage.
extern "C" int sdk_sample_progress_context() __asm__("sdk_sample_progress_context");
extern "C" int sdk_sample_popup_active() __asm__("sdk_sample_popup_active");
extern "C" int sdk_sample_popup_matches(const char*, unsigned) __asm__("sdk_sample_popup_matches");
extern "C" void sdk_sample_popup_publish(const char*, unsigned) __asm__("sdk_sample_popup_publish");
extern "C" void sdk_sample_progress_update(unsigned, unsigned, unsigned)
    __asm__("sdk_sample_progress_update");

namespace {
char previous[32];
unsigned previous_length;
unsigned failed_generation;
bool have_previous;
bool failure_shown;

void publish(const char* text, unsigned length, unsigned duration) {
    sdk_sample_popup_publish(text, duration);
    for (unsigned i = 0; i <= length; ++i) previous[i] = text[i];
    previous_length = length;
    have_previous = true;
}
}

// state: 0 idle, 1 preparing, 2 ready, 3 failed, 4 cache full. Generations identify jobs,
// including retries. Only the cache GUI tick calls this function.
extern "C" void sdk_sample_progress_update(unsigned state, unsigned percent,
                                            unsigned generation) {
    if (state != 1 && state != 3 && state != 4) return;
    if (!sdk_sample_progress_context()) return;
    if (state >= 3 && failure_shown && generation == failed_generation) return;
    if (sdk_sample_popup_active() &&
        (!have_previous || !sdk_sample_popup_matches(previous, previous_length)))
        return;
    if (state >= 3) {
        if (state == 4) publish("Sample cache full", 17, 0x40400000u);
        else publish("Sample failed", 13, 0x40400000u); // Three seconds, then expires.
        failed_generation = generation;
        failure_shown = true;
        return;
    }
    char message[32];
    const char* prefix = "Preparing sample ";
    unsigned count = 0;
    while (prefix[count]) { message[count] = prefix[count]; ++count; }
    if (percent > 100) percent = 100;
    if (percent == 100) {
        message[count++] = '1';
        message[count++] = '0';
        message[count++] = '0';
    } else {
        unsigned tens = 0;
        while (percent >= 10) { percent -= 10; ++tens; }
        if (tens) message[count++] = static_cast<char>('0' + tens);
        message[count++] = static_cast<char>('0' + percent);
    }
    message[count++] = '%';
    message[count] = 0;
    // A short, nonpersistent notification expires even if the job vanishes.
    // A later system notification wins and is never cleared by this component.
    publish(message, count, 0x3f000000u); // Half a second.
}
