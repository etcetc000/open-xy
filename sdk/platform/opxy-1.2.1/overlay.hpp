// The SDK's overlay over every page (overlay.cpp): one screen in the last page
// slot, which no machine is given, drawing its clients' artwork from
// content/display/openxy_overlay.svg. Each client owns a group there and shows it
// only while it wants to be seen.
#pragma once
#include <opxy/types.hpp>

namespace opxy { namespace platform { namespace overlay {

struct Client {
    void (*bind)(void* page);   // the artwork has loaded: find its nodes (a screen::Base)
    bool (*wanted)();           // every frame, so keep it cheap
    void (*frame)(bool shown);  // before drawing: show its group, or hide it
};

}}}
