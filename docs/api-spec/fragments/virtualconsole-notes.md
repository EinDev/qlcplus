# Virtual Console fragment - notes

Grounded in `qmlui/virtualconsole/*.h/.cpp` (every header read in full; .cpp
read for `vcbutton`, `vcslider`, `vcxypad`, `vcspeeddial`, `vcframe` to
confirm behaviour the headers alone didn't make clear) and cross-checked
against `qmlui/qml/virtualconsole/*.qml`. Component-key prefix `Vc`,
method/topic prefix `vc.`. 190 messages / 25 schemas / 182 operations.

## Channel message keys to add at merge time

Per `_example.yaml`'s closing note, every `Vc*` key in `messages:` needs a
matching lowerCamelCase entry added under `channels.qlcplus.messages` in the
merged skeleton (e.g. `vcButtonPressRequest: { $ref: '#/components/messages/VcButtonPressRequest' }`).
There are 190 of them (mechanical rename, first letter lowercased) - not
listing all individually here, but flagging that this fragment is large
enough that the merge script should almost certainly generate this mapping
rather than hand-transcribe it.

## Two-tier classification judgment calls (please sanity-check)

- **`vc.page.select` / `vc.frame.gotoPage`** (which top-level VC page, and
  which internal page of a multi-page Frame, are showing) are modelled as
  **live (§4b)**, directly per 00-conventions.md §4b's own example ("which
  VC page is showing"). In reality each operator's screen could reasonably
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
- `vc.clock.timeChanged` - 1Hz per clock widget, but many clocks * many
  clients adds up; gate it.
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
