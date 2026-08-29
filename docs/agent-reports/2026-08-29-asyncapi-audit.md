# QLC+ Control API spec audit — AsyncAPI 3.0 (`docs/api-spec/`)

Read-only audit of `docs/api-spec/qlcplus-api.yaml` + the seven fragment files against the actual
engine (`engine/src/`), the qmlui UI backing classes, and the already-implemented server
(`controlapi/src/`). No files were modified. Framing: the spec's own stated purpose is to let a
client **fully replace qmlui with zero access to the C++ source**, so "the spec is ground truth"
is the standard everything below is held to.

**Provenance**: every Tier-1 finding was verified first-hand this session (engine/controlapi
source opened at the cited lines, fragment YAML quoted verbatim, merge tooling re-run against a
scratchpad copy). Tier 2–4 findings come from dedicated per-domain reviews
(fixtures+fixturedefs, functions-core, functions-advanced, virtualconsole), each grounded in
file:line citations, with the highest-impact items spot-verified first-hand. Findings already
tracked in `REVIEW-NOTES.md`, `TODO.md`, or `MERGE-PLAN.md`'s "flagged, not fixed" notes are
**not** re-reported (auth, TLS, `io.plugin.configure`, palettes, undo/redo design, learn-signal
scoping, docRevision persistence, the io.yaml `required:[params]` gaps, the inlined fixturedefs
ok-responses, the `FixtureDefs`/`fixturedefs` spelling, etc.).

## Verdict

**Not currently sufficient to build a replacement UI against — but the failure mode is a bounded
correction pass, not a redesign.** The architecture is sound and most of the spec verifies clean
against the engine (envelope, concurrency model, the large majority of enum tables, the VC widget
type list, ChaserStep/SceneValue shapes, EFX algorithm names, RGBMatrix 5-color model, all
verified correct). What breaks it today: the fragment/merged-file toolchain can no longer
regenerate its own deliverable; the connection endpoint in the spec points at the wrong server;
every endpoint that is actually implemented (blackout, grand master, universes, project lifecycle)
already diverges from its schema; several schemas make real engine values unrepresentable
(infinite/default speed sentinels, EFX ranges, attribute ranges); two enum serializations
contradict the engine's own string parsers; and two `oneOf` unions plus one YAML-corruption bug
make schema-strict clients fail validation on core messages. A client written faithfully from
this YAML would fail at the socket, then fail schema validation, then silently misbehave — in
that order.

Severity tiers: **Tier 1** = would break a client built from the spec · **Tier 2** = misleads an
implementer into wrong behavior · **Tier 3** = internal inconsistency · **Tier 4** =
polish/ambiguity.

---

## Tier 1 — would break a client built from the spec (14 findings)

**1. The fragments no longer merge, and fragments vs merged file disagree on wire-visible names —
there is currently no usable source of truth.**
Re-running `_tools/merge.py` (against a scratchpad copy; the checked-in tree was not touched)
exits 1 with **7 dangling `$ref`s**: fragment operations were renamed to point at
`functionsUpdateRequest`/`functionsUpdateResponse`/`functionsUpdatedEvent`/
`fixtureDefsSessionUpdateRequest`/`ioUniverseUpdatedEvent`, but the fragment *messages* still
carry the old keys (`FunctionsSetCommonAttributesRequest` at `fragments/functions-core.yaml:427`,
`FixtureDefsSessionSetMetadataRequest` at `fragments/fixturedefs.yaml:378`,
`IoUniverseChangedEvent` at `fragments/io.yaml:315`). Consequences, all verified:
- The README's own maintenance workflow ("re-run merge.py after editing any fragment") is broken.
- Fragment and merged file disagree on **three event topics** a client must subscribe to:
  `functions.commonAttributesChanged` vs `functions.updated`, `io.universe.changed` vs
  `io.universe.updated`, `fixturedefs.session.changed` vs `fixturedefs.session.updated`
  (fragment consts at `functions-core.yaml:504`, `io.yaml:326`, `fixturedefs.yaml:482`; merged
  consts at `qlcplus-api.yaml:9942`, `:15299`, `:8460`).
- The merged file is internally inconsistent from the same half-applied rename: the message keyed
  `FunctionsUpdateRequest` still has `method: const: functions.setCommonAttributes`
  (`qlcplus-api.yaml:9879`) paired with event topic `functions.updated` (`:9942`), contradicting
  `00-conventions.md` §8 ("use `.update`") — despite TODO 0.4 being checked off as "normalized
  across spec". Stale prose survives too (`qlcplus-api.yaml:8696` still references the
  `fixturedefs.session.changed` event that no longer exists under that name).
Fix direction: decide the post-rename names are canonical, apply the rename to the fragment
*messages* and method consts (including `functions.update`), re-run merge.py until it passes, and
diff the regenerated file against the checked-in one to confirm zero unintended reverts.

**2. The spec's connection endpoint points at the wrong server — and that port speaks a different
protocol.** `qlcplus-api.yaml` `servers.local` says `host: localhost:9999, pathname: /qlcplusapi`
(from `fragments/_skeleton.yaml:27-31`). The actual API listens on **9010**
(`controlapi/src/apiserver.h:33-40`: `#define API_SERVER_DEFAULT_PORT 9010`, whose comment
explicitly says 9999 is "webaccess's legacy remote-control server"). A client following the spec
connects to the legacy pipe-delimited webaccess protocol, not this API. Also, QWebSocketServer
does not route by path, so `pathname: /qlcplusapi` is decorative. Fix: change the server block to
9010 and document that the pathname is not enforced.

**3. Blackout: the implemented server uses field name `state` everywhere the spec says
`blackout`.** Spec (`fragments/io.yaml`): request param `blackout` required
(`:1276-1280`), `io.blackout.get`/`io.blackout.toggle` results `{blackout}` (`:1253-1258`,
`:1323-1328`), `io.blackout.changed` event data requires `blackout` (`:1345-1351`).
Implementation (`controlapi/src/domains/apiiodomain.cpp:210,216,225` and `:129`) reads/writes
`state` on all four surfaces — and `controlapi/test/apiiodomain/apiiodomain_test.cpp:200` asserts
`state`, locking the divergence in. A spec-conformant client sending `{"blackout": true}` is read
as `state=false` → **the request silently does the opposite of nothing** (sets blackout off).
Fix: pick one name (spec's `blackout`) and align impl + tests; this is exactly the drift TODO 2.9
(spec-contract validation) exists to catch.

**4. Grand Master: spec requires `{value, channelMode, valueMode}`; the implemented server sends
`{value}` only.** `IoGrandMasterState` (`fragments/io.yaml:2208-2222`) is the required result of
`io.grandMaster.get` and the required `data` of `io.grandMaster.changed`;
`apiiodomain.cpp:191-197` and `:117-123` emit only `value`. A schema-validating client rejects
every GM response/event. Fix: serialize the modes (`engine/src/grandmaster.h:50-63` —
`Limit/Reduce`, `Intensity/AllChannels`, matching the spec's strings) or make them optional.

**5. Universe messages: four implemented endpoints all violate their schemas.**
`universeToJson()` (`apiiodomain.cpp:30-38`) produces `{id, name, totalChannels, passthrough}`,
but:
- `io.universe.list` items must be `IoUniverseSummary` with 10 required fields incl. `monitor`,
  `usedChannels`, `isPatched`, `inputPatched`, `outputPatchesCount`, `hasFeedback`
  (`fragments/io.yaml:1901-1938`);
- `io.universe.get` must return `IoUniverseDetail` (summary **plus** required
  `inputPatch`/`outputPatches`/`feedbackPatch`, `:1939-1961`) — impl returns the 4-field object
  (`apiiodomain.cpp:160`);
- `io.universe.create`'s ok result requires `{universeId, docRevision}` (`:140-148`) — impl
  returns only `docRevision` (`apiiodomain.cpp:186-188`), and ignores the spec's optional `name`;
- `io.universe.created` event data must be `{universe: IoUniverseDetail, docRevision}`
  (`:165-174`) — impl broadcasts the bare 4-field object with no wrapper and no `docRevision`
  (`apiiodomain.cpp:83`).
Fix: implement the full serializers (patch info exists via `Universe`/`InputPatch`/`OutputPatch`)
or explicitly downscope the schemas — currently a client cannot even render the I/O patch page
from these responses.

**6. `core.project.loaded` is never broadcast.** The spec designates it as *the* signal to
refresh all domain state after `core.project.new/open/close` (`fragments/core.yaml:129-151`:
"this is the one place clients should refresh all their domain state from"). The implemented
handlers never emit it (`controlapi/src/domains/apicoredomain.cpp:85-90` — a comment admits the
confusion — and `:355-363` deliberately does nothing). A spec-built multi-client UI never learns
another client replaced the document. Fix: broadcast it from the three handlers (and from any
server-side load path), with the spec's `{reason, project}` payload.

**7. Speed sentinel values are undocumented and unrepresentable.** Every speed field is
`type: integer, minimum: 0` with "milliseconds" semantics (`fragments/functions-core.yaml:457-466`,
`FunctionsDetail :2729-2734`, ChaserStep/SequenceStep `:2871-2882`/`:2919-2931`), but the engine
encodes **infinite** as `(uint)-2` and **default** as `(uint)-1`
(`engine/src/function.cpp:843-851`; infinite hold handled at `engine/src/chaserstep.cpp:248-261`).
"Hold forever" is one of the most common step configurations in real shows; a spec-built client
can neither send it nor render it (it displays 4294967294 ms). Fix: document the sentinels or
define an explicit `null`/`"infinite"` wire encoding on every speed field.

**8. `functions.adjustAttribute` clamps `value` to 0..1; real attribute ranges go far outside
that.** `fragments/functions-core.yaml:688-691` (`minimum: 0, maximum: 1`) vs engine attribute
registrations: EFX Width 0–127, Rotation 0–359, offsets 0–255 (`engine/src/efx.cpp:57-62`),
Video volume 0–100 and X-rotation **-360**..360 (`engine/src/video.cpp:61-68`), RGBMatrix color
-1..16777215 (`engine/src/rgbmatrix.cpp:92-97`). The spec's own `FunctionsAttribute` schema even
carries per-attribute `min`/`max` fields (`:2648-2667`) that the request constraint contradicts.
Fix: drop the 0..1 clamp (or make it Intensity-only) and reference `FunctionsAttribute.min/max`.

**9. EFX numeric ranges contradict the engine.** `width`/`height` specified 0..255
(`fragments/functions-core.yaml:1226-1233`) but the engine clamps to 0..127
(`engine/src/efx.cpp:503,518`); `xFrequency`/`yFrequency` specified 0..5 (`:1252-1259`) but the
engine and the real editor allow 0..32 (`efx.cpp:617,628`;
`qmlui/qml/fixturesfunctions/EFXEditor.qml:781-782`). A spec-validating client rejects
frequencies 6–32 that existing show files legitimately contain. Fix: align to 0..127 / 0..32.

**10. `fixturedefs` channel `group` enum omits 9 real channel groups — and the engine's strings
contain spaces.** `fragments/fixturedefs.yaml:1209` enumerates 12 groups; the engine also has
`PositionX/Y/Z`, `RotationX/Y/Z`, `ScaleX/Y/Z` (`engine/src/qlcchannel.h:204-228`), serialized as
`"Position X"`, `"Rotation Y"`, `"Scale Z"` (`engine/src/qlcchannel.cpp:45-53`). Any definition
using them fails schema validation on read and cannot be authored via the API. Fix: extend the
enum using the engine's canonical spaced strings (not `PositionX`-style tokens).

**11. Fixture type has two incompatible serializations, and `fixturedefs`' contradicts the
engine's parser.** `fragments/fixturedefs.yaml:1280` (and `:1309`) enumerate compact tokens
(`ColorChanger`, `MovingHead`, `LEDBarBeams`...); the engine's `typeToString()`/`stringToType`
(`engine/src/qlcfixturedef.cpp:166-185`) use `"Color Changer"`, `"Moving Head"`,
`"LED Bar (Beams)"` — which is what `fixtures.yaml:141-143` correctly documents. A client
comparing `fixtures.get`'s type against `fixturedefs.list`'s type never matches, and a server
feeding the token form through `stringToType` silently gets the fallback type. Fix: standardize
on the engine's `typeToString()` strings in both fragments.

**12. `vc.slider.flash`'s parameter is literally named `true`.**
`fragments/virtualconsole.yaml:1822-1831`: `required: [widgetId, true]` and a property whose key
is `true:` — a YAML-1.1 round-trip corruption of an unquoted `on` (engine signature
`VCSlider::flashFunction(bool on)`, `qmlui/virtualconsole/vcslider.h:379`). Verified propagated
into the checked-in `qlcplus-api.yaml` (the `VcSliderFlashRequest` block after line 18622).
`required` entries must be strings, so validators/generators reject the message, and a hand-built
client has no parameter name. Fix: rename to a quoted `"on"` (or `flash`) — note an unquoted
`on` will re-corrupt on the next PyYAML round-trip.

**13. `VcWidgetDetail.typeConfig`'s `oneOf` fails validation for every widget.**
`fragments/virtualconsole.yaml:3938-3955`: eleven branches, none with any `required` field or
discriminator, `additionalProperties` open — an empty config matches all branches and a plain
Frame config also matches `VcSoloFrameConfig` (`:4256-4266` adds only optional fields), so
`oneOf`'s exactly-one rule rejects **every** `vc.widget.get`/`created`/`updated` payload under a
strict validator. Same latent class as the Sequence/Chaser `typeDetail` overlap (Tier 2 #9).
Fix: switch to `anyOf` + "interpret per `widgetType`", or add a required type tag per config.

**14. No way to read any VC widget's live state — a client can never render its initial UI.**
The only per-widget read, `vc.widget.get` → `VcWidgetDetail`
(`fragments/virtualconsole.yaml:3927-3984`), carries zero §4b state: no slider `value`
(`qmlui/virtualconsole/vcslider.h:43`), button `state` (`vcbutton.h:34`), xyPad position
(`vcxypad.h:44,49`), cuelist playback status/index (`vccuelist.h:55-58`), frame `currentPage`
(`vcframe.h:63`), speedDial current time/factor, animation fader level, audioTriggers
`captureEnabled`. A client joining a running show sees every fader/button/cue at unknown state
until each happens to change. The spec already solves this for pages (`vc.page.list` returns
`selectedPage`) — the pattern is simply absent for widgets. Untracked anywhere. Fix: add live
state to `vc.widget.get`'s result (a `liveState` sibling of `typeConfig`) or a
`vc.widget.getState` snapshot method.

---

## Tier 2 — misleads an implementer (30 findings)

### Protocol / envelope

**15. Three error codes the server actually uses are documented nowhere.**
`controlapi/src/apienvelope.cpp:28-30` defines and the core domain uses `INTERNAL_ERROR`,
`INVALID_STATE` (save-before-saveAs), `NOT_IMPLEMENTED` (saveAs download);
`00-conventions.md` §7 lists only five codes. Related contract gaps: unknown methods return
`NOT_FOUND` (`apidispatcher.cpp:60-62`) though §7 defines NOT_FOUND as "resourceId doesn't
exist", and malformed frames are **silently dropped with no response**
(`apidispatcher.cpp:41-48`), contradicting §2's "always exactly one [response], correlated by
id" — a client with a request-timeout map needs to know this. Fix: extend §7 and document the
drop rule.

**16. `originClientId` is always null on engine-signal-driven broadcasts — including ones caused
by a client's own request.** All io/core broadcasts wired through engine signals pass an empty
origin (`apiiodomain.cpp:83,114,122,129`; `apicoredomain.cpp:352`), yet §3/§9 tell clients to
apply state from events and use `originClientId` to skip their own echo (e.g. not re-animating a
fader they're dragging). As implemented that promise is undeliverable for
mode/blackout/GM/universe events. Fix: either plumb the requesting session through to the
broadcast, or downgrade the conventions text to "best effort, may be null even for
client-caused changes".

**17. The DMX subscribe key is never actually specified.** `00-conventions.md` §5's example
topic is `dmx.universe.1` (no `io.` prefix, no `.changed`); the real topic is
`io.dmx.universe.<id>.changed` (`fragments/io.yaml:1428`, matching `apiiodomain.cpp:88`), and
`io.yaml:1429-1430` defers to "io-notes.md §5 for the subscribe key" — a section that does not
exist in `io-notes.md`. Subscription matching is exact-string (`apisession.cpp:61-64`), so a
client guessing from the conventions example receives nothing, silently. Fix: fix the §5
example, document exact-match semantics, and state the key (`io.dmx.universe.<id>.changed`,
0-based universe id) in the event description itself.

**18. `fixtureId` is a string in three domains and an integer in the fourth.**
`fixtures`/`functions`/`vc` use `fixtureId: {type: string}` throughout
(`fragments/fixtures.yaml:267`, `functions-core.yaml:1871-1872`, `virtualconsole.yaml:2052-2053`);
`IoSimpleDeskChannel.fixtureId` is `[integer, null]` (`fragments/io.yaml:2254-2257`). A client
keying fixtures by id gets a type mismatch exactly where Simple Desk needs to link a channel to a
fixture. `fixtures-notes.md:17-23` even flagged "reconcile at merge time"; io.yaml was missed.

### functions-core

**19. `typeDetail`'s Chaser/Sequence `oneOf` rejects every empty Sequence.**
`FunctionsChaserDetail`'s required set is a strict subset of `FunctionsSequenceDetail`'s fields
(`fragments/functions-core.yaml:2887-2892` vs `:2938-2944`, no `additionalProperties: false`), so
a 0-step Sequence — every freshly created one — matches both branches and fails `oneOf`
(`:2750-2760`). Same hazard in `functions.steps.addStep/replaceStep`'s `step: oneOf`
(`:2469-2471`, `:2513-2515`). Fix: `anyOf` + discriminate by the `type` field, or close the
schemas.

**20. `hold` vs `duration` invariant undocumented.** Both required on chaser/sequence steps
(`functions-core.yaml:2860-2865`, `:2908-2913`) but the engine derives
`duration = fadeIn + hold` with infinite-aware reconciliation where hold wins
(`engine/src/chaserstep.cpp:32-37`, `:248-261`). Nothing says which field wins on disagreement.

**21. Creating a Sequence is underspecified.** `FunctionsSequenceDetail` requires
`boundSceneId`, and the real UI auto-creates a hidden bound Scene on Sequence creation
(`qmlui/functionmanager.cpp:289-314`); `FunctionsCreateRequest`
(`functions-core.yaml:101-140`) says nothing about whether the server auto-creates it or a
`sceneId` must be supplied. An implementer with no C++ access cannot get this right.

**22. "Index 0 is always Intensity" is false for Show functions.**
`functions-core.yaml:2656` and `:684` vs `engine/src/show.cpp:51`
(`unregisterAttribute(tr("Intensity"))`) — a client hard-coding attribute 0 = master intensity
misdrives Shows.

**23. No `functions.clone`.** `FunctionManager::cloneFunctions()`
(`qmlui/functionmanager.h:171`) is a first-class UI operation; no clone/duplicate method exists
in either functions fragment. Client-side reconstruction is racy under §4a (every intermediate
call bumps `docRevision`) and lossy (no wholesale step-list setter at create time).

### fixtures / fixturedefs

**24. `isGeneric`'s specified test is wrong for QLC+ 5.** `fragments/fixtures.yaml:1197-1199`
says "Fixture::fixtureDef() == null", but qmlui always assigns a procedural def to generic
dimmers (`qmlui/fixturemanager.cpp:319-322`, `engine/src/fixture.cpp:182-196`, def built with
manufacturer/model `"Generic"` at `fixture.cpp:844-849`); the engine's real discriminator is the
`Generic`/`Generic` name pair (`fixture.cpp:692-694`). A server implementing the spec literally
reports `isGeneric:false` for every dimmer.

**25. `fixtureType: "Generic Dimmer"` is never an engine type string.**
`fixtures.yaml:1191-1193` — `Fixture::typeString()` returns `"Dimmer"` on both code paths
(`engine/src/fixture.cpp:105-111`, `fixture.h:56`).

**26. The bulk-patch name-suffix claim is wrong on both counts.** `fixtures.yaml:364-368` says
the engine appends `" [<n>]"` per instance when quantity > 1; actual code applies it
**unconditionally** and uses **fixture id + 1**, not the instance ordinal
(`qmlui/fixturemanager.cpp:335`). "Mirroring the spec" produces names that differ from the real
UI's.

**27. No API to change a patched fixture's mode.** `FixtureManager::setFixtureModeIndex`
(`qmlui/fixturemanager.cpp:1021`, reachable from `FixtureNodeRow.qml:202`) switches a live
fixture's mode in place; `fixtures.update` covers only universe/address/name
(`fixtures.yaml:485-486`), and this isn't among `fixtures-notes.md`'s documented scope cuts.
Unpatch+repatch changes the fixture id and breaks references.

**28. Colour token `NoColour` vs engine string `"Generic"`.** `fixturedefs.yaml:1212` enum says
`NoColour`; `colourToString(NoColour)` returns `"Generic"`
(`engine/src/qlcchannel.cpp:945-974`, constant at `:55`), and `fixtures.yaml:1140` says "as
string" — the two halves of the spec produce values the other rejects, on every non-Intensity
channel.

**29. `fixturedefs.delete` on a bundled (system) definition is undefined and
engine-unsupported.** `fixturedefs.yaml:90-107` — `QLCFixtureDefCache` has no removal API at all
(`engine/src/qlcfixturedefcache.h:111-129`), system defs live in a read-only directory, and no
error code is specified (`FIXTUREDEFS_SYSTEM_READONLY` is mentioned only for save, `:371`).

### functions-advanced

**30. `functions.script.validate` mixes two line-number spaces and documents neither
correctly.** `functions-advanced.yaml:420` says "0-based line numbers"; engine syntax errors are
**1-based over all source lines including blanks** (`engine/src/script.cpp:151,164`), while
`functionRefs`/`fixtureRefs` are 0-based indices into a blank-skipping tokenized list
(`script.cpp:160-162,221-251`). A source editor highlighting lines cannot be built correctly.

**31. `functionRefs` promises four commands; the engine helper reports one.**
`functions-advanced.yaml:423` lists
startfunction/stopfunction/waitfunctionstart/waitfunctionstop; `Script::functionList()` matches
only `startFunctionCmd` (`script.cpp:227`). A thin server binding silently under-reports.

**32. `functions.show.track.setSolo(solo=false)` semantics wrong.**
`functions-advanced.yaml:1107-1109` says it "just unmutes this one";
`ShowManager::setTrackSolo` (`qmlui/showmanager.cpp:358-372`) **unmutes every track** when
solo=false (verified first-hand).

**33. `blendMode` is defined twice with different casings and two write paths.**
`functions-advanced.yaml:3146-3152` (`normal/mask/additive/subtractive`, used by
`FunctionsRgbMatrixConfig`) vs `functions-core.yaml:2561-2567` (`Normal/Mask/Additive/...`, used
by `FunctionsDetail` and `functions.setCommonAttributes`). Same persisted engine field
(`RGBMatrix::setBlendMode` overrides the Function-level property, `rgbmatrix.h:341`); copying the
value from a `functions.get` response into the RGBMatrix config fails validation. (Verified
first-hand.)

**34. No per-algorithm accepted-color count.** `listAlgorithms` items
(`functions-advanced.yaml:50-65`) omit `RGBAlgorithm::acceptColors()`
(`engine/src/rgbalgorithm.h:101-106`), which drives the real editor's dynamic color-slot count
(`qmlui/rgbmatrixeditor.h:41,88`); a spec-built client must show all 5 slots blind.

**35. Show playhead is read-only — no seek/scrub.** `FunctionsShowPlayheadEvent`
(`functions-advanced.yaml:1969`) has no write counterpart; qmlui repositions/seeks via
`ShowManager::setCurrentTime` (`qmlui/showmanager.h:63,184-185`). A transport-bar UI cannot
scrub.

### virtualconsole

**36. `vc.xyPad.setPosition` units contradict the engine and the fragment's other three
position sites.** `virtualconsole.yaml:1903-1905` says normalized 0.0–1.0 with `maximum: 1`; the
engine's domain is DMX 0..255(+fraction) (`qmlui/virtualconsole/vcxypad.cpp:103-104`,
`:333-336`), and `positionChanged` events / config ranges / presets (`:1945-1951`, `:4136-4145`,
`:4210-4216`) state no units — a client cannot legally echo a received position back into
`setPosition`.

**37. `vc.button.press` click semantics ambiguous — a natural client double-toggles.**
`virtualconsole.yaml:1645-1651` invites a press/release pair per click; the engine toggles on
**every** invocation for Toggle/Blackout (`vcbutton.cpp:387-426`, `:446-450`) and qmlui sends one
call per click (`VCButtonItem.qml:143-145`), pairs only for Flash. Blackout via a press/release
pair is a net no-op. The spec must state the one-message-per-click rule.

**38. `vc.widget.createMatrix` accepts all 11 widget types; the engine supports two.**
`virtualconsole.yaml:696-697` refs the full `VcWidgetType`; `vcframe.cpp:456` handles only
button/slider matrices (`vcframe.h:123`).

**39. The page root is unreachable for `createMatrix`/`createFromFunctions`.** Both require
`parentId` (`virtualconsole.yaml:684-690`, `:783-788`) but no message exposes a page-root widget
id (`VcPage` `:4004-4016`; `parentId` "absent when placed directly on the page root",
`:3911-3914`); `vc.widget.create` handles the absent case (`:589`) — the other two creators
can't target an empty page, a stock gesture (`VCPage` IS-A `VCFrame`, `vcpage.h:27`).

**40. Clock `weekFlags` misses the repeat bit (0x80); `stopTime = -1` undocumented.**
`virtualconsole.yaml:4301-4303` documents bits 0–6 only; the engine gates once-only vs repeat on
bit7 and masks days with 0x7F (`vcclock.cpp:415-434,420`); `stopTime` is required
(`:4285-4290`) but the engine default -1 = none (`vcclock.cpp:681`) is never stated.

**41. Slider Click&Go mismodeled: colors aren't persisted, and the actual pick gesture has no
API.** `VcSliderConfig.cngPrimaryColor/cngSecondaryColor` are doc-state fields
(`virtualconsole.yaml:4107-4110`) but `VCSlider::saveXML` persists only the type
(`vcslider.cpp:1809`); the real operations `setClickAndGoColors()` (which also live-forces
value 128, `vcslider.cpp:870-882`) and `setClickAndGoPresetValue()` (`vcslider.h:340-341`) have
no spec method.

**42. No way to release a Level-monitor override.** The engine exposes reset three ways
(`vcslider.h:49`, `vcslider.cpp:574-586`, `VCSliderItem.qml:191`, external control
`vcslider.cpp:57,1610-1612`); the spec only *reports* `isOverriding`
(`virtualconsole.yaml:1797-1804`), with no request to clear it.

**43. Input-source feedback extra params absent.**
`VCWidget::updateInputSourceExtraParams(...)` and the persisted Lower/Upper/MonitorParams
(`vcwidget.h:562-563`, `:66-68`) have no counterpart in `VcInputSource`
(`virtualconsole.yaml:3985-4003`) or `vc.widget.inputSource.set` (`:1347-1380`).

**44. `vc.widget.preset.apply` semantics defined only for XYPad.**
`virtualconsole.yaml:3743-3744` describes `VCXYPad::applyPreset()`; the merged method serves
three widget types (MERGE-PLAN #2) but `VCSpeedDial` has no applyPreset at all
(`vcspeeddial.h:251-254`) — its server-side behavior is stated nowhere, nor is
`VCAnimation::applyPreset`'s.

---

## Tier 3 — internal inconsistencies (23 findings)

**45.** Same `QLCPhysical` fields named differently across fragments:
`fixtures.yaml:1095-1096` `bulbColourTemperature`/`lensName` (matching
`engine/src/qlcphysical.h:94-95,109-110`) vs `fixturedefs.yaml:1132,1137`
`bulbColorTemperature`/`lensType`; `FixturesPhysical` also lacks the `layoutWidth`/`layoutHeight`
that `FixtureDefsPhysical` has (`fixturedefs.yaml:1143-1148`), so mode browsing can't show a
pixel-bar's head layout.

**46.** Same `QLCCapability`, two different read shapes: `FixturesCapability`
(`fixtures.yaml:1103-1123`, `color1`/`color2`/`presetType`) vs `FixtureDefsCapability`
(`fixturedefs.yaml:1163-1197`, raw `resources`/`aliases`/`warning`) — two parsers for one engine
object, nothing cross-references them.

**47.** `warning` enum token `None` vs engine `NoWarning`
(`fixturedefs.yaml:1196` vs `engine/src/qlccapability.h:222-227`).

**48.** `functions.channelsgroup.setLevel`'s description points at a notes discussion that
doesn't exist, and `functions-core-notes.md` asserts the opposite tier (§4b) of what the YAML
implements (§4a with `baseRevision`, `functions-core.yaml:2246-2253`) — also means fader drags
conflict-storm under §4a.

**49.** `typeDetail` is not in `FunctionsDetail.required` (`functions-core.yaml:2695-2709`)
even though MERGE-PLAN #1 removed the per-type gets on the premise it's always present.

**50.** `path` semantics ambiguous: `Function::path()` defaults to a type-prefixed form
(`engine/src/function.cpp:303-308`) while `setPath` strips the prefix — the spec never says
which coordinate system wire paths use (`functions-core.yaml:2688-2689`, `:369-371`).

**51.** Attribute override lifecycle unmodeled: the engine distinguishes base-value writes from
runtime overrides with an explicit release call (`engine/src/function.h:885-927`);
`functions.adjustAttribute` doesn't say which it does, and `overridden`/`overrideValue`
(`functions-core.yaml:2668-2671`) have no clearing operation.

**52.** "Either attributeIndex or attributeName is required" is prose-only — the params schema
requires neither (`functions-core.yaml:676-684`).

**53.** `FunctionsShowTracksChangedEvent` references removed method `functions.show.get`
(`functions-advanced.yaml:1215-1216`; deleted by MERGE-PLAN #1).

**54.** `FunctionsShowItemsChangedEvent` contradicts itself on the JSON Patch target: "flattened
item list" (`functions-advanced.yaml:1935`) vs "tracks[].items arrays" (`:1959`).

**55.** RGBMatrix `saveToSequence` (user-reachable: `qmlui/rgbmatrixeditor.h:66`,
`RGBMatrixEditor.qml:229`) has no API surface and cannot be replicated client-side without the
algorithm engine.

**56.** `customGeometry`: omitted-vs-null undefined on both request and event sides
(`functions-advanced.yaml:2622-2632`, `:2689-2691`).

**57.** `functions.show.item.add` default duration "the function's own totalDuration" breaks
for looping functions with totalDuration 0 — qmlui falls back to 5000/4000 ms
(`qmlui/showmanager.cpp:502,509`); spec silent.

**58.** Eight VC descriptions are corrupted by the same YAML round-trip as Tier-1 #12: sentence
tails became stray mapping keys with `null` values, truncating the real text (e.g.
`virtualconsole.yaml:2922-2923` "…orientation" + `see VcCueListConfig.sideFaderMode.: null`;
also `:3877-3878`, `:3883-3884`, `:3913-3914`, `:4032-4033`, `:4263-4264`, `:4279-4280`,
`:4371-4372`). Verified propagated into `qlcplus-api.yaml` (e.g. lines 19733, 23131). The lost
halves are exactly the null-semantics/conditionality text the fields need.

**59.** Contradictory VC event routing: `VcWidgetUpdatedEvent` and `VcWidgetsBulkUpdatedEvent`
both claim to fire on `vc.widget.reparent` (`virtualconsole.yaml:887-889` vs `:1309-1310`), and
`createMatrix`/`createFromFunctions` results arrive on the `vc.widget.bulkUpdated` topic
(`:4769-4776`) — a client adding widgets only on `vc.widget.created` misses all bulk-created
widgets.

**60.** Clock event cadence wrong by 10x: "broadcast every second"
(`virtualconsole.yaml:2566-2568`) but a running Stopwatch/Countdown ticks at 100 ms
(`vcclock.cpp:279`, verified first-hand) — changes the subscribe-gating cost analysis.

**61.** `VcXyPadPreset` can't represent multi-head presets (engine `m_fxGroup` is a list,
`vcxypadpreset.h:87`, `vcxypadpreset.cpp:213`; spec has singular `fixtureId`/`headIndex`,
`virtualconsole.yaml:4192-4224`).

**62.** `VcAnimationPreset` enum omits legacy `Color1Reset..Color5Reset` control types loadable
from VCMatrix XML (`vcanimationpreset.h:41-43,66-70` vs `virtualconsole.yaml:4447-4453`).

**63.** `VcAnimationConfig.algorithmIndex` indexes a list (`VCAnimation::algorithms()`,
`vcanimation.h:197-201`) that no vc message exposes; the only catalog is
`functions.rgbmatrix.listAlgorithms` and nothing states the orderings coincide
(`virtualconsole.yaml:4431-4432`).

**64.** Multipage input-channel encoding undefined: the engine folds the frame page into the
channel's upper bits (`vcpage.h:88-95`; `vcwidget.h:570-573`); `VcInputSource.channel`
(`virtualconsole.yaml:3996-3997`) never says whether it's the bare or composited value.

**65.** `typeConfig` description says "discriminated by VcWidgetSummary.type" — the field is
`widgetType` (`virtualconsole.yaml:3935` vs `:3907-3908`).

**66.** `CoreLogEvent` multi-topic publication semantics undefined: topics
`core.log` + `core.log.<level>` (`fragments/core.yaml:687-694`) — whether one log line is
published once or on both the firehose and per-level topic (duplicate delivery for a client
subscribed to both) is unstated, and subscription matching is exact-string.

**67.** `README.md:5` says "712 messages, 112 schemas"; the actual document (and a fresh merge)
has 710 messages / 113 schemas — stale counts in the top-level orientation doc.

---

## Tier 4 — polish / ambiguity (17 findings)

**68.** `00-conventions.md` has two sections numbered "9" ("Session handshake, identity, auth"
and "What to actually go look at"), and `fragments/io.yaml:1687` cites "the single-operator-app
assumption in 00-conventions.md §8" — which is now the verbs section.

**69.** Operation-key naming: the merged step/preset operations keep a `Request` suffix
(`functions-core.yaml:3656-3692` `sendFunctionsStepsRemoveStepRequest` etc.;
`virtualconsole.yaml:5570-5594` `sendVcWidgetPresetAddRequest` etc.; also io's
`sendIoPatchRemoveRequest`/`sendIoPatchSetRequest`, `io.yaml:2738,2750`) unlike every other
`send*` operation — consolidation-script artifact.

**70.** `FunctionsAttribute` omits the engine's attribute flags
(`Multiply`/`LastWins`/`Single`, `engine/src/function.h:82`), so a client can't show how
concurrent adjustments combine.

**71.** `FunctionsEfxFixtureModeEnum` value `PanTilt` vs engine XML string `"Position"`
(`functions-core.yaml:2604-2610` vs `engine/src/efxfixture.h:48`) — fine as a clean-slate
choice, but the server-side mapping requirement deserves a note (other enums match verbatim).

**72.** ChaserEditor's bulk step ops (multi-move, duplicate, shuffle —
`qmlui/chasereditor.h:87,95,103`) require N sequential revision-gated calls, each able to
CONFLICT mid-batch.

**73.** `fixturedefs.yaml` points three times at rationale that the reconstructed notes file
doesn't contain (`:371` FIXTUREDEFS_SYSTEM_READONLY, `:176` base64 rationale, `:617` synthetic
ids) — for a zero-C++ implementer, `FIXTUREDEFS_SYSTEM_READONLY` is defined nowhere.

**74.** Channel rename vs name-addressed aliases unspecified (`fixturedefs.yaml:816` aliases
address channels by name; `:641` allows renaming; only mode-rename documents re-validation,
`:968`).

**75.** `fixturedefs.mode.update` is rename-only under the wrong verb (required `name`,
described "Renames a mode", `fixturedefs.yaml:967-973`) — a leftover from the 0.4 sweep that
§8's `.rename` rule says shouldn't exist.

**76.** `fixturedefs.list` requires `channelCount/modeCount/defRevision` per row
(`fixturedefs.yaml:1302`) but the cache is lazy-loading
(`engine/src/qlcfixturedef.cpp:197-211`, `qlcfixturedefcache.h:92`) — serving one list call
means parsing thousands of QXF files; worth a spec warning.

**77.** Generic placeholder cache entries (`Generic`/`Generic`, `Generic`/`RGBPanel`,
`qlcfixturedef.cpp:203-208`) — unstated whether they appear in defs listings and what
`getModel` returns for them.

**78.** Audio/video capability extension format unstated: spec examples are bare (`'wav'`,
`functions-advanced.yaml:2041`), engine decoders return globs (`"*.mp3"`,
`engine/audio/plugins/mad/audiodecoder_mad.cpp:423`).

**79.** `functions-advanced.yaml:669` prose names the C++-style "BPM_* values"; the actual enum
values are `bpm_4_4` etc. (`:3186-3190`).

**80.** Show item overlap-rejection error code ambiguous ("CONFLICT-style INVALid_PARAMS",
`functions-advanced.yaml:1563-1564`; `item.move` at `:1458-1459` names none).

**81.** RGB text font is a QFont subset (family/pointSize/bold/italic only,
`functions-advanced.yaml:3073-3084`) vs the editor's full QFont round-trip
(`qmlui/rgbmatrixeditor.h:49`) — underline/weight silently dropped.

**82.** `functions.show.item.resize` has no counterpart to qmlui's stretch-on-resize mode
(`qmlui/showmanager.h:52,108-110`), including its Chaser Common→PerStep conversion
(`showmanager.h:362-363`).

**83.** `vc.widget.inputDetect.stop` has no description/widgetId, and start's "on behalf of the
requesting client" conflicts with the engine's single global autodetect slot
(`virtualconsole.yaml:1603-1619`, `:1587-1590`; `qmlui/virtualconsole/virtualconsole.h:333-337`).

**84.** `virtualconsole-notes.md:7` still says "190 messages / 25 schemas / 182 operations" —
stale since MERGE-PLAN #2 (now 178 messages / 28 schemas / 170 operations).

---

## qmlui capabilities with NO API coverage at all

Ordered by impact; tracked-status stated per item.

1. **2D/3D stage views — `MonitorProperties` (untracked anywhere).** Per-fixture (even
   per-head + linked-copy) positions, rotations, gel colors, plus environment/grid setup —
   saved into the `.qxw` (`engine/src/monitorproperties.h:66,180-185,338-347`), edited
   constantly via `ContextManager`/`MainView2D`/`MainView3D`. Grep across all of
   `docs/api-spec/` finds zero coverage; no fragment, notes file, or TODO item mentions it
   (`fixtures-notes.md:113` even acknowledges remap touches monitor properties — data no
   client can read or write). A replacement UI cannot render or edit the stage views at all.
   This is the single largest untracked scope hole and deserves a TODO Phase-2 entry of its
   own (likely a `fixtures.monitor.*` or new `monitor.*` sub-domain).
2. **VC widget live-state snapshot (untracked)** — see Tier-1 #14; listed here because it is
   a whole missing read surface, not a wrong schema.
3. **Project import — `ImportManager` (untracked).** Importing fixtures/functions/VC content
   from another `.qxw` (`qmlui/importmanager.cpp`, wired in `qmlui/app.cpp:1265`) has no API
   surface and no notes/TODO mention.
4. **Server-side file browsing (untracked).** `core.project.open source=path` and
   `saveAs target=serverPath` assume the client knows engine-machine paths
   (`fragments/core.yaml:61-73`, whose own description imagines "a server-side project
   library"), but there is no directory-listing/browse method — a remote client cannot
   populate an Open dialog. (qmlui's equivalent is `folderbrowser.cpp`.)
5. **Global beat source + BPM (notes-only, never promoted to TODO).**
   `InputOutputManager::beatType` (internal/MIDI/audio generator) and `bpmNumber`
   (`qmlui/inputoutputmanager.h:58-59,209-215`) — the tempo every `tempoType: Beats` function
   syncs to. Flagged in `virtualconsole-notes.md:81-88` ("Neither io.yaml nor core.yaml
   currently defines any *.bpm.* message"; `vc.speedDial.tap` depends on it), but absent from
   `TODO.md` — at risk of being lost.
6. **Audio output device enumeration (notes-only).** `functions.audio.setDevice` takes an
   opaque id but nothing lists devices — flagged in `functions-advanced-notes.md:58-64`, not
   in TODO.
7. **Channel modifiers / forced HTP-LTP / exclude-from-fade (notes-only).** Real per-channel
   patch settings in `FixtureProperties.qml`, deliberately deferred in
   `fixtures-notes.md:46-57` with a suggested follow-up method — not in TODO.
8. **Generic RGB panel wizard (notes-only, deliberate cut).** `FixtureManager::addRGBPanel`
   (`fixtures-notes.md:58-67`) — documented scope cut with a suggested shape; fine, but also
   absent from TODO.
9. **Already tracked (no new severity information):** palette/color-filter definitions
   (TODO 2.4), undo/redo implementation strategy (TODO 2.5 — note the spec side,
   `core.undo`/`core.redo`/`core.history.get`, is fully specified; only the implementation
   design is open), authentication (2.1), TLS (2.2), `io.plugin.configure` engine gap (2.3).

Also missing as individual methods (reported in the tiers above, cross-referenced here for
completeness): `functions.clone` (#23), change-patched-fixture-mode (#27), Show playhead seek
(#35), slider Click&Go pick + monitor-override release (#41, #42), RGBMatrix `saveToSequence`
(#55).

---

*Method note: structural validation (broken refs, operation wiring, channel message map,
fragment-vs-merged key/content diff, method/topic const extraction) was done with scratchpad
Python against PyYAML-parsed copies; `merge.py` was run only against a scratchpad copy with its
output path redirected — the checked-in `qlcplus-api.yaml` was never regenerated or touched.*
