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

## Implemented 2026-09-27: Script, Audio and Video editors (web UI slice)

Server: `controlapi/src/domains/apimediadomain.{h,cpp}` (one domain for the
three types), tests in `controlapi/test/apimediadomain/`. Web UI:
`webui/ff/ScriptEditor.jsx`, `AudioEditor.jsx`, `VideoEditor.jsx` plus the
shared `ServerFileBrowser.jsx` over `core.fs.list` (see core-notes.md).

- **Script is scriptv4 on this build.** `engine/src/scriptwrapper.h` selects
  the JavaScript `Script` (`scriptv4.cpp`, run by `ScriptRunner`) whenever
  `QMLUI` is defined, which `variables.cmake` does globally for a qmlui
  build. Consequences for the spec: `listCommands` returns the
  `Engine.<method>` names (plus `snippets`, the editor's insert-at-cursor
  menu); `validate` evaluates the source in a throwaway `QJSEngine`
  (`ScriptRunner::collectScriptData`, every `Engine.*` call is a no-op while
  the runner is not started) and reports `syntaxErrors: [{line, message}]`
  next to `syntaxErrorLines`; `fixtureRefs` carry no `line` (the v4
  `fixtureList()` reports ids only); `appendLine` runs the v4 legacy-syntax
  converter (`Script::appendData` -> `convertLine`), so a legacy
  `startfunction:3` line is stored as `Engine.startFunction(3);`.
  `functions.get`'s `FunctionsScriptDetail` also carries
  `syntaxErrorLines`/`syntaxErrors`, so an editor can mark lines without a
  second call. Pre-existing bug fixed on the way: `apifunctionsdomain.cpp`
  included the legacy `script.h` while the engine DLL compiles `scriptv4` -
  `new Script(doc)` there allocated with the wrong class size; it now
  includes `scriptwrapper.h`. `Script::syntaxErrorsLines()` also leaked a
  `ScriptRunner` per call (its `deleteLater()` was commented out); fixed.
- **Audio.** `setSource` goes through the same media-store import as
  `functions.update {source}` (`ApiFunctionsDomain::applyMediaSource`, now a
  public static) so the copy lands in `<project>.qxw.assets/`; the function
  is renamed after the file exactly like the QML editor's Replace. `setVolume`
  is 0..1 (the QML spinner's 0-100 is a UI conversion), `setDuration` sets
  both `Function::duration` and `Audio::setTotalDuration`. `listCapabilities`
  gained `devices` (output devices, `""` = default) - the fragment's original
  "enumerating devices is out of scope" gap, closed because the editor's
  combo cannot exist without it. `FunctionsAudioConfig` (in `functions.get`)
  carries volume/duration/audioDevice plus read-only `muted`, `bpm`,
  `sampleRate`/`channels`/`bitrate`. No setter for `muted` and no BPM
  re-detect trigger yet (`Audio::setMuted`, `requestBpmDetection(true)`) -
  the web editor shows them read-only and says so.
- **Video.** `setSource` accepts a host path (imported into the store) or a
  `scheme://` URL (kept as-is). `setGeometry` takes `customGeometry` object
  or `null` per the spec; `setRotation`/`setLayer` straightforward.
  `setScreenTarget` only touches `fullscreen` when it differs from the
  current value, so a Spout video is not dropped back to windowed by a
  client unaware of the fork's third mode; the optional `outputMode`
  (`windowed|fullscreen|spout`) sets the mode explicitly, `spout` is
  `UNSUPPORTED` off Windows (`listCapabilities.spoutAvailable`). Detected
  resolution / codecs / duration in `FunctionsVideoConfig` come from
  whatever the engine holds; they are probed asynchronously by qmlui's
  `App` after a source change, so `sourceChanged` carries them only when
  already known. No setter for volume / muted / spoutSize yet (read-only in
  the detail).
- Every setter that does not emit `Function::changed()` itself
  (`Audio::setVolume/setAudioDevice/setTotalDuration`,
  `Video::setCustomGeometry/setRotation/setZIndex`, `Script::appendData`)
  is followed by an explicit `Doc::setModified()`, so `docRevision` moves
  once per call. `Script::setData` bumps it itself, only when the text
  actually changed.
- Events are the ones already specified (`functions.script.sourceChanged`,
  `functions.audio.{source,volume,duration,device}Changed`,
  `functions.video.{source,geometry,rotation,layer,screenTarget}Changed`),
  all ungated. The per-instance `playback` position events are NOT
  implemented (no engine signal carries a playback position for Audio; the
  Video position lives in qmlui's player).
