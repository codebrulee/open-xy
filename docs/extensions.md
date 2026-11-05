# Firmware extensions

Extensions change how the OP-XY itself behaves: recording, controls, the
sequencer. Machines make sound and draw; extensions observe the firmware's
events and act through stock. Like machines, extensions live in repositories of
their own and reach builds through the catalog
([`catalog/extensions.json`](../catalog/extensions.json)). Capture is the first,
in [open-xy-core-machines](https://github.com/etcetc000/open-xy-core-machines/tree/main/extensions/capture), and the worked example for the next.

An extension is ordinary C++11 compiled into the firmware. It has no firmware
addresses, assembly, platform conditionals or OS headers. The SDK installs each
authenticated hook once and forwards stock input unchanged. This interface is
experimental and source-level; it is not a dynamic loader or binary ABI.

## How the SDK exposes the firmware

Three layers, kept apart:

1. **Patch sites** are private and belong to one OS release: the hooks in
   [`profiles/opxy-1.2.1.json`](../profiles/opxy-1.2.1.json). The SDK owns every
   site, each exactly once; the build rejects two edits that overlap. No
   extension or machine patches the firmware.
2. **Events** are public and outlive an OS release: the callbacks of
   [`Hooks`](../sdk/include/opxy/extension.hpp). Each extension hook in the
   profile lists the events it feeds (`events`); the build checks every hook
   names known events and every event has a site. An OS port moves the sites;
   the events stay.
3. **Subscribers** are extensions and, later, machines. They take events and
   filter them; they never see a site.

### Name an event by where it happens, and by its scope

"Note on" is ambiguous: a note passes several stages, each its own event.

| Stage | Happens when | Event |
| --- | --- | --- |
| Input | A key or MIDI note arrives, before recording and the sequencer | `recordable_note_on` and `selected_note_off` (selected track only); `input_activity` (any track, but without the note) |
| Sequencer | A pattern step plays | Not exposed |
| Scene | The playing scene's longest pattern ends | `scene_boundary`, while playing, after stock chose the next scene |
| Track | Live and sequenced notes reach a track, before its instrument | Not exposed |
| Voice | The instrument starts a voice | A machine's voice callbacks |

An event's name says what it delivers. `recordable_note_on` is not every note
on: it is a live onset stock accepted for recording, on the selected track,
while playing. A subscriber that wants a narrower scope filters; one that needs
a wider scope widens the event (below), never adds a nearby site. The same
holds for time (transport, GUI tick), controls (queued keys, keyboard keys,
encoder edits, page keys) and the project lifecycle (loading, starting a new
project, saving); of these, only queued keys are exposed.

Adjacent sites are still separate sites. The controller update behind `context`
follows stock's GUI tick in the same main loop; a GUI-tick event would be a site
of its own beside it.

### Access: observe, transform or claim

| Access | Rule | Here |
| --- | --- | --- |
| Observe | Read-only; every subscriber, in collection order | Every event |
| Transform | One ordered chain that may change or drop the event | `track_mute` and `scene_select`: any subscriber can hold a mute, or cue a scene choice, until the next scene boundary |
| Claim | One owner per build; a second claimant fails the build | `record_double_tap`, `performance_play`, `performance_stop`, `performance_undo` |

### Thread and host

Each event runs on one thread: audio, GUI and controller, or project load and
save. All current events run on the controller path. A subscriber is either
firmware-wide, as an extension, or hosted by a machine on a track, which
filters to that track for it.

### Four kinds of interface

| Kind | What | Here |
| --- | --- | --- |
| Events | Something happened | The `Hooks` callbacks |
| Actions | Change something through stock | `Actions::insert_notes`, `Actions::replace_notes`, `Actions::notify`, `Actions::read_arrangement`, `Actions::write_arrangement` |
| Surfaces | Add to the screen | None for extensions yet: menu rows, pages, overlays |
| Saved state | Data kept with a project | None for extensions yet |

An event says something happened; an action asks stock to do something. Sending
a note to a track, clearing a pattern or changing a parameter is an action, not
an event. A feature that needs a kind with nothing here yet (a setting the user
chooses, saved state, note input for effect machines) starts with an SDK pull
request for that interface, which the feature then uses.

## Events

Callbacks run synchronously on the controller path, in collection order. Keep
them bounded: no allocation, blocking, I/O or logging. Borrow event references
only for the callback. The SDK does not sandbox native code.

| Event | Delivers | Sites |
| --- | --- | --- |
| `context` | Project, pattern, selected track, length, scale, quantization and play state, and whether its pattern is `editable` and its live notes `recordable` (not while the recorder is in a step or sample workflow); before every other event | Controller update, Stop |
| `recordable_note_on` | A live onset stock accepted for recording, on the selected recordable instrument track, while playing; with stock's pickup position and collision radius | Live onset, accepted-note bookkeeping |
| `selected_note_off` | A live release on the selected track, playing or not; may have no preceding onset | Live release |
| `recording_tick` | The selected track's scaled Q8 recording time, while playing and recordable; a discontinuity asks the subscriber to discard timing | Recorder tick |
| `input_activity` | Any live note input, accepted or not, on any track; for cancelling gestures | Live onset |
| `queued_control` | A queued key (Record and the other queued controls; not keyboard keys 26–49) after stock handled it, with a microsecond time. `accepted`: stock took it as input (for Record, stock registered the press or release); `unmodified`: no other key or Shift held; `claim`: the name of the receiver's claim it fired, or null. Key numbers are in `keys` | Key queue |
| `scene_boundary` | The playing scene's longest pattern ended and stock chose the next scene: the scene that ended and the one that starts, why (`kept`, `queued` or the active `song`), the song and, for a song, the entry that starts. Only while playing; stock's own song position has already moved on | Boundary step, its queued and song scene changes |
| `track_mute` | A mute or unmute pressed (`keys`: Instrument or Aux + 1–8, or MIDI; `mixer`), playing or stopped, before stock applies it: the scene's mute slot (0–15) and whether it mutes. The answer `next_boundary` holds it while playing; the SDK applies held mutes after the next boundary, before `scene_boundary`, or when the transport stops. A press on a track with a held mute cancels it (`cancels_held`) | Mute's scene write and DSP message, the mixer's toggle |
| `scene_select` | A scene chosen on the arranger page with Shift + 1–0, or by MIDI's previous, next or set scene, which stock switches to at once, playing or stopped: the scene (0–98). The answer `next_boundary` turns it, while playing, into stock's own cue (as Shift + Play, then the scene): the scene starts at the next boundary, through the queue `scene_boundary` reports as `queued`. A cued choice (Shift + Play first) and MIDI's queued scene already wait, and are not reported | The arranger's and MIDI's immediate switch: set-scene, the arranger's notice and the playback jump |
| `indicator` | Polled about once a frame on the GUI, with the latest context: what the extension wants shown over every page, `Indicator::none`, `armed` (a record and a pause icon) or `running` (record and play), top right; when running ends the SDK shows record and stop for about two seconds. The strongest any extension asks for is shown. Keep it cheap; it takes no actions | None: the SDK's overlay asks for it |
| `page_left` | The selected stock page changed, playing or not: the page left (`Context::page` is the one now shown), and the first of the receiver's claims limited to pages that covers it, if any. While that claim's owner handles it, the claim's actions are granted, as for its key | The controller update, which compares the selected page with the last one seen |
| `punch_in` | A punch-in key pressed or released, playing or not: a note on aux track 2 (track 9), from that track or Shift + a key on an instrument track. The note (the effect), velocity (0 on release), the playhead in track 9's pattern (480 ticks a step) and that pattern's length. A transform: answering `PunchRecording::skip` to a press keeps it out of stock's recorder (and its undo snapshot) while recording; the effect still plays | Stock's live note start and release (the hooks `recordable_note_on` and `selected_note_off` use), and its record call for a live note (8d595cf8) |

Project and pattern IDs are opaque equality tokens. Lengths use pattern ticks;
scale 1 has 480 ticks per step. Stock's pickup gives negative positions near
pattern zero. Quantization belongs to stock playback; subscribers must not
invent a second grid conversion. Sequenced notes and step entry are not live
onsets.

## Claims and actions

[`profiles/extensions.json`](../profiles/extensions.json) holds the registry: the
SDK's claims and actions. Each extension's `extension.json` lists those it uses.

- **A claim** is an exclusive control on one key: a `label` the site shows and
  the `key` it governs, optionally only on some stock `pages` (up to four)
  and with Record `held` or `released` (`record`; the SDK tracks Record from
  its own key events, so it reads held with Shift down too), and Shift `held`
  or `released` (`shift`). A claim with `replaces` keeps its key from stock
  whenever it fires and its owner is built in; it must name pages, so stock
  keeps the key everywhere else, and Record (50) can be replaced only with
  Shift held, since stock tracks Record's held state from its own key. The
  event tells the owner which claim fired (`Control::claim`). One owner per build; two extensions with the same claim
  cannot be built together, and the site will not offer both. The owner
  recognizes its gesture itself; the shared
  [`DoubleTap`](../sdk/include/opxy/double_tap.hpp) helper handles taps, holds,
  repeats and timeouts on any key without delaying stock controls.
  `record_double_tap` is capture's. The performance recorder's are
  `performance_arm` (Arrange, key 4, with Record held and Shift released:
  stock opens the arranger and the recorder arms its own take; pressing
  Arrange again while still holding Record switches it between a new song and
  an overdub),
  `performance_play` (Play with Record released on the arranger, its aux view
  or the song page: 47, 49, 48), `performance_stop` (Stop without Shift) and
  `performance_undo` (Shift + Stop on those pages, in place of stock's Shift +
  Stop there). Rec + Play stays stock's clip recording; the recorder reads the
  arrangement as a take starts, writes it when it stops, and takes the last
  take back.
- **An action** is permission to change stock state. The runtime grants an
  extension's actions inside its `queued_control` callback for a key it claims,
  once stock accepted the event; an action's `requires` limits that to one
  claim (or, as a list, to any of several), and without it any claim its
  holder owns grants it.
  - `edit_notes` (requires `record_double_tap`) unlocks `Actions::insert_notes`
    (add beside the existing notes) and `Actions::replace_notes` (clear the
    track's notes, then add), on any instrument track whose pattern is
    editable. The SDK checks the caller's context is still current, validates
    the bounded batch, opens one stock undo transaction, keeps stock collision
    and capacity behavior, returns the inserted prefix length and publishes
    once. Replacing is refused, changing nothing, while stock is still
    recording a held note there.
  - `notify` unlocks `Actions::notify`: one short popup per callback.
  - `read_arrangement` (requires `performance_play`) unlocks
    `Actions::read_arrangement`: a copy of the project's 99 scene rows (each
    track's pattern and mute, and whether the slot is used) and 14 songs (their
    scenes in order and length), tagged with the project.
  - `write_arrangement` (requires `performance_stop`) unlocks
    `Actions::write_arrangement`: one planned song, while stopped, for the
    project that was read. New scenes go into slots empty then and now, the
    song into an empty slot, each entry naming a scene that exists or is new;
    patterns 0-8 and mutes 0 or 2 only. Every scene present in the read gets
    its mutes back from it, undoing what stock wrote into scenes during the
    performance, and mutes `track_mute` still holds are dropped. Anything
    invalid or no longer fitting changes nothing. Stock's undo cannot take it
    back.
    With `ArrangementWrite::replace` it rewrites the song the read found
    instead (an overdub), which must still hold exactly that. With
    `ArrangementWrite::patterns` it first adds new pattern slots on one aux
    track (`pattern_track`, 8-15), each the track's next free slot (at most 9
    in all): a copy of slot `from` (its length and scale), emptied as stock's
    own add-pattern does, holding only the given notes. New scenes may name
    them. The written song becomes the selected one, song playback left on or
    off; nothing changes page. `Write::pattern_taken` means the track's slots changed since the plan;
    undo removes the slots again if they are still the track's last and only the
    write's new scenes name them.
    It returns `Write::done` or why it refused: `playing`, `song_taken`,
    `scene_taken` (a slot is no longer empty), `invalid`, or `refused`.
  - `undo_arrangement` (requires `performance_undo`) unlocks
    `Actions::undo_arrangement`: given the same `ArrangementWrite`, while
    stopped and if the song and its new scenes are still exactly as written
    and no new scene is current, it puts that song slot and those scene slots
    back as the read held them (the song first). A selected song stays
    selected, now blank, and the sequencer is reset as when a song is chosen
    on the song page. Mutes stay as they are; the write already restored them.
    It returns `Undo::done` or why it refused: `playing`, `edited`,
    `scene_in_use`, or `refused` for anything else.
  - `stop_song` (requires `performance_play`) unlocks `Actions::stop_song`:
    it turns stock's song playback off, as choosing a scene on the arranger
    page does, so the current scene plays until another is chosen. Opening the
    song page turns it back on. It returns false outside a project or with a
    stale context.
  - `disarm_record` (requires `performance_play` or `performance_stop`)
    unlocks `Actions::disarm_record`: it turns stock's record arm off, as a
    Record press alone does, and if Play has just started the transport armed,
    it plays without recording. The recorder calls it when its armed take is
    cancelled: on Stop, or (through `page_left`) on leaving the arranger page.
    It returns false outside a project or with a stale context.
  - `play_song` (requires `performance_play` or `performance_arm`)
    unlocks `Actions::play_song`: song playback on for the selected song;
    stopped, it is chosen again as the song page does, so Play starts it from
    the top; playing, it carries on from the next boundary. Stock's "no song"
    reads as song 1. It returns false outside a project.
  - `read_pattern` (requires `performance_play` or `performance_stop`)
    unlocks `Actions::read_pattern`: a copy of one pattern slot of an aux track
    (8-15): the track's slot count, the slot's length in ticks (480 a step) and
    its notes. It returns false for a slot the track doesn't hold, outside a
    project or with a stale context.
  - `hold_song_end` (requires `performance_play`) unlocks
    `Actions::hold_song_end`: until the transport stops, a playing song that
    would stop at its end (song flag bit 0) keeps playing its last scene
    instead: song playback turns off as that entry starts, so the scene repeats
    until another is chosen. A looping song still loops. It returns false
    outside a project.

  Calls outside the callback, with a stale context, or a second write or
  popup in one callback are refused.

Observers cannot transform notes or suppress stock input; `track_mute` and `scene_select` may only delay a mute or a scene choice to the next scene boundary.

## Add an extension

An extension is a folder in a repository of its own, beside this SDK checkout:

1. `extension.json`: its `id`, the site's row and details panel (a `name`, a
   one-line `description` saying what it does and how to use it, 140 characters
   at most, and optionally a `note` for its one limit, 100), and the `claims`
   and `actions` it uses from the registry. People scan this; put the full
   behavior in your repository's README.
2. `extension.cpp`, exporting `opxy::extensions::<id>::hooks`, naming only the
   events it takes:
   `extern constexpr Hooks hooks = Hooks().with_context(context).with_queued_control(queued_control);`
   Keep its helpers in the folder.
3. `tests/`: host tests (below). An extension without them is refused.

Check it with `python ../open-xy/sdk.py test --extension <folder>`, and build it
into your own firmware with `--extension <folder>` beside `--machine` on
`compile`, `build` and `kit`. To offer it in the builder, tag a release and open
a pull request that adds its entry to `catalog/extensions.json`: the same
format as [machines](catalog.md#entries), with `"kind": "extension"` and no
`cpu`. Origin follows the catalog: Open-XY's own (`catalog.OFFICIAL_EXTENSIONS`)
show as Core, others as Community, and folders outside the catalog as Yours.

A build's collection lists its extensions after its machines; that order is the
callback order. Native builds include the whole collection. The site's Extensions step follows
Machines, starts with none selected, saves choices with the layout and omits
unselected extensions entirely. The SDK's extension hooks form the profile's
`extensions` service: a build with no extension selected links none of the
runtime and leaves every one of its stock call sites unchanged.

## Master processor

A [master-processor extension](master-filter.md) supplies a stereo audio transform
and an authored panel through the SDK's shared master-processor capability, with one active processor at a time.
It uses the stock selector and the project companion service; it does not
intercept the controller recording events listed above.

## Change the SDK for an extension

Reuse first: read the events table. Most needs are a subscriber that filters an
existing event.

**Widen an event** when it fires too rarely for you: a track other than the
selected one, notes while stopped, keyboard keys. Relax the filter at the
existing site and add fields to its event struct (`LiveNote`, `Control`) that
let every current subscriber keep its behavior: a `selected` or `recordable`
flag it now tests. Do not add a second site for the same stage.

If the event's name stops being true, rename it, and change its parameters if
needed, in the same pull request. Extensions build from source in this
repository, so update every subscriber there, and the profile's `events` tags.
Prefer a new field in the event struct to a new callback parameter. For
example, notes from every track, playing or stopped, widen the onset event to
an `input_note_on` that carries `selected` and `recordable`; capture tests
`recordable`.

**Add an event** only for a stage, thread or kind no event covers: a member of
`Hooks`, its parameter in the full constructor and its `with_` builder (the
build reads the event list from the builders). The pull request states:

- the event's name, by what it delivers and its scope;
- its stage, access (observe, transform or claim), thread and host;
- each firmware site, tagged with `events` and the `extensions` service, and
  how it was found and verified in the stock OS;
- why widening an existing event would not serve.

Extensions name their callbacks, so adding one changes none of them. Renaming
or widening one updates every extension in the same pull request.

**Add a claim** with one registry entry in [`profiles/extensions.json`](../profiles/extensions.json): its
`label` and `key`. Nothing else changes; the runtime reads claims from the
generated registry. Never add a per-claim field to the collection or a
per-claim branch to the runtime.

**Add an action** with a registry entry (and its `requires`, if one claim must
grant it), a bit in
[`extension_runtime.hpp`](../sdk/platform/extension_runtime.hpp) in registry
order (a test keeps them in step), its function appended to `Actions`, and its
stock work in the version adapter,
[`extension_stock.cpp`](../sdk/platform/opxy-1.2.1/extension_stock.cpp), never
in the extension. The runtime already decides when it is granted. An action that drives a stock path the SDK hooks (playing a
note reaches the live-note sites) says how it avoids re-entry: events raised
during a callback are dropped. An action that changes stock state uses one
stock undo transaction. A claim exists only where stock behavior would
otherwise conflict: say what stock already does with that gesture.

## Verify

An extension's tests are its `tests/*.cpp`: each is a host program that returns
0 when its checks pass, built with the SDK headers and the extension's folder
on the include path. Drive the callbacks as the runtime does, with a fake
`Actions` table for the writes (capture's
[`gesture.cpp`](https://github.com/etcetc000/open-xy-core-machines/tree/main/extensions/capture/tests/gesture.cpp) does). `sdk.py test --extension`
and `sdk.py catalog test` run them.

For SDK changes, run `python sdk.py check`, then the full
[native release verification](build.md#release-verification). The SDK's own
tests exercise callback ordering, exclusive claims, the registry, grants from
data, every hook's events, stale-context and unauthorized edits, and the
adapter end to end with stock boundary doubles and the SDK's probe extension
([`tests/fixtures/extension_probe`](../tests/fixtures/extension_probe/extension.cpp)). Device scheduling still requires
testing the exact built image.
