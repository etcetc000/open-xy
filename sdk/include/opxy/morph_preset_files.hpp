#pragma once
#include <opxy/types.hpp>
// Private Morph GUI-only file bridge. Fixed paths, bounded payload, no audio I/O.
// Read returns byte count (capacity+1 flags an overlong file); zero means failure.
extern "C" opxy::u32 sdk_morph_preset_read(opxy::u32 slot,opxy::u32 copy,opxy::u8* data,opxy::u32 capacity);
extern "C" bool sdk_morph_preset_write(opxy::u32 slot,opxy::u32 copy,const opxy::u8* data,opxy::u32 bytes);
