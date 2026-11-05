// The extensions' indicator in the SDK's overlay (overlay.hpp): the "recorder"
// group of its artwork, top right, showing "rec_armed" (record and pause) or
// "rec_running" (record and play) while an extension's indicator hook asks
// (extensions_indicator), and "rec_stopped" (record and stop) for about two
// seconds after running ends. The runtime adds this client once its collection
// has started (extension_started), since the overlay links without the
// extensions service.
#include "../extension_runtime.hpp"
#include "benchmark.hpp"
#include "overlay.hpp"
#include "screen.hpp"

namespace {
using namespace opxy::screen;
using opxy::u32;
using opxy::extensions::Indicator;

const u32 kStoppedCycles = 800000000;  // about two seconds (CCLK about 400 MHz)

enum class Shown : unsigned char { none, armed, running, stopped };
struct State {
    void* group;
    void* armed;
    void* running;
    void* stopped;
    Shown shown;
    u32 stopped_at;
};
State state;

void bind(void* raw) {
    Base* page = static_cast<Base*>(raw);
    state.group = find(page, "recorder");
    state.armed = find(page, "rec_armed");
    state.running = find(page, "rec_running");
    state.stopped = find(page, "rec_stopped");
}
bool wanted() {
    const Indicator now = opxy::platform::extensions_indicator();
    const u32 time = opxy::benchmark::cycles();
    if (now == Indicator::running) state.shown = Shown::running;
    else if (state.shown == Shown::running) {
        state.shown = Shown::stopped;  // running just ended
        state.stopped_at = time;
    }
    if (state.shown == Shown::stopped && time - state.stopped_at < kStoppedCycles && now == Indicator::none)
        return state.group != nullptr;
    if (now != Indicator::running) state.shown = now == Indicator::armed ? Shown::armed : Shown::none;
    return state.shown != Shown::none && state.group;
}
void frame(bool shown) {
    set_shown(state.group, shown);
    set_shown(state.armed, shown && state.shown == Shown::armed);
    set_shown(state.running, shown && state.shown == Shown::running);
    set_shown(state.stopped, shown && state.shown == Shown::stopped);
}
const opxy::platform::overlay::Client client = {bind, wanted, frame};
}

namespace opxy { namespace platform {
void extension_started() { sdk_overlay_add(&client); }
}}
