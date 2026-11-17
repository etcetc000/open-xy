// Physical-pad ownership only. MIDI reception never enters this adapter.
#pragma once
#include <opxy/types.hpp>
namespace opxy { namespace morphpad {
struct Input {
 bool requested,owned; i32 pressure;
 void request(bool next,i32 current) {
  requested=next;pressure=current;
  if(!current)owned=requested;
 }
 // Decide using ownership BEFORE release: finish a stock gesture in stock,
 // and consume the release of a Morph gesture without touching MIDI pitch.
 bool event(i32 value) {
  const bool consume=owned;pressure=value;
  if(!value)owned=requested;
  return consume;
 }
};
}}
extern "C" void sdk_morph_pad_event(opxy::u32,opxy::u32,opxy::i32,opxy::u32)
 __asm__("sdk_morph_pad_event");
extern "C" void sdk_morph_pad_request(bool);
extern "C" bool sdk_morph_pad_read(opxy::i32*);

extern "C" bool sdk_morph_pad_waiting();
