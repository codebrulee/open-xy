// The SDK's overlay (overlay.hpp). The compositor (8d5213f4) paints a slot that is
// the current page, or whose method-table entry +8 says so, as it does stock's
// own overlays (the octave indicator). This one says so while a client wants it,
// and for one frame after, which hides what was shown. It takes no input.
#include "overlay.hpp"
#include "registration.hpp"
#include "screen.hpp"

extern "C" {
void sdk_overlay_startup() __asm__("sdk_overlay_startup");
}

namespace {
using opxy::i32;
using opxy::u32;
using namespace opxy::screen;
using opxy::platform::overlay::Client;

const char kAsset[] = "openxy_overlay.svg";
// Clients added at run time (sdk_overlay_add); `added` is published after its slot.
const u32 kAddedLimit = 2;
const Client* volatile added_clients[kAddedLimit];
volatile u32 added;
// Frames painted while the artwork loads; a build without the file then stops asking.
const u32 kLoadFrames = 120;

struct Object {
    Base base;
    u32 bound, loading, shown, added_bound;
};
Object overlay;

// Every client in turn: the linked ones, then those added and bound so far.
template <typename F> void each(Object* o, F f) {
    for (const Client* const* c = sdk_overlay_clients; *c; ++c) f(*c);
    for (u32 i = 0; i < o->added_bound; ++i) f(added_clients[i]);
}

struct Overlay {
    typedef ::Object Object;
    static void bind(Object* o) {
        if (!o->base.svg) return;
        if (!o->bound) {
            for (const Client* const* c = sdk_overlay_clients; *c; ++c) (*c)->bind(&o->base);
            o->bound = 1;
        }
        for (const u32 n = added; o->added_bound < n; ++o->added_bound) added_clients[o->added_bound]->bind(&o->base);
    }
    static u32 paint(Object* o, u32) {
        bind(o);
        if (!o->bound) return o->loading < kLoadFrames && ++o->loading;
        bool wanted = false;
        each(o, [&](const Client* c) { wanted = c->wanted() || wanted; });
        return wanted || o->shown;
    }
    static void frame(Object* o, i32) {
        bind(o);
        if (!o->bound) return;
        o->shown = 0;
        each(o, [&](const Client* c) {
            const bool wanted = c->wanted();
            c->frame(wanted);
            o->shown |= wanted;
        });
        draw(&o->base);
    }
    static void init(Object* o) { bind(o); }
};

const VTable<Overlay> kVTable = {&destroy<Object>, &destroy<Object>, reinterpret_cast<const void*>(&Overlay::paint),
                                 &Overlay::frame, &Overlay::init, reinterpret_cast<const void*>(kStockEntry20)};

char* construct(char* state, u32 page) {
    return reinterpret_cast<char*>(opxy::screen::construct(reinterpret_cast<Object*>(state), page,
                                                           sdk_overlay_page() + 1, kAsset, &kVTable));
}
}

bool sdk_overlay_add(const Client* client) {
    const u32 n = added;
    if (!client || n >= kAddedLimit) return false;
    for (u32 i = 0; i < n; ++i)
        if (added_clients[i] == client) return true;
    added_clients[n] = client;
    added = n + 1;
    return true;
}

// GUI role, after the machines' screens (startup.cpp.in). A layout that gave the
// last page to a machine (before it was reserved) keeps it, and the overlay is absent.
void sdk_overlay_startup() {
    const opxy::platform::Record record = {nullptr, nullptr, nullptr, nullptr,
                                           reinterpret_cast<const char*>(sdk_overlay_page()), 0};
    const opxy::platform::Screen screen = {reinterpret_cast<char*>(&overlay), sizeof(overlay),
                                           reinterpret_cast<const char*>(&kVTable), kAsset, construct};
    sdk_register_screen(&record, &screen);
}
