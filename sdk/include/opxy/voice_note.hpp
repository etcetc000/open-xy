#pragma once
#include <opxy/types.hpp>

// The synth note hook's one dispatch (SDK 1.11). An engine may define, in the
// namespace that declares Voice and Shared:
//     void note(Voice& voice, Shared& shared, opxy::u32 velocity);
// called once per note onset, after initialize, before start, with the note's
// raw 1..127 velocity. Argument-dependent lookup finds it, and an exact
// non-template match wins over the fallback; a machine without one is never
// called and behaves as before. The firmware adapter, the browser preview,
// the CPU estimate and the sound-test templates all dispatch through call().
// The header keeps to the freestanding subset: the browser preview compiles
// with no C++ library headers at all.
namespace sdk_voice_hook {
template <class A, class B> struct same { static const bool value = false; };
template <class A> struct same<A, A> { static const bool value = true; };
// A declaration only: never called, used unevaluated.
template <class T> T& declval();
// Sees no `note` of its own: an engine's comes only through ADL. The comma
// expression yields true only where the engine's own `note` accepts the
// contract arguments.
struct yes { static const bool value = true; };
struct no { static const bool value = false; };
template <class V, class S>
auto viable(int) -> decltype(void(note(declval<V&>(), declval<S&>(), opxy::u32{})), yes());
template <class V, class S> no viable(long);
template <class V, class S> constexpr bool declared() {
    return decltype(viable<V, S>(0))::value;
}
}

namespace sdk_voice_note {
// The fallback, returned so the guard below can tell it from an engine's void
// hook. A machine without `note` gets this no-op.
struct Fallback {};
template <class V, class S> Fallback note(V&, S&, opxy::u32) { return Fallback(); }
// An engine's hook must carry exactly the contract signature: a near miss
// (`const Shared&`, an `int` or `u8` velocity, `Voice` by value) would lose to
// the fallback and silently never run, so the guard rejects it at compile time.
namespace probe {
template <class V, class S>
auto chosen() -> decltype(note(sdk_voice_hook::declval<V&>(), sdk_voice_hook::declval<S&>(), opxy::u32{}));
}
template <class V, class S> void call(V& voice, S& shared, opxy::u32 velocity) {
    static_assert(!sdk_voice_hook::declared<V, S>() ||
                      sdk_voice_hook::same<decltype(probe::chosen<V, S>()), void>::value,
                  "note must be void note(Voice&, Shared&, opxy::u32), in the namespace that defines Voice");
    note(voice, shared, velocity);
}
}
