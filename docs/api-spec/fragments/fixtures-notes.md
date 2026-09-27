# Fixtures domain — notes for the merge pass / repo owner

## Channel message keys to add at merge time

Per `_example.yaml`'s closing note, every message key below needs a
lowerCamelCase entry added under `channels.qlcplus.messages` in the merged
spec (e.g. `fixturesPatchRequest: { $ref: '#/components/messages/FixturesPatchRequest' }`).
That's every key in this fragment's `messages:` map — 69 of them. I did not
edit the skeleton myself, per instructions. The `*OkResponse` message keys
(e.g. `FixturesPatchOkResponse`) also need channel entries, since the
top-level `*Response` messages' `oneOf` reference them by path
(`#/components/messages/.../payload`) the same way `_example.yaml` does for
`WidgetsRenameOkResponse`.

## Data model choices

- **IDs as strings on the wire.** Engine fixture/group IDs are `quint32`,
  but I used `type: string` for `fixtureId`/`groupId` everywhere, consistent
  with the `_example.yaml` pattern (`widgetId: { type: string }`). If another
  domain's fragment used raw integers for its IDs, we should reconcile at
  merge time — I'd lean towards strings everywhere for consistency with JS
  clients that don't want to worry about `quint32` overflow semantics, but
  this is the repo owner's call.
- **Enums as strings, not ints.** `QLCFixtureDef::FixtureType`,
  `QLCChannel::Group`, `QLCChannel::Preset`, `QLCCapability::Preset`,
  `QLCCapability::PresetType`, and `QLCChannel::ControlByte` are all
  represented as strings (their existing `xToString()` engine helpers),
  not raw enum ints, for JSON readability. `PresetType` is the one actual
  closed enum (`None/SingleColor/DoubleColor/SingleValue/DoubleValue/Picture`)
  so it got a real `enum:` constraint; the others are open lists (dozens of
  values, see `qlcchannel.h`/`qlccapability.h`) so they're just `string`.
- **Universe/address are 0-based on the wire**, matching the engine
  (`Fixture::universe()`/`address()` are explicitly documented as 0-based).
  The current qmlui UI displays 1-based addresses to the user
  (`fxi->id() + 1` in fixture names, similar +1 patterns elsewhere) — that's
  a presentation-layer decision for the Electron client, not the wire
  protocol. Flagging in case another domain's fragment picked 1-based; we
  should be consistent project-wide.
- **No "tags" field.** The task brief suggested "optional custom name/tags"
  for patch, but `Fixture` (`engine/src/fixture.h`) has no tag/label concept
  at all — only `id`/`name`/`universe`/`address`/`channels`/`fixtureDef`/
  `fixtureMode` plus fade/HTP/LTP/modifier metadata that's channel-level, not
  fixture-level. I only exposed `name`. If tagging is wanted it would need a
  new engine field — out of scope for an API spec that must reflect the
  actual engine.
- **Channel modifiers, forced-HTP/LTP, exclude-from-fade** (`Fixture::
  setChannelModifier`, `setForcedHTPChannels`, `setForcedLTPChannels`,
  `setExcludeFadeChannels`) are real per-channel patch-time settings exposed
  in qmlui's `FixtureProperties.qml`/`FixtureChannelDelegate.qml`, but I left
  them **out of `fixtures.patch`/`fixtures.update`**. They read as
  "fine-tuning an already-patched fixture's playback behavior" rather than
  "patching" per se, and there's no natural home for them among my 5 numbered
  scope items. Recommend a follow-up `fixtures.setChannelBehavior`-style
  method (or folding into `fixtures.update`) in a later pass — flagging so
  it isn't silently lost. `doc.h`'s
  `updateFixtureChannelCapabilities(id, forcedHTP, forcedLTP)` is the engine
  entry point for at least the HTP/LTP half.
- **Generic RGB Panel** (`FixtureManager::addRGBPanel`,
  `Fixture::genericRGBPanelDef/Mode`) — a third "no real definition" fixture
  type alongside the generic dimmer, used to build a procedural RGB/RGBW
  pixel-bar row (columns, component order RGB/BGR/GRB/etc., 8/16-bit). I left
  it **out of scope**: it's a real feature (`RGBPanelProperties.qml`) but
  adds a distinct multi-parameter shape to `FixturesDefinitionRef` for a
  fairly niche fixture-authoring path. If it needs API coverage, it's a
  natural third variant of `FixturesDefinitionRef` (alongside
  `{manufacturer,model,mode}` and `{generic:{channels}}`) — I'd suggest
  `{rgbPanel:{columns,components,is16bit}}`.

## Address-conflict validation (task item 5)

`Doc::addFixture()` (`engine/src/doc.cpp` ~line 406) **does** reject
overlapping addresses — it scans `m_addresses` for every channel the new
fixture would occupy and returns bare `false` on any collision (only a
`qWarning()`, no structured info) — see `doc.cpp:423-432`. I modeled this as
`fixtures.patch` returning `FIXTURES_ADDRESS_OVERLAP` with `error.details`
carrying enough for the client to show *which* fixture it collided with.
**The engine's bool return doesn't give you that** — the API server
implementation will need to pre-check with `Doc::fixtureForAddress()` over
the intended channel range itself (exactly like `FixtureBrowser::
availableChannel()` already does in qmlui) to build useful `details`, rather
than just relaying `addFixture()`'s true/false.

**Bigger gap found: `fixtures.update` (move/rename) has no engine-side
overlap protection at all.** `Fixture::setAddress()`/`setUniverse()` just
mutate the fixture and emit `changed()`; `Doc::slotFixtureChanged()`
(`doc.cpp:725`) then unconditionally overwrites `m_addresses` for the new
range — the `Q_ASSERT(!m_addresses.contains(i))` there is compiled out in
release builds and, per its own comment, isn't actually reliable even in
debug (setting universe+address calls this twice with a transient wrong
combination). Today, `qmlui`'s `FixtureManager::moveFixture()` relies
entirely on the **client** pre-checking via `FixtureBrowser::
availableChannel(fixtureID, requested)` before calling it — nothing stops
a determined caller from creating an overlap by calling `moveFixture`
directly. Since this new API is the authoritative server boundary (not just
another optional client), **`fixtures.update`'s server implementation must
add its own overlap check** (excluding the fixture's own current range) and
return `FIXTURES_ADDRESS_OVERLAP` — don't just forward to
`Fixture::setAddress()`. Called out as an inline `description` in the
fragment too.

`error.code: FIXTURES_ADDRESS_OVERLAP` — suggested `error.details` shape:
`{ conflictingFixtureId: string, conflictingAddress: int }` (first
colliding absolute address found is enough; a client retrying will surface
the next one if any).

## Fixture remapping (task item 4)

`FixtureRemapper` (`engine/src/fixtureremapper.h`) is confirmed **one-shot
batch**: `autoConnectFixtures()` (pure computation, can be called
repeatedly to build up mapping tables) followed by exactly one
`applyRemap(doc, targetFixtures)` call that does, in order: replace the
fixture list (`Doc::replaceFixtures`), remap fixture groups, remap channel
groups, remap functions (Scene/Sequence/EFX), remap monitor properties.
There's no partial-apply or undo-one-mapping operation at the engine level.

I modeled this as `fixtures.remap.apply` (one atomic document-state
mutation, batch of `{sourceFixtureId, new definition/address, channelMap}`
entries) plus a read-only `fixtures.remap.suggestChannelMap` helper for the
interactive-connection-building UX qmlui's `FixtureRemapManager` provides
(auto-connect suggestions, click-to-connect). Two things worth the repo
owner's attention:

1. **`suggestChannelMap` can't be a direct passthrough to
   `FixtureRemapper::autoConnectFixtures()`.** That engine method takes two
   *live* `Fixture*` instances (it also copies fade-capability flags and
   channel modifiers as a side effect, and needs the target fixture to
   already exist in some `Doc`). In the real qmlui tool this works because
   `FixtureRemapManager` keeps a whole temporary target `Doc` around
   (`m_targetDoc`) that fixtures get created into before any connecting
   happens. My API deliberately avoids exposing that temporary-doc/staging
   concept over the wire (it's UI session state, not project state) — so
   `suggestChannelMap` needs a **lighter reimplementation** on the server
   that replicates just the matching rule (1:1 by index for same
   def+mode/generic pairs, else match by group+controlByte+colour) directly
   off `QLCFixtureMode::channels()`, without needing a live target Fixture.
   Flagging this as a "write new code, don't just forward" item for whoever
   implements the server.
2. **Cross-domain fan-out.** `applyRemap()` touches fixture groups (mine),
   channel groups, and Scene/Sequence/EFX functions (not mine) in one call.
   My `fixtures.remap.applied` event only covers the fixture-level result;
   I don't know the functions/channel-groups domains' event topic names to
   reference them here. Whoever owns those domains should either (a) also
   broadcast their own `*.changed` events as a side effect of this call, or
   (b) we document that `fixtures.remap.applied` is a signal to those
   clients to refetch affected functions/channel groups wholesale. This
   needs a decision at merge time — I did not invent event topics for
   domains I don't own.
3. Virtual Console widget remapping is explicitly **left to the caller** by
   the engine's own doc comment (`fixtureremapper.h`: "VC classes live in
   the UI layer, not in the engine") — i.e. it's not something
   `fixtures.remap.apply` can cover even in principle; that's the Virtual
   Console domain's problem if VC widgets ever need to survive a remap.

## Fixture groups — scope cuts

`FixtureGroupEditor` (`qmlui/fixturegroupeditor.h`) exposes several
UI-only convenience operations I deliberately did **not** mirror as server
methods, because they're pure client-side recomputation over data already
obtainable from `fixtures.group.get`, not new engine operations:
`groupSelection`/`headSelection`/`fixtureSelection` (hit-testing a click
against the grid), `checkSelection`/`moveSelection` (compute whether a drag
is valid, then translate it into repeated `assignHead`/`swapHeads` calls),
and `transformSelection` (rotate 90/180/270, horizontal/vertical flip —
all just recompute each selected head's `(x,y)` locally then reassign). A
future Electron client can build all of these client-side on top of the
`assignHead`/`unassignHead`/`swapHeads` primitives I did expose. Only flag
this if the repo owner wants first-class server-side "transform selection"
support instead (e.g. for undo/redo granularity reasons).

`FixtureManager::addItemsToNewGroup()` (create-a-group-and-populate-it in
one qmlui call) has no dedicated method either — it's `fixtures.group.create`
followed by N `fixtures.group.assignFixture` calls from the client. Kept the
API surface to orthogonal primitives rather than mirroring every UI
convenience combo.

## Lockable resources (§6)

- `fixture` (resourceId = fixture id) — edited in the Fixture Properties
  panel (`FixtureProperties.qml`).
- `fixtureGroup` (resourceId = group id) — edited in the Fixture Group
  Editor grid.

Fixture *definitions* (manufacturer/model/mode) aren't lockable from this
domain — they're read-only here; the `fixturedefs` domain owns locking for
definition editing.

## Subscription gating (§5)

**None of this domain's events need subscribe-gating.** Every event here
(`fixtures.patched`, `fixtures.unpatched`, `fixtures.updated`,
`fixtures.group.*`, `fixtures.remap.applied`) is a low-frequency structural
(§4a) change — patching/moving/grouping fixtures is an editing action, not
a per-tick stream — so they all broadcast to every connected client
unconditionally, same as the skeleton's `locks.changed`. There is no
per-channel/per-universe/per-tick data in this domain; live DMX values
belong to a different domain (probably `dmx`/`engine` per the conventions'
`dmx.universe.1` example topic), not `fixtures`.

## Document-state event payload shape (§4a)

I used full-resource-in-the-event everywhere (a `FixturesPatchedFixture` or
`FixturesGroup` object per changed resource), never JSON Patch — group
`heads` maps and fixture lists are small (real projects patch dozens to a
few hundred fixtures/heads, not thousands), so the "megabytes for a
one-channel tweak" concern from §4a doesn't apply here the way it would for
a Scene's channel-value list.

## Bulk operations kept from the real UI

- `fixtures.patch` takes `quantity`/`gap` and creates N fixtures in one call
  — this is directly how the existing "Add Fixture" dialog behaves
  (`FixtureManager::addFixture(..., quantity, gap, ...)`), not something I
  invented. The engine also auto-advances to the next universe mid-batch if
  a universe fills up (`fixturemanager.cpp:300-313`, creating a new universe
  via `InputOutputMap::addUniverse()` if needed) — I did **not** carry that
  auto-universe-creation behavior into the spec; `fixtures.patch` here stays
  within the single `universe` given and should error (`INVALID_PARAMS` or a
  dedicated code) if a bulk patch would overflow past address 511 without an
  explicit universe rollover, rather than silently creating universes as a
  side effect of a fixtures.* call (universe creation belongs to whatever
  domain owns I/O/universes). Flagging this behavioral simplification
  explicitly since it's a real deviation from current qmlui behavior.
- `fixtures.unpatch` takes a `fixtureIds` array (bulk), matching
  `FixtureManager::deleteFixtures(QVariantList)`. It also inherits that
  method's side effect of deleting any fixture group left empty by the
  removal (worked around a real engine bug, #2063, entirely in qmlui) —
  noted inline in the fragment; those deletions surface as ordinary
  `fixtures.group.deleted` events, not a special field on the unpatch
  response.

## Things intentionally left for the `fixturedefs` domain

Everything under `engine/src/qlcfixturedef.h` / `qlcfixturemode.h` /
`qlcchannel.h` / `qlccapability.h` that is a **mutation** (adding/removing
channels, modes, capabilities; editing physical properties; saving a
definition to disk via `QLCFixtureDefCache::storeFixtureDef`/`reloadFixtureDef`)
is out of scope here per the task brief. I only read from
`QLCFixtureDefCache` (`manufacturers()`, `models()`/`fixtureCache()`,
`fixtureDef()`) for browsing.

## Implemented 2026-09-26: definition browsing and flat patch params (web UI slice)

Server: `controlapi/src/domains/apifixturesdomain.cpp`.

- `fixtures.defs.listManufacturers/listModels/getModel/getMode` over the
  Doc's `QLCFixtureDefCache`. Shapes are a union of the web UI contract
  and this fragment's earlier draft: `listModels` returns plain `models:
  [string]` (contract) plus `modelDetails: [{model, isUser}]` (draft) and
  echoes `manufacturer`; `getModel` returns `type` and `fixtureType` (same
  value), `physical`, and inlines each mode's `channels` (the contract's
  `{index, name, group, controlByte}` per channel, as
  `FixturesModeChannel`). Unknown names are `NOT_FOUND`, never empty
  lists. `getModel`/`getMode` trigger the cache's lazy definition load.
- `fixtures.patch` additionally accepts flat top-level
  `manufacturer`/`model`/`mode` (or `generic`) when no `definition` object
  is given. Its response still carries `fixtureIds` (+ `docRevision`); the
  full `fixtures` list is on the `fixtures.patched` event, as before.

## Implemented 2026-09-27: monitor placement (fixtures.monitor.*) and remap (fixtures.remap.*)

Server: `controlapi/src/domains/apimonitordomain.cpp` and
`controlapi/src/domains/apifixtureremapdomain.cpp`; tests in
`controlapi/test/apimonitordomain/` (14 cases) and
`controlapi/test/apifixtureremapdomain/` (7 cases). Web UI driver:
`webui/tools/e2e/fixtures-views.js`.

- **New section 5 in the fragment**: `fixtures.monitor.get`, `setStage`,
  `setPlacement`, `arrange`, `detectArrangement`, `aimAt` and the
  `fixtures.monitor.changed` event, over `MonitorProperties` (the `.qxw`
  `<Monitor>` element). `detectArrangement` was not in the task brief; it is
  the Arrange dialog's "detect from placement" and needs server data.
- **One geometry implementation for both hosts.** The arrange / align /
  distribute / rotate / centre / detect maths and the "Pick a 3D point"
  pan/tilt solver moved from qmlui's `ContextManager` into
  `engine/src/monitorlayout.{h,cpp}` (pure functions over a plain item list).
  `ContextManager` keeps its selection / Tardis undo / DMX-position /
  view-refresh shell around the same calls, and `FixtureUtils`' point-of-view
  projections delegate to it, so a browser and the desktop app lay fixtures
  out identically. Ordering rules are preserved: circle / line / rotate /
  centre sort by `Fixture::operator<` (DMX address only), grid follows the
  common Fixture Group's grid order; locked items keep their place except for
  align / distribute, which ignore the lock like the Qt UI does.
- **Revision bumping.** `MonitorProperties` setters never call
  `setModified()`; every mutating monitor handler calls it once after its
  whole batch, so `baseRevision` checking and event revisions stay exact.
  `MonitorProperties` has no change signals, so a placement edited in the Qt
  UI reaches API clients only through `core.history.changed` /
  `docRevision` (the web UI refetches on it).
- **Units.** Positions are millimetres in document space (X width, Y up,
  Z depth), rotations degrees, `aimAt.point` in the same mm space. Items are
  keyed by `(fixtureId, headIndex, linkedIndex)`; `linkedIndex >= 1` in
  `setPlacement` creates a linked copy (copied from the base item), `remove`
  deletes one. `fixtures.monitor.get` also returns a synthetic
  `placed: false` entry at the stage centre for every fixture without a
  monitor entry, plus the physical footprint, head channel lists and
  `hasPan`/`hasTilt`, so a client can draw 236 fixtures in one call.
- **aimAt is live (§4b)**: the pan/tilt values go through
  `ApiIoDomain::overrideChannels()` (new public hook, same store as
  `io.simpleDesk.setChannels`), so the usual Release clears them. Unlike the
  desktop tool there is no Qt3D mesh fallback for the beam origin: fixtures
  without persisted LightEmitter data aim from their root position.
- **Remap.** `suggestChannelMap` re-implements
  `FixtureRemapper::autoConnectFixtures()`'s matching rule against a target
  *mode* (no staging Doc). `apply` drives `FixtureRemapper::applyRemap()` and
  compensates for two engine behaviours: `Doc::replaceFixtures()` replaces
  *every* fixture and `remapSceneValues()` drops unmapped values, so every
  untouched fixture is cloned into the replacement list under its own id with
  an identity channel map (otherwise every Scene would silently lose them);
  targets get fresh ids above the current maximum; the final address map is
  overlap-checked before anything changes (no rollback exists). Virtual
  Console widgets are remapped through the new `ApiVcHost::vcRemapChannels()`
  hook (qmlui's App runs the same loop as `FixtureRemapManager::applyRemap()`);
  the call also broadcasts `fixtures.monitor.changed` for the moved
  placements. The response additionally carries `fixtureIds` (the created
  targets, in mapping order).
- **Fixed on the way**: `fixtures.update` changed address and universe with
  two separately-signalling setters; the transient (new address, old
  universe) footprint could hit `Doc::slotFixtureChanged()`'s
  `Q_ASSERT(!m_addresses.contains(i))` (a Debug-build abort, found through the
  web UI's universe-grid paste) or corrupt the address map in Release. Both
  are now applied with signals blocked and one `changed()` is emitted.

## Implemented 2026-09-27: mode change, RGB panel, channel behaviour, modifier templates, colour filters

Server: `controlapi/src/domains/apifixturesdomain.cpp` (mode, RGB panel,
`fixtures.get` additions) and the new
`controlapi/src/domains/apifixturechannelsdomain.cpp`; tests in
`controlapi/test/apifixturesdomain/` (9 new cases) and
`controlapi/test/apifixturechannelsdomain/` (16 cases). Web UI driver:
`webui/tools/e2e/fixtures-misc.js`.

- **`fixtures.update {mode}`** closes the "change mode after patching" gap
  (the earlier notes' workaround was unpatch + patch). The final footprint is
  validated (range + overlap, excluding the fixture itself) before anything
  changes, then mode, address and universe are applied with signals blocked
  and ONE `changed()` (the 1dd8e44d8 idiom), so `Doc::slotFixtureChanged()`
  never sees a transient footprint - covered by
  `updateModeWithMoveIgnoresTransientOverlap` (a Debug test binary aborts on
  the `Q_ASSERT` otherwise). The Qt UI's `FixtureManager::setFixtureModeIndex()`
  does not re-track the Doc's address map at all after a mode change; the API
  does, and also pushes the new footprint's HTP/LTP, default values and
  modifiers into the universe (`Doc::updateFixtureChannelCapabilities()`,
  also after a plain move now). Per-channel settings on indices the new mode
  lacks are dropped first (`Fixture::setChannelModifier()` refuses indices
  `>= channels()` afterwards). **Functions are not touched**, exactly like the
  Qt UI: Scene values on channels the new mode lacks stay in the Scene
  (`Scene::postLoad()` drops them on the next project load; while running,
  `FadeChannel` treats such a channel as a plain HTP intensity at
  `universeAddress + channel`, which is the Qt UI's behaviour too), so
  switching back restores them.
- **`fixtures.get`** additionally reports per channel `canFade`,
  `precedence` (`auto|htp|ltp`) and `modifier` (name or null), plus
  fixture-level `availableModes` and the current mode's `physical` block -
  generic dimmer / RGB panel definitions are not in the definition cache, so
  `fixtures.defs.getModel` cannot answer for them. The fixture and universe
  summaries in the web UI are built from this (no separate summary method).
- **`fixtures.createRgbPanel`** ports `FixtureManager::addRGBPanel()`: same
  row definition, head grid (snake / zig-zag, start corner, vertical =
  transposed), 2D/3D placement maths and names. Differences: every row is
  overlap-checked before anything is created, and a row that does not fit the
  rest of a universe only rolls into the next EXISTING universe (the Qt UI
  creates universes; this domain never does, consistent with fixtures.patch).
  It broadcasts `fixtures.patched` and `fixtures.group.created` (with heads).
- **`fixtures.channel.setBehaviour`** (new domain file): forced HTP/LTP,
  can-fade and modifier for one or several channels, optionally
  `applyToSameType` (same definition AND mode pointer, the Qt UI's rule). The
  engine setters neither call `Doc::setModified()` nor touch the universe, so
  the handler does both. The Qt UI's precedence rule (intensity channels can
  only be forced LTP, others only forced HTP) is an INVALID_PARAMS here
  instead of a silent no-op. Event `fixtures.channel.behaviourChanged`.
- **`fixtures.modifiers.list/get/save/rename/delete`**: the channel modifier
  template library, a §4c library resource with the domain-local
  `modifiersRevision` (optional `baseRevision`, like `profilesRevision`).
  Templates are files in `QLCModifiersCache::userTemplateDirectory()`, which
  now honours `QLCPLUS_USER_MODIFIERS_DIR` (tests, and
  `dev-webui-sandbox.ps1 -UserModifiersDir <sandbox>\UserModifiers` - NOT `<sandbox>\ModifiersTemplates`, which is the sandboxed app's system folder: templates saved there come back as read-only system templates on the next start; the script now refuses it). System templates are read-only.
  `rename`/`delete` are new (the Qt editor only saves); they needed
  `QLCModifiersCache::renameModifier()`/`takeModifier()`. A rename keeps the
  instance (every channel follows) and bumps `docRevision` when a fixture
  uses it (the `.qxw` stores the name); a delete detaches it from every
  fixture channel and universe first, then retires the instance (freed with
  the domain, since the output thread may just have read the pointer).
  Event `fixtures.modifiers.changed`.
- **`fixtures.colorFilters.list`**: the `.qxcf` files of the system and user
  ColorFilters folders, parsed in controlapi with the same rules as
  `qmlui/colorfilters.cpp` (which controlapi cannot link). Read-only: adding /
  editing / deleting filter files (the Qt tab's edit mode) is not covered.
- None of the new events is subscribe-gated.
- **`fixtures.list` / `fixtures.get`** also report `heads` (Fixture::heads()), which the group grid
  editor needs to offer per-head placement.
- **Fixed on the way**: `fixtures.group.assignHead` onto an explicit cell for a head not yet in the
  group changed the group silently - `FixtureGroup::assignHead()` emits `changed()` only on its
  auto-place path, so there was no `docRevision` bump and no `fixtures.group.updated`. The handler
  now emits it (test `assignHeadToExplicitCellBumpsRevision`). The grid editor's rotate / flip are
  computed in the browser (FixtureGroupEditor::transformSelection()'s maths) and applied as a chain of
  `fixtures.group.swapHeads`, which never loses a head (the Qt code overwrites cells instead).
- **Web UI** (`webui/ff/FixtureMisc.jsx`): the modifier template calls go out WITHOUT
  `baseRevision` (the web UI's mutation queue stamps every call with `docRevision`, which the
  modifiers library does not use). External-controller mapping of the console is not offered: the
  Qt feature (BottomPanel's "external control", pan/tilt pages, fader pickup, VC input inhibit)
  lives entirely in qmlui's `SceneEditor` on `InputOutputMap::inputValueChanged`; there is no engine
  hook or API to route an input line to a browser console. Pan/tilt mode and the fader window shift
  are offered as on-screen console navigation instead.