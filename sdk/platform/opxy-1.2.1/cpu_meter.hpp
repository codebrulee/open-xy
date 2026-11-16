// The cpu meter's arithmetic, with no stock calls, so host tests can run it
// (tests/fixtures/cpu_meter.cpp); and the GUI interface cpu_meter.cpp gives the
// overlay. cpu_meter.cpp holds the board-specific parts.
#pragma once
#include <opxy/types.hpp>

namespace opxy { namespace platform { namespace cpu_meter {

const u32 kWindowShift = 9;
const u32 kWindow = 1u << kWindowShift;  // blocks per report, about 0.74 s
// Cycles per 64-sample block, the stock diagnostic's normalization budget
// (docs/profiling.md), used until a window has measured the period.
const u32 kBudget = 580352;
// Parts timed within a block: the eight tracks' synth voices, then the stages.
enum Part : u32 { kTrack1 = 0, kAux = 8, kTape, kFx1, kFx2, kMaster, kParts };
const u32 kTracks = kAux;
const u32 kHistory = 40;  // reports in "30s", about 30 s

inline u32 percent(u32 part, u32 whole) {
    const u32 hundredth = whole / 100;
    return hundredth ? part / hundredth : 0;
}
inline u32 field(u32 value, u32 bits) {
    const u32 top = (1u << bits) - 1;
    return value > top ? top : value;
}
// Decimal digits of n at p; returns the end.
inline char* digits(char* p, u32 n) {
    char d[10];
    u32 count = 0;
    do { d[count++] = static_cast<char>('0' + n % 10); n /= 10; } while (n && count < 10);
    while (count) *p++ = d[--count];
    return p;
}

// DSP board, audio: block timings in CYCLES, gathered into reports every kWindow
// blocks. Loads are percent of the measured block period; a late block took
// longer than one period, which drops audio. Reports, each 0 until the first
// window, bit 31 marking a report:
// - report: the window's load (7 bits), slowest block (9), late blocks (6), and
//   late blocks since power-on (9);
// - recent: over the last kHistory reports, the slowest block (9) and late blocks (10);
// - parts[4]: each part's load, 8 bits each, four a word.
// Each field saturates. Zero-initialised, as the stock loader runs no constructors.
struct Meter {
    u32 start, busy, blocks, peak, late, period, late_total;
    u32 pending[kParts];  // this block's part timings
    u32 cycles[kParts];   // the window's
    unsigned short history_peak[kHistory], history_late[kHistory];
    u32 history_next;
    volatile u32 report, recent, parts[4];
};

inline void time_part(Meter& m, u32 part, u32 cycles) {
    if (part < kParts) m.pending[part] += cycles;
}

// Publishes a full window; `start` is the next block's, so the window spans
// kWindow whole periods, start to start.
inline void close(Meter& m, u32 start) {
    const u32 elapsed = start - m.start;
    m.period = elapsed >> kWindowShift;
    const u32 now = percent(m.busy, elapsed), peak = percent(m.peak, m.period);
    m.report = 1u << 31 | field(now, 7) | field(peak, 9) << 7 | field(m.late, 6) << 16 | field(m.late_total, 9) << 22;
    m.history_peak[m.history_next] = static_cast<unsigned short>(field(peak, 9));
    m.history_late[m.history_next] = static_cast<unsigned short>(field(m.late, 10));
    m.history_next = m.history_next + 1 == kHistory ? 0 : m.history_next + 1;
    u32 worst = 0, late = 0;
    for (u32 i = 0; i < kHistory; ++i) {
        if (m.history_peak[i] > worst) worst = m.history_peak[i];
        late += m.history_late[i];
    }
    m.recent = 1u << 31 | field(worst, 9) | field(late, 10) << 9;
    u32 packed[4] = {0, 0, 0, 0};
    for (u32 p = 0; p < kParts; ++p) {
        packed[p / 4] |= field(percent(m.cycles[p], elapsed), 8) << (8 * (p % 4));
        m.cycles[p] = 0;
    }
    for (u32 i = 0; i < 4; ++i) m.parts[i] = packed[i];
    m.busy = m.peak = m.late = m.blocks = 0;
}

// One block's whole DSP callback, after its parts were timed.
inline void block(Meter& m, u32 start, u32 end) {
    if (m.blocks == kWindow) close(m, start);
    if (!m.blocks) m.start = start;
    const u32 busy = end - start;
    if (busy > (m.period ? m.period : kBudget)) {
        ++m.late;
        ++m.late_total;
    }
    m.busy += busy;
    if (busy > m.peak) m.peak = busy;
    ++m.blocks;
    for (u32 p = 0; p < kParts; ++p) {
        m.cycles[p] += m.pending[p];
        m.pending[p] = 0;
    }
}

struct Report { u32 now, peak, late, total; };
inline Report decode(u32 r) { return {r & 0x7f, r >> 7 & 0x1ff, r >> 16 & 0x3f, r >> 22 & 0x1ff}; }
inline u32 part(const u32 (&parts)[4], u32 p) { return parts[p / 4] >> (8 * (p % 4)) & 0xff; }

// GUI: Project > M4 > cpu. Under its header readout, one setting: the breakdown
// (open or closed), which the value encoders (input types 2 and 3) open and
// close. As on voices, input type 1 moves the highlight, which closes it.
// Zero-initialised: closed.
const i32 kBreakdownRow = 0;
struct Settings {
    bool breakdown;
};
inline void input(Settings& s, i32& selected, u32 type, i32 delta) {
    if (!delta) return;
    if (type == 1) {
        selected = kBreakdownRow;
        s.breakdown = false;
    } else if (type == 2 || type == 3) {
        s.breakdown = delta > 0;
    }
}

// The breakdown's rows: "now", six summary rows, the four aux stages, the tracks.
const u32 kSummaryRows = 6, kStageRows = 4;
const u32 kBreakdownRows = 1 + kSummaryRows + kStageRows + kTracks;
static_assert(kStageRows + 1 == kParts - kAux, "Every stage but master is a stage row");

// GUI, implemented in cpu_meter.cpp for the overlays (cpu_overlay.cpp, and
// extension_overlay.cpp, which gives extensions cpu_load).
struct Sample {
    Report report;
    u32 parts[kParts];
};
bool sample(Sample& out);  // polls the DSP board; false before its first window
bool breakdown_open();  // the cpu page is drawn with its breakdown open
// Fills kBreakdownRows keys and values; returns how many.
u32 breakdown(const char** keys, const char** values);

}}}
