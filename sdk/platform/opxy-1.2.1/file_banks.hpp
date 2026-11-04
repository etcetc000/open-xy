// File banks on OS 1.2.1 (opxy/file_bank.hpp). The generated table
// (tooling/file_banks.py) lists the build's unique bank files and every view
// that reads one; file_banks.cpp loads them once per role before the first
// engine registers.
#pragma once
#include "../file_bank_loader.hpp"

namespace opxy { namespace platform {
struct FileBankUse { FileBank* view; u32 file; };
extern const file_banks::Spec native_file_banks[];
extern const u32 native_file_bank_count;
extern const FileBankUse native_file_bank_uses[];
extern const u32 native_file_bank_use_count;
extern const u32 native_file_bank_budget;  // payload plus allowance, every file, one role
extern const bool native_file_banks_gui;   // profiling builds also load the GUI role, for the benchmark
}}

// Loads every bank on first use; always returns 1. A bank that fails leaves its
// views inactive, and the voice adapter keeps those machines silent.
extern "C" int sdk_file_banks_prepare() __asm__("sdk_file_banks_prepare");
// A zeroed block of the role's application heap for a heapState machine, kept
// for the session; null when there is no room.
extern "C" void* sdk_boot_allocate(opxy::u32) __asm__("sdk_boot_allocate");
