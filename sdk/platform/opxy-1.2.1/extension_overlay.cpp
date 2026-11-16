// The extensions' part of the SDK's overlay (overlay.hpp), and where they get
// cpu_load. Two groups of its artwork show what extensions' indicator hooks ask
// (extensions_indicator):
// - "recorder", top right: "rec_armed" (record and pause) or "rec_running"
//   (record and play), and "rec_stopped" (record and stop) for about two seconds
//   after running ends;
// - "cpu_badge": stock's cpu chip in red where stock shows a muted track's M,
//   with the indicator's label to its left. It wins over the recorder.
// While an extension takes cpu_load, the client polls the cpu meter (cpu_meter.hpp)
// about twice a second and hands each sample to the runtime. The runtime adds this
// client once its collection has started (extension_started), since the overlay
// links without the extensions service.
#include "../extension_runtime.hpp"
#include "benchmark.hpp"
#include "cpu_meter.hpp"
#include "overlay.hpp"
#include "screen.hpp"

namespace {
using namespace opxy::screen;
using opxy::i32;
using opxy::u32;
using opxy::extensions::Indicator;
namespace meter = opxy::platform::cpu_meter;

const u32 kStoppedCycles = 800000000;  // about two seconds (CCLK about 400 MHz)
const u32 kLoadCycles = 200000000;     // cpu_load about twice a second
const u32 kLabelChars = 5;
const i32 kLabelRight = 421;  // the label ends here, 8 px left of the chip
const i32 kBackWidth = 46;    // cpu_back's width at home, from x 377

enum class Shown : unsigned char { none, armed, running, stopped };
struct State {
    void* group;
    void* armed;
    void* running;
    void* stopped;
    void* badge;
    Leaf* label;
    Leaf* back;
    i32 label_home[2], back_home;
    Shown shown;
    u32 stopped_at;
    u32 loaded, load_at;
    const char* cpu;  // the cpu indicator's label while one is shown, else null
    char text[kLabelChars + 1];
};
State state;

// The label's width in pixels at 16 px UniversNextPro-Light, near enough to
// right-align it: the overlay's text has no anchor.
i32 width(const char* s) {
    i32 w = 0;
    for (; *s; ++s) w += *s == ' ' ? 4 : *s == 'I' || *s == '1' ? 5 : *s >= '0' && *s <= '9' ? 9 : 10;
    return w;
}

void bind(void* raw) {
    Base* page = static_cast<Base*>(raw);
    state.group = find(page, "recorder");
    state.armed = find(page, "rec_armed");
    state.running = find(page, "rec_running");
    state.stopped = find(page, "rec_stopped");
    state.badge = find(page, "cpu_badge");
    state.label = text_node(find(page, "cpu_label"));
    state.back = static_cast<Leaf*>(find(page, "cpu_back"));
    if (state.label) {
        state.label_home[0] = state.label->matrix[6];
        state.label_home[1] = state.label->matrix[7];
    }
    if (state.back) state.back_home = state.back->matrix[6];
}

void poll_load() {
    const u32 time = opxy::benchmark::cycles();
    if (state.loaded && time - state.load_at < kLoadCycles) return;
    if (!opxy::platform::extensions_cpu_wanted()) return;
    state.loaded = 1;
    state.load_at = time;
    meter::Sample sample;
    if (!meter::sample(sample)) return;
    opxy::extensions::CpuLoad load = {sample.report.now, sample.report.peak, sample.report.late, {}};
    for (u32 p = 0; p < meter::kParts; ++p)
        load.parts[p] = static_cast<opxy::u8>(sample.parts[p] > 255 ? 255 : sample.parts[p]);
    opxy::platform::extensions_cpu_load(load);
}

bool wanted() {
    poll_load();
    const Indicator now = opxy::platform::extensions_indicator();
    const u32 time = opxy::benchmark::cycles();
    state.cpu = now.kind == Indicator::cpu ? now.label : nullptr;
    if (now == Indicator::running) state.shown = Shown::running;
    else if (state.shown == Shown::running) {
        state.shown = Shown::stopped;  // running just ended
        state.stopped_at = time;
    }
    if (state.cpu) return state.badge != nullptr;
    if (state.shown == Shown::stopped && time - state.stopped_at < kStoppedCycles && now == Indicator::none)
        return state.group != nullptr;
    if (now != Indicator::running) state.shown = now == Indicator::armed ? Shown::armed : Shown::none;
    return state.shown != Shown::none && state.group;
}

// Writes the label and moves it and its black only when it changes.
void place_label() {
    char next[kLabelChars + 1];
    u32 n = 0;
    for (const char* s = state.cpu; *s && n < kLabelChars; ++s) next[n++] = *s;
    next[n] = 0;
    bool same = true;
    for (u32 i = 0; i <= n; ++i) same = same && state.text[i] == next[i];
    if (same && n) return;
    for (u32 i = 0; i <= n; ++i) state.text[i] = next[i];
    const i32 w = width(state.text);
    if (state.label) {
        set_text(state.label, state.text);
        const u32 at[1] = {6};
        const i32 x[1] = {state.label_home[0] + ((kLabelRight - w - 400) << 10)};  // home x is 400
        set_words(state.label, at, x);
    }
    if (state.back) {
        const i32 shift = kBackWidth - w - 6 > 0 ? kBackWidth - w - 6 : 0;
        const u32 at[1] = {6};
        const i32 x[1] = {state.back_home + (shift << 10)};
        set_words(state.back, at, x);
    }
}

void frame(bool shown) {
    const bool cpu = shown && state.cpu;
    if (cpu) place_label();
    set_shown(state.badge, cpu);
    const bool recorder = shown && !cpu;
    set_shown(state.group, recorder);
    set_shown(state.armed, recorder && state.shown == Shown::armed);
    set_shown(state.running, recorder && state.shown == Shown::running);
    set_shown(state.stopped, recorder && state.shown == Shown::stopped);
}
const opxy::platform::overlay::Client client = {bind, wanted, frame};
}

namespace opxy { namespace platform {
void extension_started() { sdk_overlay_add(&client); }
}}
