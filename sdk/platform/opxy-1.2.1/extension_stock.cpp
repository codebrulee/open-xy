// Exact 1.2.1 call replacements. Each forwards the original C ABI unchanged.
// Recovery calls the pattern editor, never the live/audio note dispatcher.
#include "../extension_runtime.hpp"
#include "note_container.hpp"
#include "stock.hpp"

extern "C" {
void sdk_extensions_controller_update(opxy::u32, opxy::u32) __asm__("sdk_extensions_controller_update");
void sdk_extensions_key(const opxy::u32*) __asm__("sdk_extensions_key");
opxy::u32 sdk_extensions_key_queue(const opxy::u32*) __asm__("sdk_extensions_key_queue");
void sdk_extensions_recorder_tick(opxy::u32, opxy::u32) __asm__("sdk_extensions_recorder_tick");
void sdk_extensions_stop(opxy::u32) __asm__("sdk_extensions_stop");
void sdk_extensions_note_accepted(opxy::u32, opxy::u32) __asm__("sdk_extensions_note_accepted");
void sdk_extensions_live_note_on(opxy::u32, opxy::u32, opxy::u32, opxy::u32, opxy::u32,
    opxy::u32, opxy::u32, opxy::u32, opxy::u32, opxy::u32, opxy::u32, const void*)
    __asm__("sdk_extensions_live_note_on");
void sdk_extensions_live_note_off(opxy::u32, opxy::u32, opxy::u32, opxy::u32, opxy::u32,
    const void*) __asm__("sdk_extensions_live_note_off");
opxy::u32 sdk_extensions_scene_boundary(opxy::u32) __asm__("sdk_extensions_scene_boundary");
opxy::u32 sdk_extensions_scene_queued(opxy::u32, opxy::u32) __asm__("sdk_extensions_scene_queued");
opxy::u32 sdk_extensions_scene_song(opxy::u32, opxy::u32) __asm__("sdk_extensions_scene_song");
opxy::u32 sdk_extensions_mute_keys(opxy::u32, opxy::u32) __asm__("sdk_extensions_mute_keys");
opxy::u32 sdk_extensions_mute_dsp(opxy::u32, opxy::u32) __asm__("sdk_extensions_mute_dsp");
opxy::u32 sdk_extensions_mute_mixer(opxy::u32, opxy::u32) __asm__("sdk_extensions_mute_mixer");
opxy::u32 sdk_extensions_scene_select(opxy::u32, opxy::u32) __asm__("sdk_extensions_scene_select");
opxy::u32 sdk_extensions_scene_switched(opxy::u32, opxy::u32, opxy::u32) __asm__("sdk_extensions_scene_switched");
opxy::u32 sdk_extensions_scene_jump(opxy::u32, opxy::u32, opxy::u32) __asm__("sdk_extensions_scene_jump");
}

namespace {
// A scene choice scene_select held as stock's cue, until the boundary applies it,
// and the copy flag its immediate switch would have passed to set-scene.
const opxy::u32 no_held_scene = ~0u;
opxy::u32 held_scene = no_held_scene, held_copy = 0;
}
namespace opxy { namespace platform {
namespace {
const u32 kController = 0x860c6adc;
const u32 kSongPosition = kController + 0x3064;  // word: the song entry stock reads next
const u32 kRecorder = 0x860c072c;
const u32 kProject = 0x840a0524;
const u32 kRecordArmed = kPlaying + 2;  // byte: record armed (8d592998 writes it)
const u32 kRecordHeld = kPlaying + 3;  // byte: Record held
const u32 kInputEnabled = 0x08023c48;
const u32 kPage = 0x84039230;
using Wide = __UINT64_TYPE__;
// Only the normal live-input call establishes this scope. Sequencer playback
// calls the broad dispatcher directly and cannot become a history event.
struct LiveInput { u32 track, pitch, velocity, flags; bool active; };
LiveInput live_input;

using PatternNote = notes::Note;  // Stock copies the first eleven payload bytes.

// No other key and no Shift held. Press dispatch precedes insertion into
// stock's held-key list, so a chord keeps stock behavior.
bool unmodified(u32 key) {
    if (shift_held()) return false;
    for (u32 other = 0; other < 56; ++other)
        if (other != key && stock<u32(u32)>(0x8d5b7d7c)(other)) return false;
    return true;
}
}

TrackPattern extension_pattern(u32 track) {
    TrackPattern p = {};
    if (!word(kProject) || track >= 8) return p;
    p.pattern = stock<u32(u32)>(0x8d586014)(track);
    if (!p.pattern) return p;
    p.length = 480u * stock<u32(u32)>(0x8d586800)(track);
    p.scale = stock<u32(u32)>(0x8d586b50)(track);
    p.editable = p.length != 0 && p.scale < 16;
    return p;
}

RecordingContext extension_context(bool& playing) {
    RecordingContext c = {};
    c.project = word(kProject);
    c.track = byte(kSelectedTrack);
    playing = byte(kPlaying) || byte(kPlaying + 1);
    const TrackPattern p = extension_pattern(c.track);
    if (!p.pattern) return c;
    c.pattern = p.pattern;
    c.length = p.length;
    c.scale = p.scale;
    c.quantization = stock<u32(u32)>(0x8d586c8c)(c.track);
    c.editable = p.editable;
    // The other recorder modes are step/sample workflows, not live notes.
    c.recordable = c.editable && !byte(kRecorder) && !byte(kRecorder + 1);
    return c;
}

i32 extension_position(u32 track) {
    i32 position = stock<i32(u32, u32)>(0x8d592a94)(kController, track);
    const i32 length = 480 * stock<i32(u32)>(0x8d586800)(track);
    // Match the ordinary recorder's pickup around pattern zero.
    if (position > length - 240) position -= length;
    return position;
}
u32 extension_scaled_delta(u32 track, u32 delta) {
    return stock<u32(u32, u32)>(0x8d5950e0)(track, delta << 8);
}
bool extension_record_held() { return byte(kRecordHeld) != 0; }
bool extension_record_armed() { return byte(kRecordArmed) != 0; }
// Stock's selected page: the word its getter 8d4d6ed8 returns (the compositor
// paints it; navigation sets it through 8d4d6e5c). Pages are numbered as
// registered with 8d5208a4: pattern.svg (the arranger) is 0x2f, songmode.svg 0x30.
u32 extension_page() { return word(kPage); }

i32 extension_collision_radius(u32 track) {
    const u32 scale = stock<u32(u32)>(0x8d586b50)(track);
    if (scale >= 16) return -1;
    const u32 numerator = word(0x840a36e0 + scale * 8);
    const u32 denominator = word(0x840a36e4 + scale * 8);
    if (!denominator) return -1;
    return 180u * numerator / denominator;
}

u32 extension_write(u32 track, const RecordedNote* notes, u32 count, bool replace) {
    if (!notes || !count || count > 120 || track >= 8) return 0;
    auto* container = stock<unsigned char*(u32, u32)>(0x8d5875c8)(track, 0);
    if (!container || (!replace && *reinterpret_cast<const i16*>(container + 0x786) >= 120)) return 0;
    auto& list = *reinterpret_cast<notes::Container*>(container);
    if (replace && !notes::clearable(list)) return 0;
    const i32 radius = extension_collision_radius(track);
    if (radius < 0) return 0;
    // Start one stock undo transaction before changing any notes. This reset
    // only clears the previous note-undo identity; it does not move transport.
    stock<void(u32)>(0x8d591930)(kController);
    stock<void(u32, u32)>(0x8d593c18)(kController, track);
    // Replacing clears the track's notes first, inside the same undo transaction.
    if (replace && !notes::clear(list)) return 0;
    u32 written = 0;
    for (u32 i = 0; i < count; ++i) {
        const RecordedNote& note = notes[i];
        const PatternNote payload = {note.position, note.length, note.pitch, note.velocity, 0, 0};
        const Wide result = stock<Wide(void*, const PatternNote*, i32)>(0x8d58f24c)(container, &payload, radius);
        if (static_cast<i16>(result >> 32) < 0) break;
        ++written;
    }
    if (written || replace) stock<void(u32)>(0x8d587668)(track);  // A cleared pattern is a change too.
    return written;
}
u32 extension_scene() {
    return word(kProject) ? stock<u32()>(0x8d5842cc)() : 0;  // project +6
}
u32 extension_song() {
    if (!word(kProject)) return extensions::no_song;
    const u32 song = stock<u32()>(0x8d58426c)();  // project +7; bit 4 set: no song
    return song & 16 ? extensions::no_song : song;
}
// Stock's disarm, 8d592998(controller, 0), writes the arm (+0x305f), tells the
// recorder (8d59025c(0x860c072c)) and, disarming, calls 8d591964, the stop the
// SDK's Stop hook wraps, which would stop a transport Play has just started.
// Play has also already copied the arm into +0x3061, which the transport reads
// when it starts. So this writes both bytes and sends the notice, without the stop.
bool extension_disarm_record() {
    if (!word(kProject)) return false;
    *at<unsigned char>(kRecordArmed) = 0;
    *at<unsigned char>(kRecordArmed + 2) = 0;  // +0x3061: Play's copy
    stock<void(u32)>(0x8d59025c)(kRecorder);
    return true;
}
bool extension_stop_song() {
    if (!word(kProject)) return false;
    stock<void()>(0x8d584284)();  // project +7 bit 4: song off, as an arranger scene choice sets it
    return true;
}
void extension_notify(const char* text) {
    // Two seconds, as most of stock's own popups (0x40000000).
    stock<void(const char*, u32, u32, u32)>(0x8d549168)(text, 1, 0, 0x40000000u);
}
}}

using namespace opxy;
using namespace opxy::platform;

// Mutes a track_mute subscriber held: applied after the next scene boundary,
// or once the transport has stopped, through stock's own mute 8d5934fc
// (controller, track, value), which writes the scene and tells the DSP board.
namespace {
struct HeldMute { bool set; u32 value; };
HeldMute held_mutes[16];
bool applying_mutes, skip_dsp_mute;
bool stock_playing() { return byte(kPlaying) || byte(kPlaying + 1); }
void apply_held_mutes() {
    applying_mutes = true;
    for (u32 track = 0; track < 16; ++track) {
        if (!held_mutes[track].set) continue;
        held_mutes[track].set = false;
        stock<void(u32, u32, u32)>(0x8d5934fc)(kController, track, held_mutes[track].value);
    }
    applying_mutes = false;
}
// True when the press is held or cancels a held one, so stock must not act on it.
bool mute_pressed(u32 track, u32 value, extensions::MuteSource source) {
    if (applying_mutes || track >= 16) return false;
    const bool held = held_mutes[track].set;
    const extensions::TrackMute mute = {track, value != 0, source, held};
    const bool hold = extensions_track_mute(mute);
    if (held) {
        held_mutes[track].set = false;
        return true;
    }
    if (!hold) return false;
    held_mutes[track] = {true, value};
    return true;
}
}

void sdk_extensions_controller_update(u32 controller, u32 elapsed) {
    extensions_sync();
    stock<void(u32, u32)>(0x8d5986b8)(controller, elapsed);
    extensions_sync();
    if (!stock_playing()) apply_held_mutes();  // however the transport stopped
}
void sdk_extensions_recorder_tick(u32 state, u32 delta) {
    extensions_recording_tick(delta);
    stock<void(u32, u32)>(0x8d590b0a)(state, delta);
}
void sdk_extensions_stop(u32 controller) {
    stock<void(u32)>(0x8d591964)(controller);
    apply_held_mutes();
    held_scene = no_held_scene;
    // Observe Stop even when an external Stop/Start pair arrives in one update.
    // Both stock playing flags are already clear at this call site.
    extensions_sync();
}
void sdk_extensions_live_note_on(u32 controller, u32 track, u32 note, u32 a3, u32 a4,
                        u32 velocity, u32 a6, u32 a7, u32 low, u32 high, u32 flags,
                        const void* event) {
    extensions_input_activity();
    const LiveInput previous = live_input;
    live_input = {track, note, velocity, flags, true};
    stock<void(u32,u32,u32,u32,u32,u32,u32,u32,u32,u32,u32,const void*)>(0x8d595738)
        (controller,track,note,a3,a4,velocity,a6,a7,low,high,flags,event);
    live_input = previous;
}
void sdk_extensions_note_accepted(u32 track, u32 note) {
    stock<void(u32,u32)>(0x8d5b7d54)(track, note);
    // This call follows stock's acceptance/capacity checks, immediately before
    // its record-arm gate. Apply its ordinary-recording gates without arming it.
    if (live_input.active && live_input.track == track && live_input.pitch == note &&
        (live_input.flags & 1) && !stock<u32()>(0x8d5b7e34)())
        extensions_recordable_note_on(track, note, live_input.velocity);
}
void sdk_extensions_live_note_off(u32 controller, u32 track, u32 note, u32 a3, u32 flags,
                         const void* event) {
    extensions_selected_note_off(track, note);
    stock<void(u32,u32,u32,u32,u32,const void*)>(0x8d595420)(controller,track,note,a3,flags,event);
}
void sdk_extensions_key(const u32* event) {
    extensions_sync();
    extensions::Control control = {event[0], event[1] != 0, false, unmodified(event[0]), 0, nullptr};
    const bool was_held = extension_record_held();
    const bool enabled = byte(kInputEnabled) != 0;
    const ClaimState state = {extension_page(), was_held, shift_held()};
    // The clock writes a full 64-bit microsecond value through its argument.
    stock<void(extensions::Time*)>(0x8d5be3a0)(&control.time);
    // A replacing claim keeps the key from stock. Record reaches stock unless
    // Shift is held (stock's undo, which does not track Record as held).
    control.accepted = enabled;
    const bool replaced = (control.key != extensions::keys::record || state.shift_held) &&
                          extensions_replaces(control, state);
    if (!replaced) stock<void(const u32*)>(0x8c0b5d58)(event);
    // Stock records Record's held state; other keys are accepted while input is enabled.
    control.accepted = enabled && (replaced || control.key != extensions::keys::record ||
        (control.pressed ? extension_record_held() : was_held && !extension_record_held()));
    extensions_queued_control(control, state);
}
u32 sdk_extensions_key_queue(const u32* event) {
    sdk_extensions_key(event);
    // The authenticated span includes stock's following R0 = queue address.
    // A 12-byte span admits CALL.A across the module's relative-call range.
    return 0x08023c78;
}

// The sequencer's scene boundary step 8d597e10(controller), called once each
// time the playing scene's longest pattern ends (8d598548, inside the controller
// update). It applies the queued scene (controller +0x8434) or else the active
// song's next entry, each through set-scene 8d5857ac(scene, copy) at its own
// call (8d597e40, 8d597f02), and moves the song position on.
namespace {
bool in_boundary;
extensions::SceneBoundary boundary;
}
u32 sdk_extensions_scene_boundary(u32 controller) {
    boundary = {extension_scene(), 0, extensions::SceneCause::kept, extensions::no_song, 0};
    in_boundary = true;
    const u32 result = stock<u32(u32)>(0x8d597e10)(controller);
    in_boundary = false;
    boundary.scene = extension_scene();
    boundary.song = extension_song();
    if (boundary.cause != extensions::SceneCause::song) boundary.entry = 0;
    apply_held_mutes();  // onto the scene that starts, before its event
    extensions_scene_boundary(boundary);
    return result;
}
u32 sdk_extensions_scene_queued(u32 scene, u32 copy) {
    if (in_boundary && scene == held_scene) copy = held_copy;  // as the immediate switch would
    held_scene = no_held_scene;
    const u32 result = stock<u32(u32, u32)>(0x8d5857ac)(scene, copy);
    if (in_boundary) boundary.cause = extensions::SceneCause::queued;
    return result;
}
u32 sdk_extensions_scene_song(u32 scene, u32 copy) {
    // Stock has read this entry and moves the position on after this call.
    if (in_boundary) {
        boundary.cause = extensions::SceneCause::song;
        boundary.entry = word(kSongPosition);
    }
    return stock<u32(u32, u32)>(0x8d5857ac)(scene, copy);
}

// The arranger page's scene choice 8c095cde(page, scene). Shift + Play sets the
// page's cue byte (+0x28), and the choice then only writes the controller's
// queue (+0x8434), which the boundary step applies. Otherwise it switches at
// once: set-scene(scene, 1) (8c095d1a; 1 clones the current scene into an empty
// one), 8d585ee8() (8c095d22), and 8d597ad0(controller, scene, 0) (8c095d36),
// which moves playback to the new scene. MIDI's immediate scene changes
// (previous 8d58e8bc, next 8d58e8f8, set 8d58e930) make the same set-scene
// (passing 0: no clone) and jump calls, without the notice; MIDI's queued
// change (8d58e894) already writes the cue. A held switch takes the cue's path:
// the first call queues the scene and the rest are skipped.
// The cue's set-scene at the boundary passes 0, so it would not clone the
// current scene into an empty one as the arranger's immediate switch does: a
// held choice is remembered (held_scene, held_copy) and its boundary call
// passes the caller's flag, so the scene plays as the immediate switch would
// have made it.
namespace {
bool skip_scene_switch;
const u32 kSceneQueue = 0x860c6adc + 0x8434;
}
u32 sdk_extensions_scene_select(u32 scene, u32 copy) {
    skip_scene_switch = scene < extensions::arrangement::scenes && extensions_scene_select({scene});
    if (!skip_scene_switch) return stock<u32(u32, u32)>(0x8d5857ac)(scene, copy);
    *at<u32>(kSceneQueue) = scene;
    held_scene = scene;
    held_copy = copy;
    return 0;
}
u32 sdk_extensions_scene_switched(u32 a, u32 b, u32 c) {
    if (skip_scene_switch) return 0;
    return stock<u32(u32, u32, u32)>(0x8d585ee8)(a, b, c);
}
u32 sdk_extensions_scene_jump(u32 controller, u32 scene, u32 restart) {
    if (skip_scene_switch) {
        skip_scene_switch = false;
        return 0;
    }
    return stock<u32(u32, u32, u32)>(0x8d597ad0)(controller, scene, restart);
}

// Mutes. 8d5934fc(controller, track, value), reached by the track keys and MIDI,
// writes the current scene's mute byte through 8d585394(track, value) (8d59350c),
// then the DSP board's through 8d4dc8d2(track, value) (8d593514). The mixer's
// toggle calls 8d585394 directly (8c093a60). Value 2 is muted, 0 unmuted.
u32 sdk_extensions_mute_keys(u32 track, u32 value) {
    skip_dsp_mute = mute_pressed(track, value, extensions::MuteSource::keys);
    if (skip_dsp_mute) return 1;
    return stock<u32(u32, u32)>(0x8d585394)(track, value);
}
u32 sdk_extensions_mute_dsp(u32 track, u32 value) {
    if (skip_dsp_mute) {
        skip_dsp_mute = false;
        return 0;
    }
    return stock<u32(u32, u32)>(0x8d4dc8d2)(track, value);
}
u32 sdk_extensions_mute_mixer(u32 track, u32 value) {
    if (mute_pressed(track, value, extensions::MuteSource::mixer)) return 1;
    return stock<u32(u32, u32)>(0x8d585394)(track, value);
}

// The arrangement in the open project (word 0x840a0524): scene rows of 33 bytes
// at +0x98 (16 pattern numbers, 16 mutes, present), and 14 songs of 0x68 bytes at
// +0x5401bc (96 scene numbers, then the length as a word at +0x60). Stock's own
// setters (8d585394) mark a change by bumping the word at 0x840a0530 and setting
// the byte at 0x840a052c, which the save path reads.
namespace opxy { namespace platform {
namespace {
namespace arr = extensions::arrangement;
const u32 kScenes = 0x98, kSceneBytes = 33, kSongs = 0x5401bc, kSongBytes = 0x68, kSongLength = 0x60;
const u32 kChanges = 0x840a0530, kChanged = 0x840a052c;
unsigned char* project_byte(u32 project, u32 offset) { return at<unsigned char>(project + offset); }
unsigned char* scene_byte(u32 project, u32 scene, u32 offset) {
    return project_byte(project, kScenes + scene * kSceneBytes + offset);
}
u32* song_length(u32 project, u32 song) { return at<u32>(project + kSongs + song * kSongBytes + kSongLength); }
}

bool extension_read_arrangement(extensions::Arrangement& out) {
    const u32 project = word(kProject);
    if (!project) return false;
    out.current_song = extension_song();
    for (u32 s = 0; s < arr::scenes; ++s) {
        for (u32 t = 0; t < arr::tracks; ++t) {
            out.scene[s].pattern[t] = *scene_byte(project, s, t);
            out.scene[s].mute[t] = *scene_byte(project, s, arr::tracks + t);
        }
        out.scene[s].present = *scene_byte(project, s, 2 * arr::tracks) != 0;
    }
    for (u32 g = 0; g < arr::songs; ++g) {
        for (u32 e = 0; e < arr::entries; ++e) out.song[g].entry[e] = *project_byte(project, kSongs + g * kSongBytes + e);
        out.song[g].length = *song_length(project, g);
    }
    return true;
}

extensions::Write extension_write_arrangement(const extensions::ArrangementWrite& w) {
    using extensions::Write;
    const u32 project = word(kProject);
    if (!project) return Write::refused;
    if (stock_playing()) return Write::playing;
    const u32 length = *song_length(project, w.song);
    if (length > 1 || (length == 1 && *project_byte(project, kSongs + w.song * kSongBytes))) return Write::song_taken;
    for (u32 i = 0; i < w.new_count; ++i)
        if (*scene_byte(project, w.new_slots[i], 2 * arr::tracks)) return Write::scene_taken;
    // Held mutes would land after the write; the write replaces them.
    for (HeldMute& h : held_mutes) h.set = false;
    skip_dsp_mute = false;
    // Every scene the performance started from gets its mutes back; the current
    // one through stock's mute, so the DSP board agrees with it.
    const u32 current = extension_scene();
    for (u32 s = 0; s < arr::scenes; ++s) {
        if (!w.before->scene[s].present) continue;
        for (u32 t = 0; t < arr::tracks; ++t) {
            const u8 mute = w.before->scene[s].mute[t];
            if (*scene_byte(project, s, arr::tracks + t) == mute) continue;
            if (s == current) {
                applying_mutes = true;
                stock<void(u32, u32, u32)>(0x8d5934fc)(kController, t, mute);
                applying_mutes = false;
            } else {
                *scene_byte(project, s, arr::tracks + t) = mute;
            }
        }
    }
    for (u32 i = 0; i < w.new_count; ++i) {
        const extensions::SceneRow& row = w.new_scenes[i];
        for (u32 t = 0; t < arr::tracks; ++t) {
            *scene_byte(project, w.new_slots[i], t) = row.pattern[t];
            *scene_byte(project, w.new_slots[i], arr::tracks + t) = row.mute[t];
        }
        *scene_byte(project, w.new_slots[i], 2 * arr::tracks) = 1;
    }
    for (u32 e = 0; e < w.length; ++e) *project_byte(project, kSongs + w.song * kSongBytes + e) = w.entries[e];
    *song_length(project, w.song) = w.length;  // last: the song exists only once whole
    *at<u32>(kChanges) += 1;
    *at<unsigned char>(kChanged) = 1;
    stock<void()>(0x8d55adf0)();  // the pattern screen redraws, as after a MIDI scene change
    return Write::done;
}

extensions::Undo extension_undo_arrangement(const extensions::ArrangementWrite& w) {
    using extensions::Undo;
    const u32 project = word(kProject);
    if (!project) return Undo::refused;
    if (stock_playing()) return Undo::playing;
    if (*song_length(project, w.song) != w.length) return Undo::edited;
    for (u32 e = 0; e < w.length; ++e)
        if (*project_byte(project, kSongs + w.song * kSongBytes + e) != w.entries[e]) return Undo::edited;
    for (u32 i = 0; i < w.new_count; ++i) {
        const u32 slot = w.new_slots[i];
        if (!*scene_byte(project, slot, 2 * arr::tracks)) return Undo::edited;
        for (u32 t = 0; t < arr::tracks; ++t)
            if (*scene_byte(project, slot, t) != w.new_scenes[i].pattern[t] ||
                *scene_byte(project, slot, arr::tracks + t) != w.new_scenes[i].mute[t]) return Undo::edited;
    }
    // No new scene may be current. A selected song is selected again once blank
    // (below), as choosing it on the song page does.
    const u32 current = extension_scene();
    for (u32 i = 0; i < w.new_count; ++i)
        if (w.new_slots[i] == current) return Undo::scene_in_use;
    // The reverse of the write: the song goes first (its length, then entries),
    // so it never names a scene that has gone.
    const extensions::SongRow& song = w.before->song[w.song];
    *song_length(project, w.song) = song.length;
    for (u32 e = 0; e < arr::entries; ++e) *project_byte(project, kSongs + w.song * kSongBytes + e) = song.entry[e];
    for (u32 i = 0; i < w.new_count; ++i) {
        const extensions::SceneRow& row = w.before->scene[w.new_slots[i]];
        for (u32 t = 0; t < arr::tracks; ++t) {
            *scene_byte(project, w.new_slots[i], t) = row.pattern[t];
            *scene_byte(project, w.new_slots[i], arr::tracks + t) = row.mute[t];
        }
        *scene_byte(project, w.new_slots[i], 2 * arr::tracks) = row.present ? 1 : 0;
    }
    if (extension_song() == w.song) {
        // As the song page's song choice (8c0afc78): select it, then reset the
        // sequencer, so nothing still plays the entries just removed.
        stock<void(u32)>(0x8d58586c)(w.song);
        stock<void(u32, u32)>(0x8d5919bc)(kController, 0);
    }
    *at<u32>(kChanges) += 1;
    *at<unsigned char>(kChanged) = 1;
    stock<void()>(0x8d55adf0)();
    return Undo::done;
}
}}
