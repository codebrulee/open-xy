// The cpu meter's part of the SDK's overlay (overlay.hpp), the "breakdown" group
// of its artwork: every figure over Project > M4 > cpu while its breakdown is
// open, in two columns, so nothing scrolls and nothing is selectable. Warnings
// while playing are an extension's (cpu_load, and the indicator's cpu badge).
#include "benchmark.hpp"
#include "cpu_meter.hpp"
#include "overlay.hpp"
#include "screen.hpp"

extern "C" {
extern const opxy::platform::overlay::Client sdk_cpu_overlay __asm__("sdk_cpu_overlay");
}

namespace {
using opxy::u32;
using namespace opxy::screen;
namespace meter = opxy::platform::cpu_meter;

const u32 kTextCycles = 50000000;  // the breakdown's texts, about eight times a second (CCLK about 400 MHz)
const char* const kKeyIds[meter::kBreakdownRows] = {"bd_k0", "bd_k1", "bd_k2", "bd_k3", "bd_k4", "bd_k5",
    "bd_k6", "bd_k7", "bd_k8", "bd_k9", "bd_k10", "bd_k11", "bd_k12", "bd_k13", "bd_k14", "bd_k15", "bd_k16",
    "bd_k17", "bd_k18"};
const char* const kValueIds[meter::kBreakdownRows] = {"bd_v0", "bd_v1", "bd_v2", "bd_v3", "bd_v4", "bd_v5",
    "bd_v6", "bd_v7", "bd_v8", "bd_v9", "bd_v10", "bd_v11", "bd_v12", "bd_v13", "bd_v14", "bd_v15", "bd_v16",
    "bd_v17", "bd_v18"};

struct State {
    void* breakdown;
    Leaf* keys[meter::kBreakdownRows];
    Leaf* values[meter::kBreakdownRows];
    u32 written;
};
State state;

void bind(void* raw) {
    Base* page = static_cast<Base*>(raw);
    state.breakdown = find(page, "breakdown");
    for (u32 i = 0; i < meter::kBreakdownRows; ++i) {
        state.keys[i] = text_node(find(page, kKeyIds[i]));
        state.values[i] = text_node(find(page, kValueIds[i]));
    }
}

bool wanted() { return meter::breakdown_open(); }

void write_breakdown() {
    const u32 now = opxy::benchmark::cycles();
    if (state.written && now - state.written < kTextCycles) return;
    state.written = now | 1;
    const char* keys[meter::kBreakdownRows] = {};
    const char* values[meter::kBreakdownRows] = {};
    const u32 n = meter::breakdown(keys, values);
    for (u32 i = 0; i < meter::kBreakdownRows; ++i) {
        if (state.keys[i]) set_text(state.keys[i], i < n ? keys[i] : "");
        if (state.values[i]) set_text(state.values[i], i < n ? values[i] : "");
    }
}

void frame(bool shown) {
    const bool open = shown && meter::breakdown_open();
    if (open) write_breakdown();
    else state.written = 0;
    set_shown(state.breakdown, open);
}
}

const opxy::platform::overlay::Client sdk_cpu_overlay = {bind, wanted, frame};
