#pragma once
#include <opxy/types.hpp>

namespace opxy { namespace extensions {

// Opaque project/pattern identities are only for equality, never dereferencing.
// Positions and lengths use stock pattern ticks (480 per step at scale 1).
struct RecordingContext {
    u32 project, pattern, track, length, scale, quantization;
    // The selected track's pattern can be edited: a project, an instrument
    // track, a pattern, a length and a supported scale.
    bool editable;
    // Live notes on it can be recorded: editable, and the recorder is not in a
    // step or sample workflow.
    bool recordable;
    bool operator==(const RecordingContext& other) const {
        return project == other.project && pattern == other.pattern &&
            track == other.track && length == other.length && scale == other.scale &&
            quantization == other.quantization && editable == other.editable &&
            recordable == other.recordable;
    }
};
// record_armed: stock's record arm (Rec + Play, or recording while playing)
// is on; plain Play then starts the transport recording.
struct Context { RecordingContext recording; bool playing; bool record_armed; };
struct Note { i32 position; u32 length; u8 pitch, velocity; };
struct LiveNote { u32 track, pitch, velocity; i32 position; u32 collision_radius; };
struct NoteOff { u32 track, pitch; };
struct Tick { u32 delta_q8; bool discontinuity; };
using Time = __UINT64_TYPE__;
// The current scene's longest pattern ended while playing, and stock chose the
// scene that plays next. Scenes, songs and song entries count from 0.
enum class SceneCause : u8 {
    kept,    // no song and nothing queued: the same scene plays again
    queued,  // a scene picked while playing (scene screen or MIDI) takes over
    song,    // the active song's next entry
};
const u32 no_song = 0xffffffffu;
struct SceneBoundary {
    u32 previous, scene;  // the scene that ended, and the one that starts
    SceneCause cause;
    // The active song and, for cause song, the entry that starts; otherwise
    // no_song and 0. Stock moves its own song position on before the
    // boundary ends, so read the entry here, not from a later event.
    u32 song, entry;
};
// A track's mute or unmute was pressed, before stock applies it. Mutes belong to
// the current scene: stock writes them into it, and the next scene switch loads
// that scene's own mutes.
enum class MuteSource : u8 {
    keys,   // Instrument or Aux + 1-8, or MIDI
    mixer,  // the mixer page
};
struct TrackMute {
    u32 track;  // the scene's mute slot, 0-15
    bool muted;  // what the press asks for
    MuteSource source;
    // A mute is already held for this track: this press cancels it, nothing
    // changes, and the subscribers' answers are ignored.
    bool cancels_held;
};
// A track_mute subscriber's answer. While playing, the SDK holds the mute if any
// subscriber asks: it applies it after the next scene boundary, before that
// boundary's event, or at once when the transport stops. Stopped, mutes always
// apply at once.
enum class MuteTiming : u8 { now, next_boundary };
// A scene chosen on the arranger page with Shift + 1-0, which stock switches to
// at once (its Shift + Play cue waits instead, and is not reported).
struct SceneSelect {
    u32 scene;  // 0-98
};
// A scene_select subscriber's answer. While playing, the SDK turns the switch
// into stock's own cue if any subscriber asks: the scene starts at the next
// boundary, as Shift + Play then the scene would. Stopped, switches are at once.
enum class SceneTiming : u8 { now, next_boundary };
// Key numbers as stock reports them in queued controls.
namespace keys { const u32 record = 50, play = 51, stop = 52; }
struct Control {
    u32 key;
    bool pressed;
    // Stock took the event as input: input was enabled and, for Record, stock
    // registered the press or release.
    bool accepted;
    // No other key and no Shift held when the event arrived.
    bool unmodified;
    Time time;
    // The claim this event fired for the receiving extension (its name in
    // profiles/extensions.json), or nullptr. A claim that replaces stock keeps
    // the key from stock: stock never saw this event.
    const char* claim;
};

// The project's scenes and songs (read_arrangement, write_arrangement). Scenes,
// songs, tracks and patterns count from 0.
namespace arrangement {
const u32 scenes = 99, songs = 14, entries = 96, tracks = 16, patterns = 9;
const u8 muted = 2, unmuted = 0;
}
struct SceneRow {
    u8 pattern[arrangement::tracks];  // each track's pattern
    u8 mute[arrangement::tracks];     // arrangement::muted or ::unmuted
    bool present;                     // false: an empty slot
};
struct SongRow {
    u8 entry[arrangement::entries];  // scenes, in order
    u32 length;                      // 0: an empty slot
};
// Stock's blank song: no entries, or the single scene-1 entry stock gives songs
// 2-10 of every new project. write_arrangement writes only into a blank song.
inline bool blank(const SongRow& song) { return !song.length || (song.length == 1 && !song.entry[0]); }
struct Arrangement {
    u32 project;  // the read's project identity (RecordingContext::project)
    u32 current_song;  // stock's selected song, or no_song
    SceneRow scene[arrangement::scenes];
    SongRow song[arrangement::songs];
};
// What write_arrangement did.
enum class Write : u8 {
    done,
    refused,       // not allowed here, or no longer the project that was read
    playing,       // the transport is running
    song_taken,    // the song slot is not empty
    scene_taken,   // a new scene's slot is not empty
    invalid,       // the plan breaks a rule (a row, an entry, a length)
};
// What undo_arrangement did.
enum class Undo : u8 {
    done,
    refused,       // not allowed here, or the write is malformed
    playing,       // the transport is running
    edited,        // the song or a new scene no longer holds what was written
    scene_in_use,  // a new scene is the current scene
};
// One planned song, written whole or not at all, while stopped. New scenes go
// into slots empty in `before` and still empty now; the song into a slot
// blank (see blank()) in both. Every scene present in `before` gets its mutes back from it, undoing
// the mutes stock wrote into scenes during a performance.
struct ArrangementWrite {
    const Arrangement* before;  // the read the performance started from
    u32 song;
    const u8* entries;          // each a scene present in `before` or one of new_scenes
    u32 length;                 // 1 to arrangement::entries
    const u8* new_slots;
    const SceneRow* new_scenes;
    u32 new_count;
};
// Each action needs its permission (profiles/extensions.json) and works only in
// a queued_control callback for a key the extension claims, once stock accepted
// it. The SDK validates the context again and owns undo/publication: one stock
// undo transaction per callback. Append new actions at the end.
struct Actions {
    // Add notes to an instrument track's pattern (0-7) beside its existing
    // notes. Returns the inserted prefix length. Needs edit_notes.
    u32 (*insert_notes)(const Context&, u32 track, const Note*, u32 count);
    // Show a short popup (1-31 characters), once per callback. Needs notify.
    void (*notify)(const char* text);
    // Replace an instrument track's pattern notes with these. Refused, changing
    // nothing, while stock is still recording a held note there. Needs edit_notes.
    u32 (*replace_notes)(const Context&, u32 track, const Note*, u32 count);
    // Copy the project's scenes and songs. Needs read_arrangement.
    bool (*read_arrangement)(const Context&, Arrangement&);
    // Write one planned song, or nothing if any part is invalid or no longer
    // fits; while stopped, for the project that was read. Any mute track_mute
    // still holds is dropped first. Says why it refused. Needs write_arrangement.
    Write (*write_arrangement)(const Context&, const ArrangementWrite&);
    // Take back a song write_arrangement made (the same ArrangementWrite):
    // while stopped, if its song and new scenes are still exactly as written
    // and no new scene is current, put the song slot and those scene slots
    // back as `before` held them. A selected song stays selected, now blank,
    // with the sequencer reset. Mutes are left as they are: the write already
    // restored them. Says why it refused. Needs undo_arrangement.
    Undo (*undo_arrangement)(const Context&, const ArrangementWrite&);
    // Turn stock's song playback off, as choosing a scene on the arranger page
    // does, so the current scene plays until a scene is chosen. Opening the song
    // page turns it back on. Returns false outside a project. Needs stop_song.
    bool (*stop_song)(const Context&);
};

using OnContext = void (*)(const Context&);
using OnRecordableNoteOn = void (*)(const Context&, const LiveNote&);
using OnSelectedNoteOff = void (*)(const Context&, const NoteOff&);
using OnRecordingTick = void (*)(const Context&, const Tick&);
using OnInputActivity = void (*)();
using OnQueuedControl = void (*)(const Context&, const Control&, const Actions&);
using OnSceneBoundary = void (*)(const Context&, const SceneBoundary&);
using OnTrackMute = MuteTiming (*)(const Context&, const TrackMute&);
using OnSceneSelect = SceneTiming (*)(const Context&, const SceneSelect&);

// One callback per event, named for what it delivers. Each event lists its
// firmware sites in the profile ("events"); docs/extensions.md says how to
// widen one or propose another. An extension names the events it takes:
//
//     extern constexpr Hooks hooks = Hooks().with_context(context)
//                                           .with_queued_control(queued_control);
//
// All callbacks are synchronous on the controller path, in collection order.
// Arguments are borrowed for the callback. No allocation, blocking or I/O.
// Observers cannot transform live input or consume stock controls; track_mute
// and scene_select are the transforms, and only delay a mute or a scene switch
// to the next boundary. A claim declared with
// `replaces` keeps its key from stock (see docs/extensions.md).
struct Hooks {
    // Selected-track recording context and play state; before every other event.
    OnContext context;
    // A live onset stock accepted for recording, on the selected recordable
    // instrument track, while playing. Sequenced notes and step entry excluded.
    OnRecordableNoteOn recordable_note_on;
    // A live release on the selected track, playing or not; it may have no
    // preceding recordable onset.
    OnSelectedNoteOff selected_note_off;
    // The selected track's scaled recording time, while playing and recordable.
    OnRecordingTick recording_tick;
    // Any live note input, accepted or not, on any track.
    OnInputActivity input_activity;
    // A queued control (Record and other queued keys; not keyboard keys
    // 26-49) after stock handled it. Actions act only for their claim owner.
    OnQueuedControl queued_control;
    // A scene boundary while playing, after stock chose the next scene.
    OnSceneBoundary scene_boundary;
    // A mute or unmute pressed, playing or not; the only transform: its answer
    // can hold the mute until the next scene boundary.
    OnTrackMute track_mute;
    // A scene chosen to play at once (arranger page, Shift + 1-0), playing or
    // not; a transform: its answer can cue it to the next scene boundary.
    OnSceneSelect scene_select;

    constexpr Hooks() : Hooks(nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr) {}
    // Every callback in order; prefer the with_ builders below.
    constexpr Hooks(OnContext a, OnRecordableNoteOn b, OnSelectedNoteOff c, OnRecordingTick d,
                    OnInputActivity e, OnQueuedControl f, OnSceneBoundary g, OnTrackMute h,
                    OnSceneSelect i)
        : context(a), recordable_note_on(b), selected_note_off(c), recording_tick(d),
          input_activity(e), queued_control(f), scene_boundary(g), track_mute(h), scene_select(i) {}

    constexpr Hooks with_context(OnContext f) const {
        return Hooks(f, recordable_note_on, selected_note_off, recording_tick, input_activity, queued_control, scene_boundary, track_mute, scene_select);
    }
    constexpr Hooks with_recordable_note_on(OnRecordableNoteOn f) const {
        return Hooks(context, f, selected_note_off, recording_tick, input_activity, queued_control, scene_boundary, track_mute, scene_select);
    }
    constexpr Hooks with_selected_note_off(OnSelectedNoteOff f) const {
        return Hooks(context, recordable_note_on, f, recording_tick, input_activity, queued_control, scene_boundary, track_mute, scene_select);
    }
    constexpr Hooks with_recording_tick(OnRecordingTick f) const {
        return Hooks(context, recordable_note_on, selected_note_off, f, input_activity, queued_control, scene_boundary, track_mute, scene_select);
    }
    constexpr Hooks with_input_activity(OnInputActivity f) const {
        return Hooks(context, recordable_note_on, selected_note_off, recording_tick, f, queued_control, scene_boundary, track_mute, scene_select);
    }
    constexpr Hooks with_queued_control(OnQueuedControl f) const {
        return Hooks(context, recordable_note_on, selected_note_off, recording_tick, input_activity, f, scene_boundary, track_mute, scene_select);
    }
    constexpr Hooks with_scene_boundary(OnSceneBoundary f) const {
        return Hooks(context, recordable_note_on, selected_note_off, recording_tick, input_activity, queued_control, f, track_mute, scene_select);
    }
    constexpr Hooks with_track_mute(OnTrackMute f) const {
        return Hooks(context, recordable_note_on, selected_note_off, recording_tick, input_activity, queued_control,
                     scene_boundary, f, scene_select);
    }
    constexpr Hooks with_scene_select(OnSceneSelect f) const {
        return Hooks(context, recordable_note_on, selected_note_off, recording_tick, input_activity, queued_control,
                     scene_boundary, track_mute, f);
    }
};
}}
