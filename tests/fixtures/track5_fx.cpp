// Execute FX III (track5_fx.cpp) against stock boundary doubles: the effect list
// and picker, the FX page's hooks, encoder 1's bypass and the audio path.
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <track5_fx.cpp>

namespace {
using opxy::i32;
using opxy::u32;
using opxy::platform::Address;
void require(bool ok, const char* message) {
    if (!ok) { std::fprintf(stderr, "%s\n", message); std::exit(1); }
}

// The project's stored words on the GUI side, and the audio side's copy.
u32 host_words[64], host_audio[64];
u32 host_edits, host_edit_index, host_edit_event;
unsigned char host_selected = 12, host_flag = 1;
u32 host_tab;
i32 host_buffer[128];
u32 host_scratch = 0x84a00000, host_scratch_in_init;
u32 host_init_slot;
unsigned char host_page[0x200];  // the FX page and its badge group, zeroed

// Effects: one record per ID. A record holds its factory first.
struct Record { void* (*factory)(void*); unsigned char rest[48 - sizeof(void*)]; };
static_assert(sizeof(Record) == 48, "Record stride");
Record host_records[63];
u32 built_count[63], processed;
u32 last_effect;
i32 last_left0, last_parameters[8];

// A test effect: process doubles the signal, so saturation shows.
void host_process(void* self, i32* left, i32* right, const i32* parameters) {
    ++processed;
    last_effect = static_cast<u32*>(self)[2];
    last_left0 = left[0];
    for (u32 i = 0; i < 8; ++i) last_parameters[i] = parameters[i];
    for (u32 i = 0; i < 64; ++i) {
        left[i] = left[i] * 2;
        right[i] = right[i] * 2;
    }
}
void host_init(void*, u32 slot) { host_init_slot = slot; host_scratch_in_init = host_scratch; }
void* host_vtable[8] = {nullptr, nullptr, nullptr, nullptr, reinterpret_cast<void*>(&host_process),
                        reinterpret_cast<void*>(&host_init), nullptr, nullptr};
template <u32 N> void* host_factory(void* storage) {
    ++built_count[N];
    void** object = static_cast<void**>(storage);
    object[0] = host_vtable;
    static_cast<u32*>(storage)[2] = N;  // after the vtable pointer on 32- and 64-bit hosts
    return storage;
}

int present() { return 1; }
int absent() { return 0; }

// The stock picker: what the last open listed.
u32 host_opened, host_count, host_initial;
const char* host_labels[32];
opxy::platform::stock_browser::Callbacks host_callbacks;
i32 host_number;

u32 host_stored(u32 project, u32 track, u32 index) {
    require(project == 0x86bd027c && track == 12 && index < 64, "Wrong stored word");
    return host_words[index];
}
u32 host_set(u32 project, u32 track, u32 index, i32 value) {
    require(project == 0x86bd027c && track == 12 && index < 64, "Wrong set");
    host_words[index] = static_cast<u32>(value);
    return 1;
}
u32 host_edit(u32 project, u32 track, u32 index, u32 event) {
    require(project == 0x86bd027c && track == 12, "Wrong edit");
    ++host_edits; host_edit_index = index; host_edit_event = event;
    return 1;
}
const u32* host_audio_words(u32 base, u32 track) {
    require(base == 0x84eab39c && track == 12, "Wrong audio words");
    return host_audio;
}
u32 host_stock_vector(u32) { return 0xabc; }
i32 host_stock_effect(u32) { return -7; }
const char* host_stock_name(u32) { return "stock"; }
}

// The table track5_fx.hpp declares: two stock effects, one present collection
// effect (20) and one this build lacks (21).
const opxy::platform::Fx3Effect sdk_fx3_effects[] = {
    {reinterpret_cast<const char*>(0), nullptr, "delay", {100, 101, 102, 103, 104, 105, 106, 107}},
    {reinterpret_cast<const char*>(5), nullptr, "reverb", {500, 501, 502, 503, 0, 0, 0, 0}},
    {reinterpret_cast<const char*>(20), present, "probefx", {2000, 2001, 2002, 2003, 2004, 0, 0, 0}},
    {reinterpret_cast<const char*>(21), absent, "gone", {2100, 2101, 2102, 2103, 0, 0, 0, 0}},
};
const u32 sdk_fx3_effect_count = 4;
const u32 sdk_fx3_records = 0x8d4cc380;
const u32 sdk_fx3_record_bytes = 48;
const u32 sdk_fx3_record_count = 63;
const u32 sdk_fx3_buffer_limit = 524288;

bool opxy::platform::stock_browser::detail::open(Session&, Row*, u32 count, const char* title,
        const char* const* labels, u32 initial, Callbacks callbacks, i32 track) {
    require(std::strcmp(title, "fx III") == 0, "Picker title");
    ++host_opened; host_count = count; host_initial = initial; host_callbacks = callbacks; host_number = track;
    for (u32 i = 0; i < count; ++i) host_labels[i] = labels[i];
    return true;
}

// 8d4ff264: the track's Input, from the arguments Aux 5's stage passes on.
u32 host_input_value, host_input_a, host_input_b;
u32 host_input(u32 a, u32 b) { host_input_a = a; host_input_b = b; return host_input_value; }

Address opxy::platform::host_stock_address(Address address) {
    if (address == 0x8d4ff264) return reinterpret_cast<Address>(&host_input);
    if (address == 0x840a0534) return reinterpret_cast<Address>(&host_selected);
    if (address == 0x0801eae4) return reinterpret_cast<Address>(&host_tab);
    if (address == 0x84039579) return reinterpret_cast<Address>(&host_flag);
    if (address == 0x84a50b90) return reinterpret_cast<Address>(&host_scratch);
    if (address == 0x11b00d48) return reinterpret_cast<Address>(host_buffer);
    if (address >= 0x8d4cc380 && address < 0x8d4cc380 + sizeof(host_records))
        return reinterpret_cast<Address>(host_records) + address - 0x8d4cc380;
    if (address >= 0x90000000 && address < 0x90000000 + sizeof(host_page))
        return reinterpret_cast<Address>(host_page) + address - 0x90000000;
    if (address == 0x8d5ac910) return reinterpret_cast<Address>(&host_stored);
    if (address == 0x8d5acf28) return reinterpret_cast<Address>(&host_set);
    if (address == 0x8d5ad5bc) return reinterpret_cast<Address>(&host_edit);
    if (address == 0x8d518d94) return reinterpret_cast<Address>(&host_audio_words);
    if (address == 0x8d4d72a8) return reinterpret_cast<Address>(&host_stock_vector);
    if (address == 0x8d53b4f4) return reinterpret_cast<Address>(&host_stock_effect);
    if (address == 0x8d53b53a) return reinterpret_cast<Address>(&host_stock_name);
    std::fprintf(stderr, "Unmapped stock address %#lx\n", static_cast<unsigned long>(address)); std::abort();
}

namespace {
const u32 kSend = 0x7a000000;      // Input's top detent (0.95)
const u32 kMain = 0x6ccccccc;      // main out's own detent (0.85)
u32 tag(u32 number) { return 0x53000000 | number; }
i32 aux[128];

void fill(i32 value) { for (u32 i = 0; i < 128; ++i) aux[i] = value; }
void block(u32 input = 4) {
    host_input_value = input;
    sdk_track5_stage(0x8d000001u, 0x8d000002u, aux);
    require(host_input_a == 0x8d000001u && host_input_b == 0x8d000002u, "The stage's arguments reach the Input query");
}
// The GUI's store reaches the audio side as stock's set does.
void publish() { for (u32 i = 0; i < 64; ++i) host_audio[i] = host_words[i]; }
}

int main() {
    host_records[0].factory = host_factory<0>;
    host_records[5].factory = host_factory<5>;
    host_records[20].factory = host_factory<20>;
    // The page's badge group, at page + 0x40, with no children.
    *reinterpret_cast<u32*>(host_page + 0x40) = 0x90000100;

    // Off send nothing changes: no picker, stock's page, encoder 1 is stock's.
    host_words[0] = kMain;
    sdk_track5_pick(12);
    require(host_opened == 0 && sdk_track5_screen() == 0, "FX III acted off send");
    require(sdk_track5_enabled() == host_flag && sdk_track5_shown() == host_flag, "Encoder 1 changed off send");
    require(sdk_track5_vector(12) == 0xabc && sdk_track5_effect(12) == -7 &&
            std::strcmp(sdk_track5_name(12, 0x90000000), "stock") == 0, "FX page left stock off send");

    // On send, Shift + the key lists the available effects only, on track 12 only.
    host_words[0] = kSend;
    host_words[7] = 0x12345678;  // whatever an old project holds: the first effect
    sdk_track5_pick(11);
    require(host_opened == 0, "Picker opened on another track");
    sdk_track5_pick(12);
    require(host_opened == 1 && host_count == 3 && host_initial == 0 && host_number == 12, "Picker rows");
    require(std::strcmp(host_labels[0], "delay") == 0 && std::strcmp(host_labels[1], "reverb") == 0 &&
            std::strcmp(host_labels[2], "probefx") == 0, "Picker skipped or misnamed an effect");
    // Choosing stores the tagged ID and the effect's first three defaults.
    host_callbacks.confirm(host_callbacks.context, 2);
    require(host_words[7] == tag(20) && host_words[4] == 2000 && host_words[5] == 2001 && host_words[6] == 2002,
            "Choice not stored");
    sdk_track5_pick(12);
    require(host_initial == 2, "Picker didn't start on the current effect");

    // The FX page on M1: stock page 21 with the effect's controls, name and number.
    host_selected = 12; host_tab = 0;
    require(sdk_track5_screen() == 21, "FX page not shown");
    require(sdk_track5_effect(12) == 20 && std::strcmp(sdk_track5_name(12, 0x90000000), "probefx") == 0,
            "FX page names the wrong effect");
    require(std::strcmp(sdk_track5_title(3), "Level") == 0 && sdk_track5_title(0) == nullptr, "Titles");
    require(sdk_track5_turn(1, 77) == 1 && host_edit_index == 5 && host_edit_event == 77, "Encoder 2 edit");
    require(sdk_track5_turn(3, 78) == 1 && host_edit_index == 41, "Encoder 4 is Level");
    require(sdk_track5_turn(4 + 2, 79) == 1 && host_edit_index == 6, "Second-row encoders map by position");
    // On another track or tab, stock's.
    host_selected = 11;
    require(sdk_track5_screen() == 0 && sdk_track5_turn(0, 1) == 0, "FX III page on another track");
    host_selected = 12; host_tab = 1;
    require(sdk_track5_screen() == 0, "FX III page on another tab");
    host_tab = 0;

    // Shift + M1 swaps to the Input page; M1 brings the FX page back; turning
    // Input keeps the Input page up.
    sdk_track5_shift_m1();
    require(sdk_track5_screen() == 0, "Shift + M1 didn't show the Input page");
    const u32 m1 = 6, m2 = 7;
    sdk_track5_key(&m2);
    require(sdk_track5_screen() == 0, "Another key left the Input page");
    sdk_track5_key(&m1);
    require(sdk_track5_screen() == 21, "M1 didn't bring the FX page back");
    sdk_track5_shift_m1();
    const u32 edits = host_edits;
    require(sdk_track5_turn(0, 1) == 0 && host_edits == edits, "Input page turn was taken");
    sdk_track5_shift_m1();

    // Encoder 1 on the Input page toggles the saved bypass, leaving the flag; on the
    // FX page it does nothing.
    sdk_track5_shift_m1();
    const unsigned char flag = host_flag;
    require(sdk_track5_enabled() == static_cast<u32>(!flag) && host_words[7] == (tag(20) | 0x100), "Bypass on");
    require(sdk_track5_shown() == 0, "Input page doesn't show the bypass");
    require(sdk_track5_enabled() == static_cast<u32>(!flag) && host_words[7] == tag(20), "Bypass off");
    require(sdk_track5_shown() == 1, "Input page doesn't show the send on");
    sdk_track5_key(&m1);
    sdk_track5_enabled();
    require(host_words[7] == tag(20), "Encoder 1 on the FX page toggled the bypass");
    // Choosing again keeps the bypass.
    host_words[7] = tag(20) | 0x100;
    host_callbacks.confirm(host_callbacks.context, 1);
    require(host_words[7] == (tag(5) | 0x100) && host_words[4] == 500, "Rechoosing lost the bypass");
    host_callbacks.confirm(host_callbacks.context, 2);
    host_words[7] = tag(20);

    // An effect the build lacks (21), or an ID out of range, means the first
    // effect, with its own defaults rather than the stored words.
    host_words[7] = tag(21);
    require(sdk_track5_effect(12) == 0 && std::strcmp(sdk_track5_name(12, 0x90000000), "delay") == 0,
            "A missing effect wasn't replaced");
    host_words[7] = tag(70);
    require(sdk_track5_effect(12) == 0, "An out-of-range ID was used");
    host_words[7] = tag(20);

    // Audio: off send, the buffer is stock's and nothing runs.
    publish();
    for (u32 i = 0; i < 128; ++i) host_buffer[i] = 9;
    fill(1 << 20);
    block(3);
    require(processed == 0 && host_buffer[0] == 9, "FX III ran on another input");
    host_audio[0] = kMain;
    block(4);
    require(processed == 0 && host_buffer[0] == 9, "FX III ran on main out");

    // On send the effect is built once, as FX II's slot with the allocator moved
    // above FX II's memory and put back, and runs at the FX buses' level.
    publish();
    host_flag = 0;
    block();
    require(built_count[20] == 1 && host_init_slot == 15, "Effect not built in FX II's slot");
    require(host_scratch_in_init == 0x84a00000 + 524288 && host_scratch == 0x84a00000, "Scratch not borrowed and restored");
    require(host_flag == 1, "Input flag doesn't follow the send");
    require(processed == 1 && last_effect == 20 && last_left0 == (1 << 9), "Effect didn't get the FX bus level");
    require(last_parameters[0] == 2000 && last_parameters[2] == 2002 && last_parameters[3] == 2003 &&
            last_parameters[4] == 2004, "Parameters: words 4..6, then the effect's defaults");
    require(host_buffer[0] == (1 << 21) && host_buffer[127] == (1 << 21), "Return not back at the aux level");
    block();
    require(built_count[20] == 1 && processed == 2, "Effect rebuilt without a change");

    // The return saturates instead of wrapping.
    fill(0x60000000);
    block();
    require(host_buffer[0] == 0x7fffffff, "Positive return wrapped");
    fill(-0x60000000);
    block();
    require(host_buffer[0] == static_cast<i32>(0x80000000u), "Negative return wrapped");

    // Bypass: nothing runs, the flag shows off.
    host_audio[7] = tag(20) | 0x100;
    const u32 before = processed;
    block();
    require(processed == before && host_flag == 0, "Bypass ran the effect");
    host_audio[7] = tag(20);

    // An untagged word runs the first effect with its defaults, not the stored words.
    host_audio[7] = 0x12345678;
    fill(1 << 20);
    block();
    require(built_count[0] == 1 && last_effect == 0 && last_parameters[0] == 100, "Untagged word misread");
    // The audio side can't ask ready(), so an effect the GUI wouldn't list still
    // runs if its record has a factory; one without leaves the return silent.
    host_audio[7] = tag(21);
    for (u32 i = 0; i < 128; ++i) host_buffer[i] = 9;
    const u32 runs = processed;
    block();
    require(processed == runs && host_buffer[0] == 0, "A missing factory wasn't silenced");

    // Silence gate: after kTail silent blocks in and out, the effect stops, and
    // signal starts it again.
    host_audio[7] = tag(20);
    fill(0);
    for (u32 i = 0; i < 4096 + 2; ++i) block();
    const u32 gated = processed;
    block();
    require(processed == gated && host_buffer[0] == 0, "Silence gate didn't stop the effect");
    fill(1 << 20);
    block();
    require(processed == gated + 1, "Signal didn't restart the effect");

    // Leaving send restores encoder 1's flag as it was before.
    host_audio[0] = kMain;
    block();
    require(host_flag == 0, "Input flag not restored on leaving send");
    return 0;
}
