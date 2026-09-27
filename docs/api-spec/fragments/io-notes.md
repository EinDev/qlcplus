# io domain — notes

> Reconstructed by the coordinator, not the original authoring agent: that
> agent's run was killed by an account spend-limit error right after writing
> `io.yaml` but before writing this file. This is a summary derived from
> reading the finished YAML and its inline comments.

## Scope

Universes (`io.universe.*`), input/output patching (`io.patch.*`), generic
plugin discovery/config (`io.plugin.*`), input profiles (`io.inputProfile.*`),
Grand Master (`io.grandMaster.*`), Blackout (`io.blackout.*`), live DMX
monitoring (`io.dmx.universe.*`), and Simple Desk (`io.simpleDesk.*`).

## Confirmed design decisions (verified by reading the schemas directly)

- **Universes/patches are §4a document-state**, gated on the shared
  `docRevision` (not a domain-local revision) — correct, since they're part
  of the saved show.
- **Grand Master and Blackout are §4b live/runtime state** — no
  `baseRevision`, last-write-wins, matches a real console (`io.grandMaster.
  setValue`/`setMode`, `io.blackout.set`/`toggle`).
- **DMX live monitoring got real design thought**: `io.dmx.universe.get` is
  a one-shot full 512-channel *post*-Grand-Master snapshot (matches
  `Universe::postGMValues()`, i.e. what's actually transmitted, and matches
  what `qmlui/mainviewdmx.cpp` displays) used to seed a baseline; the
  ongoing `io.dmx.universe.<id>.changed` event is **delta-only** (changed
  channels since the last tick with any change), explicitly subscribe-gated
  per-universe (§5) rather than blasting full frames to every client. This
  is exactly the right call for a 44Hz-ish data source - good, no follow-up
  needed here.

## Open questions for the merge pass

- **Input profile revisioning — checked, consistent**: `io.yaml` uses its
  own `profilesRevision` counter for `io.inputProfile.*`, the same pattern
  fixturedefs used for its analogous problem (shared library resource, not
  part of one show). Good, no reconciliation needed. Minor: double check
  `IoInputProfileSaveRequest.params.baseRevision`'s description explicitly
  says it means `profilesRevision` and not the show's `docRevision` - the
  field is generically named `baseRevision` in both schemes, which is fine
  within each domain but worth the merged spec's glossary being explicit
  that "baseRevision" is always relative to *whichever* revision counter
  the resource in question uses, not always the global one.
- **`io.inputProfile.learn.*`** (`start`/`stop` + `io.inputProfile.learn.
  signal` event) — this is "MIDI/OSC learn" (wiggle a physical control,
  engine detects which channel it is and offers to map it). Checked: the
  `IoInputProfileLearnSignalEvent` payload has no session/requester-scoping
  field (just `channelNumber`/`alreadyMapped`, `originClientId` is
  nullable) - as written this looks like a plain broadcast to every
  connected client, not just the one who started the learn session. That's
  a real, minor UX/noise issue (every other client's UI would see
  irrelevant "signal detected" flashes during someone else's learn
  session) worth fixing at merge time, e.g. by adding a `requesterClientId`
  field clients can filter on, or by only emitting to the requester
  server-side.
- **Plugin config — real architecture finding, already flagged by the
  fragment itself**: `io.plugin.configure` is documented in `io.yaml` as "a
  thin passthrough to `InputOutputMap::configurePlugin()`... kept for
  parity with the engine's existing interface, but on today's engine build
  this pops a **native Qt dialog server-side** and is NOT usable from a
  remote Electron client." The fragment's recommended fix is
  `io.patch.setParameters` (a generic key-value parameter set) as the real
  remote-friendly path, with `io.plugin.configure` kept only for
  completeness/parity. **This is a genuine engine-side limitation, not
  just an API-design gap** — several plugins (per this repo's
  `CLAUDE.md`, notably `dmxusb` which needs the FTDI D2XX SDK for USB
  device selection) likely rely on that native dialog for things like
  live hardware enumeration that a flat key-value `setParameters` call
  can't replicate without engine changes to expose the same data
  programmatically. Flag this clearly to the repo owner: full plugin
  configuration parity for "100% of the functionality" may require engine
  work beyond just writing the API spec, for `dmxusb` and possibly others.
- **Simple Desk vs Virtual Console overlap**: `io.simpleDesk.*` includes a
  live per-channel set/reset plus `sendKeypadCommand` (QLC+'s Simple Desk
  has a numeric-keypad command language, e.g. "channel @ level") and a
  `commandHistoryChanged` event - this is a genuinely distinct raw-channel
  surface from the Virtual Console, no overlap concern, just noting it's
  its own thing for the merged spec's prose.
- **`io.simpleDesk.dump` added (2026-08-31)** - the one operation this
  domain was missing: baking Simple Desk's currently-held live values into a
  real, saved Scene Function (`SimpleDesk::dumpDmxChannels`'s wire
  equivalent). Unlike the rest of `io.simpleDesk.*`, this is a §4a
  structural mutation (creates/updates a Function in the project), so it
  carries `baseRevision` and returns `docRevision` like any other
  document-state write. Deliberately does **not** define a new
  `io.simpleDesk.dumped`-style event: the structural side effect is
  reported via the already-specified `functions.created`/`functions.updated`
  events (`functions-core.yaml`) instead, per the repo owner's "prefer
  fewer, more general methods" guidance (README.md) - a client watching
  those two generic events already learns about this the same way it would
  learn about any other new/changed Scene, with no `io.simpleDesk`-specific
  event to special-case. `channelGroups` (an array of `QLCChannel::Group`
  names) replaces `SimpleDesk::dumpDmxChannels`'s raw bitmask parameter for
  the wire format - a JSON client shouldn't need to know QLC+'s internal
  bit layout for channel groups.

## Implemented 2026-09-26: plugins, patches, universe update/delete, profiles (web UI slice)

Server: `controlapi/src/domains/apiiodomain.cpp`; tests load
`engine/test/iopluginstub` as the patchable plugin.

- `io.plugin.list` (with inline `inputLines`/`outputLines`, each line
  carrying both `index` and `line`), `io.inputProfile.list`
  (`profilesRevision` is always 0 - no profile library mutations exist
  yet), `io.patch.set`/`io.patch.remove`, `io.universe.update`,
  `io.universe.delete`. Every patch/universe mutation calls
  `Doc::setModified()` (the engine doesn't on its own, qmlui does it in
  `InputOutputManager`) and broadcasts `io.universe.updated` with the full
  `IoUniverseDetail`; removal broadcasts `io.universe.deleted`
  (`io.universe.created` already existed).
- **baseRevision on the web UI methods (deviation from §4a):** these four
  methods enforce `baseRevision` only when the client sends one (CONFLICT
  with `details.docRevision` on mismatch, as usual). The web UI contract
  lists none for them and requiring it would make them unusable for a
  client written to it; `io.universe.create` keeps requiring it.
- Parameter aliases: `direction`/`patchType`, `plugin`/`pluginName`,
  `profile`/`profileName` - either spelling. Output `index` defaults to 0
  (replace/create the primary patch) rather than the earlier draft's
  "omit = append"; `io.patch.remove` without `index` removes every output
  patch. `profile` absent keeps the current profile, `""` clears it.
- `io.universe.delete`: only the highest id can be deleted (engine rule,
  `INVALID_PARAMS` + `details.deletableUniverseId`), never the last one
  (`INVALID_STATE`), and patched fixtures block it (`INVALID_STATE` +
  `details.fixtureIds`) unless `force: true` - then they are unpatched
  first with a `fixtures.unpatched` event. qmlui deletes them silently;
  the API chose the explicit form.

## Implemented 2026-09-27: plugin lines/rescan/configure, patch parameters and output state, input profile CRUD + learn, GM modes, monitor, audio devices, server-side keypad

Server: `controlapi/src/domains/apiioconfigdomain.cpp` (new domain, all of
the below except the keypad), `apiiodomain.cpp` (`io.simpleDesk.
sendKeypadCommand`, `commandHistory` on `io.simpleDesk.get`, the
`profilesRevision` counter). Tests: `controlapi/test/apiioconfigdomain/`
(32 cases, engine/test/iopluginstub as the patchable plugin). Web UI:
`webui/io/*.jsx` + `InputOutput.jsx` + `SimpleDesk.jsx`, driver
`webui/tools/e2e/io.js` (sandbox `io`, ports 9190/9191).

- **`io.plugin.getLines`** returns `{pluginName, inputs, outputs}` with
  `IoPluginLine` (both `index` and `line`). **`io.plugin.rescan`**: QLCIOPlugin
  has no rescan entry point, so the domain looks for a `Q_INVOKABLE
  rescanWidgets()` (now marked so on `DMXUSB`, no vtable/ABI change for
  already-deployed plugin DLLs) or `rescan()` by name and answers
  `UNSUPPORTED` when neither exists. Line changes arrive via the plugin's
  `configurationChanged()` -> `InputOutputMap::pluginConfigurationChanged`
  -> **`io.plugin.linesChanged`** (null origin, not subscribe-gated).
- **`io.plugin.configure`**: `UNSUPPORTED` when `canConfigure()` is false or
  the host process is not a `QGuiApplication` (nowhere to show a dialog);
  otherwise `InputOutputMap::configurePlugin()` and `{openedOnHost: true}`
  - the plugin's native dialog appears on the machine running QLC+, and a
  modal one keeps the response waiting until it is closed. The web UI
  offers it as a button with that warning; the remote path is
  `io.patch.setParameters`.
- **`io.patch.setParameters`**: `patchType`/`direction` alias, `index` for
  outputs, null value -> `QLCIOPlugin::unSetParameter`, integral JSON
  numbers are handed to the plugin as ints. `Doc::setModified()` (the
  parameters are saved with the patch), `io.universe.updated` with the
  patch's `parameters`, `baseRevision` enforced only when sent (same
  deviation as the other web-UI methods). Result also echoes the plugin's
  current `parameters`.
- **`io.patch.output.setState`**: live only, `io.patch.output.stateChanged`.
  Only API-driven changes are broadcast; a pause toggled in the desktop UI
  is not relayed (no per-patch signal wiring yet - `io.universe.get` is the
  source of truth).
- **`io.inputProfile.get/save/delete`**: `IoInputProfile` gains read-only
  `name`, `path`, `isUser`. `save` writes `<Manufacturer>-<Model>.qxi` (name
  sanitised) into `InputOutputMap::userProfileDirectory()` and updates a
  loaded profile of the same name **in place** (`QLCInputProfile::
  operator=`), so an `InputPatch` holding it keeps working - qmlui's
  `saveInputProfile()` leaves the loaded copy stale. `delete` refuses
  bundled system profiles (`INVALID_STATE`), clears the profile from every
  universe using it first (`io.universe.updated` per universe +
  `Doc::setModified()`; qmlui's `removeInputProfile()` leaves the patch
  pointer dangling) and removes the file. `profilesRevision` (§4c) is
  bumped by both and reported by `list`/`get`; CONFLICT carries
  `details.profilesRevision`. Channel `upperChannel` is NOT served: the
  engine declares it but never defines, saves or reads it. Custom feedback
  (`lowerValue`/`upperValue`/`lowerChannel`) is persisted for Button
  channels only (engine `.qxi` format).
- **Profile directory override**: `InputOutputMap::userProfileDirectory()`
  honours `QLCPLUS_USER_INPUTPROFILE_DIR`. `dev-webui-sandbox.ps1` sets it
  to `C:\qlcsandbox\<Name>\InputProfiles`, the tests to a `QTemporaryDir`,
  so neither ever writes `%UserProfile%\QLC+\InputProfiles`. Verified in
  the e2e run (file lands in the sandbox, the real folder stays untouched).
- **`io.inputProfile.learn.start/stop`**: one session per server, owned by
  the requesting client (`INVALID_STATE` + `details.clientId` for anyone
  else; stop is idempotent for the owner; the session ends when that
  client disconnects). **`io.inputProfile.learn.signal`** is sent to that
  client only (the io-notes scoping issue), with `universeId`,
  `channelNumber` (0-based profile key), `value`, `key`, `alreadyMapped`
  (against `profileName` or the universe's current profile). Input keeps
  flowing to the Virtual Console meanwhile, as in qmlui. The web editor
  computes "already mapped" against its unsaved channel list itself and
  mirrors `InputProfileEditor`'s button->slider promotion.
- **`io.grandMaster.setMode`**: broadcasts `io.grandMaster.changed` from the
  handler (no engine signal) and flags the doc modified. Caveat found: this
  fork's qmlui `VirtualConsole::saveXML` does not write the GM modes (the
  two setters in its load path are commented out too), so the modes are
  runtime-only in practice until that is fixed upstream.
- **`io.universe.setMonitor`**: live only, `io.universe.monitorChanged`.
- **`io.audio.listDevices` / `io.audio.setDevice`** (new in the fragment):
  the host's devices from `AudioPluginCache::audioDevicesList()`, the
  selection in the same QSettings keys qmlui uses (`audio/input`,
  `audio/output`), `Doc::destroyAudioCapture()` after an input change,
  `io.audio.deviceChanged`. Sample rate / channels / buffer size are not
  exposed (follow-up). The e2e driver only reads the list: a set would
  change the developer's real audio setting.
- **`io.simpleDesk.sendKeypadCommand`**: the engine's `KeyPadParser` (one
  instance, remembers the channel selection across commands), optional
  `universeId` (default: the desk universe filter), server-side
  normalisation (`@` -> `AT`, `ENTER` dropped, upper-cased), values applied
  as Simple Desk overrides (`io.simpleDesk.channelChanged` per channel),
  history capped at 10 and shared by every client
  (`io.simpleDesk.commandHistoryChanged`, `history` in the response and
  `commandHistory` on `io.simpleDesk.get`). `accepted` mirrors qmlui: every
  non-empty command is accepted, `channelsChanged` says what it wrote.
  Behaviour change for web users, now identical to the desktop: a bare
  relative command on the remembered selection (`1 THRU 4`, then `+ 10`)
  writes the absolute value 10 - `KeyPadParser` stores the operand instead
  of adding it in that branch (`webui/io/keypad-parser.js` had corrected
  this locally; its header documents the deviation). An engine quirk worth
  fixing upstream, not a regression of the server path.
- Not exercised in the browser (the sandbox has no IO plugins): parameter
  editing, per-output pause/blackout, additional outputs, feedback,
  profile assignment and learn - all unit-tested against the plugin stub.

## Implemented 2026-09-27: dump fixture filter, channel inspection (toolbar / settings slice)

- `io.simpleDesk.dump` gained `fixtureIds` (string or number ids): only those fixtures are dumped,
  combined with `channelGroups` and `nonZeroOnly`; an unknown id answers `NOT_FOUND` before any
  Scene is created. This is PopupDMXDump.qml's "Dump the selected fixture channels"; the earlier
  note that the API has no selection concept is superseded - the client sends its own selection.
  The desktop's "RGB/CMY/WAUV" type box has no filter of its own: colour mixing channels are
  `Intensity`-group channels in QLCChannel.
- New `io.dmx.channel.inspect {universeId, channel}` (ApiToolsDomain,
  `controlapi/src/domains/apitoolsdomain.cpp`): SimpleDesk::debugChannelInfo() as data - fixture and
  channel, pre / post Grand Master value, this API's Simple Desk override, every GenericFader holding
  the channel (`source`: `function` with the owning Function and `Function::sources()` as `startedBy`
  [function / VC widget id / engine MasterId name], `controlApiSimpleDesk`, `desktopTool` with the
  GenericDMXSource `feature`, or `unidentified` for untagged faders: the desktop Simple Desk, VC
  sliders in level mode, CueStacks, Scripts), and `Universe::lastChannelWrite()`. The desktop Simple
  Desk's own held values are not visible to the API (they live in qmlui's SimpleDesk). Read-only,
  not subscribe-gated (one-shot).
- ApiIoDomain names its per-universe faders "Control API Simple Desk" so the inspection can
  attribute them.
- Fixed while testing: the API Simple Desk resolved a fixture channel with `Fixture::address()`
  (the low 9 bits) against the universe-qualified address, so overrides on fixtures in universe 2 and
  up landed on a bogus channel; and a project load (InputOutputMap::loadXML() deletes and re-creates
  every Universe without `universeRemoved`) left ApiIoDomain holding the deleted Universe's fader, so
  every web Simple Desk override silently stopped reaching the output until the universe was reset.
  Both covered by `apiiodomain_test`.

## Implemented 2026-09-27: audio format, input level check, universe thread; Loopback verification

- `io.audio.listDevices` also reports `inputSampleRate`, `inputChannels`, `outputBufferMs`; **NEW
  `io.audio.setConfig`** writes them (PopupAudioConfiguration.qml's choices: 8000..48000 Hz, mono /
  stereo, 10..1000 ms) into the same QSettings keys as InputOutputManager - a default value removes
  the key, a changed input format tears the capture down - and broadcasts **`io.audio.configChanged`**.
  The desktop's own popup is not notified (it re-reads the settings when reopened).
- **NEW `io.audio.inputPreview.set {enabled}`**: the popup's Signal level check. While any client has
  it on, the host's audio input capture runs and each previewing client (only) receives
  **`io.audio.inputLevel {level 0..32767}`** about 20 per second; a disconnect ends that client's
  preview; a device / format change re-opens a running preview on the new input.
- `io.universe.create` now starts the new universe's thread (`startUniverses()`, as
  InputOutputManager does); before, the universe processed nothing until the project was reloaded.
- The web UI sandbox (`dev-webui-sandbox.ps1`) keeps QSettings in `<sandbox>\Settings`
  (`QLCPLUS_SETTINGS_DIR`, qmlui/main.cpp), so `io.audio.setDevice` / `setConfig` can be exercised
  without touching the host's real settings, and `-Plugins loopback` adds the Loopback plugin (the only
  one allowed). Verified with it end to end (webui/tools/e2e/partials-vc.js): several outputs per
  universe, per-output pause (holds the first frame output after the pause - a value changed within
  the same 20 ms tick can be the frozen one) and blackout (zeroes intensity channels only: the engine
  keeps LTP channels in `m_blackoutValues`, like the global blackout), profile assignment, learn, VC
  auto-detection and feedback. Loopback hands feedback back only to the input line with the same
  number and only for the universe that sent it (`Loopback::sendFeedBack`). Plugin line parameters of
  network plugins stay unit-tested (Loopback has none).
