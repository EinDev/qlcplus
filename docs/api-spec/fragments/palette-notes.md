# `palette.*` domain notes

Fills TODO.md 2.4 ("Palette/color-filter definitions... not covered by any
domain as currently scoped"). Scope here is Palettes only (`QLCPalette`,
`Doc::addPalette`/`deletePalette`/`palette`/`palettes`,
`qmlui/palettemanager.cpp`) - color *filters* (`qmlui/colorfilters.cpp`) are
a separate engine concept with their own storage and are explicitly left out
of this fragment; a future `colorFilter.*` domain (or folding it into this
one, if it turns out to be structurally identical) is a separate decision.

## Value encoding: generic array, not a per-type object

The task brief left this open - object-with-named-fields (e.g. `{rgb:
"#ff0033"}` for Color, `{pan, tilt}` for PanTilt) vs. a generic
`values: [number|string, ...]` mirroring `QLCPalette::values()`'s
`QVariantList`. Went with **the generic array** (`PaletteValues` in
`palette.yaml`'s `schemas`), for three reasons:

1. **One schema, nine types.** `QLCPalette::PaletteType` has 9 members, and
   the engine already treats them uniformly as an ordered `QVariantList` -
   `setValue(v)` / `setValue(v1,v2)` / `setValue(v1,v2,v3)` are arity-based,
   not per-type. A discriminated `oneOf` of 9 named-field objects would be
   strictly more spec (and more implementation branching) to express the
   exact same information, for a domain the brief itself says shouldn't be
   over-engineered on day one.
2. **No invented field names to get wrong.** `qlcpalette.cpp` doesn't name
   Shutter's two values or document what Gobo's value even holds (see
   `PaletteValues`'s own description - Shutter and Gobo are handled
   generically, "whatever the engine has" round-tripped as-is). Naming those
   fields (`shutterOpen`/`shutterClose`? `speed`/`mode`?) would be guessing at
   semantics the engine itself doesn't expose, i.e. exactly the
   over-engineering the brief warned against. A positional array carries
   the same real information without inventing meaning that isn't there.
3. **Zero drift risk against the implementation.** `ApiPaletteDomain`'s
   to-JSON/from-JSON conversion is one small loop over `QLCPalette::values()`/
   `setValues()` for every type, instead of nine bespoke (de)serializers that
   could silently diverge from the spec one type at a time as new palette
   types get added later.

Cost of this choice: a client has to know positional semantics per type
(documented in full on `PaletteValues` in `palette.yaml`) rather than reading
self-explanatory field names off the wire. Given this is an internal API for
a client written against this exact spec (not a public/discoverable REST
API), that's an acceptable trade for #1-#3 above. Color and PanTilt - the two
the brief calls out as mattering most this session - are both fully and
precisely specified (packed `"#rrggbb[wwaauv]"` string via
`QLCPalette::colorToString()`/`stringToColor()`; `[pan, tilt]` two-int array).

## Type is immutable after creation

`QLCPalette::type()` has no setter and is a `Q_PROPERTY(... CONSTANT)` - the
engine itself treats a palette's type as fixed at construction. `palette.create`
takes `type`; `palette.update` deliberately does not accept it at all (not even
as a no-op field) - changing a palette's type isn't a supported engine
operation, so the spec doesn't pretend otherwise. A client that needs a
different type deletes and re-creates.

## Revision/broadcast tier: §4a throughout

Palettes are saved into the `.qxw` project (`Doc::m_palettes`,
`QLCPalette::saveXML`/`loader`) and `Doc::addPalette`/`deletePalette` already
call `setModified()` themselves (bumping the shared `docRevision` and firing
`paletteAdded`/`paletteRemoved`) - this is unambiguously §4a document state,
using the global `docRevision`/`baseRevision`, not a §4c domain-local counter
like `fixturedefs`/`io`'s shared-library resources. All three mutations
broadcast the full new/old resource (`PaletteDetail`, small - a handful of
scalars plus at most 3 values) rather than a JSON Patch, per §4a's "fine for
most things" guidance; there's nothing here approaching Scene/Show's
nested-resource size.

**Gap the implementation has to paper over itself**: `Doc` has no
`paletteChanged`/`paletteUpdated` signal - only `paletteAdded(quint32)` and
`paletteRemoved(quint32)` exist (see `engine/src/doc.h`). Rename
(`QLCPalette::setName`) and value changes (`setValue`/`setValues`) only emit
`QLCPalette`'s own `nameChanged()`/no signal at all (there is no
`valueChanged` signal on `QLCPalette`), and neither of those touches `Doc`'s
modified/revision state - `qmlui/palettemanager.cpp`'s own `updatePalette()`
methods call `m_doc->setModified()` by hand for exactly this reason. So
`palette.update`'s handler in `ApiPaletteDomain` calls `doc->setModified()`
itself after mutating the `QLCPalette`, then broadcasts `palette.updated`
directly from the handler (not from a signal slot the way `io.universe.created`
does) - there's no engine signal to hang the broadcast off of. `palette.create`/
`palette.delete` *do* have `paletteAdded`/`paletteRemoved` to key off of, and
the implementation uses them (mirrors `ApiIoDomain::slotUniverseAdded`), for
consistency with the rest of the codebase's originClientId-attribution
pattern - even though, per `Doc::addPalette`/`deletePalette` in `doc.cpp`,
these signals already fire synchronously within the mutator call, same as
`universeAdded`.

## Locking

`palette` is a lockable resource type per §6 (a client editing a palette's
color/position live, the way `qmlui`'s Palette editor already does via
`getEditingPalette`/`releaseEditingPalette`, benefits from the same soft-lock
UX as any other structural resource) - no new `locks.*` messages needed,
already generic in the skeleton.

## Subscriptions

None of `palette.created`/`.updated`/`.deleted` are subscribe-gated - all
§4a structural events, always delivered per §5, same as `io.universe.*`.

## Deliberately left out of this pass

- **Fanning** (`QLCPalette::FanningType`/`FanningLayout`/`fanningAmount`/
  `fanningValue`) - real engine functionality (used when applying a palette
  across a fixture group to spread values across fixtures) but not part of
  "create/edit a palette definition" in the CRUD sense the brief scoped this
  to; would be a `palette.setFanning`-shaped follow-up (§8's `.set<Property>`
  verb) once someone actually needs to drive it remotely.
- **`valuesFromFixtures`/`valuesFromFixtureGroups`/`previewPalette`** - these
  apply a palette's values to live DMX output for preview, which is §4b
  runtime behavior layered on top of the §4a resource this fragment defines,
  not part of the resource's CRUD. *Superseded 2026-09-27*: `palette.apply`
  (see "Implemented 2026-09-27: palette.apply" below) now covers
  `valuesFromFixtures`/`previewPalette`; `valuesFromFixtureGroups` is still
  not exposed.
- **`addPaletteToNewScene`** - this is Scene creation with a side-effect on a
  palette, i.e. `functions.scene.*` territory once that domain exists, not a
  palette operation.
- **`isTemporary`** - an engine/UI-internal editing-buffer flag
  (`getEditingPalette`'s scratch copies before `createPalette` commits them);
  irrelevant to a client that always talks in terms of already-committed
  palettes via `palette.create`/`.update`.
- **Color filters** (`qmlui/colorfilters.cpp`) - see the top of this file.

## Open question for the repo owner

`PaletteValues`'s positional-array choice (above) trades self-description for
spec/implementation simplicity. If a future Electron client ends up needing
richer per-type validation/tooling (e.g. a color-picker widget that wants a
typed `rgb` field rather than parsing array element 0 itself), that's a
reasonable trigger to revisit this and split `PaletteValues` into a
discriminated `oneOf` keyed by `type` - flagging it here rather than doing it
preemptively, per the brief's "don't over-engineer every type" guidance and
the repo owner's general "prefer fewer, more general methods" steer in
`00-conventions.md`.

## Implemented 2026-09-27: palette fanning

- NEW `PaletteFanning` schema (type / layout / amount / value), returned in `palette.get`,
  `palette.created` / `palette.updated` and accepted (partially) by `palette.create` / `palette.update`.
  `value` is a number for the numeric types and a `#rrggbb` string for Color, exactly as
  QLCPalette::loadXML() reads FanValue. Unknown type / layout names are INVALID_PARAMS.

## Fixed 2026-09-27: Gobo values persist, Pan / Tilt are degrees

- **Engine**: `QLCPalette::saveXML()` wrote no `Value` for a Gobo palette and `loadXML()` had no Gobo
  case, so a Gobo palette lost its value on every save / reload (the API happily created one). Both
  now treat it as one integer, the wheel's DMX value that `valuesFromFixtures()` writes;
  `engine/test/qlcpalette` round-trips every palette type (`saveLoadEveryType`). Note that applying a
  Gobo palette to a fixture still produces nothing: `QLCFixtureHead::cacheChannels()` never maps the
  Gobo group (the existing `fixturesGobo` XFAIL) - separate, not changed. (Fixed later the same day,
  see "Implemented 2026-09-27: palette.apply" below.)
- **Values are unchanged on the wire, the web UI now reads them right**: Pan / Tilt / PanTilt values
  are DEGREES (`Fixture::positionToValues`: `dmx16 = deg * 65535 / focusPanMax|focusTiltMax`, 360 / 270
  when the definition says 0). The web palette editor offered them as 0-255 and "apply to selection"
  wrote them as DMX; it now edits degrees up to the selected fixtures' range and converts per fixture
  with that fixture's own range (clamped - the engine would wrap past it).

## Implemented 2026-09-27: palette.apply

- NEW `palette.apply` {paletteId, fixtureIds} -> {channels: [{fixtureId, channel, address, value}]}:
  the desktop's double-click on a palette (`PaletteManager::previewPalette`). The values come from
  `QLCPalette::valuesFromFixtures()` itself, so every type and the fanning (ordered by the fanning
  layout over the fixtures' monitor positions) are the desktop's maths, not a re-implementation.
  They are written through `ApiIoDomain::overrideChannels()`, i.e. as Simple Desk overrides like
  `fixtures.monitor.aimAt`: each one broadcasts `io.simpleDesk.channelChanged` and the web UI's
  "Release fixtures" (`io.simpleDesk.resetChannel`) clears them. The desktop writes into
  ContextManager's own generic fader instead; the web client has no such per-client fader, and the
  override path is the one the other web fixture tools already release.
- Live (§4b): no `baseRevision`, nothing saved, no event of its own.
- `fixtureIds` is required and non-empty (the server does not know the client's selection); an
  unknown id is `NOT_FOUND` rather than skipped, because `valuesFromFixtures()` skips a missing
  fixture *before* advancing the fan progress and would silently shift the fan across the rest.
- Heads are not addressable: `valuesFromFixtures()` takes fixture ids and fills every head itself
  (Dimmer / Color per head, Shutter per head's shutter channels). A per-head apply would need a new
  engine entry point; not done. `valuesFromFixtureGroups()` (apply to a fixture group) is a possible
  follow-up (`groupIds`), not exposed yet.
- **Engine fix**: a Gobo palette now writes the gobo wheel. `QLCFixtureHead::cacheChannels()` never
  mapped the Gobo group, so `Fixture::channelNumber(QLCChannel::Gobo)` was always invalid and Gobo
  palettes wrote nothing (on the desktop too - the "applying a Gobo palette still produces nothing"
  note above). It now maps the first coarse Gobo channel that is not a `GoboIndex`, preferring one
  with the `GoboWheel` preset; `engine/test/qlcpalette`'s `fixturesGobo` passes (no longer XFAIL).
- **`PaletteValues` units corrected** (the description said Dimmer / Zoom were 0-100 percent):
  Dimmer is DMX 0-255 (`valuesFromFixtures()` writes it as-is; the desktop IntensityTool stores
  percent * 2.55), Zoom is the beam angle in degrees (`Fixture::zoomToValues()`), Position 3D is
  metres. The web palette editor had stored percent / percent / millimetres; it now uses these units.
- Known engine quirk, not changed: `Fixture::zoomToValues()` divides by the lens range, so a
  definition with `DegreesMin == DegreesMax` (every SF3 custom definition has 0 / 0) gets a
  meaningless zoom value (0 / 0 as an integer; 255 on this build's zoom channel) - the desktop does
  the same.
