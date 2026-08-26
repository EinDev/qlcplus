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
