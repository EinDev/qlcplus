# fixturedefs domain — notes

> Reconstructed by the coordinator, not the original authoring agent: that
> agent's run was killed by an account spend-limit error right after writing
> `fixturedefs.yaml` but before writing this file. This is a summary derived
> from reading the finished YAML and its inline comments.

## Scope

Fixture *definition* authoring only (`fixturedefs.*`: channel/capability/
mode/head/alias CRUD, physical properties, save/export) — explicitly not
patching a definition onto a real fixture (that's `fixtures.*`, a different
domain). Mirrors `qmlui/fixtureeditor/`.

## The docRevision-vs-own-revision decision (this domain's big call)

Fixture definitions are a shared library resource reused across shows, not
part of one project's `.qxw`, so `fixturedefs.yaml` does **not** gate its
mutations on the show's global `docRevision` from 00-conventions.md §4a.
Instead it introduces a two-tier scheme, visible in the schemas:

- **`defRevision`** — per-definition (manufacturer+model) revision in the
  shared library cache. `fixturedefs.session.create`/`.open` take a
  `baseDefRevision` to detect if the library copy changed since the client
  last looked.
- **`sessionRevision`** — an in-memory editing session (`fixturedefs.session.*`
  — create/open/close/import/list/forkToUser/setMetadata/setPhysical/
  validate) is a private draft cloned from the library, starting its own
  revision counter at 0. All the channel/mode/capability/alias mutation
  methods operate against a `sessionId`, not the library definition
  directly. `fixturedefs.save` is presumably the point where a session's
  edits get committed back into the library (bumping `defRevision`) — worth
  double-checking `fixturedefs.yaml`'s `FixtureDefsSaveRequest`/
  `FixtureDefsSavedEvent` payloads to confirm this is exactly how it works
  before finalizing the merged spec's prose description of the model.
- There's also a `forkToUser` method (a "Save As" for definitions, likely
  writing to the user's fixture directory rather than the system one, per
  QLC+'s usual manufacturer/model dual-lookup path) - confirm this
  interpretation against `qlcfixturedefcache.h`'s system-vs-user directory
  handling if it matters for the merged spec.

## Other flags for the merge pass

- No topics in this fragment look like they need subscribe-gating (§5) -
  definition editing isn't a high-frequency live-data domain like DMX.
- Lockable resources: a `sessionId` (one user editing one definition) is the
  natural advisory-lock (§6) target here, not the raw manufacturer/model -
  confirm the merge pass wires `locks.acquire` examples/docs accordingly.
- `fixturedefs.channel.capability.alias.*` and `.autoPatchColors` look like
  QLC+-specific conveniences worth a one-line explanation in the merged
  spec's prose (aliasing = reusing one capability set across near-identical
  channels; autoPatchColors likely bulk-assigns color-preset capabilities
  from a colour-wheel definition) - verify against `qlccapability.h`/
  `qmlui/fixtureeditor/aliasedit.cpp` before writing that prose.

## Error codes

- **`FIXTUREDEFS_SYSTEM_READONLY`** — used whenever an operation would
  write to a bundled/system (`isUser: false`) definition's on-disk file.
  Two call sites: `fixturedefs.save` when the session's `isUser` is
  currently `false` (the client must call `fixturedefs.session.forkToUser`
  first, which clones the definition into the user's own fixture directory
  and flips `isUser` to `true` for that session going forward), and
  `fixturedefs.delete` when the target manufacturer/model's `isUser` is
  `false` (there is no "fork and delete" path — deleting a system
  definition is simply unsupported, full stop; see `fixturedefs.delete`'s
  description in the fragment). This mirrors the real editor's own
  constraint: `EditorView::save()` refuses to overwrite a definition that
  didn't come from the user's fixture path, and there's no delete UI for
  bundled fixtures at all in `qmlui/fixtureeditor/`.
- **`FIXTUREDEFS_ACTS_ON_SELF`** (used by `fixturedefs.mode.setChannels`) —
  self-explanatory from its inline description in the fragment; listed here
  only so both domain-specific error codes are discoverable from one place.

## Why base64-in-params for QXF import/export

`fixturedefs.session.import` and `fixturedefs.export` carry the raw `.qxf`
XML file as a base64 string inside the JSON `params`/`result`, rather than
a separate binary upload/download channel. Reasoning: this API is a single
WebSocket connection with one JSON message framing for everything else
(00-conventions.md §2) — introducing a second transport (e.g. a companion
HTTP endpoint for file bytes) just for these two operations would mean
every client needs two connection types instead of one, for a payload class
(one XML fixture definition file) that's realistically a few KB to a few
tens of KB, never large enough that base64's ~33% size overhead matters in
practice. If bulk-import of many files at once ever becomes a real use case,
that's the point to reconsider a dedicated upload path — not needed for the
current one-file-at-a-time `FixtureEditor`-mirroring scope.

## Why synthetic ids for channels/modes instead of name-based addressing

The engine itself addresses `QLCChannel`s and `QLCFixtureMode`s within a
`QLCFixtureDef` by **name** (`QLCFixtureDef::channel(const QString &name)`,
`mode(const QString &name)`) — there's no engine-level numeric or UUID
channel/mode id. `fixturedefs.yaml` deliberately does NOT mirror that:
every channel/mode-scoped method here takes a server-assigned `channelId`/
`modeId` string instead. Reasons:

1. **Names are user-editable and not unique-enforced at every point in the
   UI's edit flow.** `fixturedefs.channel.update`'s `name` field lets a
   client rename a channel while other requests referencing it are still
   in flight; if those requests addressed the channel by name, a
   rename-then-mutate race would silently mutate the wrong channel (or
   fail) depending on request ordering. A stable synthetic id sidesteps
   this entirely — renaming never changes what `channelId` refers to.
2. **Aliases already show what goes wrong with name-addressing** — see
   fixturedefs.yaml's `FixtureDefsAlias` schema and `fixturedefs.mode.rename`
   description for the concrete failure mode (rename orphans references).
   Using synthetic ids for channel/mode identity elsewhere in the API,
   while keeping aliases name-addressed (matching the engine's own
   `AliasEdit`, which stores target mode/channel as plain strings), was a
   deliberate scope boundary: aliases are a QXF-persisted, name-based
   concept in the file format itself (see any bundled `.qxf`'s `<Alias>`
   elements), so faithfully round-tripping saved files means keeping that
   representation, warts and all — whereas within-session channel/mode
   addressing is purely an API-layer convenience with no persisted-format
   constraint forcing name-addressing on it.
3. Ids only need to be stable for the lifetime of one editing session (see
   `FixtureDefsChannel.channelId`'s description) — they are NOT persisted
   into the `.qxf` file itself and are re-assigned fresh each time a
   session is opened/created, matching that this is purely an in-memory
   addressing convenience, not a new engine/file-format concept.

## Why session-mutation events carry the full definition, not a JSON Patch

`FixtureDefsSessionChangedEvent` (topic `fixturedefs.session.changed`)
always carries the session's complete, current `FixtureDefsDefinition`
snapshot after every mutation, rather than a JSON Patch describing just
what changed (the alternative §4a permits for large nested resources like
a Scene's channel-value list or a Show's timeline). Reasoning: a fixture
definition being actively edited is small — realistically a few dozen
channels, a handful of modes, at most a few hundred capabilities total, an
order of magnitude smaller than a Scene's per-channel value array or a
Show's timeline — so the "megabytes for a one-channel tweak" concern
00-conventions.md §4a warns about for JSON Patch's alternative doesn't
apply here. Full-snapshot-per-event also means a client never needs to
maintain its own patch-application logic for this domain, at negligible
bandwidth cost given the resource size.

## Why fixturedefs.mode.setChannels replaces the whole channel list in one call

`ModeEdit` (`qmlui/fixtureeditor/modeedit.h`) exposes fine-grained C++
methods — `addChannel(index)`, `moveChannel(fromIndex, toIndex)`,
`deleteChannel(index)`, `setActsOnChannel(index, actsOn)` — for editing one
mode's channel slot list incrementally. `fixturedefs.mode.setChannels`
deliberately collapses all of that into a single "replace the whole
ordered list" call instead of mirroring each C++ method as its own
request. Reasoning: a mode's channel list is edited almost exclusively via
drag-and-drop reordering and bulk channel selection in the real UI
(`ModeEditor.qml`), where the natural unit of "one user action" is already
"the list ends up looking like this", not "move slot 3 to position 7, then
insert slot 9". Modeling each C++ primitive as its own network round-trip
under `00-conventions.md` §4a's `baseRevision` scheme would also mean a
single drag-reorder becomes N sequential conflict-checked requests, each
able to independently CONFLICT mid-gesture if another client touches the
same session's `sessionRevision` in between — exactly the multi-step
batch-conflict hazard called out for ChaserEditor's bulk step ops
elsewhere in the full audit. One "set channels" call is atomic against
`baseRevision` and matches the actual granularity of a real edit.
