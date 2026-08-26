# Merge plan — notes before executing the final merge

Repo owner's explicit guidance (verbatim intent): expect a lot of cross-
domain overlap; merge similar requests into one rather than preserving every
domain's bespoke shape; fewer methods overall makes the eventual client
codebase easier to read. This file is the running plan for where that
applies, checked against the actual fragment content (not assumed) before
being written down.

## Confirmed consolidation #1: collapse `functions.<type>.get` into `functions.get`

**Finding** (verified by reading `functions-core.yaml` directly): the
generic `FunctionsGetOkResponse.result` is `FunctionsDetail` — deliberately
base-only (id/name/type/path/runOrder/direction/tempoType/fadeInSpeed/
fadeOutSpeed/duration/totalDuration/blendMode/attributes), with an explicit
comment: *"Type-specific detail (values/steps/geometry/members) comes from
the matching functions.<type>.get call."* Each of the 10 function types then
has its own `functions.<type>.get` (`FunctionsSceneDetail`,
`FunctionsChaserDetail`, etc. — the extension-only fields: e.g. Scene's
`values`/`fixtures`/`fixtureGroups`/`channelGroups`/`palettes`).

This is clean base/extension field separation (no field-level duplication),
but it forces two round-trips to load one function's full picture. Fix:
have the single `functions.get` response embed the type-specific detail
directly — e.g. `result: { ...FunctionsDetail fields..., typeDetail: {
oneOf: [FunctionsSceneDetail, FunctionsChaserDetail, ...] } }`, discriminated
by the already-present `type` field. This removes, outright:

- `functions.scene.get`, `functions.chaser.get`, `functions.efx.get`,
  `functions.collection.get`, `functions.sequence.get` (functions-core.yaml)
- `functions.script.get`, `functions.rgbmatrix.get`, `functions.show.get`,
  `functions.audio.get`, `functions.video.get` (functions-advanced.yaml)

...and their paired `*GetResponse`/`*GetOkResponse` messages and `operations`
entries — roughly 9-10 request/response message pairs removed, replaced by
richer output from the one call that already exists. `functions.list`
correctly stays summary-only (`FunctionsSummary`, lighter weight, used for
list/tree views) — that's normal list-vs-detail API shape, not the kind of
duplication being targeted here; no change needed there.

## Where NOT to over-consolidate

Distinct *mutations* per type (`functions.scene.setValues`,
`functions.chaser.addStep`/`moveStep`/`removeStep`/`replaceStep`,
`functions.efx.setParameters`, etc.) should stay separate, granular
methods — collapsing these into one generic "functions.setConfig(partial
blob)" would (a) lose type safety/schema validation per-operation, which is
half the point of using AsyncAPI/JSON-Schema at all, and (b) work against
the §4a optimistic-concurrency model: smaller, purpose-specific mutations
mean smaller conflict surface (two clients editing different scene channels
concurrently shouldn't conflict; they would more often if forced through
one coarse "replace the whole config" method). The target for consolidation
is redundant **reads of the same conceptual object**, not writes.

## Confirmed consolidation #2: three VC widget types' "named preset list" -> `vc.widget.preset.*`

**Finding** (verified by reading `virtualconsole.yaml` directly, field-by-
field, before touching anything - see the earlier ChannelsGroup near-miss in
consolidation #1's execution for why that discipline matters): VcXyPad,
VcSpeedDial, and VcAnimation each independently built their own preset-list
CRUD (`vc.<type>.preset.add/remove`, `vc.<type>.applyPreset`,
`vc.<type>.presetsChanged`/`activePresetChanged`). Checked each operation's
actual payload rather than assuming from the name match:

- `preset.remove` - **identical** `{widgetId, presetId, baseRevision}` across
  all three, and all three already reused the shared `VcDocRevisionOkResponse`
  rather than defining their own OkResponse. Collapsed to one
  `vc.widget.preset.remove`.
- `applyPreset` (§4b live, no baseRevision) - **identical**
  `{widgetId, presetId}` across all three, all reusing `GenericAckResponse`.
  Collapsed to one `vc.widget.preset.apply`.
- `preset.add` - genuinely **different** payload per type (xyPad: `presetType`
  enum + function/fixtureGroup/head refs; speedDial: `name`+`valueMs`;
  animation: `presetType` enum + color/text/algorithm fields) - real
  variation, not superficial. Collapsed to one `vc.widget.preset.add` using
  the same discriminated-union pattern as consolidation #1
  (`params.preset: oneOf[VcXyPadPresetData, VcSpeedDialPresetData,
  VcAnimationPresetData]`), three new small schemas holding each type's own
  fields. Along the way, found that speedDial's and animation's
  `PresetAddResponse` were already (buggily) pointing at
  `VcXyPadPresetAddOkResponse` instead of having their own - harmless in
  practice since the OkResponse shape (`{docRevision, presetId}`) really was
  identical, but confirms this collapse was the right level of abstraction
  rather than an invented one.

**Explicitly not merged** (checked and found genuinely asymmetric, not just
differently-named): `preset.move`/`preset.rename` (xyPad only, no
equivalent on the other two), `preset.update` (speedDial only). Forcing
these into a shared method would require the API to expose operations two
of the three widget types don't actually support.

Net: virtualconsole.yaml 190 -> 178 messages (-19 removed, +7 added, +3 new
schemas). Script: `_tools/consolidate_vc_presets.py`.

**Flagged, not fixed**: `VcXyPadPresetsChangedEvent`'s and
`VcAnimationPresetsChangedEvent`'s `data` didn't include `docRevision` in
their `required` list, while `VcSpeedDialPresetsChangedEvent`'s did - since
`preset.add`/`preset.remove` are §4a mutations (`baseRevision` required),
the resulting `presetsChanged` broadcast should probably always carry the
new `docRevision` the same way every other structural event in this spec
does. Left as-is rather than risk a rushed fix at the end of this pass -
worth a real look before implementing this domain, not blocking the spec.
Also left the three types' event *topic names* distinct
(`vc.xyPad.presetsChanged` etc.) rather than unifying to one
`vc.widget.presetsChanged` - lower payoff (1 message each vs. 2-3 for
request/response) and topic distinctness is arguably more readable for a
human filtering logs/subscriptions, so didn't force it.

## Confirmed consolidation #3: Chaser/Sequence step CRUD -> `functions.steps.*`

**Finding** (verified by reading `functions-core.yaml` directly, field-by-
field, before touching anything): Chaser and Sequence (Sequence subclasses
Chaser's own step-runner) each independently defined a full step-list CRUD -
`functions.chaser.addStep/removeStep/replaceStep/moveStep` and
`functions.sequence.addStep/removeStep/replaceStep/moveStep`. Checked each
operation's actual `params` shape rather than assuming from the name match:

- `removeStep` - **identical** `{functionId, index, baseRevision}` (same
  `required` list, same property types) across both types. Collapsed to one
  `functions.steps.removeStep`.
- `moveStep` - **identical** `{functionId, sourceIndex, destIndex,
  baseRevision}` across both types. Collapsed to one
  `functions.steps.moveStep`.
- `addStep`/`replaceStep` - params identical *except* the `step` field's
  schema ref: `FunctionsChaserStep` (has `targetFunctionId`, no `values` -
  a Chaser step points at an arbitrary Function) vs. `FunctionsSequenceStep`
  (no `targetFunctionId` - every step implicitly targets the Sequence's
  `boundSceneId` - but has a `values` field Chaser's step doesn't). Read
  both schemas in full: this is real semantic variance, not a coincidence,
  so both got the same discriminated-union treatment as consolidation #1's
  `typeDetail` and consolidation #2's `preset` field:
  `params.step: oneOf[FunctionsChaserStep, FunctionsSequenceStep]`,
  discriminated implicitly by `functionId`'s own type (looked up
  server-side, no separate discriminator field - same pattern as
  `vc.widget.preset.add`'s `widgetId`). No new schemas needed here (unlike
  consolidation #2's `preset.add`) since `FunctionsChaserStep` and
  `FunctionsSequenceStep` were already standalone named schemas, not inline
  objects. Collapsed to `functions.steps.addStep` / `functions.steps.replaceStep`.
- Both types' step-mutation `OkResponse` messages
  (`FunctionsChaserStepsMutationOkResponse` /
  `FunctionsSequenceStepsMutationOkResponse`) were already the *same shape*
  (`{docRevision}` via `$ref FunctionsDocRevisionResult`), just separately
  named - folded into one shared `FunctionsStepsMutationOkResponse`, reused
  by all four merged operations. `functions.sequence.applyDumpValues` (see
  below) also pointed at the old Sequence-specific OkResponse and was
  redirected to the new shared one as part of the same mechanical pass.

**Explicitly not merged** (checked and found genuinely asymmetric or too
divergent to be a redundant read/write of the same conceptual object):

- `functions.sequence.applyDumpValues` has no Chaser equivalent - Chaser
  steps target arbitrary functions, only a Sequence step captures live DMX
  values against its bound Scene. Left as its own method (with its
  `OkResponse` redirected to the new shared one, see above).
- `functions.chaser.setSpeedModes` / `functions.chaser.changed` /
  `functions.chaser.setAction` have no `functions.sequence.*` counterpart
  in the spec at all - `setAction`'s own description already documents that
  it also drives a running Sequence (a shared runtime method living under
  the `chaser.*` name, not a duplicate call to remove).
- `FunctionsChaserStepsChangedEvent` / `FunctionsSequenceStepsChangedEvent`
  are left as distinct topics (`functions.chaser.stepsChanged` /
  `functions.sequence.stepsChanged`) even though their `data` shape
  (`functionId`, `patch`, `docRevision`) is identical - same call already
  made for consolidation #2's `presetsChanged` events: topic distinctness
  reads better for a human filtering logs/subscriptions than the 2-message
  saving is worth, so not forced.
- `functions.audio.setSource` vs `functions.video.setSource` (and their
  `listCapabilities`/`sourceChanged` siblings) - the concrete lead this
  pass was asked to double-check. Superficially similar
  (`functionId` + one string field + `baseRevision`), but checked
  field-by-field: `listCapabilities`' results differ completely (Audio:
  `{extensions}`; Video: `{videoExtensions, pictureExtensions, screens}`),
  and `sourceChanged`'s events differ substantially (Video's carries
  `isPicture`/`detectedResolution`/`videoCodec`/`audioCodec` that Audio's
  event has no equivalent of - Audio's carries only `detectedDurationMs`).
  Real per-type variance, not superficial name-rhyming - left as separate
  methods, no merge.

Net: `functions-core.yaml` 131 -> 122 messages (-18 removed, +9 added: 4
merged Request/Response pairs x 2 + 1 shared OkResponse), 107 -> 99
operations, schemas unchanged (30). `functions-advanced.yaml` untouched (no
genuine duplication found there beyond the Audio/Video pair checked and
rejected above) - still 133 messages, 16 schemas, 98 operations. Combined
`functions-core.yaml` + `functions-advanced.yaml`: 264 -> 255 messages.
Script: `_tools/consolidate_functions_steps.py`.

## Confirmed consolidation #4: `io.patch.{input,output,feedback}.set`/`.remove` -> `io.patch.set`/`io.patch.remove`

**Finding** (verified by reading `io.yaml` directly, field-by-field): three
patch-type namespaces (`io.patch.input.*`, `io.patch.output.*`,
`io.patch.feedback.*`) each independently define a `.set` (attach a plugin
line to a universe) and a `.remove` (detach it) method.

- `.remove` — checked the actual `params` schemas rather than assuming from
  the name match: `io.patch.input.remove` and `io.patch.feedback.remove`
  are **byte-for-byte identical**, `{universeId, baseRevision}` required,
  no other properties — both are singleton patches (a universe has at most
  one input patch and one feedback patch). `io.patch.output.remove` adds
  one extra required field, `index`, because output patches are an
  *indexed array* (`Universe::outputPatch(index)` — a universe can have
  more than one output patch). All three already shared the same
  `IoPatchMutationOkResponse` for their ok-branch (no duplication to fix
  there). Collapsed to one `io.patch.remove`, discriminated by a new
  `patchType` enum (`[input, output, feedback]`), with `index` left
  optional/documented as "required when patchType='output'".
- `.set` — `io.patch.input.set` and `io.patch.output.set`/
  `io.patch.feedback.set` all attach a plugin line to a universe, and are
  close but not identical: `output.set` has the same optional `index`
  slot-array field as `output.remove`; `input.set` has an extra optional
  `profileName` (input-profile attachment — meaningless for an
  output/feedback line). The line-index field itself was even named
  differently per type (`input` vs `output`) despite meaning the same
  thing (a `QLCIOPlugin` line index, see `io.plugin.getLines`). Collapsed
  to one `io.patch.set`, same `patchType` discriminator, line field renamed
  to the neutral `line`, `index`/`profileName` both left optional and
  documented as to which `patchType` they apply to.

Neither of these needed a discriminated-union (`oneOf`) sub-schema the way
`functions.get`'s `typeDetail` or `vc.widget.preset.add`'s `preset` field
did — the per-type variance here is small enough (0-2 extra optional
fields) to express as a flat object with descriptive text, which is exactly
the pattern `io.patch.setParameters` (a generic per-line parameter setter
already using a `patchType` enum with a conditionally-relevant `index`
field, "ignored for input/feedback") already established *in this same
fragment* before this pass touched it. This consolidation is really "apply
`io.patch.setParameters`'s own already-accepted pattern to its two sibling
operations", not an invented abstraction — and since input/output/feedback
patches on a universe already share one `docRevision`-gated
`IoUniverseChangedEvent` broadcast regardless of which patch type changed
(see `IoUniverseChangedEvent`'s description: "Broadcast for
io.universe.update AND for any patch mutation"), merging the request
methods doesn't add any new optimistic-concurrency conflict surface beyond
what already existed — all three patch types were already conflicting
through the same global revision counter and the same change event.

Net: `io.yaml` 108 -> 100 messages (-12 removed, +4 added), operations 91
-> 83 (-12 removed, +4 added). Script: `_tools/consolidate_io_patch.py`.

**Explicitly not touched**: `io.patch.output.setState` (live/§4b
paused+blackout toggle on an output patch) — a different tier of state
from the §4a `set`/`remove` structural mutations above (no `baseRevision`,
not saved to the show file), and it has no equivalent at all for
input/feedback patches (`paused`/`blackout` are meaningful only for an
*output* line being transmitted) — genuinely singleton to output patches,
not a redundant near-miss of `io.patch.output.set`.

### Checked and rejected — `io.yaml`

- **`io.universe.create`/`update`/`delete` vs. per-resource-type CRUD
  elsewhere in this fragment** — checked for the base-vs-type-specific
  split pattern found in `functions.get` (a generic read that a per-type
  read collapsed into). No match: Universe, Patch, Plugin, and
  InputProfile are genuinely distinct resource types in this fragment, not
  N variants of one repeated concept the way Function types or VC widget
  types are, so there is no shared "detail" payload being redundantly
  fetched per type here. Left as-is.
- **`io.simpleDesk.resetChannel` vs. `io.simpleDesk.resetUniverse`** —
  same verb (`reset`), but operate at different granularity
  (`{address}` vs `{universeId}`) with no shared discriminator that would
  make one subsume the other cleanly (a `resetChannel` call addressing "all
  channels in a universe" isn't how the engine models it — `resetUniverse`
  is a distinct `SimpleDesk` operation, not a loop over `resetChannel`).
  Not merged.
- **`io.blackout.set`/`.toggle` vs `io.grandMaster.setValue`/`.setMode`** —
  superficially both "§4b live scalar setters", but different resources
  with no field overlap (`{blackout: bool}` vs `{value: int 0-255}` /
  `{channelMode, valueMode}` enums) and `io.blackout.toggle` legitimately
  returns the resulting `blackout` value directly (`IoBlackoutToggleOkResponse`)
  where `io.blackout.set` just acks (`GenericAckResponse`) since the caller
  already knows the value it set — not a near-duplicate pair, just two
  reasonable minor asymmetries. Not merged.
- **`io.universe.setMonitor` vs. `io.patch.output.setState`** — both are
  §4b live per-universe/per-patch flag setters with a similar shape at a
  glance, but distinct resources (a universe-level monitor toggle vs.
  paused/blackout on one output patch slot) with no field overlap. Not
  merged.
- **Grand Master / Blackout / DMX monitoring / Simple Desk internal
  duplication** — skimmed for any repeated verb pattern within each of
  these smaller sections (5 sub-resources apiece at most); found none —
  each method there does something the others in its section don't.

Overall this fragment had markedly less duplication than
`virtualconsole.yaml` or `functions-core.yaml`/`functions-advanced.yaml`,
confirming the suspicion already noted in the original task prompt:
Universe/Patch/Plugin/InputProfile/GrandMaster/Blackout/SimpleDesk are
mostly genuinely distinct resources, not N variants of one repeated
concept, so there was only the one real seam (`io.patch.*`'s `.set`/
`.remove`) to close.

## Still to check once all fragments are in

- **Virtual Console** (pending the retry agent): 11 widget types is the
  single most repetitive domain by construction. Check for the identical
  `vc.<type>.get` vs generic `vc.widget.get` split pattern found in
  functions-core/advanced, and apply the same fix if present. The original
  task prompt for that domain *did* ask for a shared `vc.widget.*` base for
  structural CRUD, so this may already be handled correctly — verify rather
  than assume, the same way the functions.list summary-vs-detail split
  turned out to already be correct on inspection.
- **Cross-domain "own revision" scheme naming**: `fixturedefs.yaml` and
  `io.yaml` (input profiles) both independently invented a per-domain
  revision counter (`defRevision`/`sessionRevision` vs `profilesRevision`)
  for the same underlying problem (shared library resource, not part of the
  per-show docRevision). Not a method-count issue, but worth unifying the
  *concept* in the merged spec's shared vocabulary/glossary section so it
  reads as one deliberate pattern applied twice, not two ad hoc inventions
  that happen to coincide.
- **Skim for smaller name-only inconsistencies** across all domains once
  merged into one file (e.g. `.rename` vs `.setName`, `.delete` vs
  `.remove`) and normalize to one verb choice repo-wide before finalizing -
  cheap, high-readability-payoff pass, do it last once everything is in one
  document and greppable together.
