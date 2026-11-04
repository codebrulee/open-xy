#include "extension_runtime.hpp"

namespace opxy { namespace platform {
namespace {
extensions::Context current;
const ExtensionEntry* entries;
const ExtensionRegistry* registry;
u32 entry_count;
bool initialized, dispatching;
char notification[32];

// The actions one callback may take, and what it has done with them: one stock
// undo transaction and at most one notification per callback.
struct Transaction { u32 granted; bool attempted, notified; };
Transaction transaction;

bool same_text(const char* a, const char* b) {
    if (!a || !b) return false;
    while (*a && *a == *b) { ++a; ++b; }
    return *a == *b;
}
u32 bits(u32 count) { return count >= 32 ? ~0u : (1u << count) - 1; }

// The collection must agree with the registry, whatever the build checked:
// claims exclusive and known, every action backed by a claim its holder owns.
bool ready() {
    if (initialized) return entries != nullptr;
    initialized = true;
    u32 count = 0;
    const ExtensionEntry* list = extension_collection(count);
    const ExtensionRegistry& known = extension_registry();
    if (!list || count > 16 || known.claim_count > 32 || known.action_count > 32) return false;
    u32 claimed = 0;
    for (u32 i = 0; i < count; ++i) {
        const ExtensionEntry& entry = list[i];
        if (!entry.id || !*entry.id || !entry.hooks || (entry.claims & claimed) ||
            (entry.claims & ~bits(known.claim_count)) || (entry.actions & ~bits(known.action_count)) ||
            (entry.claims && !entry.hooks->queued_control)) return false;
        for (u32 a = 0; a < known.action_count; ++a) {
            if (!(entry.actions & (1u << a))) continue;
            const u32 needs = known.action_claims[a] ? known.action_claims[a] : bits(known.claim_count);
            if (!(entry.claims & needs)) return false;
        }
        claimed |= entry.claims;
        for (u32 j = 0; j < i; ++j)
            if (same_text(entry.id, list[j].id)) return false;
    }
    entries = list;
    registry = &known;
    entry_count = count;
    extension_started();
    return true;
}

bool holds(ClaimHeld wanted, bool held) { return wanted == ClaimHeld::any || (wanted == ClaimHeld::held) == held; }
bool on_page(const ClaimSpec& claim, u32 page) {
    if (!claim.page_count) return true;
    for (u32 i = 0; i < claim.page_count && i < pages_per_claim; ++i)
        if (claim.pages[i] == page) return true;
    return false;
}
bool matches(const ClaimSpec& claim, const extensions::Control& control, const ClaimState& state) {
    return claim.key == control.key && on_page(claim, state.page) &&
           holds(claim.record, state.record_held) && holds(claim.shift, state.shift_held);
}
// The entry's claims this key fires; stock (or, replaced, the key) took it.
u32 fired_claims(const ExtensionEntry& entry, const extensions::Control& control, const ClaimState& state) {
    if (!control.accepted) return 0;
    u32 fired = 0;
    for (u32 c = 0; c < registry->claim_count; ++c)
        if ((entry.claims & (1u << c)) && matches(registry->claims[c], control, state)) fired |= 1u << c;
    return fired;
}
// Actions granted while a claimed key fires.
u32 granted(const ExtensionEntry& entry, u32 fired) {
    u32 result = 0;
    for (u32 a = 0; fired && a < registry->action_count; ++a) {
        const u32 needs = registry->action_claims[a] ? registry->action_claims[a] : entry.claims;
        if ((entry.actions & (1u << a)) && (fired & needs)) result |= 1u << a;
    }
    return result;
}

// The caller's context must still be current: a stale one means the project,
// pattern or selection moved on since the event it acts on.
u32 write_notes(const extensions::Context& expected, u32 track, const RecordedNote* notes, u32 count,
                bool replace) {
    if (!(transaction.granted & action_edit_notes) || transaction.attempted || !notes || !count ||
        count > 120 || track >= 8) return 0;
    bool playing = false;
    const RecordingContext now = extension_context(playing);
    if (!(now == expected.recording) || playing != expected.playing ||
        !(now == current.recording) || playing != current.playing) return 0;
    const TrackPattern target = extension_pattern(track);
    if (!target.editable || !target.length || target.length >= (1u << 23)) return 0;
    for (u32 i = 0; i < count; ++i)
        if (notes[i].pitch > 127 || !notes[i].velocity || notes[i].velocity > 127 ||
            notes[i].position < -240 || notes[i].position >= static_cast<i32>(target.length) ||
            notes[i].length > target.length) return 0;
    transaction.attempted = true; // One undo transaction per callback, including failures.
    return extension_write(track, notes, count, replace);
}
void notify(const char* text) {
    if (!(transaction.granted & action_notify) || transaction.notified || !text) return;
    u32 length = 0;
    while (length < 32 && text[length]) ++length;
    if (!length || length == 32) return;
    for (u32 i = 0; i <= length; ++i) notification[i] = text[i];
    transaction.notified = true;
    extension_notify(notification);
}
u32 insert_notes(const extensions::Context& expected, u32 track, const RecordedNote* notes, u32 count) {
    return write_notes(expected, track, notes, count, false);
}
u32 replace_notes(const extensions::Context& expected, u32 track, const RecordedNote* notes, u32 count) {
    return write_notes(expected, track, notes, count, true);
}
namespace arr = extensions::arrangement;
// The caller's project is the current one, and stock is stopped.
bool same_project(const extensions::Context& expected) {
    bool playing = false;
    const RecordingContext now = extension_context(playing);
    return now.project && now.project == expected.recording.project &&
           now.project == current.recording.project && !playing && !current.playing;
}
bool read_arrangement(const extensions::Context& expected, extensions::Arrangement& out) {
    if (!(transaction.granted & action_read_arrangement)) return false;
    bool playing = false;
    const RecordingContext now = extension_context(playing);
    if (!now.project || now.project != expected.recording.project || now.project != current.recording.project)
        return false;
    if (!extension_read_arrangement(out)) return false;
    out.project = now.project;
    return true;
}
bool valid_row(const extensions::SceneRow& row) {
    for (u32 t = 0; t < arr::tracks; ++t)
        if (row.pattern[t] >= arr::patterns || (row.mute[t] != arr::muted && row.mute[t] != arr::unmuted))
            return false;
    return true;
}
extensions::Write write_arrangement(const extensions::Context& expected, const extensions::ArrangementWrite& w) {
    using extensions::Write;
    if (!(transaction.granted & action_write_arrangement) || transaction.attempted) return Write::refused;
    const extensions::Arrangement* before = w.before;
    if (!before || before->project != current.recording.project) return Write::refused;
    bool playing = false;
    extension_context(playing);
    if (playing || current.playing) return Write::playing;
    if (!same_project(expected)) return Write::refused;
    if (w.song >= arr::songs || !w.entries || !w.length || w.length > arr::entries || w.new_count > arr::scenes ||
        (w.new_count && (!w.new_slots || !w.new_scenes)))
        return Write::invalid;
    if (!extensions::blank(before->song[w.song])) return Write::song_taken;
    bool fresh[arr::scenes] = {};
    for (u32 i = 0; i < w.new_count; ++i) {
        const u32 slot = w.new_slots[i];
        if (slot >= arr::scenes || fresh[slot] || !valid_row(w.new_scenes[i])) return Write::invalid;
        if (before->scene[slot].present) return Write::scene_taken;
        fresh[slot] = true;
    }
    for (u32 i = 0; i < w.length; ++i)
        if (w.entries[i] >= arr::scenes || !(before->scene[w.entries[i]].present || fresh[w.entries[i]]))
            return Write::invalid;
    for (u32 s = 0; s < arr::scenes; ++s)
        if (before->scene[s].present && !valid_row(before->scene[s])) return Write::invalid;
    transaction.attempted = true;
    return extension_write_arrangement(w);
}
// The written song's shape, as write_arrangement checked it; the adapter then
// checks the project still holds exactly what was written.
extensions::Undo undo_arrangement(const extensions::Context& expected, const extensions::ArrangementWrite& w) {
    using extensions::Undo;
    if (!(transaction.granted & action_undo_arrangement) || transaction.attempted) return Undo::refused;
    const extensions::Arrangement* before = w.before;
    if (!before || before->project != current.recording.project) return Undo::refused;
    bool playing = false;
    extension_context(playing);
    if (playing || current.playing) return Undo::playing;
    if (!same_project(expected)) return Undo::refused;
    if (w.song >= arr::songs || !w.entries || !w.length || w.length > arr::entries || w.new_count > arr::scenes ||
        (w.new_count && (!w.new_slots || !w.new_scenes)))
        return Undo::refused;
    bool seen[arr::scenes] = {};
    for (u32 i = 0; i < w.new_count; ++i) {
        const u32 slot = w.new_slots[i];
        if (slot >= arr::scenes || seen[slot] || before->scene[slot].present) return Undo::refused;
        seen[slot] = true;
    }
    transaction.attempted = true;
    return extension_undo_arrangement(w);
}
bool stop_song(const extensions::Context& expected) {
    if (!(transaction.granted & action_stop_song)) return false;
    bool playing = false;
    const RecordingContext now = extension_context(playing);
    if (!now.project || now.project != expected.recording.project || now.project != current.recording.project)
        return false;
    return extension_stop_song();
}
bool disarm_record(const extensions::Context& expected) {
    if (!(transaction.granted & action_disarm_record)) return false;
    bool playing = false;
    const RecordingContext now = extension_context(playing);
    if (!now.project || now.project != expected.recording.project || now.project != current.recording.project)
        return false;
    return extension_disarm_record();
}
const extensions::Actions actions = {insert_notes, notify, replace_notes, read_arrangement, write_arrangement,
                                     undo_arrangement, stop_song, disarm_record};

// The page left, to every page_left subscriber: actions as for a key of the
// first of its claims limited to pages that covers the page left.
void page_left(u32 page) {
    dispatching = true;
    for (u32 i = 0; i < entry_count; ++i) {
        if (!entries[i].hooks->page_left) continue;
        extensions::PageLeft left = {page, nullptr};
        u32 fired = 0;
        for (u32 c = 0; c < registry->claim_count && !fired; ++c) {
            const ClaimSpec& claim = registry->claims[c];
            if ((entries[i].claims & (1u << c)) && claim.page_count && on_page(claim, page)) {
                fired = 1u << c;
                left.claim = claim.name;
            }
        }
        transaction = {granted(entries[i], fired), false, false};
        entries[i].hooks->page_left(current, left, actions);
        transaction = {0, false, false};
    }
    dispatching = false;
}
bool page_seen;
u32 last_page;
}

void extensions_sync() {
    if (dispatching || !ready()) return;
    current.recording = extension_context(current.playing);
    current.record_armed = extension_record_armed();
    current.page = extension_page();
    dispatching = true;
    for (u32 i = 0; i < entry_count; ++i)
        if (entries[i].hooks->context) entries[i].hooks->context(current);
    dispatching = false;
    const u32 left = last_page;
    const bool changed = page_seen && current.page != left;
    page_seen = true;
    last_page = current.page;
    if (changed) page_left(left);
}
void extensions_input_activity() {
    if (dispatching || !ready()) return;
    dispatching = true;
    for (u32 i = 0; i < entry_count; ++i)
        if (entries[i].hooks->input_activity) entries[i].hooks->input_activity();
    dispatching = false;
}
void extensions_recordable_note_on(u32 track, u32 pitch, u32 velocity) {
    if (dispatching) return;
    extensions_sync();
    if (!entries || track != current.recording.track || !current.recording.recordable ||
        !current.playing || pitch > 127 || velocity > 127) return;
    const i32 radius = extension_collision_radius(track);
    if (radius < 0) return;
    const extensions::LiveNote note = {track, pitch, velocity, extension_position(track),
                                       static_cast<u32>(radius)};
    dispatching = true;
    for (u32 i = 0; i < entry_count; ++i)
        if (entries[i].hooks->recordable_note_on) entries[i].hooks->recordable_note_on(current, note);
    dispatching = false;
}
void extensions_selected_note_off(u32 track, u32 pitch) {
    if (dispatching) return;
    extensions_sync();
    extensions_input_activity();
    if (!entries || track != current.recording.track) return;
    const extensions::NoteOff note = {track, pitch};
    dispatching = true;
    for (u32 i = 0; i < entry_count; ++i)
        if (entries[i].hooks->selected_note_off) entries[i].hooks->selected_note_off(current, note);
    dispatching = false;
}
void extensions_recording_tick(u32 delta) {
    if (dispatching) return;
    extensions_sync();
    if (!entries || !current.recording.recordable || !current.playing) return;
    const bool discontinuity = delta > 0x007fffffu;
    const extensions::Tick tick = {discontinuity ? 0 : extension_scaled_delta(current.recording.track, delta),
                                   discontinuity};
    dispatching = true;
    for (u32 i = 0; i < entry_count; ++i)
        if (entries[i].hooks->recording_tick) entries[i].hooks->recording_tick(current, tick);
    dispatching = false;
}
bool extensions_replaces(const extensions::Control& control, const ClaimState& state) {
    if (dispatching || !ready()) return false;
    for (u32 i = 0; i < entry_count; ++i) {
        const u32 fired = fired_claims(entries[i], control, state);
        for (u32 c = 0; c < registry->claim_count; ++c)
            if ((fired & (1u << c)) && registry->claims[c].replaces) return true;
    }
    return false;
}
void extensions_queued_control(const extensions::Control& control, const ClaimState& state) {
    if (dispatching) return;
    extensions_sync();
    if (!entries) return;
    dispatching = true;
    for (u32 i = 0; i < entry_count; ++i) {
        const u32 fired = fired_claims(entries[i], control, state);
        extensions::Control event = control;
        event.claim = nullptr;
        for (u32 c = 0; c < registry->claim_count && !event.claim; ++c)
            if (fired & (1u << c)) event.claim = registry->claims[c].name;
        transaction = {granted(entries[i], fired), false, false};
        if (entries[i].hooks->queued_control) entries[i].hooks->queued_control(current, event, actions);
        transaction = {0, false, false};
    }
    dispatching = false;
}
void extensions_scene_boundary(const extensions::SceneBoundary& boundary) {
    if (dispatching) return;
    extensions_sync();
    if (!entries || !current.playing) return;
    dispatching = true;
    for (u32 i = 0; i < entry_count; ++i)
        if (entries[i].hooks->scene_boundary) entries[i].hooks->scene_boundary(current, boundary);
    dispatching = false;
}
bool extensions_track_mute(const extensions::TrackMute& mute) {
    if (dispatching) return false;
    extensions_sync();
    if (!entries) return false;
    bool hold = false;
    dispatching = true;
    for (u32 i = 0; i < entry_count; ++i)
        if (entries[i].hooks->track_mute &&
            entries[i].hooks->track_mute(current, mute) == extensions::MuteTiming::next_boundary) hold = true;
    dispatching = false;
    return hold && current.playing && !mute.cancels_held;
}
bool extensions_scene_select(const extensions::SceneSelect& select) {
    if (dispatching) return false;
    extensions_sync();
    if (!entries) return false;
    bool hold = false;
    dispatching = true;
    for (u32 i = 0; i < entry_count; ++i)
        if (entries[i].hooks->scene_select &&
            entries[i].hooks->scene_select(current, select) == extensions::SceneTiming::next_boundary) hold = true;
    dispatching = false;
    return hold && current.playing;
}
extensions::Indicator extensions_indicator() {
    using extensions::Indicator;
    static Indicator last = Indicator::none;
    if (dispatching || !entries) return entries ? last : Indicator::none;
    Indicator shown = Indicator::none;
    dispatching = true;
    for (u32 i = 0; i < entry_count; ++i) {
        if (!entries[i].hooks->indicator) continue;
        const Indicator wanted = entries[i].hooks->indicator(current);
        if (wanted == Indicator::running || (wanted == Indicator::armed && shown == Indicator::none)) shown = wanted;
    }
    dispatching = false;
    last = shown;
    return shown;
}
}}
