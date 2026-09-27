# functions-advanced domain — notes

> Reconstructed by the coordinator, not the original authoring agent: that
> agent's run was killed by an account spend-limit error right after writing
> `functions-advanced.yaml` but before writing this file. This is a summary
> derived from reading the finished YAML and its inline comments.

## Scope

The 5 `Function::Type` subtypes not owned by `functions-core`: Script,
RGBMatrix, Show (+ Track/ShowFunction sub-resources), Audio, Video. Every
message builds on "the generic function object" (id, name, type,
totalDuration, runOrder, direction, tempoType, speeds, attributes,
blendMode, start/stop/running) owned by `functions-core.yaml` — this
fragment does not redefine start/stop/rename/etc., only type-specific
config and sub-resources.

## Per-type summary (from the method/topic list)

- **RGBMatrix** — algorithm selection (`listAlgorithms`), config
  (fixtureGroupId, algorithm, up to 5 direct hex color slots, controlMode —
  no palette indirection, see functions-core-notes.md), a script-algorithm
  variant with its own inspectable properties (`getScriptProperties`/
  `setScriptProperty`), and `getPreview` — worth confirming in the merged
  spec whether preview returns a static rendered frame or something
  richer (an animated preview would arguably need to be a §4b live/
  subscribable thing, not a one-shot request/response).
- **Script** — `setSource`/`getSource`/`appendLine`/`listCommands`/
  `validate`. QLC+'s own line-command scripting language (distinct from
  RGBScript's JS-based variant, which lives under RGBMatrix). `validate`
  suggests server-side syntax checking before save — confirm its error
  shape matches conventions §7 (INVALID_PARAMS vs. a script-specific code).
- **Show** — the most developed part of this fragment: Track CRUD
  (add/remove/move/rename/setMute/setSolo) and ShowFunction item CRUD
  (add/move/remove/resize/setColor/setLocked), plus timeline-level ops
  `setTimeDivision`, `rippleCutTime`/`rippleInsertTime` (timeline
  ripple-edit, i.e. shifting everything after a cut point — a real editing
  primitive worth double-checking the payload shape covers "which track(s)"
  the ripple applies to). Checked: live playhead position IS covered, via
  `FunctionsShowPlayheadEvent` (topic pattern
  `functions.show.<id>.playhead`, §4b/subscribe-gated, carries elapsed ms)
  - this is the highest-value live-state piece for a transport-bar UI and
  it's present.
- **Audio** — source/volume/duration/device selection,
  `listCapabilities`. Per the original task's scoping instruction, this
  should exclude generic audio-capture-device plumbing (that's either out
  of scope entirely or belongs to `io.*`) — confirm no capture-device
  config leaked in here.
- **Video** — source/geometry/rotation/layer/screenTarget. No mention of
  thumbnail/waveform-provider capabilities in the method list, consistent
  with the original task's guidance that those are local-rendering
  concerns out of scope for a remote API.

## Flags for the merge pass

- `functions.rgbmatrix.getPreview`'s live-vs-one-shot nature (see above) -
  the only remaining open question in this fragment.
- Checked: `functions.audio.setDevice` is an opaque *output*-device
  identifier (playback), no capture-device plumbing leaked in - clean. The
  fragment itself flags that enumerating available output devices is out
  of scope here; that's a real small gap (a client can't populate a device
  picker without it) worth a decision - either add a
  `functions.audio.listDevices` method or note it's covered by a generic
  `io.*` host-capabilities method if one exists (check `io-notes.md`).

## Media provenance and `functions.media.reload` (implemented)

- `FunctionsAudioDetail`/`FunctionsVideoDetail` carry `origin`,
  `originAvailable`, `originChanged` next to `source`/`managed`/
  `importPending`. The values come from the media store's `manifest.json`
  (`MediaAssets::originOf/originAvailable/originChanged`), keyed by the
  stored copy - not from the `.qxw`, whose `<Source>` element is unchanged.
- `originChanged` is cheap unless the origin has the same size and a
  different mtime (then hashed once, verdict cached per size/mtime), so
  `functions.get` on a large re-saved file can cost one full read the
  first time.
- `functions.media.reload` is a structural edit (`baseRevision`, bumps
  `docRevision` only when something was actually re-pointed). One method
  covers both cases: a managed copy is re-imported from its origin, an
  external file is re-probed in place (status `unchanged`). Bulk reload is
  a UI action (Actions menu "Reload changed media"); a client wanting that
  iterates `functions.list` + `functions.get` and calls reload per
  function - deliberately no `functions.media.reloadAll`, matching the
  "fewer general methods" guidance without adding a second mutation shape.
- `functions.media.reloaded` is broadcast from the engine's
  `originReloaded` signal, so it also fires when a queued background copy
  lands later and when the reload was triggered from the QML editors or
  the Actions menu rather than through the API (originClientId null then).

## RGBMatrix: `functions.rgbmatrix.*` (implemented 2026-09-27)

Server: `controlapi/src/domains/apirgbmatrixdomain.{h,cpp}` (tests in
`controlapi/test/apirgbmatrixdomain/`, 18 cases), web UI:
`webui/ff/RgbMatrixEditor.jsx`, e2e driver `webui/tools/e2e/rgbmatrix.js`.

- **The open question above is settled: `getPreview` stays a one-shot
  request/response.** The Qt editor animates in-process on MasterTimer's
  tick; a client animates by polling one frame per step and advancing the
  step itself on the function's own step duration / run order / direction
  (the web UI does exactly RGBMatrixStep::checkNextStep, capped at 10 fps).
  To make that cheap the result gained `step` (the index actually rendered,
  params.step normalised modulo `stepsCount`, negatives wrap) next to the
  specced `stepsCount`/`width`/`height`/`pixels`. Beats tempo has no beat
  feed in a browser; the web UI steps every 500 ms then and says so.
- Rendering goes through a **private `RGBMatrixStep`** (colour delta from
  Color1/Color2 + `updateStepColor` for the step, then
  `RGBMatrix::previewMap`), never the function's own step handler, so a
  running matrix is not disturbed. `stepsCount` is computed fresh from the
  algorithm at the group's current size, not `RGBMatrix::stepsCount()`
  (that cache does not refresh when the group grows through
  `fixtures.group.*`). Pixels are masked to 24 bit (`QColor::rgb()` carries
  0xFF alpha).
- `setConfig` accepts a **partial config** - absent keys, and absent
  algorithm parameters, are left unchanged (documented on the params
  schema). Strict superset of the specced "whole config" shape. Applied in
  the order group -> algorithm -> parameters/script properties -> colours
  -> modes, because `RGBMatrix::setAlgorithm()` and `::setProperty()` both
  read colours back from a script and would clobber colours applied earlier.
  Everything is validated before anything is applied (unknown script or
  group, bad colour string or enum -> `INVALID_PARAMS`/`NOT_FOUND`, nothing
  changed). Re-selecting the algorithm already loaded is a no-op (the Qt
  editor does the same), so a client resending the full config does not
  re-evaluate the script and reset its properties.
- `FunctionsRgbMatrixAlgorithm` gained two **read-only** fields, `name`
  (catalog name) and `acceptedColors` (live `acceptColors()` of the loaded
  algorithm). `acceptedColors` is now `0..5`, not `{0,1,2}`: apiVersion 3
  scripts declare up to 5 (Plasma), and Plasma even flips it between 0 and
  5 from its own "Preset" property - which is why a client should re-read
  the function after `setScriptProperty` (the web UI does).
- `fixtureGroupId` is **nullable**: a `functions.create`d RGB Matrix has no
  group (`FixtureGroup::invalidId()`); the Qt editor silently binds the
  first group when opened, the API deliberately does not.
- `dimmerControl` (legacy flag) is read and written as specced.
- Text `font`: only family/pointSize/bold/italic are applied, onto the
  *existing* `QFont`, so the other attributes a file carried survive a
  setConfig (the schema's "silently drops" caveat no longer applies).
- Not mirrored from `qmlui/rgbmatrixeditor.cpp`, on purpose: the editor's
  cosmetic recolouring when switching to a non-RGB control mode / Mask
  blend (forces Color1 white, greys picked colours) - the engine's write
  path already converts through `rgbToGrey()`, so output is identical; and
  "Save to Sequence" (`RGBMatrixEditor::saveToSequence`), a bulk
  Sequence-authoring helper with no method in the fragment - a follow-up
  if wanted.
- Revision: every RGBMatrix setter's `changed()` is turned into
  `Doc::setModified()` (one bump each), plus one explicit bump so parameter
  edits on the plain-C++ Text/Image algorithms also advance it - one
  `setConfig` may advance `docRevision` by more than one, like
  `functions.scene.setValues`. Response and event carry the final value.
- Events (`functions.rgbmatrix.configChanged`, `scriptPropertyChanged`)
  are broadcast for API-driven edits only, with the config read back from
  the engine. Edits made in the Qt UI are expected to reach clients through
  the existing `core.history.changed` refetch path (the web UI's
  `useFunctionDetail` refetches on it), not through these topics - not
  verified in this slice.
- Lockable resource type: `function` (nothing new).

## Show: `functions.show.*` (implemented 2026-09-27)

Server: `controlapi/src/domains/apishowdomain.{h,cpp}` (tests in
`controlapi/test/apishowdomain/`, 20 cases over a real ApiServer/QWebSocket
with MasterTimer running), web UI: `webui/ShowManager.jsx`, e2e driver
`webui/tools/e2e/show.js`. Everything in the fragment's Show section is
registered; behaviour notes and the few additive deviations:

- **typeDetail** (`FunctionsShowDetail`) gained `totalDuration`, and every
  `FunctionsShowItem` carries read-only `functionType`/`functionName` (absent
  when the placed Function no longer exists). `color` is always a string: an
  item saved without a colour reports `ShowFunction::defaultColor(type)`, the
  colour the Qt editor draws it with. Tracks are in `Show::tracks()` order
  (ascending id).
- **`track.move` swaps track ids** (that is what `Show::moveTrack` does):
  after a move the moved Track answers to the other track's id. The result
  and the `tracksChanged` event carry `trackId` = the moved track's *new* id,
  and the event's patch is one `replace` of `/tracks`. A move at the top/
  bottom edge is `INVALID_PARAMS`, not a silent no-op. `track.setSolo` uses
  the same whole-array `tracksChanged` patch.
- **Overlaps** are refused with `INVALID_PARAMS` exactly where the Qt editor
  refuses them (`ShowManager::checkOverlapping`: half-open intervals, an item
  whose Function is gone blocks nothing), plus `error.details`:
  `item.move` -> `{blockingItemId, suggestedStartTime}` (the nearest free
  spot from `ShowMoveHelper::resolveCollision`, now in `engine/src` so both
  front ends share it - the web UI drops there, like the QML drag does),
  `item.resize` -> `{blockingItemId, maxDuration}`. `item.add` refuses an
  overlapping spot the same way (the Qt drop path does not check - it relies
  on the later drag to resolve - so this is stricter than the desktop). Locked
  items refuse `move`/`resize` and are skipped by the ripples; the minimum
  duration is 1 ms (Time) / 125 ms (Beats) like
  `ShowManager::minimumTimelineDuration`.
- **`item.add`** defaults: `duration` = the Function's `totalDuration()`, else
  5000 ms (Time) / 4000 ms (Beats); `color` = `defaultColor(type)`. Placing the
  Show itself, or any Function that contains it, is `INVALID_PARAMS`.
- **`setTimeDivision`** also sets the Show's own `tempoType` (Time/Beats)
  like `ShowManager::setTimeDivision`, so ShowRunner schedules by beats in a
  BPM Show; `bpm` is only required for the BPM types and is kept otherwise.
  Accepts `functionId` (as specced) or `showId`.
- **Ripples** (`rippleInsertTime`/`rippleCutTime`) are a host-free mirror of
  `ShowManager::insertTimeAtCursor`/`cutTimeAtCursor` (same order, same
  per-type rules incl. the Chaser Common->PerStep conversion and step
  surgery), without Tardis - API edits are not undoable. The result carries
  `changed`; nothing at the cursor is `changed:false` with no revision bump
  and no event (the Qt buttons do nothing then too). `itemsChanged` carries
  per-field `replace` ops (`/tracks/<t>/items/<i>/startTime|duration`) for
  the items that moved or grew; Chaser step edits made by a ripple are not
  announced as `functions.chaser.stepsChanged` (a client re-reads the Chaser
  if it shows it).
- **Revision**: nothing in `ShowFunction`/`Track` bumps `docRevision`, so
  every mutation calls `Doc::setModified()` once (a Chaser step edit inside a
  ripple may add more bumps, the response carries the final value).
- **Playhead** `functions.show.<id>.playhead` is the one subscribe-gated
  topic of this section: relayed from `Show::timeChanged` (every MasterTimer
  tick while the Show runs, any starter - API, VC, desktop), throttled to one
  event per 100 ms per Show; the first tick after a start and any jump
  backwards (seek) are always sent; `originClientId` is null. Stopping is
  reported by `functions.status.changed`, not by a final playhead event.
- **Play from the cursor** is `functions.start` with the new optional
  `startTime` (functions-core.yaml); see its notes.
- Not implemented (no method in the fragment, desktop-only today): the
  preview-at-cursor scrub mode (`Show::setScrubMode`/`requestSeek`), the
  stretch-resize mode, track Spout output size, the legacy beat-pseudo-count
  conversion (ADR 0001), waveform/beat-grid data for Audio items.
- Edits made through the API reach a desktop Show Manager that has the same
  Show open only through the engine (a running Show reschedules; the QML
  view refreshes its duration via `scheduleChanged` but does not re-render
  the moved/added items until the Show is reopened) - the same gap the other
  domains have, not new here.
- Lockable resource type: `function` (nothing new).
