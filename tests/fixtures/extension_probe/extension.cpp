#include <opxy/extension.hpp>

// The SDK's adapter test drives this extension through the 1.2.1 adapter and
// reads what it saw. It is a test fixture, not a shipped extension.
namespace opxy { namespace extensions { namespace probe {
struct Seen {
    u32 contexts, notes_on, notes_off, ticks, activity, controls, boundaries, mutes, selects, punches;
    Context context;
    LiveNote note;
    NoteOff off;
    Tick tick;
    Control control;
    SceneBoundary boundary;
    TrackMute mute;
    SceneSelect select;
    PunchIn punch;
};
Seen seen;
// What an accepted Record release does: 0 nothing, 1 replace, 2 insert, 3 replace track 3.
u32 command, written;
// What Play and Stop do: 0 nothing, 1 read the arrangement on Play and write
// `plan` on Stop, and undo `plan` on the performance_undo claim.
u32 arrangement_command;
bool read_ok, write_ok, undo_ok, stop_ok, disarm_ok, hold_ok;
// Play asks for hold_song_end while set.
u32 hold_command;
// Play turns the selected song's playback on while set.
u32 song_command;
bool song_ok;
// Play reads track 9's pattern slot 0 while set.
u32 pattern_command;
bool pattern_ok;
PatternRead pattern_seen;
// Leaving a page its performance_play claim covers disarms stock's record arm while set.
u32 disarm_command;
Undo undo_result = Undo::refused;
Write write_result = Write::refused;
Arrangement snapshot;
ArrangementWrite plan;
// The probe's answer to every track_mute.
MuteTiming mute_answer = MuteTiming::now;
// The probe's answer to every scene_select.
SceneTiming select_answer = SceneTiming::now;
const Note take[2] = {{-12, 40, 60, 90}, {480, 60, 64, 100}};

namespace {
bool named(const char* claim, const char* name) {
    if (!claim) return false;
    while (*claim && *claim == *name) { ++claim; ++name; }
    return *claim == *name;
}
void context(const Context& c) { ++seen.contexts; seen.context = c; }
void recordable_note_on(const Context&, const LiveNote& n) { ++seen.notes_on; seen.note = n; }
void selected_note_off(const Context&, const NoteOff& n) { ++seen.notes_off; seen.off = n; }
void recording_tick(const Context&, const Tick& t) { ++seen.ticks; seen.tick = t; }
void input_activity() { ++seen.activity; }
void scene_boundary(const Context&, const SceneBoundary& b) { ++seen.boundaries; seen.boundary = b; }
MuteTiming track_mute(const Context&, const TrackMute& m) { ++seen.mutes; seen.mute = m; return mute_answer; }
SceneTiming scene_select(const Context&, const SceneSelect& s) { ++seen.selects; seen.select = s; return select_answer; }
// The probe's answer to every punch_in.
PunchRecording punch_answer = PunchRecording::stock;
PunchRecording punch_in(const Context&, const PunchIn& p) { ++seen.punches; seen.punch = p; return punch_answer; }
u32 pages_left;
PageLeft last_left;
void page_left(const Context& c, const PageLeft& left, const Actions& actions) {
    ++pages_left;
    last_left = left;
    if (disarm_command) disarm_ok = actions.disarm_record(c);
}
void queued_control(const Context& c, const Control& k, const Actions& actions) {
    ++seen.controls;
    seen.control = k;
    if (hold_command && k.pressed && k.key == keys::play) hold_ok = actions.hold_song_end(c);
    if (pattern_command && k.pressed && k.key == keys::play) pattern_ok = actions.read_pattern(c, 9, 0, pattern_seen);
    if (song_command && k.pressed && k.claim) song_ok = actions.play_song(c);
    if (arrangement_command && k.pressed && k.key == keys::play) {
        stop_ok = actions.stop_song(c);
        read_ok = actions.read_arrangement(c, snapshot);
    }
    if (arrangement_command && k.pressed && k.key == keys::stop && !named(k.claim, "performance_undo")) {
        write_result = actions.write_arrangement(c, plan);
        write_ok = write_result == Write::done;
        if (actions.write_arrangement(c, plan) != Write::refused) write_ok = false;  // one write per callback
    }
    if (arrangement_command && k.pressed && named(k.claim, "performance_undo")) {
        undo_result = actions.undo_arrangement(c, plan);
        undo_ok = undo_result == Undo::done;
    }
    if (k.key != keys::record || k.pressed || !command) return;
    const u32 track = command == 3 ? 3 : c.recording.track;
    written = command == 2 ? actions.insert_notes(c, track, take, 2) : actions.replace_notes(c, track, take, 2);
    if (written) actions.notify("probed");
}
}
extern constexpr Hooks hooks = Hooks()
    .with_context(context)
    .with_recordable_note_on(recordable_note_on)
    .with_selected_note_off(selected_note_off)
    .with_recording_tick(recording_tick)
    .with_input_activity(input_activity)
    .with_queued_control(queued_control)
    .with_scene_boundary(scene_boundary)
    .with_track_mute(track_mute)
    .with_scene_select(scene_select)
    .with_page_left(page_left)
    .with_punch_in(punch_in);
}}}
