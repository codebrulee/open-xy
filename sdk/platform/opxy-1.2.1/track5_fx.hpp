// FX III's effects (track5_fx.cpp): stock's send effects, then the collection's,
// as track5_fx_table.cpp (generated) lists them.
#pragma once
#include <opxy/types.hpp>

namespace opxy { namespace platform {
// An effect FX III can run: its effect ID (an absolute symbol for the
// collection's), whether this build has it (null for stock's, always present; a
// kit's unselected effects answer 0), its list label (1..15 printable characters)
// and its eight default controls, Q31.
struct Fx3Effect {
    const char* id;
    int (*ready)();
    const char* label;
    i32 defaults[8];
    u32 number() const { return static_cast<u32>(reinterpret_cast<__UINTPTR_TYPE__>(id)); }
    bool available() const { return !ready || ready() == 1; }
};
// The most effects the list holds (tooling.adapters.FX3_LIMIT checks it).
const u32 kFx3Limit = 32;
// Aux 5 (stock index 12), and the track word holding each of its effect's eight
// controls (docs/abi-1.2.1.md#fx-iii). Its shift row is word 36, as on the sends.
const u32 kFx3Track = 12;
const u8 kFx3Words[8] = {4, 5, 6, 37, 49, 50, 53, 54};
}}

extern "C" {
extern const opxy::platform::Fx3Effect sdk_fx3_effects[] __asm__("sdk_fx3_effects");
extern const opxy::u32 sdk_fx3_effect_count __asm__("sdk_fx3_effect_count");
// From tooling/stock_tables.py and tooling/definitions.py: the relocated effect
// records, their size and count, and the most scratch memory an effect may ask for.
extern const opxy::u32 sdk_fx3_records __asm__("sdk_fx3_records");
extern const opxy::u32 sdk_fx3_record_bytes __asm__("sdk_fx3_record_bytes");
extern const opxy::u32 sdk_fx3_record_count __asm__("sdk_fx3_record_count");
extern const opxy::u32 sdk_fx3_buffer_limit __asm__("sdk_fx3_buffer_limit");
// GUI: the effect whose own page M1 shows on Aux 5's send, or 0 (track5_fx.cpp).
opxy::u32 sdk_track5_own() __asm__("sdk_track5_own");
}
