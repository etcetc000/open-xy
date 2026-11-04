// File banks: large immutable u32 tables that a synth reads but does not keep
// in the 2 MiB module. A native build exports each declared array as a
// content-addressed file (content/banks/shared/<sha256>-bank.hb) and the SDK
// loads it once at startup, before the engine registers, into the stock
// application heap of the audio role. Identical tables in different machines
// share one file and one allocation. Published data lives for the session.
//
// A machine declares each table with OPXY_FILE_BANK_WORDS and reads it only
// through a view declared with OPXY_FILE_BANK_VIEW, in the sound engine. Host
// builds (tests, previews) read the authored array through the same view.
// If a bank cannot be loaded, its view stays inactive and the SDK's voice
// adapter renders the machine silent; contributor code never sees a null view.
#pragma once
#include <opxy/types.hpp>

namespace opxy {
struct FileBank {
    const u32* active;
    u32 word_count;
    u32 operator[](u32 index) const { return active[index]; }
};
}

#if defined(__bfin__)
#define OPXY_FILE_BANK_WORDS(NAME, WORDS) \
    extern const opxy::u32 NAME[WORDS] __attribute__((section(".opxy.filebank." #NAME), aligned(32)))
#define OPXY_FILE_BANK_VIEW(NAME, ARRAY, WORDS) opxy::FileBank NAME = {nullptr, WORDS}
#else
#define OPXY_FILE_BANK_WORDS(NAME, WORDS) const opxy::u32 NAME[WORDS]
#define OPXY_FILE_BANK_VIEW(NAME, ARRAY, WORDS) opxy::FileBank NAME = {ARRAY, WORDS}
#endif
