#pragma once
#include <opxy/extension.hpp>

namespace opxy { namespace platform {
using extensions::RecordingContext;
using RecordedNote = extensions::Note;

// Actions are SDK code, one bit each in profiles/extensions.json's order; a
// test keeps the two in step. Claims are data: the generated registry gives
// each claim's key, and each action the claims that grant it (0: any claim).
const u32 action_edit_notes = 1u << 0;
const u32 action_notify = 1u << 1;
const u32 action_read_arrangement = 1u << 2;
const u32 action_write_arrangement = 1u << 3;
const u32 action_undo_arrangement = 1u << 4;
const u32 action_stop_song = 1u << 5;
const u32 action_disarm_record = 1u << 6;

struct ExtensionEntry {
    const char* id;
    u32 claims, actions;  // Bits in registry order.
    const extensions::Hooks* hooks;
};
// A claim: the key it governs and when (on some stock pages, with Record or
// Shift held or released), and whether stock then never sees the key.
enum class ClaimHeld : u8 { any, held, released };
const u32 pages_per_claim = 4;
struct ClaimSpec {
    const char* name;
    u32 key;
    u8 pages[pages_per_claim];
    u32 page_count;  // 0: any page
    ClaimHeld record, shift;
    bool replaces;
};
// What a claim's conditions are checked against, as the key arrives.
struct ClaimState { u32 page; bool record_held, shift_held; };
struct ExtensionRegistry {
    const ClaimSpec* claims;
    u32 claim_count;
    const u32* action_claims;
    u32 action_count;
};
// Generated with the collection (tooling.extensions.generate_collection).
const ExtensionEntry* extension_collection(u32& count);
const ExtensionRegistry& extension_registry();

// These entry points belong only to the version adapter.
void extensions_sync();
void extensions_input_activity();
void extensions_recordable_note_on(u32 track, u32 pitch, u32 velocity);
void extensions_selected_note_off(u32 track, u32 pitch);
void extensions_recording_tick(u32 delta);
// True when a held claim that replaces stock fires for this key: the adapter
// then keeps it from stock. control.accepted is whether stock would take it.
bool extensions_replaces(const extensions::Control& control, const ClaimState& state);
void extensions_queued_control(const extensions::Control& control, const ClaimState& state);
void extensions_scene_boundary(const extensions::SceneBoundary& boundary);
// Every subscriber sees the press; true when one asks to hold it and stock is playing.
bool extensions_track_mute(const extensions::TrackMute& mute);
// True: cue the scene to the next boundary instead of switching now.
bool extensions_scene_select(const extensions::SceneSelect& select);
// The strongest indicator any extension asks for now (the overlay polls it each
// frame), with the latest context; none before the collection has started.
extensions::Indicator extensions_indicator();

// An instrument track's pattern, as notes are written to it.
struct TrackPattern { u32 pattern, length, scale; bool editable; };
TrackPattern extension_pattern(u32 track);
RecordingContext extension_context(bool& playing);
i32 extension_position(u32 track);
u32 extension_scaled_delta(u32 track, u32 delta);
i32 extension_collision_radius(u32 track);
bool extension_record_held();
bool extension_record_armed();  // stock's record arm (controller +0x305f)
u32 extension_page();  // the stock page selected now (the song screen is 0x30)
u32 extension_write(u32 track, const RecordedNote* notes, u32 count, bool replace);
void extension_notify(const char* text);
// The scene and active song as stock holds them now: scene 0-98, song 0-13 or
// extensions::no_song.
// The project's arrangement, and one validated write of it (the runtime has
// checked bounds and slots against `before`; the adapter checks the project
// still agrees, then writes).
bool extension_read_arrangement(extensions::Arrangement& out);
extensions::Write extension_write_arrangement(const extensions::ArrangementWrite& write);
extensions::Undo extension_undo_arrangement(const extensions::ArrangementWrite& write);
bool extension_stop_song();  // set project +7 bit 4 (8d584284)
bool extension_disarm_record();  // clear the record arm (+0x305f) and Play's copy of it (+0x3061)
// The collection was found valid: the adapter starts what it adds beside the
// events (the indicator's overlay client).
void extension_started();
u32 extension_scene();
u32 extension_song();
}}
