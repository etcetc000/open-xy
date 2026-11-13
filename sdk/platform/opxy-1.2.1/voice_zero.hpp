#pragma once
// Zeroes a voice for a new owner. The voice adapters run this once per note
// that takes a voice, in the note-start path, so its cost lands in one audio
// block for every voice of a chord. It clears whole 32-bit words whenever the
// type is word aligned and sized (the byte loop it replaces stored each byte
// separately), and only the leading `Voice::zeroed_bytes` when the machine
// declares them: storage past that prefix keeps its previous contents, and
// the machine's initialize/start/render must never read it before writing it
// (for example, a delay line cleared progressively from note start).
#include <opxy/types.hpp>

namespace opxy { namespace platform {
template<class T> constexpr auto voice_zero_extent(int) -> decltype(unsigned(T::zeroed_bytes)) { return T::zeroed_bytes; }
template<class T> constexpr unsigned voice_zero_extent(long) { return sizeof(T); }
template<class T> inline void zero_voice(T& voice) {
    constexpr unsigned extent = voice_zero_extent<T>(0);
    static_assert(extent <= sizeof(T), "zeroed_bytes exceeds the voice");
    // A voice need not be assignable, so it is cleared through its bytes.
    if (alignof(T) >= 4 && extent % 4 == 0) {
        typedef u32 __attribute__((may_alias)) Word;
        Word* words = reinterpret_cast<Word*>(&voice);
        for (unsigned i = 0; i < extent / 4; ++i) words[i] = 0;
    } else {
        unsigned char* bytes = reinterpret_cast<unsigned char*>(&voice);
        for (unsigned i = 0; i < extent; ++i) bytes[i] = 0;
    }
}
} }
