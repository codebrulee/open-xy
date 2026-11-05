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

// The clients the build links, ending in null (generated: tooling/overlay_clients.py).
extern "C" const opxy::platform::overlay::Client* const sdk_overlay_clients[] __asm__("sdk_overlay_clients");

// A client from a service the overlay doesn't link against (the extensions'
// indicator), added once at run time from any thread. The overlay binds it on
// its own next frame. False when it is full.
extern "C" bool sdk_overlay_add(const opxy::platform::overlay::Client* client) __asm__("sdk_overlay_add");
