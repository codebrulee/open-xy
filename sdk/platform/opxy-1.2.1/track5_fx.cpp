// FX III: a third send effect on Aux 5. On the Input encoder's top detent the
// track takes this block's aux bus, runs it through the effect chosen with Shift
// and the track's key, using that effect's stock FX object, and leaves the return
// in the track's input buffer. The effects are stock's send effects and the
// collection's (track5_fx.hpp). The choice is the track's word 7, which stock saves
// with the project and which no stock page reaches: kTag plus the effect's ID, with
// kBypass for encoder 1's bypass and kFull once all eight controls are stored. Only
// tagged values count, so whatever a project already holds there means the first
// effect. kFx3Words holds the effect's eight controls (words 4..6 its first three,
// as before kFull) and word 36 its shift row, as on the sends. On send, M1 shows
// the effect's own page when it has one, whose encoders, presses and Shift row edit
// those words as on a send (synth_controls.cpp); otherwise the stock FX page, as on
// FX I and II, whose encoders edit the first three through stock's editor and
// whose fourth position is the track's Level, which the Input page also edits.
#include "stock.hpp"
#include "stock_browser.hpp"
#include "track5_fx.hpp"

extern "C" {
void sdk_track5_stage(opxy::u32 a, opxy::u32 b, const opxy::i32* aux) __asm__("sdk_track5_stage");
void sdk_track5_pick(opxy::u32 track) __asm__("sdk_track5_pick");
opxy::u32 sdk_track5_screen() __asm__("sdk_track5_screen");
void sdk_track5_shift_m1() __asm__("sdk_track5_shift_m1");
const char* sdk_track5_title(opxy::u32 index) __asm__("sdk_track5_title");
opxy::u32 sdk_track5_vector(opxy::u32 track) __asm__("sdk_track5_vector");
opxy::i32 sdk_track5_effect(opxy::u32 track) __asm__("sdk_track5_effect");
const char* sdk_track5_name(opxy::u32 track, opxy::u32 page) __asm__("sdk_track5_name");
opxy::u32 sdk_track5_enabled() __asm__("sdk_track5_enabled");
opxy::u32 sdk_track5_shown() __asm__("sdk_track5_shown");
opxy::u32 sdk_track5_find_inputs(opxy::u32, const char*, opxy::u32) __asm__("sdk_track5_find_inputs");
opxy::u32 sdk_track5_find_inactive(opxy::u32, const char*, opxy::u32) __asm__("sdk_track5_find_inactive");
opxy::u32 sdk_track5_icons() __asm__("sdk_track5_icons");
opxy::u32 sdk_track5_turn(opxy::u32 encoder, opxy::u32 event) __asm__("sdk_track5_turn");
// The send page's turn (synth_controls.cpp), which FX III's own page shares.
opxy::u32 sdk_effect_turn(opxy::u32, opxy::u32, opxy::u32, opxy::u32) __asm__("sdk_effect_turn");
void sdk_track5_key(const opxy::u32* event) __asm__("sdk_track5_key_press");
}

namespace {
using opxy::i32;
using opxy::u32;
using opxy::platform::Address;
using opxy::platform::Fx3Effect;
using opxy::platform::at;
using opxy::platform::byte;
using opxy::platform::stock;
using opxy::platform::word;
namespace browser = opxy::platform::stock_browser;
using Process = void (*)(void*, i32*, i32*, const i32*);
using Init = void (*)(void*, u32);
using Factory = void* (*)(void*);
const u32 kFrames = 64;
const u32 kAudioTrack = opxy::platform::kFx3Track, kChoiceWord = 7, kShiftWord = 36;
const u32 kMainOut = 4;
const u32 kSendFrom = 1932735283;  // 0.9, Q31
const u32 kProject = 0x86bd027c;   // the GUI's project
const u32 kTag = 0x53000000, kTagMask = 0xffffff00, kNumberMask = 0xff;
const u32 kBypass = 0x100;         // in the choice word: encoder 1 has the send off
const u32 kFull = 0x200;           // and every control word and the shift row are stored
const u32 kControls = 3, kLevelWord = 41;
const u32 kAuxPage = 0x0801eae4;   // 0 on M1
const u32 kKeyM1 = 6;
const u32 kFxPage = 21, kBadge = 0x40;
const u32 kAudioWords = 0x84eab39c;  // the audio side's track parameter words
const u32 kInput = 0x11b00d48;       // Aux 5's input buffer: 64 interleaved stereo frames
const u32 kInputFlag = 0x84039579;   // encoder 1's input on/off, global RAM
// Stock's scratch allocator for the sends' effects (0x8d50e048) hands out memory
// from a 1 MiB region whose next free address is here.
const u32 kScratch = 0x84a50b90, kBorrowedSlot = 15;
// generic_fx2.svg's x for fx2's "II" and "FX", and the shift that centres "FX III", Q10.
const i32 kStockLabelX = 221952, kStockFxX = 192973, kCentre = 3686;
bool labelled;
u32 object[120];  // 480 bytes, the stock FX slot's size
i32 left[kFrames], right[kFrames];
bool input_view;  // GUI: on send, M1 shows the Input page
u32 built;        // the choice word whose effect the object holds, plus one; 0 none
bool sending;
unsigned char restore;
// Silence gate: once the bus and the effect's return have both stayed below kQuiet
// (about -114 dBFS) for kTail blocks, the effect stops running until the bus carries
// signal again. kTail, about 6 s, outlasts the longest echo gap: a delay holds at
// most one second per side (stock's and Ping Pong's), and a reverb's tail shows in
// its return, which keeps the count at zero.
const i32 kQuiet = 1 << 12;
const u32 kTail = 4096;
u32 quiet;

// The aux bus runs 2^11 above the FX buses stock's effects are written for (measured
// on the device: 2^10.7..2^11.2 with equal sends). Linear effects don't mind, but
// level-dependent ones (dist, lofi) saturate, so every effect gets the FX buses'
// level and its return goes back up, saturating.
const u32 kBusShift = 11;
i32 to_aux(i32 sample) {
    const i32 limit = 0x7fffffff >> kBusShift;
    if (sample > limit) return 0x7fffffff;
    if (sample < -limit - 1) return static_cast<i32>(0x80000000u);
    return static_cast<i32>(static_cast<u32>(sample) << kBusShift);
}

bool silent(const i32* samples, u32 count) {
    for (u32 i = 0; i < count; ++i)
        if (samples[i] > kQuiet || samples[i] < -kQuiet) return false;
    return true;
}

// The table index of the effect a choice word names, or -1: an untagged word, an
// ID out of the records' range, or one the table lacks. The GUI also asks whether
// this build has the effect (ready); the audio side can't, since an effect with its
// own display is ready only once the GUI's pages are, so it checks the effect's
// record for a factory before running it instead.
i32 find(u32 choice, bool gui = true) {
    const u32 value = choice & ~(kBypass | kFull), number = value & kNumberMask;
    if ((value & kTagMask) != kTag || number >= sdk_fx3_record_count) return -1;
    for (u32 i = 0; i < sdk_fx3_effect_count; ++i)
        if (sdk_fx3_effects[i].number() == number && (!gui || sdk_fx3_effects[i].available()))
            return static_cast<i32>(i);
    return -1;
}
bool chosen(u32 choice, bool gui = true) { return find(choice, gui) >= 0; }
bool bypassed(u32 choice, bool gui = true) { return chosen(choice, gui) && (choice & kBypass); }
// What runs: the chosen effect, or the first (stock delay, always present).
const Fx3Effect& effect_of(u32 choice, bool gui = true) {
    const i32 index = find(choice, gui);
    return sdk_fx3_effects[index < 0 ? 0 : index];
}

u32 stored(u32 index) { return stock<u32(u32, u32, u32)>(0x8d5ac910)(kProject, kAudioTrack, index); }
void store(u32 index, u32 value) { opxy::platform::set_parameter(kProject, kAudioTrack, index, static_cast<i32>(value)); }

// The picker lists the available effects; rows[] maps its rows to the table.
browser::StockSelector<opxy::platform::kFx3Limit> selector = {};
const char* labels[opxy::platform::kFx3Limit];
u32 rows[opxy::platform::kFx3Limit];

// Gives the effect's controls from `first` on their defaults and its shift row its
// first choices, then marks the choice word full.
void fill(const Fx3Effect& effect, u32 choice, u32 first) {
    for (u32 i = first; i < 8; ++i) store(opxy::platform::kFx3Words[i], static_cast<u32>(effect.defaults[i]));
    store(kShiftWord, 0);
    store(kChoiceWord, choice | kFull);
}
void apply(u32 index) {
    const u32 choice = stored(kChoiceWord);
    const Fx3Effect& effect = sdk_fx3_effects[index];
    fill(effect, (kTag | effect.number()) | (chosen(choice) ? choice & kBypass : 0), 0);
}
// A project from before kFull holds only the first three controls: the rest start
// from their defaults, as the audio side has played them.
void complete() {
    const u32 choice = stored(kChoiceWord);
    if (!chosen(choice)) apply(0);
    else if (!(choice & kFull)) fill(effect_of(choice), choice, kControls);
}
void choose(void*, u32 row) { apply(rows[row]); }

bool on_track() { return byte(opxy::platform::kSelectedTrack) == kAudioTrack; }
bool on_send() { return stored(0) >= kSendFrom; }
bool on_m1() { return on_track() && word(kAuxPage) == 0; }
// GUI: track 5's M1 shows the effect's page, its own or the stock FX page.
bool showing_fx() { return on_m1() && on_send() && !input_view; }
// The chosen effect's own page: its record's page (word 3), or 0 on the stock FX page.
u32 own_page() {
    const Fx3Effect& effect = effect_of(stored(kChoiceWord));
    if (!effect.ready) return 0;  // stock's
    const u32 page = word(sdk_fx3_records + effect.number() * sdk_fx3_record_bytes + 12);
    return page == kFxPage ? 0 : page;
}
}

u32 sdk_track5_own() { return showing_fx() && own_page() ? effect_of(stored(kChoiceWord)).number() : 0; }

// GUI: stock's Shift and track key, which acts only on the two sends.
void sdk_track5_pick(u32 track) {
    if (track != kAudioTrack || !on_send()) return;
    const i32 current = find(stored(kChoiceWord));
    u32 count = 0, initial = 0;
    for (u32 i = 0; i < sdk_fx3_effect_count && count < opxy::platform::kFx3Limit; ++i) {
        if (!sdk_fx3_effects[i].available()) continue;
        if (static_cast<i32>(i) == current) initial = count;
        labels[count] = sdk_fx3_effects[i].label;
        rows[count++] = i;
    }
    if (count) selector.open("fx III", labels, count, initial, browser::Callbacks{nullptr, choose, nullptr, nullptr},
                             kAudioTrack);
}

// GUI: 8c08f540, the aux page (through aux_controller.cpp, as the next three). 0 leaves it to stock.
u32 sdk_track5_screen() {
    if (!showing_fx()) return 0;
    complete();  // gives the controls their defaults
    const u32 page = own_page();
    return page ? page : kFxPage;
}

// GUI: 8c08fa14, an encoder turn. 1 when it was this page's. Turning Input keeps
// the Input page up, so arriving on send doesn't take it away mid-turn.
u32 sdk_track5_turn(u32 encoder, u32 event) {
    const u32 position = encoder & 3;
    if (!showing_fx()) {
        if (on_m1() && position == 0) input_view = true;
        return 0;
    }
    if (own_page()) {
        sdk_effect_turn(kProject, kAudioTrack, position, event);
        return 1;
    }
    stock<u32(u32, u32, u32, u32)>(0x8d5ad5bc)(kProject, kAudioTrack,
        position < kControls ? opxy::platform::kFx3Words[position] : kLevelWord, event);
    return 1;
}

// GUI: 8c08fd30, a key on the aux controller. M1 on send shows the FX page.
void sdk_track5_key(const u32* event) {
    if (*event == kKeyM1 && on_track()) input_view = false;
}

// GUI: 8c08f6ec, Shift and M1, which stock swallows. On send, swaps FX and Input.
void sdk_track5_shift_m1() {
    if (on_m1() && on_send()) input_view = !input_view;
}

// GUI: 8d53b642, the FX page's titles. The fourth position is the send's Level.
const char* sdk_track5_title(u32 index) { return showing_fx() && index == kControls ? "Level" : nullptr; }

// GUI: 8c08f7d2, encoder 1's press reading the input flag it is about to invert.
// On send the press toggles the saved bypass instead, or nothing on the controls
// page, and the flag stays as it is: the inverse here, since stock inverts it.
u32 sdk_track5_enabled() {
    const u32 flag = byte(kInputFlag);
    if (!on_send()) return flag;
    if (!showing_fx()) {
        if (!chosen(stored(kChoiceWord))) apply(0);
        store(kChoiceWord, stored(kChoiceWord) ^ kBypass);
    }
    return !flag;
}

// GUI: 8d5299a0, the Input page reading that flag to draw its state. On send the
// saved bypass decides.
u32 sdk_track5_shown() {
    if (!on_send()) return byte(kInputFlag);
    return !bypassed(stored(kChoiceWord));
}

// GUI: the Input page's icons. aux_track.svg's "inputs" and "inputs_inactive" groups
// hold one child per input; stock binds the first five. Artwork with a sixth child in
// each, send's, gets it shown on send in place of main out's. Without that artwork the
// groups hold five and nothing changes.
namespace {
const u32 kFindNode = 0x8d520308, kSendIcon = 5, kVisible = 0x5c;
u32 groups[2];  // inputs, inputs_inactive
u32 child(u32 group, u32 index) {
    if (!group) return 0;
    const u32 begin = word(group + 0x48), end = word(group + 0x4c);
    return end - begin > index * 4 ? word(begin + index * 4) : 0;
}
void show(u32 node, bool visible) {
    if (node) *at<volatile unsigned char>(node + kVisible) = visible;
}
u32 find_group(u32 root, const char* name, u32 flags, u32 slot) {
    const u32 group = stock<u32(u32, const char*, u32)>(kFindNode)(root, name, flags);
    groups[slot] = group;
    return group;
}
}

// GUI: 8d529730 and 8d529742, the page's bind finding the two groups.
u32 sdk_track5_find_inputs(u32 root, const char* name, u32 flags) { return find_group(root, name, flags, 0); }
u32 sdk_track5_find_inactive(u32 root, const char* name, u32 flags) { return find_group(root, name, flags, 1); }

// GUI: 8d529a80, a call just after the draw has set the five icons' visibility.
u32 sdk_track5_icons() {
    const u32 result = stock<u32()>(0x8d585370)();
    const bool send = on_send();
    const bool lit = sdk_track5_shown();
    for (u32 slot = 0; slot < 2; ++slot) {
        if (!child(groups[slot], kSendIcon)) continue;
        if (send) show(child(groups[slot], kMainOut), false);
        show(child(groups[slot], kSendIcon), send && (slot == 0) == lit);
    }
    return result;
}

// GUI: the stock FX page (21) on track 5. It reads three things by selected track:
// the parameter vector, the effect number, whose -1 here would end its draw, and
// the name. Off the third send's page each passes to stock.
namespace {
bool on_page(u32 track) { return track == kAudioTrack && showing_fx(); }
}

u32 sdk_track5_vector(u32 track) {
    if (!on_page(track)) return stock<u32(u32)>(0x8d4d72a8)(track);
    static u32 words[8];
    static struct { u32 allocator, begin, end, capacity; } vector;
    for (u32 i = 0; i < kControls; ++i) words[i] = stored(opxy::platform::kFx3Words[i]);
    words[kControls] = stored(kLevelWord);
    vector.allocator = 0;
    vector.begin = static_cast<u32>(reinterpret_cast<Address>(words));
    vector.end = vector.begin + (kControls + 1) * 4;
    vector.capacity = vector.begin + sizeof(words);
    return static_cast<u32>(reinterpret_cast<Address>(&vector));
}

i32 sdk_track5_effect(u32 track) {
    if (!on_page(track)) return stock<i32(u32)>(0x8d53b4f4)(track);
    return static_cast<i32>(effect_of(stored(kChoiceWord)).number());
}

// The page's fx2 badge (a group holding its box, "II" and "FX") reads "II" as its
// second child whichever way the group lists them. Stock's draw has just hidden the
// badge on this track; the real FX II shares this page, so it gets its text back.
// "FX" is the third child; both texts move to keep the wider label centred in the
// box. A leaf's Q10 matrix is a b . c d . tx ty ., after its method table.
namespace {
void place_text(u32 page, u32 node_index, const char* text, i32 x) {
    const u32 node = stock<u32(u32, u32, u32)>(0x8d520228)(page, node_index, 0);
    if (!node) return;
    if (text) opxy::platform::set_text(at<void>(node), text);
    *at<volatile i32>(node + 4 + 6 * 4) = x;
}

void badge(u32 page, const char* text, i32 text_x, i32 fx_x) {
    const u32 group = word(page + kBadge);
    const u32 begin = word(group + 0x48), end = word(group + 0x4c);
    if (end - begin < 12) return;
    place_text(page, word(begin + 4), text, text_x);
    place_text(page, word(begin + 8), nullptr, fx_x);
}
}

const char* sdk_track5_name(u32 track, u32 page) {
    if (!on_page(track)) {
        if (labelled) badge(page, "II", kStockLabelX, kStockFxX);
        labelled = false;
        return stock<const char*(u32)>(0x8d53b53a)(track);
    }
    *at<volatile unsigned char>(word(page + kBadge) + kVisible) = 1;
    badge(page, "III", kStockLabelX - kCentre, kStockFxX - kCentre);
    labelled = true;
    return effect_of(stored(kChoiceWord)).label;
}

// Audio: 8d51234e, before Aux 5's processor, with the mixer's aux bus and the
// track's input index.
// Audio: Aux 5's stage effect (aux5_stage.cpp), before the track's processor
// 8d50fdc0, with its first two arguments and the mixer's aux bus. 8d4ff264, with
// those arguments, gives the track's Input. While it reads index 4 (main out),
// whose input case leaves silence in 11b00d48, on send this runs the bus through
// the effect into that buffer. Both hold 64 interleaved stereo frames.
void sdk_track5_stage(u32 a, u32 b, const i32* aux) {
    const u32 input = stock<u32(u32, u32)>(0x8d4ff264)(a, b);
    // Input steps through ten buckets, two per stock input, storing each centre.
    // Of main out (index 4, 0.8..1), 0.85 stays that and 0.95 is send.
    const u32* words = stock<const u32*(u32, u32)>(0x8d518d94)(kAudioWords, kAudioTrack);
    const bool send = input == kMainOut && words[0] >= kSendFrom;
    // Encoder 1's input flag is global RAM, not saved with a project, which stock
    // can clear at any time. On send it follows the saved bypass every block; on
    // leaving it goes back, so a physical input never goes live by this route.
    volatile unsigned char& enabled = *at<volatile unsigned char>(kInputFlag);
    if (send != sending) {
        sending = send;
        if (send) restore = enabled;
        else enabled = restore;
    }
    if (!send) {
        quiet = 0;
        return;
    }
    const u32 choice = words[kChoiceWord];
    const bool off = bypassed(choice, false);
    enabled = !off;
    if (off) {
        quiet = 0;
        return;
    }
    i32* buffer = at<i32>(kInput);
    const bool idle = silent(aux, 2 * kFrames);
    if (idle && quiet >= kTail) {
        for (u32 i = 0; i < 2 * kFrames; ++i) buffer[i] = 0;
        return;
    }
    const Fx3Effect& effect = effect_of(choice, false);
    const bool own = chosen(choice, false);  // the words hold this effect's controls
    void* self = object;
    if (built != effect.number() + 1) {
        // Records hold each effect's factory first. Its init asks the scratch allocator
        // for its slot's memory, which only FX I and II have: as FX II's while init runs,
        // with the allocator moved up by the most any effect may take, which FX II's own
        // effect never reaches. A record without a factory leaves the return silent.
        // Stock's effects are given FX II's slot; the collection's are given Aux 5's,
        // whose adapter borrows FX II's scratch and reads Aux 5's shift row
        // (tooling/adapters.py).
        const Factory factory = *at<const Factory>(sdk_fx3_records + effect.number() * sdk_fx3_record_bytes);
        if (!factory) {
            for (u32 i = 0; i < 2 * kFrames; ++i) buffer[i] = 0;
            return;
        }
        factory(self);
        void** fresh = *static_cast<void***>(self);
        volatile u32& scratch = *at<volatile u32>(kScratch);
        const u32 lower = scratch;
        scratch = lower + sdk_fx3_buffer_limit;
        reinterpret_cast<Init>(fresh[0x14 / 4])(self, effect.ready ? kAudioTrack : kBorrowedSlot);
        scratch = lower;
        built = effect.number() + 1;
        quiet = 0;
    }
    void** methods = *static_cast<void***>(self);
    for (u32 i = 0; i < kFrames; ++i) {
        left[i] = aux[2 * i] >> kBusShift;
        right[i] = aux[2 * i + 1] >> kBusShift;
    }
    // A project from before kFull holds the first three controls; the rest play their defaults.
    i32 parameters[8];
    const u32 stored_controls = !own ? 0 : choice & kFull ? 8 : kControls;
    for (u32 i = 0; i < 8; ++i)
        parameters[i] = i < stored_controls ? static_cast<i32>(words[opxy::platform::kFx3Words[i]]) : effect.defaults[i];
    reinterpret_cast<Process>(methods[0x10 / 4])(self, left, right, parameters);
    for (u32 i = 0; i < kFrames; ++i) {
        buffer[2 * i] = to_aux(left[i]);
        buffer[2 * i + 1] = to_aux(right[i]);
    }
    if (idle && silent(buffer, 2 * kFrames)) {
        if (quiet < kTail) ++quiet;
    } else {
        quiet = 0;
    }
}
