# functions-core domain — notes

> Reconstructed by the coordinator, not the original authoring agent: that
> agent's run was killed by an account spend-limit error right after writing
> `functions-core.yaml` but before writing this file. This is a summary
> derived from reading the finished YAML and its inline comments.

## Scope

The generic `Function` base API (`functions.list/get/create/delete/rename/
move/start/stop/setPause/tap/adjustAttribute/update`, plus the
`functions.status.changed` live event) applying to all 10
`Function::Type` subtypes, **plus** full type-specific CRUD for Scene,
Chaser, EFX, Collection, Sequence, and the non-Function auxiliary resource
ChannelsGroup. The sibling `functions-advanced.yaml` fragment owns Script/
RGBMatrix/Show/Audio/Video and builds on top of this fragment's generic
base rather than redefining it.

## Scope decisions called out in the YAML's own header comment

The header explicitly flags three decisions as needing a look before the
merge finalizes:

1. **CueStack** (`engine/src/cuestack.h`/`cue.h`) — the low-level step-runner
   shared internally by Chaser/Sequence/VCCueList. This fragment appears to
   have modeled Chaser and Sequence each with their own
   `addStep/moveStep/removeStep/replaceStep` methods rather than exposing a
   generic reusable CueStack sub-API — reasonable (keeps the wire API
   resource-oriented rather than mirroring an internal implementation
   sharing detail), but worth a deliberate one-line justification in the
   merged spec since a reader familiar with the C++ engine will wonder why
   CueStack isn't exposed directly.
2. **Palettes/color filters**: `functions.scene.*` payloads reference
   `palettes` as an array of opaque IDs (`Scene::palettes()`) but explicitly
   do **not** define CRUD for palette/color-filter *definitions* themselves
   (see the `palettes` field descriptions in `functions-core.yaml` around
   the Scene value schemas) — this is a real gap for "100% of the
   functionality" (qmlui's `palettemanager.cpp`/`colorfilters.cpp` clearly
   manage these as first-class editable resources) that isn't covered by
   *any* of the seven domains as dispatched. Flag this to the repo owner:
   either it needs a small follow-up fragment/domain, or an explicit,
   deliberate scope cut for v1.
3. **ChannelsGroup live-vs-doc level — confirmed, and it's the *other* way
   round from what this header comment guessed.** `functions.channelsgroup.setLevel`
   actually requires `baseRevision` just like `create/delete/rename/setChannels/
   setInputSource` — the whole ChannelsGroup surface, including the master
   level, is modeled as full §4a document state (matches the engine: the
   level is persisted via `KXMLQLCChannelsGroupValue`, not ephemeral). This
   is a real design tension worth flagging rather than a bug to fix: a VC
   slider bound to a channels-group level would, per the §4a contract, need
   a fresh `baseRevision` for every drag tick and risk `CONFLICT` responses
   under fast dragging or concurrent editors — the same tradeoff a genuine
   §4b live-fader design would avoid. Left as §4a here because the engine's
   own persistence model leaves no alternative without inventing an
   unpersisted live-preview value the engine doesn't have; a future revision
   could add a separate, explicitly-unpersisted "live preview" message if
   this becomes a real usability problem.

## Other flags for the merge pass

- `functions.status.changed` (running/elapsed state) is the obvious
  subscribe-gate (§5) candidate if it fires per-tick during playback for
  every running function - confirm the emission frequency assumption in
  `functions-core.yaml` (or add the caveat in the merged spec if unclear)
  rather than assuming.
- Checked: `functions-advanced.yaml`'s RGBMatrix uses direct hex color
  slots (`colors: [Color1..Color5]`), not palette references, so the
  palette gap above is specific to Scene and doesn't recur there.

## Implemented 2026-09-26: run-state feed, stopAll, pause alias (web UI slice)

Server: `controlapi/src/domains/apifunctionsdomain.cpp`.

- `FunctionsSummary`/`FunctionsDetail` carry `running`/`paused`
  (additive); `functions.status.changed` is broadcast ungated from
  `MasterTimer::functionStarted/functionStopped` (any source) and from a
  new engine signal `Function::pauseChanged(id, paused)` (added in
  `engine/src/function.cpp` because the engine had no pause notification).
  Data carries both `id` and `functionId`, plus `elapsed`; the spec's
  optional `elapsedBeats`/`attributes`/`currentStepIndex`/
  `runningStepsNumber` are still not implemented.
- `functions.stopAll` and `functions.pause` (alias of `functions.setPause`
  with `{id, paused}`) added; every `functions.*` method accepts `id` as
  an alias of `functionId` (string or number).
- Deviation: `functions.stopAll` blocks until MasterTimer has flushed its
  list (one or two ticks), exactly like the toolbar action; on a headless
  server without a running MasterTimer it would spin - not a supported
  configuration.

## Implemented 2026-09-27: functions.collection.* and functions.efx.* (web UI slice)

Server: `controlapi/src/domains/apiefxcollectiondomain.cpp` (one domain for both
types), tests in `controlapi/test/apiefxcollectiondomain/`. Registers the
`functions.get` typeDetail providers for Collection and EFX through
`ApiFunctionsDomain::setTypeDetailProvider()`.

- `functions.collection.addFunction` / `removeFunction` / `setMembers` as specified.
  Rejections (all `INVALID_PARAMS`): self-membership, duplicates, and any member
  whose `Function::contains()` already reaches the collection (Collection, Chaser
  and Show override it, so every nesting type the engine has is walked).
  `setMembers` validates the whole list before touching anything. `removeFunction`
  of a non-member is `NOT_FOUND`. Event `functions.collection.membersChanged`
  carries the full member list.
- `functions.efx.setParameters` / `addFixture` / `removeFixture` /
  `setFixtureParameters` / `reorderFixture` as specified, plus two additions:
  `functions.efx.setFixturesOffset` (the Qt editor's "Set an offset on all
  fixtures" popup, Absolute / Increasing / Random) and `functions.efx.getPreview`
  (read-only: the 512-point pattern polygon in 0-255 space plus each head's start
  index and walking direction, i.e. `EFXEditor::algorithmData` / `fixturesData`;
  `includeFixturePaths` adds `EFX::previewFixtures()` per head). `addFixture`
  gained `allHeads` (add every head of the fixture, what dropping a fixture on the
  Qt editor does) because `fixtures.list` summaries carry no head count for a
  client to offer a per-head picker. `FunctionsEfxDetail` gained `algorithms`
  (`EFX::algorithmList()`) and `docRevision`; `FunctionsEfxFixture` gained
  `availableModes` (`EFXFixture::modeList()`, mapped Position->PanTilt).
- Revision bumping: the engine is uneven (Collection add/remove and most EFX
  setters emit `changed()` -> `Doc::setModified()`; `EFXFixture`'s setters,
  `EFX::setDimmerControlEnabled()` and `EFX::removeFixture(id, head)` do not), so
  every mutation compares `docRevision` before/after and calls
  `Doc::setModified()` itself when nothing bumped it. A multi-parameter
  `setParameters` may bump more than once; the response carries the final value.
- `removeFixture` frees the `EFXFixture` when the EFX is not running (the engine
  never does; the Qt editor keeps the pointer for undo). While running it is
  leaked like the Qt editor does, rather than freed under MasterTimer's feet.
- Duplicate heads are rejected here (`EFX::addFixture()` itself never does - its
  own `@todo`). `head` is range-checked against `Fixture::heads()`; a fixture
  without a mode counts as one head (`Fixture::heads()` dereferences the mode
  unguarded).
- Engine flag, not fixed: `EFX::removeFixture(quint32, int)` neither emits
  `changed()` nor frees the object; only the `EFXFixture*` overload emits.

Web UI: `webui/ff/EfxEditor.jsx`, `webui/ff/CollectionEditor.jsx`; e2e driver
`webui/tools/e2e/efx-collection.js`. Found while driving it: the design-system
`CustomComboBox` (`_ds_bundle.js`) never closes on an outside click, so a stale
open dropdown swallows the next click that lands on it - left as is (bundle
file), noted for the next re-import.
## Implemented 2026-09-27: `functions.start` `startTime` (Show Manager slice)

`FunctionsStartRequest.params.startTime` (optional, ms, default 0) is passed
straight to `Function::start()`'s `startTime`. A Show played with it starts
its timeline there (what `ShowManager::playShow` does with the cursor), Audio/
Video seek into the file, a Chaser starts on the step covering the offset;
Scene/EFX/RGBMatrix ignore it (they have no notion of an offset). Verified by
`controlapi/test/apishowdomain`'s playhead case (the first playhead event of
a Show started at 1000 ms is past 1000 ms).

## Implemented 2026-09-27: function-side leftovers (server `apifunctionsmiscdomain.cpp`)

- `functions.chaser.setSpeedModes` (4a): partial, each of fadeInMode / fadeOutMode /
  durationMode optional; broadcasts `functions.chaser.changed` only when a mode changed.
  Works on a Sequence too (Sequence is-a Chaser).
- `functions.chaser.setAction` (4b ack): nextStep / previousStep / setStepIndex / stopStep /
  pause. On a stopped Chaser the engine keeps the action as its startup action (the next
  start begins there) - that is how the Qt editor previews a step, too. stepIndex is
  range-checked (INVALID_PARAMS).
- `functions.sequence.setBoundScene` (4a): also re-points every step's fid to the new Scene
  (the engine setter alone would leave the steps on the old one); values are kept.
- `functions.sequence.applyDumpValues`: additive `captureLive: true` (values then optional)
  snapshots the live pre-GM output of the bound Scene's channels server side - the web UI has
  no other way to read preGM values. The result also carries `stepIndex` (where the values
  landed) and `capturedChannels`. Because `Sequence::applyDumpValues()` re-normalises every
  step, the `functions.sequence.stepsChanged` patch is one `replace` of `/steps`.
- `functions.adjustAttribute` (4b ack): resolves by name or index, clamps to the attribute's
  own min/max, broadcasts the NEW `functions.attributeChanged` so every open header follows.
- `functions.tap`: `Function::tap()` (meaningful for a running Chaser / Sequence).
- NEW `functions.clone` (4a): FunctionManager::cloneFunctions() semantics (see the message
  description); returns the new ids, one `functions.created` per copy.
- NEW `functions.usage`: Doc::getUsage() pairs + the VC widgets through `ApiVcHost`
  (`vcWidgetsUsingFunction`, the same host method vc.widget.usage uses); `vcAvailable` is false
  when no host is present (tests, headless).
- Function Preview (the Qt editors' "preview" toggle) is `Function::start/stop` with a
  different FunctionParent (FunctionEditor::setPreviewEnabled); nothing else differs, so
  `functions.start` / `functions.stop` are the parity - no `preview` flag was added.
- Tests: `controlapi/test/apifunctionsmiscdomain` (18 cases, also covering the media,
  RGB-matrix and palette additions below).
