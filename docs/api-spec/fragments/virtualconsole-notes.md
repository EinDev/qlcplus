# Virtual Console fragment - notes

Grounded in `qmlui/virtualconsole/*.h/.cpp` (every header read in full; .cpp
read for `vcbutton`, `vcslider`, `vcxypad`, `vcspeeddial`, `vcframe` to
confirm behaviour the headers alone didn't make clear) and cross-checked
against `qmlui/qml/virtualconsole/*.qml`. Component-key prefix `Vc`,
method/topic prefix `vc.`. 184 messages / 31 schemas / 174 operations (per a
fresh `merge.py` run on 2026-09-26 - re-derive with the same command if this
fragment changes again, rather than trusting this number indefinitely; it has
drifted from an original 190/25/182 via MERGE-PLAN #2's consolidation passes,
then to 178/28/170, then to the current count when the live-interaction slice
was implemented).

## Channel message keys to add at merge time

Per `_example.yaml`'s closing note, every `Vc*` key in `messages:` needs a
matching lowerCamelCase entry added under `channels.qlcplus.messages` in the
merged skeleton (e.g. `vcButtonPressRequest: { $ref: '#/components/messages/VcButtonPressRequest' }`).
There are 178 of them (mechanical rename, first letter lowercased) - not
listing all individually here, but flagging that this fragment is large
enough that the merge script should almost certainly generate this mapping
rather than hand-transcribe it.

## Two-tier classification judgment calls (please sanity-check)

- **`vc.page.select` / `vc.frame.gotoPage`** (which top-level VC page, and
  which internal page of a multi-page Frame, are showing) are modelled as
  **live (§4b)**, directly per 00-conventions.md §4b's own example ("which
  VC page is showing"). **Implementation finding (2026-09-26):** for
  `vc.frame.gotoPage` this is only half true in the engine - `VCFrame::
  setCurrentPage()` persists the page into the show file and calls
  `setDocModified()`, so every real page flip bumps `docRevision` as a side
  effect (see "Live interaction - implemented" below). `vc.page.select` is
  genuinely ephemeral. In reality each operator's screen could reasonably
  show a *different* page independently, but `VirtualConsole::selectedPage`
  and `VCFrame::currentPage` are single engine-side values (not per-client),
  so a shared broadcast is the only option that matches current engine
  behaviour. If a future Electron client wants per-window page navigation
  without affecting other clients, that's a client-local UI concern (just
  don't apply the broadcast) - the *engine's* concept of "current page" stays
  single-valued until/unless the engine itself changes.
- **`isDisabled` / `isCollapsed` / `showHeader` / `showEnable` /
  `multiPageMode` / PIN** on `VCFrame` are all confirmed **document-state**
  (§4a) by checking `vcframe.cpp::saveXML` - e.g.
  `KXMLQLCVCFrameIsDisabled` is written to the show file. This is a bit
  counter-intuitive since toggling a frame's "Enable" button is a very live,
  in-the-moment show-control gesture (much like Blackout), but unlike
  Blackout it IS persisted, so it goes through `vc.widget.update` with
  `baseRevision` rather than a live/ack-only message. Flagging in case the
  repo owner wants this to behave more like Blackout (ephemeral) instead of
  matching the engine's actual (arguably surprising) persistence behaviour.
- **VCAnimation colors/algorithm** (`vc.animation.*` config) are treated as
  document-state even though editing them via a live color picker *feels*
  identical to dragging a live fader. Justification: they are XML-persisted
  (`KXMLQLCVCAnimationStartColor`/`EndColor`), and this mirrors how
  `functions-core.yaml` already treats Scene channel-value dragging
  (`functions.scene.setValue`, also doc-state despite being edited by drag).
  Client is expected to debounce rapid drags into a reasonable number of
  `vc.widget.setConfig` calls rather than one per pixel of drag, exactly as
  presumably assumed for Scene editing already.
- **`VCAudioTriggers::captureEnabled`** is treated as **live** (whether the
  mic/line-in is actively being read) on the reasoning that audio hardware
  capture shouldn't silently auto-start just because a show file loaded with
  it previously turned on. `volumeLevel` (gain/threshold calibration) and
  `barsNumber`/per-bar config are treated as document-state. This is a
  judgment call, not confirmed against an XML save/load code path the way
  the Frame-disabled case above was - flagging for review.
- **`VCAnimation` preset-knob live value** (`vc.animation.setPresetKnobValue`)
  is treated as live/ephemeral (not written back into the preset's stored
  base color) - reasoning from the header comment on
  `VCAnimationPreset::valueToRgb`/`rgbToValue` being a live transform, not a
  persisted field, but not independently verified against the .cpp.

## Live interaction - implemented (2026-09-26)

`controlapi/src/domains/apivcdomain.cpp` (`registerLiveMethods()`) now
registers the live-interaction slice a browser web UI needs, against the
CONTRACT the web UI agents were handed. Where that contract differed from
this fragment's original (never implemented) drafts, **the contract won and
the fragment was rewritten to match** - a client written against the old
drafts would have broken anyway, since nothing ever served them:

| Was in the draft | Is now (implemented) |
| --- | --- |
| `vc.button.press`: one call per click for Toggle/Blackout/StopAll, edge pair only for Flash | edge pair (`pressed` true on down, false on up) for **every** actionType; the server filters: Toggle/Blackout/StopAll act on the down edge only, Flash on both. `pressed` must be a JSON boolean. Toggle/Flash with no Function attached, or any disabled widget, is `INVALID_STATE` instead of the engine's silent no-op. Note the on-screen QML fires Toggle on *release/click*; the contract asked for the down edge, so the API is ~one pointer-up earlier than a mouse click would be. |
| `vc.button.stateChanged.state` enum `Inactive/Monitoring/Active` | lowercase `inactive/monitoring/active` (also in the new `VcWidgetSummary.state`). |
| `vc.xyPad.setPosition`/`positionChanged` x/y in the engine's 0..255.996 domain | normalized **0..1** (1.0 = full 16-bit span, scale factor 65535/256). `VcXyPadConfig.horizontalRange`/`verticalRange` and `VcXyPadPreset.position` stay in engine units. |
| `vc.cueList.setPlaybackIndex {playbackIndex}` | `{index}` (`playbackIndex` accepted as a deprecated alias). Range-checked to -1..steps-1 (`INVALID_PARAMS` with `details.stepCount`). `index >= 0` also plays the step (`playCurrentStep()`); -1 only clears the selection. |
| `vc.cueList.playbackChanged {playbackStatus, playbackIndex, nextStepIndex, primaryTop}` | `{widgetId, playbackIndex, running, paused}` (`VcCueListPlaybackState`; `running` = Playing **or** Paused, `paused` = Paused). `nextStepIndex`/`primaryTop` dropped - side-fader crossfade UI is out of this slice. |
| - | **new** `vc.cueList.get` -> `{steps:[{index,name,functionId,fadeIn,fadeOut,hold,notes}], playbackIndex, running, paused}` (`VcCueListStep`: resolved speeds like the on-screen list, infinite = 4294967295). |
| `vc.speedDial.setCurrentTime {valueMs}` / `vc.speedDial.currentTimeChanged {valueMs}` | `vc.speedDial.setValue {ms}` / `vc.speedDial.valueChanged {ms}` (`valueMs` accepted as a deprecated alias). Message/operation keys renamed `VcSpeedDialSetValue*` / `VcSpeedDialValueChangedEvent`. |
| `vc.frame.gotoPage {pageIndex}` / `vc.frame.currentPageChanged {currentPage}` | `vc.frame.gotoPage {page}` (`pageIndex` alias) / `vc.frame.pageChanged {page}` (`VcFramePageChangedEvent`). Same-page requests are a no-op (no event). |
| - | **new** `vc.frame.get` -> `{pages, currentPage, multipage}` (`VcFrameLiveState`). |
| `VcWidgetSummary` had no live state | additive per-type live seed fields: Button `state`; Slider `value`,`min`,`max`; CueList `playbackIndex`,`running`,`paused`; XYPad `x`,`y`; Speed `ms`; Frame/SoloFrame `currentPage`,`pages`,`multipage`. Nothing removed or renamed. |

Other implementation facts worth knowing:

- **`vc.frame.gotoPage` bumps `docRevision`.** `VCFrame::setCurrentPage()`
  calls `setDocModified()` because the current page is saved in the show
  file. The method is still modelled live (no `baseRevision`, `{}` ack), but
  after every successful flip other clients' `baseRevision` is stale and
  their next §4a request will `CONFLICT` until they refresh. Documented on
  the message; deliberately not worked around server-side.
- **Disabled widgets refuse live input** (`INVALID_STATE`), matching the QML
  items disabling their MouseArea/TouchArea. The read-only `vc.cueList.get` /
  `vc.frame.get` still work on a disabled widget.
- **`vc.slider.setValue` is confined to `[rangeLowLimit, rangeHighLimit]`**
  server-side (the fader is physically confined to them); the 0..255 contract
  range is validated first (`INVALID_PARAMS` outside it, or for non-integers).
- **No event when nothing changed**: every widget setter early-returns on an
  equal value (`VCSlider::setValue`, `VCButton::setState`, ...), so a repeat
  request acks `{}` without a broadcast. `vc.speedDial.tap`'s first tap is
  the same: it only arms the timer.
- **`vc.speedDial.setValue` with `ms = 0`** is stored but not applied to the
  attached Functions (`VCSpeedDial::setCurrentTime()` skips
  `applyFunctionsTime()` for 0) - engine behaviour, surfaced as-is.
- **Two `vc.cueList.playbackChanged` events per transport action** are
  normal (status signal + index signal, each carrying full state).
- **Delivery**: all six live event topics are broadcast to every session
  (not subscribe-gated) - the contract asked for that, and per-event volume
  is one frame per discrete gesture/step; a client dragging a fader is
  expected to throttle its own `vc.slider.setValue` calls. This overrides
  the "recommend gating `vc.slider.valueChanged`/`vc.xyPad.positionChanged`"
  judgment call further down for these six topics only.
- **Origin**: `originClientId` is the requesting client for API-caused
  changes, `null` for anything else (QML UI, external MIDI/DMX/keyboard
  input, a Function stopping on its own, Solo Frame side effects) - the
  qmlui host relays every widget's own change signal through one
  `VirtualConsole::widgetRegistered` hook, so widgets loaded from a show
  file are covered exactly like ones created over the API.
- **Not verified against a running GUI**: the `apivcdomain_test` suite runs
  against a headless `FakeVcHost`; the real `App` implementation
  (`qmlui/app_apivchost.cpp`) compiles and mirrors the QML call paths line
  by line, but no live `qlcplus5.exe` session exercised it yet.

## Cue list side fader, speed dial extras, widget presets - implemented (2026-09-27)

Appended to `ApiVcDomain` (`registerCueSpeedDialMethods()` /
`registerPresetMethods()` at the end of `apivcdomain.cpp`; the host side is
`qmlui/app_apivchost_cue.cpp` + `app_apivcconfig_cue.cpp`) and exercised
end to end from the web UI (`webui/vc/vc-props-cue.jsx`,
`webui/tools/e2e/vc-cue.js`) against a plugin-less sandbox instance running
the SF3 project. Spec changes made alongside, all additive:

- `VcCueListConfig` and `VcSpeedDialConfig` are served by `vc.widget.get`'s
  `typeConfig` and accepted by `vc.widget.setConfig` for CueList / Speed
  widgets. `chaserID` must name an existing Chaser or Sequence (or
  `"4294967295"` to detach) - `VCCueList::setChaserID()` would otherwise
  silently detach on an unknown id, so the host validates first.
  `VcSpeedDialConfig.functions` replaces the whole list; omitted factors
  keep the current ones for an already-attached Function.
- **`VcSpeedDialConfig.presets`** (new, `readOnly`): the preset list rides
  along in `typeConfig` so one `vc.widget.get` seeds a client; a
  `setConfig` patch carrying it is rejected.
- **`vc.widget.preset.apply` on a Speed widget is now supported** (was
  "NOT SUPPORTED / undefined effect"): the host does what the on-screen
  preset button does, `setCurrentTime(valueMs)`, reported as
  `vc.speedDial.valueChanged`. XYPad and Animation presets go through the
  same generic plumbing (`App::vcWidgetPreset*()` dispatching into
  `ApiVcConfig::{xyPad,animation}Preset*()`) but those functions are
  stubs returning INVALID_STATE ("not yet supported") until the XYPad /
  Animation slice fills them in `app_apivcconfig_live.cpp`.
- **Live seeds added to `VcWidgetSummary`**: CueList `sideFaderLevel`,
  `nextStepIndex`, `primaryTop`; Speed `factor`, `tapTimeValue`.
- **`vc.cueList.sideFaderChanged` carries `nextStepIndex` + `primaryTop`**
  besides `level` (the on-screen fader's two step labels need all three),
  and is also emitted when only those change - so a running crossfade
  list emits it next to `playbackChanged`. `vc.cueList.setSideFaderLevel`
  confines the level to 0..100 in Crossfade mode and answers
  INVALID_STATE while `sideFaderMode` is None. Engine caveats surfaced
  as-is: `setSideFaderMode()` resets the level (Steps → 255, Crossfade →
  100), so a `setConfig` changing the mode emits `sideFaderChanged` right
  before `configChanged`; in Steps mode `setSideFaderLevel()` stores the
  level but returns before emitting while the Chaser is stopped or the
  level maps onto the current step.
- **`vc.speedDial.setFactor` accepts OneSixteenth..Sixteen only** - the
  dial's own factor is what the 1/16..16 buttons and +/- set; None/Zero
  are per-function overrides and would zero every attached Function.
- **`vc.speedDial.tapChanged.tapTimeValue` is the tap interval in ms**,
  not a BPM (the spec used to say "Computed BPM"; `VCSpeedDial::tap()`
  stores the interval and derives its BPM as 60000 / interval). Emitted
  on change only, so a `resetTap` with no series to clear sends nothing.
- `vc.speedDial.apply` is a bare ack: `applyFunctionsTime(true)` only
  touches the attached Functions' speeds (visible through
  `functions.get`, e.g. currentTime 1600 ms x dial factor 1 x per-function
  duration factor 2 → duration 3200 ms, verified).
- Structural preset methods (`vc.widget.preset.add/remove`,
  `vc.speedDial.preset.update`) bump `docRevision` and broadcast
  `vc.<speedDial|xyPad|animation>.presetsChanged` with the full list to
  every session; unknown `presetId` → NOT_FOUND, a widget type without
  presets → INVALID_PARAMS with `details.widgetType`.
- Not covered by the headless `FakeVcHost` tests: the App-side config
  validation (enum spellings, Chaser existence) - that part was exercised
  in the sandbox only.

## Cross-domain touch points (things this fragment deliberately does NOT redefine)

- **Grand Master / Blackout**: `io.yaml` already owns
  `io.grandMaster.get/setValue/setMode` + `io.grandMaster.changed`, and
  `io.blackout.get/set/toggle` + `io.blackout.changed`. A `VCSlider` in
  GrandMaster mode (`vc.slider.setValue`) and a `VCButton` with
  `actionType: Blackout` (`vc.button.press`) both call the *exact same*
  engine entry points (`InputOutputMap::setGrandMasterValue()` /
  `toggleBlackout()`). We did not duplicate those messages here; a client
  pressing such a widget should expect `io.grandMaster.changed` /
  `io.blackout.changed` events to fire as the actual source of truth, in
  addition to (or instead of, for the slider case where there's no VC-owned
  "GrandMaster value changed" event at all) any VC-specific event. Flagging
  for the merge pass to confirm this dual-path is acceptable, or whether
  `vc.slider.valueChanged` in GrandMaster mode should be suppressed
  server-side in favour of clients just listening to
  `io.grandMaster.changed`.
- **Global BPM**: `VCSpeedDial::tap()` with `controlBPM` enabled calls
  `InputOutputMap::setBpmNumber()` - a genuinely global, engine-wide value.
  **Neither `io.yaml` nor `core.yaml` currently defines any `*.bpm.*`
  message or event.** This looks like a gap in the overall spec, not
  something in scope for this fragment to fix unilaterally. Recommend the
  merge pass add a small `io.bpm.get`/`io.bpm.set`/`io.bpm.changed` (or
  `core.bpm.*`) triplet; `vc.speedDial.tap`'s description already notes the
  dependency.
- **Chaser/Sequence step content**: `VCCueList::addFunctions()` and
  `VCCueList::setStepNote()` both ultimately mutate the attached Chaser's
  step list, which `functions-core.yaml` already fully owns
  (`functions.chaser.addStep/removeStep/replaceStep/moveStep`, with a
  `note` field already present on the step schema at
  functions-core.yaml:2045/2071). We deliberately did **not** add
  `vc.cueList.addFunctions` / `vc.cueList.setStepNote` - a client editing a
  Cue List's steps should call `functions.chaser.*` directly, using
  `chaserID` from `VcCueListConfig` to know which Chaser. This trimmed a
  meaningful amount of scope from this fragment.
- **`functions.tap`/`functions.adjustAttribute`** (functions-core.yaml)
  cover per-Function tap-tempo and attribute fraction adjustment.
  `VCSpeedDial`'s tap/apply are *not* thin wrappers over these - the widget
  computes its own BPM from tap intervals and applies an absolute
  time x multiplier-factor to potentially several Functions at once
  (`fadeIn`/`fadeOut`/`duration`, each independently multiplied) - genuinely
  bespoke widget behaviour, so `vc.speedDial.*` stands on its own.
- **Fixture/FixtureGroup pickers**: `VCSlider`/`VCXYPad`/`VCAudioTriggers`
  all expose a `groupsTreeModel`/`fixtureList` QML-only convenience view
  for their "pick channels/heads" property panel. These are pure UI
  presentation built from data `fixtures.yaml` already exposes
  (`fixtures.list`, `fixtures.group.get`) - not modelled here; the client
  builds its own tree.
- **VC widget selection / clipboard** (`VirtualConsole::setWidgetSelection`,
  `selectedWidget`, `copyToClipboard`/`cutToClipboard`/`pasteFromClipboard`)
  are client-local editing UI state, not shared multi-client state - out of
  scope entirely. A client wanting to signal "I'm editing this widget" to
  other clients should use the existing `locks.acquire` mechanism instead
  (see Lockable resources below).

## Lockable resources (§6)

Recommended `resourceType` values for `locks.acquire`/`locks.release`:
- `"vcPage"` (resourceId = page index as string) - discourage two clients
  editing the same page's layout/PIN simultaneously.
- `"vcWidget"` (resourceId = widgetId) - the natural unit; covers a client
  opening any widget's properties panel (VCButton config, VCSlider config,
  VCXYPad preset editor, etc.) or dragging/resizing it.

Everything else in this fragment is either read-only, a live/ephemeral
action with inherent last-write-wins semantics (§4b, no lock needed by
design), or a sub-resource of a widget (presets, bars, schedules, input
sources) that's reasonably covered by locking the owning widget.

## Subscribe-gated event topics (§5)

High-frequency or per-resource - clients must `subscribe` to receive:
- `vc.audioTriggers.levelsChanged` - live spectrum/volume meter, driven at
  audio-capture rate (fastest stream in this fragment by far).
- `vc.slider.monitorValueChanged` - Level-mode DMX monitor readback,
  effectively per-DMX-frame while monitoring is on.
- `vc.clock.timeChanged` - 1Hz per clock widget for a plain wall-clock
  display, but a RUNNING Stopwatch/Countdown ticks at 100ms/10Hz instead
  (VCClock::playPauseTimer() switches the interval - confirmed in vcclock.cpp,
  not just a documentation guess); many clocks * many clients adds up either
  way, gate it.
- `vc.xyPad.positionChanged` / `vc.xyPad.floorPositionChanged` - can be
  dragged continuously at pointer-move rate.
- `vc.slider.valueChanged` - likewise, continuous drag.
- `vc.button.stateChanged`, `vc.cueList.playbackChanged`,
  `vc.cueList.sideFaderChanged`, `vc.speedDial.*Changed`,
  `vc.animation.*Changed` - lower-frequency (discrete presses/steps), but
  still recommend gating any of these that fire per-widget-instance rather
  than "always deliver to everyone" the way `fixtures.*`/`functions.*`
  structural events do, simply because a large VC can have hundreds of
  widgets. Left as a judgment call for the merge pass on where exactly to
  draw the "always deliver" vs. "subscribe-gated" line for this fragment;
  everything listed above this paragraph is unambiguous, the rest is a
  volume/scale tradeoff rather than a hard technical requirement.

All **document-state (§4a)** events in this fragment (`vc.page.*`,
`vc.widget.created/updated/configChanged/deleted/repositioned/bulkUpdated`,
`vc.*.presetsChanged`, `vc.*.fixturesChanged`, `vc.clock.schedulesChanged`,
`vc.audioTriggers.barsChanged`, `vc.widget.inputSourcesChanged`,
`vc.widget.keySequencesChanged`) are always delivered to every connected
client per 00-conventions.md §5, matching `fixtures.yaml`/`functions-
core.yaml` precedent - not subscribe-gated.

## Scope cuts

- **Undo/redo** (`Tardis::instance()->enqueueAction(...)` calls visible
  throughout the .cpp files) is a `core.undo`/`core.redo` concern
  (core.yaml already owns it) - every doc-state mutation in this fragment
  is presumed to push a Tardis action server-side the same way the existing
  Qt UI does, but that's an implementation detail, not part of the wire
  API.
- **VCPage's relationship to `VirtualConsole::renderPage`/
  `currentPageItem`/`enableFlicking`/`setPageInteraction`/`setPageScale`**
  are all QML-rendering-only concerns (pixel density, flick-scroll
  enablement, on-screen zoom) - not modelled; an Electron client owns its
  own rendering/zoom entirely client-side. Same for
  `VirtualConsole::pixelDensity()`/`snapping`/`snappingSize`/`editMode` -
  purely local editor-UI toggles, not shared state.
- **VCWidget::propertiesResource/presetsResource/typeToIcon** - QML
  resource-path strings for the legacy UI's own property-panel/icon
  lookup. An Electron client designs its own property panels and icons per
  `widgetType`; not part of the wire contract.
- **`VCFrame::checkSubmasterConnection`/submaster cascading** - when a
  `VCSlider` in Submaster mode changes, `VCFrame::applySubmasterValue()`
  silently scales every *sibling* widget's `intensity()` (multiplying
  whatever they're currently outputting). This is pure server-side engine
  behaviour with no client-facing message of its own - it manifests
  indirectly as those sibling widgets' own live-value events changing
  (e.g. a sibling VCButton's controlled Function dims, which isn't
  separately observable via this fragment since Function attribute levels
  aren't broadcast at that granularity - see functions-core.yaml). Flagging
  as a known observability gap: a client watching only `vc.*` events cannot
  currently see the *effect* of a submaster on its siblings' actual output,
  only that the submaster slider itself moved.
- **XML legacy-only fields**: `KXMLQLCVCFrameAllowChildren` (legacy),
  `VCSlider::loadXMLLegacyPlayback`, `VirtualConsole::loadXMLLegacyInput`
  are old-format compatibility shims for loading pre-existing show files
  and have no forward-facing API surface - correctly absent here.

## Preset id vs. index note

`VCXYPadPreset`, `VCSpeedDialPreset` and `VCAnimationPreset` all use a
stable `quint8 m_id` (assigned once, survives reordering) - modelled here
as `presetId: integer`, safe to cache client-side. `VCClockSchedule` and
`VCAudioTriggers`'s bars, by contrast, are plain array entries with **no
stable id** - `vc.clock.schedule.*` and `vc.audioTriggers.setBarConfig` use
a positional `index` instead, which is only valid until the next add/
remove/reorder of that same array. Clients should always re-derive `index`
from the latest `vc.clock.schedulesChanged`/`vc.audioTriggers.barsChanged`
event rather than caching it across a mutation.

## Things intentionally deferred to the merge pass rather than decided here

- Whether `vc.slider.setValue` in GrandMaster mode should suppress its own
  `vc.slider.valueChanged` broadcast in favour of clients relying solely on
  `io.grandMaster.changed` (see Cross-domain touch points above).
- Exact subscribe-gating cutoff for the "medium frequency" event group
  listed in Subscribe-gated event topics above.
- Global BPM message gap (`io.bpm.*` / `core.bpm.*` - see Cross-domain
  touch points above) - not this fragment's resource to define, but
  `vc.speedDial.tap` depends on it existing somewhere for full fidelity.
