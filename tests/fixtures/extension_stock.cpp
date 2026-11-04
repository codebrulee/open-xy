// The runtime and the 1.2.1 adapter end to end, with stock doubles at their mapped
// addresses and the SDK's probe extension (tests/fixtures/extension_probe) subscribed.
#include <extension_runtime.cpp>
#include <extension_probe.cpp>
#include <extension_collection.cpp>
#include <extension_stock.cpp>

#define CHECK(value) do { if (!(value)) return __LINE__; } while (false)
using namespace opxy;
using namespace opxy::platform;
namespace {
u32 project_id = 1, selected = 0, input_enabled = 1, screen = 0, page = 0;
u32 pattern_id = 2, track_length = 16, scale = 3, quantization = 0;
i32 position = 0;
u32 playing = 0, counting = 0, armed = 0, held_record = 0, recorder_mode = 0, recorder_alt = 0;
u32 other_held = 0, shift = 0;
extensions::Time clock_time;
alignas(4) unsigned char pattern_notes[0x788];
u32 on_calls, off_calls, tick_calls, loop_calls, stop_calls, event_calls, writes, publishes, popups, resets, undos;
u32 args[11]; const void* last_event;
RecordedNote inserted[120];
i32 last_radius;
bool failed, consume_record, reject_note, step_entry;

// Any instrument track has a pattern; writes may target another than the selected one.
u32 last_track, last_delta;
u32 get_pattern(u32 t) { failed |= t >= 8; last_track = t; return pattern_id; }
u32 get_length(u32 t) { failed |= t >= 8; return track_length; }
u32 get_scale(u32 t) { failed |= t >= 8; return scale; }
u32 get_quantization(u32 t) { failed |= t != selected; return quantization; }
i32 get_position(u32 controller, u32 t) {
    failed |= controller != 0x860c6adc || t != selected;
    return position;
}
u32 scaled_delta(u32 t, u32 delta) { failed |= t != selected; return scale == 5 ? delta / 2 : delta; }
u32 shift_down() { return shift; }
u32 key_down(u32 key) { return other_held && key == other_held; }
void get_clock(extensions::Time* out) { *out = clock_time; }
void forward_loop(u32 controller, u32 elapsed) {
    failed |= controller != 0x860c6adc || elapsed != 17; ++loop_calls;
}
void forward_tick(u32 state, u32 delta) {
    failed |= state != 0x860c072c; last_delta = delta; ++tick_calls;
}
void forward_stop(u32 controller) { failed |= controller != 0x860c6adc; ++stop_calls; }
void forward_event(const u32* e) {
    ++event_calls;
    if (!input_enabled) return;
    if (e[0] == 50 && !consume_record && !shift) held_record = e[1] != 0;
    if (e[0] == 50 && e[1] && !shift) armed = 0;  // a Record press disarms
    if (e[0] == 51 && e[1] && held_record) armed = 1;  // Rec + Play arms
    if (e[0] == 51 && e[1]) playing = 1;
    if (e[0] == 52 && e[1]) playing = counting = 0;
}
void forward_on(u32 a0,u32 a1,u32 a2,u32 a3,u32 a4,u32 a5,u32 a6,u32 a7,u32 a8,u32 a9,u32 a10,const void* e) {
    const u32 a[] = {a0,a1,a2,a3,a4,a5,a6,a7,a8,a9,a10};
    for (u32 i=0;i<11;++i) args[i]=a[i];
    last_event=e; ++on_calls;
    if (!reject_note) sdk_extensions_note_accepted(a1,a2);
}
void accepted_note(u32, u32) {}
u32 steps_held() { return step_entry; }
void forward_off(u32 a0,u32 a1,u32 a2,u32 a3,u32 a4,const void* e) {
    args[0]=a0; args[1]=a1; args[2]=a2; args[3]=a3; args[4]=a4;
    last_event=e; ++off_calls;
}
unsigned char* container(u32 t, u32 mode) { failed |= t >= 8 || mode != 0; last_track = t; return pattern_notes; }
void undo_reset(u32 controller) { failed |= controller != 0x860c6adc; ++resets; }
void undo_snapshot(u32 controller,u32 t) {
    failed |= controller != 0x860c6adc || t != last_track || resets != undos + 1; ++undos;
}
notes::Container& pattern() { return *reinterpret_cast<notes::Container*>(pattern_notes); }
// A valid stock list holding `filled` notes, the first `held` of them still recording.
void fill_pattern(u32 filled, u32 held = 0) {
    notes::Container& list = pattern();
    for (u32 i = 0; i < notes::capacity; ++i) {
        list.slots[i].previous = i < filled ? static_cast<i16>(i) - 1 : -1;
        list.slots[i].next = i + 1 < notes::capacity && i + 1 != filled ? static_cast<i16>(i + 1) : -1;
        list.slots[i].note = {static_cast<i32>(i) * 10, 10, 40, 50, static_cast<u8>(i < held ? notes::held_flag : 0), 0};
    }
    list.head = filled ? 0 : -1; list.tail = filled ? static_cast<i16>(filled - 1) : -1;
    list.free = filled < notes::capacity ? static_cast<i16>(filled) : -1;
    list.count = static_cast<i16>(filled);
}
// Stock insertion itself is qualified separately against authenticated firmware.
// Here the boundary double appends to the list, rejects at capacity and checks
// transaction/payloads.
__UINT64_TYPE__ insert(void* c, const PatternNote* note, i32 radius) {
    failed |= c != pattern_notes || resets != undos || undos == 0 || note->flags != 0;
    notes::Container& list = pattern();
    if (list.count == 120 || list.free < 0) return 0xffffull << 32;
    const i16 slot = list.free;
    list.free = list.slots[slot].next;
    list.slots[slot] = {list.tail, -1, *note};
    if (list.tail >= 0) list.slots[list.tail].next = slot; else list.head = slot;
    list.tail = slot; ++list.count;
    inserted[writes++ % 120] = {note->position,note->length,note->pitch,note->velocity};
    last_radius=radius;
    return static_cast<__UINT64_TYPE__>(slot) << 32;
}
void publish(u32 t) { failed |= t != last_track; ++publishes; }
void popup(const char* text,u32 a,u32 b,u32 duration) {
    const char expected[]="probed";
    for (u32 i=0;i<sizeof(expected);++i) failed |= text[i]!=expected[i];
    failed |= a!=1 || b || duration!=0x40000000u; ++popups;
}
// Scenes and songs: stock's boundary step applies the queued scene, or else the
// active song's next entry, each through set-scene at its own (hooked) call.
u32 scene_now = 0, song_now = 16, song_position = 0, queued_scene = ~0u, set_calls = 0, step_calls = 0;
const u32 song_scenes[8] = {5, 6, 7, 8, 9, 10, 11, 12};
u32 set_copy = 0;
u32 set_scene(u32 scene, u32 copy) { scene_now = scene; set_copy = copy; ++set_calls; return 7; }
// The arranger page's immediate switch, after set-scene: a notice and the jump.
u32 switch_notices = 0, jumps = 0, jumped_to = ~0u;
u32 scene_notice(u32, u32, u32) { ++switch_notices; return 3; }
u32 scene_jump(u32 controller, u32 scene, u32 restart) {
    failed |= controller != 0x860c6adc || restart != 0; ++jumps; jumped_to = scene; return 4;
}
// Stock's 8c095cde, uncued: set-scene, notice, jump, through the hooked sites.
void arranger_select(u32 scene) {
    sdk_extensions_scene_select(scene, 1);
    sdk_extensions_scene_switched(0, 0, 0);
    sdk_extensions_scene_jump(0x860c6adc, scene, 0);
}
u32 get_scene() { return scene_now; }
u32 get_song() { return song_now; }
void song_off() { song_now |= 16; }
u32 song_sets, sequencer_resets;
void set_song(u32 song) { song_now = song > 13 ? 16 : song; ++song_sets; }
void reset_sequencer(u32 controller, u32 mode) { if (controller == 0x860c6adc && !mode) ++sequencer_resets; }
u32 boundary_step(u32 controller) {
    failed |= controller != 0x860c6adc;
    ++step_calls;
    if (queued_scene != ~0u) {
        failed |= sdk_extensions_scene_queued(queued_scene, 0) != 7;  // stock's cue passes 0
        queued_scene = ~0u;
    } else if (!(song_now & 16)) {
        failed |= sdk_extensions_scene_song(song_scenes[song_position], 0) != 7;
        ++song_position;
    }
    return 5;
}
// Mutes: stock's scene write and DSP message, and its mute 8d5934fc, which calls
// both through the hooked sites as stock does.
u32 scene_mutes[16], dsp_mutes[16], scene_mute_calls = 0, dsp_mute_calls = 0;
// The project's scene rows and songs, at the addresses the adapter computes from
// the project word (project_id), and stock's change markers.
unsigned char scene_memory[99 * 33], song_memory[14 * 0x68] alignas(4);
u32 changes = 0, changed = 0, redraws = 0;
void redraw() { ++redraws; }
u32 scene_mute(u32 t, u32 v) {
    failed |= t >= 16; scene_mutes[t] = v; ++scene_mute_calls;
    scene_memory[scene_now * 33 + 16 + t] = static_cast<unsigned char>(v);  // stock writes the current scene
    return 1;
}
u32 dsp_mute(u32 t, u32 v) { failed |= t >= 16; dsp_mutes[t] = v; ++dsp_mute_calls; return 0; }
void sequencer_mute(u32 controller, u32 t, u32 v) {
    failed |= controller != 0x860c6adc;
    sdk_extensions_mute_keys(t, v);
    sdk_extensions_mute_dsp(t, v);
}
void key(u32 key, bool pressed, u32 elapsed=50000) {
    clock_time += elapsed;
    const u32 event[4]={key, pressed ? 100u : 0u, 123, 456};
    failed |= sdk_extensions_key_queue(event) != 0x08023c78;
}
void record() { key(50,true); key(50,false); }
void play() { key(51,true); key(51,false); }
void note(u32 pitch=60,u32 velocity=87) {
    sdk_extensions_live_note_on(0x860c6adc,selected,pitch,0xffffffffu,0xffffffffu,velocity,0,1,123,456,33,nullptr);
}
void advance(u32 delta) { sdk_extensions_recorder_tick(0x860c072c,delta); }
void release(u32 pitch=60) { sdk_extensions_live_note_off(0x860c6adc,selected,pitch,1,2,nullptr); }
}

namespace opxy { namespace platform {
Address host_stock_address(Address address) {
    switch (address) {
        case 0x840a0524: return reinterpret_cast<Address>(&project_id);
        case 0x840a0534: return reinterpret_cast<Address>(&selected);
        case 0x08023c48: return reinterpret_cast<Address>(&input_enabled);
        case 0x84039230: return reinterpret_cast<Address>(&page);
        case 0x8400b9d0: return reinterpret_cast<Address>(&screen);
        case 0x860c9b39: return reinterpret_cast<Address>(&playing);
        case 0x860c9b3a: return reinterpret_cast<Address>(&counting);
        case 0x860c9b3b: return reinterpret_cast<Address>(&armed);
        case 0x860c9b3c: return reinterpret_cast<Address>(&held_record);
        case 0x860c072c: return reinterpret_cast<Address>(&recorder_mode);
        case 0x860c072d: return reinterpret_cast<Address>(&recorder_alt);
        case 0x8d586014: return reinterpret_cast<Address>(&get_pattern);
        case 0x8d586800: return reinterpret_cast<Address>(&get_length);
        case 0x8d586b50: return reinterpret_cast<Address>(&get_scale);
        case 0x8d586c8c: return reinterpret_cast<Address>(&get_quantization);
        case 0x8d592a94: return reinterpret_cast<Address>(&get_position);
        case 0x8d5950e0: return reinterpret_cast<Address>(&scaled_delta);
        case 0x8d51f184: return reinterpret_cast<Address>(&shift_down);
        case 0x8d5b7d7c: return reinterpret_cast<Address>(&key_down);
        case 0x8d5be3a0: return reinterpret_cast<Address>(&get_clock);
        case 0x8d5986b8: return reinterpret_cast<Address>(&forward_loop);
        case 0x8d590b0a: return reinterpret_cast<Address>(&forward_tick);
        case 0x8d591964: return reinterpret_cast<Address>(&forward_stop);
        case 0x8c0b5d58: return reinterpret_cast<Address>(&forward_event);
        case 0x8d595738: return reinterpret_cast<Address>(&forward_on);
        case 0x8d5b7d54: return reinterpret_cast<Address>(&accepted_note);
        case 0x8d5b7e34: return reinterpret_cast<Address>(&steps_held);
        case 0x8d595420: return reinterpret_cast<Address>(&forward_off);
        case 0x8d5875c8: return reinterpret_cast<Address>(&container);
        case 0x8d591930: return reinterpret_cast<Address>(&undo_reset);
        case 0x8d593c18: return reinterpret_cast<Address>(&undo_snapshot);
        case 0x8d58f24c: return reinterpret_cast<Address>(&insert);
        case 0x8d587668: return reinterpret_cast<Address>(&publish);
        case 0x8d549168: return reinterpret_cast<Address>(&popup);
        case 0x8d597e10: return reinterpret_cast<Address>(&boundary_step);
        case 0x8d5857ac: return reinterpret_cast<Address>(&set_scene);
        case 0x8d585ee8: return reinterpret_cast<Address>(&scene_notice);
        case 0x8d597ad0: return reinterpret_cast<Address>(&scene_jump);
        case 0x860c6adc + 0x8434: return reinterpret_cast<Address>(&queued_scene);
        case 0x8d5842cc: return reinterpret_cast<Address>(&get_scene);
        case 0x8d584284: return reinterpret_cast<Address>(&song_off);
        case 0x8d58586c: return reinterpret_cast<Address>(&set_song);
        case 0x8d5919bc: return reinterpret_cast<Address>(&reset_sequencer);
        case 0x8d58426c: return reinterpret_cast<Address>(&get_song);
        case 0x8d585394: return reinterpret_cast<Address>(&scene_mute);
        case 0x8d4dc8d2: return reinterpret_cast<Address>(&dsp_mute);
        case 0x8d5934fc: return reinterpret_cast<Address>(&sequencer_mute);
        case 0x8d55adf0: return reinterpret_cast<Address>(&redraw);
        case 0x840a0530: return reinterpret_cast<Address>(&changes);
        case 0x840a052c: return reinterpret_cast<Address>(&changed);
        case 0x860c9b40: return reinterpret_cast<Address>(&song_position);
    }
    static const u32 ratios[32]={3,1,2,1,4,3,1,1,3,4,1,2,1,3,1,4,1,5,1,6,1,7,1,8,1,9,1,12,1,16,1,32};
    if (address>=0x840a36e0 && address<0x840a3760) return reinterpret_cast<Address>(ratios)+(address-0x840a36e0);
    if (address >= project_id + 0x98 && address < project_id + 0x98 + sizeof(scene_memory))
        return reinterpret_cast<Address>(scene_memory) + (address - project_id - 0x98);
    if (address >= project_id + 0x5401bc && address < project_id + 0x5401bc + sizeof(song_memory))
        return reinterpret_cast<Address>(song_memory) + (address - project_id - 0x5401bc);
    __builtin_trap();
}
}}

using extensions::probe::seen;
using extensions::probe::command;
using extensions::probe::written;
using extensions::probe::mute_answer;
using extensions::probe::select_answer;
using extensions::probe::arrangement_command;
using extensions::probe::read_ok;
using extensions::probe::stop_ok;
using extensions::probe::write_ok;
using extensions::probe::write_result;
using extensions::Write;
using extensions::probe::undo_ok;
using extensions::probe::undo_result;
using extensions::Undo;
using extensions::probe::snapshot;
using extensions::probe::plan;

int main() {
    fill_pattern(0);
    // The controller update forwards and refreshes the context.
    sdk_extensions_controller_update(0x860c6adc,17); CHECK(loop_calls==1 && seen.contexts>=1);
    const extensions::RecordingContext& c = seen.context.recording;
    CHECK(c.project==1 && c.pattern==2 && c.track==0 && c.length==16*480 && c.scale==3);
    CHECK(c.editable && c.recordable && !seen.context.playing);

    // Live input is forwarded unchanged; while stopped it is only activity.
    note(64,91);
    const u32 expected[]={0x860c6adc,0,64,0xffffffffu,0xffffffffu,91,0,1,123,456,33};
    for (u32 i=0;i<11;++i) CHECK(args[i]==expected[i]);
    CHECK(on_calls==1 && seen.activity==1 && seen.notes_on==0);
    release(64); CHECK(off_calls==1 && seen.notes_off==1 && seen.off.pitch==64);

    // Playing: an accepted live onset on the selected track is recordable, with
    // stock's position and its scale's collision radius.
    play(); CHECK(seen.context.playing);
    position=123; note(60,87);
    CHECK(seen.notes_on==1 && seen.note.pitch==60 && seen.note.velocity==87);
    CHECK(seen.note.position==123 && seen.note.collision_radius==180);
    // Rejected notes, step entry, sequencer calls and other tracks are not.
    reject_note=true; note(); reject_note=false;
    step_entry=true; note(); step_entry=false;
    sdk_extensions_note_accepted(selected,60);
    sdk_extensions_live_note_on(0x860c6adc,7,72,1,2,80,3,4,5,6,7,nullptr);
    CHECK(seen.notes_on==1);
    // A step or sample recorder workflow leaves the pattern editable, not recordable.
    recorder_mode=1; sdk_extensions_controller_update(0x860c6adc,17);
    CHECK(c.editable && !c.recordable); note(); CHECK(seen.notes_on==1);
    recorder_mode=0; sdk_extensions_controller_update(0x860c6adc,17);

    // Recorder ticks forward unchanged and reach the probe scaled to Q8.
    advance(60); CHECK(tick_calls==1 && last_delta==60 && seen.ticks==1 && seen.tick.delta_q8==(60u<<8) && !seen.tick.discontinuity);
    advance(0x00800000u); CHECK(seen.tick.discontinuity && seen.tick.delta_q8==0);

    // Stop forwards, and the context sees it within the same update.
    playing=counting=0; sdk_extensions_stop(0x860c6adc); CHECK(stop_calls==1 && !seen.context.playing);

    // Queued keys: stock's own handling first, then the event with the clock's time.
    record(); CHECK(seen.control.key==50 && !seen.control.pressed && seen.control.accepted);
    CHECK(seen.control.unmodified && seen.control.time==clock_time && event_calls==4);
    key(10,true); CHECK(seen.control.key==10 && seen.control.accepted);
    other_held=55; key(50,true); CHECK(!seen.control.unmodified); key(50,false); other_held=0;
    shift=1; key(50,true); CHECK(!seen.control.unmodified && !seen.control.accepted); key(50,false); shift=0;
    consume_record=true; key(50,true); CHECK(!seen.control.accepted); key(50,false); consume_record=false;
    input_enabled=0; key(10,true); CHECK(!seen.control.accepted); input_enabled=1;
    CHECK(!writes && !undos && !popups);
    // Play on the arranger (47) or song screen (48) fires the probe's claim, after
    // stock handled it: the context then shows stock's own record arm.
    {
        auto named = [](const char* claim, const char* name) {
            if (!claim) return false;
            while (*claim && *claim == *name) { ++claim; ++name; }
            return *claim == *name;
        };
        page=47; key(50,true); u32 calls=event_calls; key(51,true);
        CHECK(event_calls==calls+1 && named(seen.control.claim, "performance_play") && seen.context.record_armed);
        key(51,false); key(50,false); playing=counting=0; sdk_extensions_stop(0x860c6adc);
        key(50,true); CHECK(!seen.context.record_armed); key(50,false);  // Record alone disarms
        page=48; key(51,true); CHECK(!seen.control.claim && !seen.context.record_armed);  // arranger page only
        key(51,false); playing=counting=0; sdk_extensions_stop(0x860c6adc);
        page=12; calls=event_calls; key(51,true);
        CHECK(event_calls==calls+1 && !seen.control.claim);
        key(51,false); playing=counting=0; sdk_extensions_stop(0x860c6adc);
        page=47; input_enabled=0; key(51,true);
        CHECK(!seen.control.accepted && !seen.control.claim);  // input off: no claim
        key(51,false); input_enabled=1; page=0; armed=0;
    }

    // A write replaces the sequence in one undo transaction and publishes once.
    fill_pattern(120);
    command=1; record();
    CHECK(written==2 && writes==2 && resets==1 && undos==1 && publishes==1 && popups==1);
    CHECK(pattern().count==2 && inserted[0].position==-12 && inserted[0].length==40 && inserted[0].pitch==60);
    CHECK(inserted[1].position==480 && inserted[1].velocity==100 && last_radius==180);
    bool used[notes::capacity]; CHECK(notes::valid(pattern(), used));
    // A note stock is still recording refuses the replacement before any undo.
    fill_pattern(5, 1); record();
    CHECK(written==0 && writes==2 && undos==1 && publishes==1 && popups==1 && pattern().count==5);
    // Inserting keeps the existing notes.
    fill_pattern(3); command=2; record();
    CHECK(written==2 && writes==4 && undos==2 && pattern().count==5);
    // Writes reach another instrument track.
    fill_pattern(0); command=3; record();
    CHECK(written==2 && writes==6 && last_track==3 && undos==3 && publishes==3);
    // Not accepted, nothing is granted.
    command=1; consume_record=true; key(50,true); key(50,false); consume_record=false;
    CHECK(writes==6 && undos==3);

    // Scene boundaries: stock's step always runs; the event follows it, while playing.
    using extensions::SceneCause;
    playing=counting=0; sdk_extensions_stop(0x860c6adc);
    CHECK(sdk_extensions_scene_boundary(0x860c6adc)==5 && step_calls==1 && seen.boundaries==0);
    play(); scene_now=2;
    CHECK(sdk_extensions_scene_boundary(0x860c6adc)==5 && seen.boundaries==1 && set_calls==0);
    CHECK(seen.boundary.previous==2 && seen.boundary.scene==2 && seen.boundary.cause==SceneCause::kept);
    CHECK(seen.boundary.song==extensions::no_song && seen.boundary.entry==0);
    // A queued scene takes over.
    queued_scene=4; sdk_extensions_scene_boundary(0x860c6adc);
    CHECK(seen.boundaries==2 && set_calls==1 && seen.boundary.previous==2 && seen.boundary.scene==4 && set_copy==0);
    CHECK(seen.boundary.cause==SceneCause::queued && seen.boundary.song==extensions::no_song);
    // A song's entry, read before stock moves its position on.
    song_now=1; song_position=3; sdk_extensions_scene_boundary(0x860c6adc);
    CHECK(seen.boundaries==3 && seen.boundary.previous==4 && seen.boundary.scene==8 && song_position==4);
    CHECK(seen.boundary.cause==SceneCause::song && seen.boundary.song==1 && seen.boundary.entry==3);
    // Set-scene outside a boundary (the scene screen while stopped) is no event.
    CHECK(sdk_extensions_scene_queued(6,0)==7 && scene_now==6 && seen.boundaries==3);
    CHECK(sdk_extensions_scene_song(7,0)==7 && scene_now==7 && seen.boundaries==3);

    // Arranger scene choices (Shift + 1-0) reach scene_select first. Answered now,
    // or while stopped, stock switches at once; held while playing, the choice
    // becomes stock's cue, and the boundary applies it.
    using extensions::SceneTiming;
    song_now=16; queued_scene=~0u;
    u32 sets=set_calls;
    arranger_select(3);
    CHECK(seen.selects==1 && seen.select.scene==3 && scene_now==3 && set_calls==sets+1 && set_copy==1);
    CHECK(switch_notices==1 && jumps==1 && jumped_to==3 && queued_scene==~0u);
    select_answer=SceneTiming::next_boundary;
    arranger_select(5);
    CHECK(seen.selects==2 && seen.select.scene==5 && scene_now==3 && set_calls==sets+1);
    CHECK(switch_notices==1 && jumps==1 && queued_scene==5);
    arranger_select(6);  // a later choice replaces the cue, as stock's own does
    CHECK(queued_scene==6 && scene_now==3 && jumps==1);
    sdk_extensions_scene_boundary(0x860c6adc);
    CHECK(scene_now==6 && queued_scene==~0u && seen.boundary.cause==SceneCause::queued && seen.boundary.scene==6);
    CHECK(set_copy==1);  // a held choice clones into an empty scene, as the immediate switch does
    // Stock's own cue (Shift + Play, then the scene) keeps its 0.
    queued_scene=7; sdk_extensions_scene_boundary(0x860c6adc); CHECK(scene_now==7 && set_copy==0);
    // A held choice still pending at Stop is forgotten: a later cue of it is stock's.
    arranger_select(8); CHECK(queued_scene==8);
    playing=counting=0; sdk_extensions_stop(0x860c6adc); play();
    sdk_extensions_scene_boundary(0x860c6adc); CHECK(scene_now==8 && set_copy==0);
    // After a held choice the next one is not skipped by mistake.
    select_answer=SceneTiming::now;
    arranger_select(2);
    CHECK(scene_now==2 && switch_notices==2 && jumps==2 && jumped_to==2);
    select_answer=SceneTiming::next_boundary;
    playing=counting=0; sdk_extensions_stop(0x860c6adc);
    arranger_select(4);  // stopped: at once
    CHECK(scene_now==4 && switch_notices==3 && jumps==3 && queued_scene==~0u);
    select_answer=SceneTiming::now;
    play();
    seen.boundaries=3;  // the mute checks below count from here

    // Mutes: every press reaches track_mute first. Answered now, stock acts at once.
    using extensions::MuteSource;
    using extensions::MuteTiming;
    song_now=16;
    sequencer_mute(0x860c6adc,3,2);
    CHECK(seen.mutes==1 && seen.mute.track==3 && seen.mute.muted && seen.mute.source==MuteSource::keys);
    CHECK(!seen.mute.cancels_held && scene_mutes[3]==2 && dsp_mutes[3]==2 && scene_mute_calls==1 && dsp_mute_calls==1);
    // Held while playing: stock does nothing until the boundary, then applies it
    // through its own mute, before the boundary's event and without a second event.
    mute_answer=MuteTiming::next_boundary;
    sequencer_mute(0x860c6adc,3,0);
    CHECK(seen.mutes==2 && !seen.mute.muted && scene_mutes[3]==2 && scene_mute_calls==1 && dsp_mute_calls==1);
    CHECK(sdk_extensions_mute_mixer(9,2)==1 && seen.mutes==3 && seen.mute.source==MuteSource::mixer && scene_mute_calls==1);
    sdk_extensions_scene_boundary(0x860c6adc);
    CHECK(seen.mutes==3 && seen.boundaries==4 && scene_mutes[3]==0 && dsp_mutes[3]==0 && scene_mutes[9]==2);
    CHECK(scene_mute_calls==3 && dsp_mute_calls==3);
    // A second press before the boundary cancels the held mute.
    sdk_extensions_mute_mixer(5,2); sdk_extensions_mute_mixer(5,2);
    CHECK(seen.mutes==5 && seen.mute.cancels_held && scene_mute_calls==3);
    sdk_extensions_scene_boundary(0x860c6adc); CHECK(scene_mute_calls==3 && scene_mutes[5]==0);
    // Stop applies a held mute at once, and so does any update that finds the transport stopped.
    sequencer_mute(0x860c6adc,6,2); CHECK(scene_mute_calls==3);
    playing=counting=0; sdk_extensions_stop(0x860c6adc); CHECK(scene_mutes[6]==2 && dsp_mutes[6]==2 && scene_mute_calls==4);
    play(); sdk_extensions_mute_mixer(7,2); CHECK(scene_mute_calls==4);
    playing=counting=0; sdk_extensions_controller_update(0x860c6adc,17); CHECK(scene_mutes[7]==2 && scene_mute_calls==5);
    // Stopped, an answer to hold is ignored.
    sequencer_mute(0x860c6adc,2,2); CHECK(scene_mutes[2]==2 && scene_mute_calls==6);
    mute_answer=MuteTiming::now;

    // The arrangement: scenes 0-2 present (scene 1 plays pattern 1 on track 0),
    // song 0 holds two entries.
    for (u32 s=0;s<3;++s) scene_memory[s*33+32]=1;
    scene_memory[1*33+0]=1;
    song_memory[0]=0; song_memory[1]=1; *reinterpret_cast<u32*>(song_memory+0x60)=2;
    for (u32 t=0;t<16;++t) scene_memory[2*33+16+t]=0;
    // Play reads it (the claimed key), after stock started.
    // Play stops song playback first (song 1 was playing), so the read sees no song.
    arrangement_command=1; scene_now=2; song_now=1;
    page=47; play(); page=0;
    CHECK(stop_ok && song_now==(1|16) && snapshot.current_song==extensions::no_song);
    CHECK(read_ok && snapshot.project==1 && snapshot.scene[1].present && snapshot.scene[1].pattern[0]==1);
    CHECK(!snapshot.scene[3].present && snapshot.song[0].length==2 && snapshot.song[0].entry[1]==1 && snapshot.song[1].length==0);
    // The performance mutes track 4 in the current scene and track 5 in scene 0,
    // as stock writes them into the scene records.
    scene_memory[2*33+16+4]=2; scene_memory[0*33+16+5]=2;
    // The plan: song 1 plays scene 0, then a new scene 3 (scene 1 with track 3 muted).
    static extensions::SceneRow row;
    row=snapshot.scene[1]; row.mute[3]=2;
    static const u8 entries[]={0,3}, slots[]={3};
    plan={&snapshot,1,entries,2,slots,&row,1};
    const u32 calls=scene_mute_calls;
    // Stop writes it, once, and restores the mutes: the current scene's through stock's mute.
    key(52,true); key(52,false);
    CHECK(write_ok && song_memory[0x68]==0 && song_memory[0x69]==3 && *reinterpret_cast<u32*>(song_memory+0x68+0x60)==2);
    CHECK(scene_memory[3*33+32]==1 && scene_memory[3*33+0]==1 && scene_memory[3*33+16+3]==2);
    CHECK(scene_memory[2*33+16+4]==0 && scene_mute_calls==calls+1 && dsp_mutes[4]==0);
    CHECK(scene_memory[0*33+16+5]==0 && scene_memory[1*33+16+3]==0);
    CHECK(changes==1 && changed==1 && redraws==1);
    // Refused, changing nothing: the song slot is no longer empty; a new slot taken;
    // an entry naming an empty scene; a write while playing.
    key(52,true); key(52,false); CHECK(!write_ok && write_result==Write::song_taken && changes==1);
    static const u8 bad_entries[]={9};
    plan={&snapshot,2,bad_entries,1,nullptr,nullptr,0};
    key(52,true); key(52,false); CHECK(!write_ok && write_result==Write::invalid && changes==1);
    plan={&snapshot,2,entries,2,slots,&row,1};  // scene 3 is present now
    key(52,true); key(52,false); CHECK(!write_ok && write_result==Write::scene_taken && changes==1);
    static const u8 fine[]={0};
    plan={&snapshot,2,fine,1,nullptr,nullptr,0};
    playing=1; key(52,false); CHECK(!write_ok);  // a release grants nothing
    // A song holding one scene other than scene 1 is taken; stock's blank song
    // (one entry, scene 1) is not.
    playing=counting=0;
    *reinterpret_cast<u32*>(song_memory+2*0x68+0x60)=1; song_memory[2*0x68]=3;
    key(52,true); key(52,false); CHECK(!write_ok && write_result==Write::song_taken && changes==1);
    song_memory[2*0x68]=0;
    key(52,true); key(52,false); CHECK(write_ok && changes==2 && song_memory[2*0x68]==0);

    // Undo: Shift + Stop on the arranger or song screen is the probe's replacing
    // claim, so stock never sees it; elsewhere it stays stock's, and Stop's
    // write claim never fires with Shift held.
    plan={&snapshot,1,entries,2,slots,&row,1};
    shift=1; const u32 undo_calls=event_calls;
    key(52,true); key(52,false); CHECK(event_calls==undo_calls+2 && !undo_ok && !write_ok && changes==2);
    page=48;
    CHECK(snapshot.current_song==extensions::no_song);
    song_now=16; scene_now=3; key(52,true); key(52,false);
    CHECK(!undo_ok && undo_result==Undo::scene_in_use);  // its new scene is current
    scene_now=2; scene_memory[3*33+16+7]=2;
    key(52,true); key(52,false); CHECK(!undo_ok && undo_result==Undo::edited && changes==2);  // the new scene changed since
    scene_memory[3*33+16+7]=0; playing=1;
    key(52,true); key(52,false); CHECK(!undo_ok && undo_result==Undo::playing);  // playing
    playing=counting=0; page=47;
    // Song 1 (the take's) selected since the take was read: undo goes ahead,
    // then selects it again and resets the sequencer, as the song page does.
    song_now=1;
    key(52,true); CHECK(event_calls==undo_calls+2 && undo_ok && changes==3 && redraws==3);
    CHECK(song_now==1 && song_sets==1 && sequencer_resets==1);
    song_now=16;
    key(52,false);
    CHECK(*reinterpret_cast<u32*>(song_memory+0x68+0x60)==0 && song_memory[0x68]==snapshot.song[1].entry[0]);
    CHECK(song_memory[0x69]==snapshot.song[1].entry[1] && scene_memory[3*33+32]==0 && scene_memory[3*33+16+3]==0);
    CHECK(scene_memory[3*33+0]==snapshot.scene[3].pattern[0] && song_memory[2*0x68]==0);  // song 2 untouched
    // Nothing as written is left to undo.
    key(52,true); key(52,false); CHECK(!undo_ok && undo_result==Undo::edited && changes==3);
    shift=0; page=0;
    arrangement_command=0;

    CHECK(!failed);
    return 0;
}
