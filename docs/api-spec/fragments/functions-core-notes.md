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
3. **ChannelsGroup live-vs-doc level**: `functions.channelsgroup.setLevel`
   exists alongside the structural `create/delete/rename/setChannels/
   setInputSource` — meaning this fragment treats a channels-group's live
   fader level as §4b (no `baseRevision`) while its membership/definition is
   §4a. Confirm `functions.channelsgroup.setLevel`'s payload indeed omits
   `baseRevision` per that split before finalizing (skim the schema).

## Other flags for the merge pass

- `functions.status.changed` (running/elapsed state) is the obvious
  subscribe-gate (§5) candidate if it fires per-tick during playback for
  every running function - confirm the emission frequency assumption in
  `functions-core.yaml` (or add the caveat in the merged spec if unclear)
  rather than assuming.
- Checked: `functions-advanced.yaml`'s RGBMatrix uses direct hex color
  slots (`colors: [Color1..Color5]`), not palette references, so the
  palette gap above is specific to Scene and doesn't recur there.
