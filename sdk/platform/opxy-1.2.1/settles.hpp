// The settled detector the voice adapters share (voice.cpp.in in builds with idle, and
// any adapter that has to refuse it). An engine may define `bool settled(const Voice&,
// const Shared&)` in its namespace: true once nothing can make its voice louder again
// (no excitation, burst or event still to come, controls latched), so it may go once
// its output has stayed below the project's idle threshold, relative to the note's own
// peak, for idle_cut::kHold blocks (idle_cut.hpp). Argument-dependent lookup finds it;
// an engine without one is never asked.
#pragma once
namespace sdk_settles {
struct Absent {};
template <class V, class S> Absent settled(const V&, const S&) { return Absent(); }
template <class T> T& make();
template <class A, class B> struct same { static const bool value = false; };
template <class A> struct same<A, A> { static const bool value = true; };
template <class V, class S> struct has {
    static const bool value = !same<decltype(settled(make<const V>(), make<const S>())), Absent>::value;
};
inline bool value(Absent) { return false; }
template <class R> bool value(R v) { return static_cast<bool>(v); }
template <class V, class S> bool query(const V& voice, const S& state) { return value(settled(voice, state)); }
}
