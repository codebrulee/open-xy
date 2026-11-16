// The cpu meter's arithmetic (cpu_meter.hpp) on synthetic block timings: window
// load and peak, late blocks, the 30 s history, part loads and the cpu page's
// setting. Bridge polling, the renderer hooks and the
// overlay's drawing need the device.
#include <cstdio>
#include <cstdlib>
#include <cpu_meter.hpp>

namespace {
namespace meter = opxy::platform::cpu_meter;
using opxy::u32;
using opxy::i32;

void require(bool condition, const char* message) {
    if (!condition) { std::fprintf(stderr, "%s\n", message); std::exit(1); }
}

// Blocks `period` cycles apart and `busy` long (the first `late_blocks` a cycle
// over the period) until a window closes, timing `cycles` to each part given.
// The block that closes a window also opens the next. Returns the next start.
u32 window(meter::Meter& m, u32 start, u32 period, u32 busy, u32 late_blocks = 0,
           const u32* cycles = nullptr) {
    for (u32 i = 0;; ++i, start += period) {
        if (cycles)
            for (u32 p = 0; p < meter::kParts; ++p) meter::time_part(m, p, cycles[p]);
        meter::block(m, start, start + (i < late_blocks ? period + 1 : busy));
        if (m.blocks == 1 && i) return start + period;
    }
}
}

int main() {
    // A window at half load: 50% now and peak, nothing late.
    static meter::Meter m;
    require(!m.report && !m.recent, "No report before the first window");
    u32 start = window(m, 1000, 1000, 500);
    meter::Report r = meter::decode(m.report);
    require(m.report >> 31 && r.now == 50 && r.peak == 50 && r.late == 0 && r.total == 0, "Half-load window");
    require(m.period == 1000, "The period is measured from the window");

    // Late blocks count in their window and since power-on; the next window resets
    // its own count but not the total.
    start = window(m, start, 1000, 500, 3);
    r = meter::decode(m.report);
    require(r.late == 3 && r.total == 3 && r.peak == 100, "Late blocks and their peak");
    start = window(m, start, 1000, 200);
    r = meter::decode(m.report);
    require(r.late == 0 && r.total == 3 && r.now == 20, "A later window keeps only the total");

    // The 30 s history keeps the slowest block and the late blocks of recent windows.
    require((m.recent & 0x1ff) == 100 && (m.recent >> 9 & 0x3ff) == 3, "Recent peak and late blocks");
    for (u32 i = 0; i < meter::kHistory; ++i) start = window(m, start, 1000, 200);
    require((m.recent & 0x1ff) == 20 && (m.recent >> 9 & 0x3ff) == 0, "Old windows leave the history");

    // Each part's load is percent of the window, packed four to a word.
    u32 cycles[meter::kParts] = {};
    cycles[0] = 100;  // track 1: 10%
    cycles[7] = 50;   // track 8: 5%
    cycles[12] = 20;  // master: 2%
    meter::time_part(m, 99, 900);  // not a part: ignored
    // The first such window still holds the block that closed the last; the second is all timed.
    start = window(m, start, 1000, 300, 0, cycles);
    start = window(m, start, 1000, 300, 0, cycles);
    u32 parts[4];
    for (u32 i = 0; i < 4; ++i) parts[i] = m.parts[i];
    require(meter::part(parts, 0) == 10 && meter::part(parts, 7) == 5 && meter::part(parts, 12) == 2 &&
            meter::part(parts, 1) == 0, "Part loads");

    // The slowest block keeps its own parts: track 3 spikes in one block while
    // track 1 is heaviest on average, so the spike names track 3.
    {
        static meter::Meter s;
        u32 at = window(s, 1000, 1000, 300);
        for (u32 i = 0; i < meter::kWindow; ++i, at += 1000) {
            const bool spike = i == 7;
            meter::time_part(s, 0, 200);
            if (spike) meter::time_part(s, 2, 700);
            meter::block(s, at, at + (spike ? 950 : 250));
        }
        meter::block(s, at, at + 250);  // closes the window
        u32 average[4], spike[4], peak_parts[meter::kParts];
        for (u32 i = 0; i < 4; ++i) {
            average[i] = s.parts[i];
            spike[i] = s.peak_parts[i];
        }
        for (u32 q = 0; q < meter::kParts; ++q) peak_parts[q] = meter::part(spike, q);
        // The window opened with the last one's closing block, untimed: track 1 is 19.96%.
        require(meter::part(average, 0) == 19 && meter::part(average, 2) == 0, "Averages hide the spike");
        require(peak_parts[2] == 70 && peak_parts[0] == 20, "The slowest block's parts");
        require(meter::decode(s.report).peak == 95, "Its peak");
        require(meter::peak_from(95, peak_parts) == 2, "The spike names track 3");
        // A window later, the slowest block's parts are that window's own.
        at = window(s, at + 1000, 1000, 300);
        for (u32 i = 0; i < 4; ++i) spike[i] = s.peak_parts[i];
        require(!spike[0] && !spike[1] && !spike[2] && !spike[3], "Each window starts afresh");
    }
    {
        u32 peak_parts[meter::kParts] = {};
        peak_parts[4] = 30;
        require(meter::peak_from(95, peak_parts) == meter::kParts, "Untimed load larger than any part is other");
        require(meter::peak_from(50, peak_parts) == 4, "A part larger than the untimed load is named");
        u32 none[meter::kParts] = {};
        require(meter::peak_from(0, none) == meter::kParts + 1, "Nothing measured names nothing");
    }

    // Fields saturate rather than wrap.
    require(meter::field(1000, 7) == 127 && meter::field(5, 7) == 5, "Saturating fields");

    // Project > M4 > cpu: one setting under the header, the breakdown. The value
    // encoders open and close it, and moving the highlight closes it.
    static meter::Settings s;
    require(!s.breakdown, "It starts closed");
    i32 selected = 0;
    meter::input(s, selected, 1, -1);
    require(selected == meter::kBreakdownRow, "The header is not selectable");
    meter::input(s, selected, 3, 1);
    require(s.breakdown, "The value encoder opens the breakdown");
    meter::input(s, selected, 2, 0);
    require(s.breakdown, "A zero turn changes nothing");
    meter::input(s, selected, 2, -1);
    require(!s.breakdown, "Turning back closes it");
    meter::input(s, selected, 2, 1);
    meter::input(s, selected, 1, 5);
    require(!s.breakdown && selected == meter::kBreakdownRow, "Moving the highlight closes it and stays on it");
    meter::input(s, selected, 4, 1);
    require(!s.breakdown, "Other input changes nothing");
    return 0;
}
