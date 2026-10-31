#pragma once
#include <opxy/types.hpp>
namespace opxy { namespace control_interaction {
// A snapshot of the last physical interaction for this track. Serial zero is
// an available idle baseline. Reads do not consume events or reset serials.
struct Event {
    u32 available,serial,track,physical_knob,parameter_index;
    i32 normalized_value_at_event;
};
namespace detail {
struct Record { u32 engine; Event event; };
struct Bank {
    u32 serial,available;
    Record tracks[8];
    bool publish(u32 engine,u32 track,u32 physical,u32 parameter,i32 value) {
        if(!engine || track>=8 || physical>=4 || parameter>=8 || value<0)return false;
        ++serial;if(!serial)++serial;
        tracks[track]=Record{engine,Event{1,serial,track,physical,parameter,value}};
        return true;
    }
    Event read(u32 engine,u32 track) const {
        if(!available || !engine || track>=8)return Event{};
        if(tracks[track].engine!=engine)return Event{1,0,track,0,0,0};
        return tracks[track].event;
    }
};
Event native_latest(u32 track);
#if !defined(__bfin__)
inline Bank& preview_bank(){static Bank bank={};return bank;}
#endif
}
inline Event latest(u32 track) {
#if defined(__bfin__)
    return detail::native_latest(track);
#else
    return detail::preview_bank().read(1,track);
#endif
}
// Explicit host fixtures/gesture bridge. Native authors cannot inject events.
inline bool configure_preview_source(bool available) {
#if defined(__bfin__)
    (void)available;return false;
#else
    auto& bank=detail::preview_bank();
    if(!available)bank=detail::Bank{};
    bank.available=available?1:0;return true;
#endif
}
inline bool submit_preview(u32 track,u32 physical,u32 parameter,i32 value) {
#if defined(__bfin__)
    (void)track;(void)physical;(void)parameter;(void)value;return false;
#else
    auto& bank=detail::preview_bank();
    return bank.available && bank.publish(1,track,physical,parameter,value);
#endif
}
static_assert(sizeof(Event)==24,"Control interaction event layout");
}}
