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
