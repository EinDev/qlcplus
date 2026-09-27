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
  shared library cache. `fixturedefs.delete` and `fixturedefs.save` take a
  `baseRevision` (referring to this `defRevision`) to detect if the library
  copy changed since the client last looked.
- **`sessionRevision`** — an in-memory editing session (`fixturedefs.session.*`
  — create/open/close/import/list/forkToUser/setMetadata/setPhysical/
  validate) is a private draft cloned from the library, starting its own
  revision counter at 0. All the channel/mode/capability/alias mutation
  methods operate against a `sessionId` and a `baseRevision` (referring
  to this `sessionRevision`).
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
- **`FIXTUREDEFS_IN_USE`** (`fixturedefs.delete`) — the definition is
  referenced by at least one patched `Fixture` in the open project;
  `error.details.fixtureIds` lists them. Unpatch/re-patch those fixtures
  first (`fixtures.*`). Reflects the conservative choice the delete
  description in the fragment recommends.
- **`FIXTUREDEFS_RANGE_OVERLAP`** (`fixturedefs.channel.capability.add`,
  `.update` when `min`/`max` change, and `.wizard`) — the requested DMX range
  collides with another capability of the same channel. The engine's
  `QLCChannel::addCapability()`/`setCapabilityRange()` refuse overlaps, so
  the server never gets to store an overlapping range; the wizard mirrors
  PopupChannelWizard's "Overlapping range detected" check up front.

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

## Implemented 2026-09-27: whole domain (server + tests + client wrapper, no web UI yet)

Server: `controlapi/src/domains/apifixturedefsdomain.{h,cpp}`; tests:
`controlapi/test/apifixturedefsdomain/` (26 cases, one per method plus the
read-only/system errors and a create -> edit everything -> validate -> save
-> reopen -> export -> delete round trip); client wrapper
`webui/api/domains/fixturedefs.js` (`qlc.fixtureDefs.*`). The editor screen
is a follow-up slice - every "Fixture Editor" row of `docs/webui-parity.md`
is `partial: server only` until it lands.

Decisions made while implementing, all of them visible to a client:

- **Wizards added to the spec** (`fixturedefs.channel.wizard`,
  `fixturedefs.channel.capability.wizard`): the fragment had no counterpart
  for `qmlui/qml/popup/PopupChannelWizard.qml`, whose two faces (bulk
  preset channels by type/amount/label, bulk capabilities by
  start/width/amount/label with an overlap check) are exactly what a web
  editor needs to offer the "Wizard" button. Both are single atomic
  session mutations (one `baseRevision`, one `fixturedefs.session.updated`
  with `changeKind` `channel.wizard` / `capability.wizard`).
- **Where the user directory is**: `QLCFixtureDefCache::userDefinitionDirectory()`
  now honours, in order, `QLCFixtureDefCache::setUserDefinitionDirectoryOverride()`
  (tests), the `QLCPLUS_USER_FIXTURE_DIR` environment variable (sandboxes -
  `dev-webui-sandbox.ps1` inherits it from the launching PowerShell), then
  the platform default. Every save/fork/import/delete resolves through it.
  `fixturedefs.delete` additionally refuses (SYSTEM_READONLY) to remove a
  file that isn't inside that directory, even if `isUser` says otherwise.
- **`defRevision` bookkeeping** lives in the domain (`QHash` keyed by
  manufacturer+model), starts at 0 for every library entry, increments on
  every `save` and `delete`, and is never reset - a definition deleted at
  revision 2 and re-saved comes back at 4, so a stale client can never
  collide with 0. `fixturedefs.save`'s `baseRevision` must be `null` when
  the library has no entry for the session's current manufacturer/model
  and the entry's `defRevision` when it has; anything else is `CONFLICT`
  with `details.defRevision` (`null` when there is no entry).
- **Session `CONFLICT` details** carry `{sessionRevision, definition}` so a
  client can rebase without a refetch.
- **Duplicate names are rejected** (`INVALID_PARAMS`) by `channel.add`,
  `channel.update`, `channel.wizard`, `mode.add`, `mode.rename`: the engine
  resolves channels and modes by name when cloning a definition
  (`QLCFixtureMode`'s copy constructor) and when saving aliases, so two
  same-named channels silently merge on the next open. The desktop editor
  avoids this by construction (its editors are keyed by name).
- **Channel rename keeps aliases consistent on the source side**: an
  `AliasInfo` stores the owning channel's name as `sourceChannel` (saved to
  the QXF as `Channel="..."`), so `channel.update` with `name` rewrites that
  field in the renamed channel's own Alias capabilities. Aliases *targeting*
  the old name are left alone, exactly as the fragment documents.
- **Heads and acts-on survive slot edits**: `QLCFixtureMode::removeChannel()`
  / `removeAllChannels()` leave the index-based head list and acts-on map
  untouched, so `mode.setChannels` and `channel.remove` snapshot both by
  channel identity, rebuild the slot list and remap; a head whose channels
  all vanished is dropped, an acts-on pointing at a removed channel becomes
  `null`.
- **Capability `resources` are rebuilt, not patched**: `QLCCapability` can
  only append resources, so `capability.update` with `preset` or
  `resources` swaps in a fresh capability at the same index carrying exactly
  the values that fit the resulting `presetType` (colours as `#rrggbb`,
  frequencies as numbers, pictures as strings; a preset change within the
  same type keeps the old values). `autoPatchColors` does the same per
  detected colour.
- **`autoPatchColors` reads `namedrgb.qxcf`** from the system colour
  filter directory with a bare `QXmlStreamReader` (the `ColorFilters` class
  lives in qmlui); when the file is missing it only title-cases names, like
  the desktop editor.
- **`fixturedefs.get` ids** (`ch-1`, `mode-1`, ... in pool order) are
  index-based and only meaningful within that response - they are not
  session ids. Session ids are assigned when the session opens (same
  scheme) and then stay stable for its life.
- **Sessions outlive their client connection** (a browser reload can list
  and continue them via `fixturedefs.session.list`), mirroring
  `FixtureEditor` keeping editors open; they are freed by
  `fixturedefs.session.close` or when the server shuts down.
- **`fixturedefs.list` cost**: implemented per spec (force-loads every
  definition once); measured against the full bundled library in the
  sandbox smoke test (figure in the slice report). A manufacturer filter
  keeps it cheap.
- **`save` re-points patched fixtures**: after `reloadOrAddFixtureDef()`
  overwrites the cache entry (deleting its modes), every `Fixture` using
  that manufacturer/model is re-attached to the mode of the same name (or
  the first mode) - the same thing `FixtureEditor::slotReloadFixture()` does.
- **`fixturedefs.session.get` (added by the editor slice, 2026-09-27)**:
  read-only snapshot of one open session (`session.open`'s result shape plus
  `isModified`). Without it a browser that reloads and finds its sessions
  in `session.list` had no way to fetch their definitions (`export` is QXF,
  and a no-op mutation would bump the revision and mark the session
  modified). Test: `sessionGetReturnsSnapshot`.
- **`fixturedefs.delete` restores a shadowed bundled definition (fixed by the
  editor slice, 2026-09-27)**: deleting a user copy made by
  `session.forkToUser` + `save` removed the cache entry, which also took the
  bundled definition of the same manufacturer/model out of the library until
  the next restart (found in the browser: Generic / Generic Smoke vanished).
  The domain now looks the pair up in the system `FixturesMap.xml` and
  reloads the bundled `.qxf`. Test: `deleteUserCopyRestoresBundledDefinition`.
