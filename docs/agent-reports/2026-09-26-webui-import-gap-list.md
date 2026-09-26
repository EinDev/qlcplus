# Web UI import: verification, gap list and completeness audit (2026-09-26)

Scope: `webui/` (the browser front end for the Control API) as imported from the design-system
prototype and rewired against the server in `controlapi/src` at `07ff871e5`. Method availability
below is per that source tree (`registerMethod(...)` grep) and confirmed read-only against the
running instance (QLC+ 5.3.1 GIT, SF3.qxw). The running binary may lag master, so "server has X"
means "the source registers X".

## Verification performed

- Served `webui/` with `python -m http.server`, loaded in headless Chrome (DevTools protocol,
  real 5-7 s waits): all four screens render with live data, zero console errors, zero 404s other
  than the expected `qlcplus-config.json` fallback. Every icon, font and script resolves relative
  to the served root.
- Interactions driven over CDP (read-only): expand universe -> fixture detail (channel list,
  addressing); expand folder -> function detail (Scene: 48 fixtures, 384 values); VC page switch
  to "HS: Mobile Truss" (37 widgets, nested Solo frames, slider, view-only speed dial); Simple
  Desk values equal `io.dmx.universe.get`, per-channel group icons resolved.
- Config path: a copy served with `{"apiPort":9010,"apiHost":"127.0.0.1"}` connected to
  `127.0.0.1:9010`; with `apiPort: 9599` the lamp reads "connection failed" and every screen shows
  its labelled mock state.
- Client reconnect: Node script forced a socket close; the client re-`hello`ed after the backoff.
- `file://` in Chrome fails (Babel XHR blocked) - documented in `webui/README.md`.
- Not exercised: any mutating method (live rig). Wired but untested: `functions.start/stop/
  rename/delete`, `fixtures.update/unpatch`, `io.simpleDesk.setChannel(s)/resetChannel/
  resetUniverse/dump`, `io.universe.create`, `io.blackout.set`, `io.grandMaster.setValue`,
  `core.project.save`, `core.mode.set`, `vc.button.press`, `vc.slider.setValue`.

## Gap list

### (a) Server-side missing (UI calls or needs it)

| Method / event | Needed for |
| --- | --- |
| `vc.button.press` | VC button press (UI sends it; falls back to view-only on `Unknown method`) |
| `vc.slider.setValue` | VC slider/knob move (same fallback) |
| `vc.button.stateChanged`, `vc.slider.valueChanged` (events) | showing the real button state / slider position (UI listens for them already) |
| `vc.cueList.play/stop/next/previous/setPlaybackIndex` + `vc.cueList.playbackChanged` | cue list widgets |
| `vc.xyPad.setPosition`, `vc.speedDial.*`, `vc.clock.*`, `vc.animation.*`, `vc.audioTriggers.*` | the remaining live widget types |
| `functions.status.changed` (event) or a `running` field on `functions.list/get` | play/pause button state (UI currently shows "last sent") |
| a stop-all method (`functions.stopAll`) | main toolbar Stop-all |
| BPM / tap tempo (`functions.tap` exists in spec, not registered; no BPM getter) | beat indicator, tap |
| `fixtures.defs.listManufacturers/listModels/getModel/getMode` | Add-fixture dialog (`fixtures.patch` itself exists) |
| `fixtures.remap.*` | address remap |
| `functions.collection.*`, `functions.efx.*`, `functions.rgbmatrix.*`, `functions.script.*`, `functions.audio.*`, `functions.video.*`, `functions.show.*`, `functions.channelsgroup.*`, `functions.adjustAttribute`, `functions.tap` | editors / live intensity for those types |
| `io.patch.set/remove/setParameters`, `io.plugin.list/getLines/rescan`, `io.universe.update/delete`, `io.patch.output.setState`, `io.inputProfile.*` | I/O patching, rename/passthrough, per-patch blackout, input profiles |
| `io.simpleDesk.sendKeypadCommand` | keypad (UI parses a subset locally instead) |
| `core.undo/redo`, `core.history.get` | Undo / Redo |
| a fixture-level live control (set capability/channel by fixture+channel, palette apply) | F&F intensity/colour/position tools, applying palettes |
| `vc.widget.keySequence.*`, `vc.widget.inputSource.*` | key bindings / external input |

### (b) UI-side: still mocked or missing while connected

- Offline only: `data.js` (fixtures, functions, channels, VC widgets, universes, "Winter Tour.qxw")
  is used solely when disconnected and is labelled "(mock)".
- While connected, still not real: function running state (local guess), VC slider positions
  (start at 0; no value feed), the Add-fixture dialog (mock; disabled online), the beat indicator
  (static), fixture addressing/mode edit (display only), Show Manager (absent).
- UI work that the server already supports: scene value / chaser step editors
  (`functions.scene.*`, `functions.steps.*`), function create/move/pause/timing
  (`functions.create/move/setPause/update`), fixture re-address (`fixtures.update`), fixture
  groups (`fixtures.group.*`), VC page/widget CRUD (`vc.page.*`, `vc.widget.*` for Button/Slider
  config), palette create/edit (`palette.*`), workspace new/open/save-as (`core.project.*`,
  `recentFiles`), universe filter (`io.simpleDesk.setUniverseFilter`).

### (c) Client fixes made (`webui/api/qlcplus-api.js`)

1. Simple Desk seeding: `io.simpleDesk.get` returns only overridden channels; the prototype used
   it as the value source, so unoverridden faders kept mock values. Now merges
   `io.dmx.universe.get` (values) with `io.simpleDesk.get` (overrides) and subscribes to the
   gated `io.dmx.universe.<id>.changed` stream (delta `{channel,value}` within universe) plus the
   ungated `io.simpleDesk.channelChanged` (flat `address`).
2. Numbering: dropped the legacy 1-based `absoluteChannel()` layer; the client speaks the
   server's 0-based `universeId`/`channel`/flat `address`; screens add 1 for display.
3. `hello` sends `{apiVersion, clientName}`; `docRevision` kept from the welcome and refreshed
   from `core.project.loaded/saved`, so structural calls pass a `baseRevision`.
4. Reconnect with capped exponential backoff (previously off in the UI), `reconnecting` events.
5. `NOT_FOUND` + `Unknown method "..."` is tracked per method (`isUnsupported()`, `unsupported`
   event) so the UI can degrade to view-only instead of failing on every click.
6. VC: `setWidget()` no-op replaced by `pressButton()` / `setSliderValue()` calling the spec's
   `vc.button.press` / `vc.slider.setValue`.
7. `api/domains/*.js` were never loaded by the prototype's `index.html`; they are now, and the
   stale header note in `io.js` (claimed the transport used a wrong blackout field) is fixed.
   The desktop-app copy of the client (`value` instead of `blackout`) was not used.

## Completeness audit (functional, not visual)

Legend: **live** = works against the API; **mock** = rendered from `data.js` only; **missing** =
absent/placeholder. Blocker: **S** = server lacks method/event (named), **U** = needs JSX work.

### Main toolbar / Actions menu (`MainView.qml`, `ActionsMenu.qml`)

| QML action | Web UI | Blocker |
| --- | --- | --- |
| Switch F&F / VC / Simple Desk / I/O | live | - |
| Show Manager context | missing | S (`functions.show.*`, playback position) + U |
| New project / Open / Save as / Import / recent files | missing (Save is live) | U (`core.project.new/open/saveAs/recentFiles` exist) |
| Save project (Ctrl+S) | live | - |
| Undo / Redo | missing | S (`core.undo/redo`) |
| Blackout (Ctrl+B) | live | - |
| Stop all functions | missing (disabled) | S (no stop-all) |
| BPM / tap | missing (static dot) | S |
| Dump DMX values on a Scene | live (Simple Desk) | - |
| Operate/Design mode | live (extra vs. QML toolbar) | - |
| Fullscreen, language, UI settings, network setup, address tool, about | n/a / About only | browser-native or desktop-only |

### Fixtures & Functions

| Operator action | Web UI | Blocker |
| --- | --- | --- |
| Browse fixtures per universe, see addressing/mode/channels | live | - |
| Browse functions per folder, see type/speeds/contents | live | - |
| Start / stop a function | live; state = last sent | S (`functions.status.changed` / running flag) |
| Pause a function | missing | U (`functions.setPause` exists) |
| Rename fixture / function | live | - |
| Delete function / unpatch fixture | live (confirm dialog) | - |
| Add fixture | mock dialog, disabled online | S (`fixtures.defs.*`); `fixtures.patch` exists |
| Re-address fixture, change mode | missing (display only) | U (`fixtures.update`) |
| Fixture groups (create/assign) | missing | U (`fixtures.group.*`) |
| Remap addresses | missing | S (`fixtures.remap.*`) |
| Multi-select, select odd/even/Nth | missing | U |
| Live fixture tools (intensity, colour, position, gobo, beam, shutter, speed) | missing | S (no fixture-level live set; UI could route through `io.simpleDesk.setChannel` addresses as a stopgap) |
| Palettes: list | live | - |
| Palettes: create/edit | missing | U (`palette.*`) |
| Palettes: apply to selection | missing | S |
| Create function (scene/chaser/...) | missing | U (`functions.create`) |
| Clone function, function usage, autostart, preview | missing | S |
| Move function to folder | missing | U (`functions.move`) |
| Timing settings (fade in/out, duration) | display only | U (`functions.update`) |
| Scene editor (values, fixtures) | missing | U (`functions.scene.setValue(s)/setMembers/unsetValue`) |
| Chaser / Sequence step editor | missing | U (`functions.steps.*`) |
| Collection / EFX / RGB matrix / Script / Audio / Video / Show editors | missing | S (none registered) |
| 2D / 3D / DMX / Universe views | missing | U for DMX view (data exists); S for 2D/3D monitor properties |
| Search | live (client-side) | - |

### Virtual Console

| Operator action | Web UI | Blocker |
| --- | --- | --- |
| See pages, switch page | live (local; `vc.page.select` deliberately not called) | - |
| See widgets at their geometry, nested frames, captions/colours | live | - |
| Press / toggle / flash a button | UI ready, server rejects | S (`vc.button.press`) |
| Move a slider / knob | UI ready, server rejects | S (`vc.slider.setValue`) |
| See button state / slider value | missing | S (`vc.button.stateChanged`, `vc.slider.valueChanged`) |
| Cue list play/stop/next/previous, side fader | view-only | S (`vc.cueList.*`) |
| XY pad, speed dial, clock, animation, audio triggers | view-only | S |
| Grand Master | live | - |
| Frame page navigation (multipage frames), PIN pages | missing | S (`vc.frame.gotoPage`), U for PIN (`vc.page.validatePin` exists) |
| Edit mode: add/remove/move/resize/configure widgets, copy/paste | missing (buttons disabled) | U (`vc.widget.create/delete/update/setConfig/reposition/reparent`, Button/Slider config only) |
| Page create/rename/delete | missing | U (`vc.page.*`) |
| Key bindings, external input | missing | S |

### Simple Desk

| Operator action | Web UI | Blocker |
| --- | --- | --- |
| Pick universe | live | - |
| See live DMX values + override flags for all 512 channels | live | - |
| Set a channel (override) | live | - |
| Reset a channel / whole universe | live | - |
| Keypad (`1 THRU 12 @ FULL`, `+`, `BY`) | live (parsed locally) | S for full qmlui syntax and command history (`io.simpleDesk.sendKeypadCommand`) |
| DMX / percent display | live | - |
| Dump to scene | live (untested) | - |
| Fixture list side panel, channel debug | missing | U |

### Input / Output

| Operator action | Web UI | Blocker |
| --- | --- | --- |
| See universes and their input/output/feedback patches | live | - |
| Add universe | live (untested) | - |
| Remove universe | missing | S (`io.universe.delete`) |
| Rename universe, passthrough | missing (read-only) | S (`io.universe.update`) |
| Patch input/output/feedback, choose plugin lines | missing | S (`io.plugin.list/getLines`, `io.patch.set/remove`) |
| Plugin configuration | missing | S (engine gap, TODO 2.3) |
| Input profiles | missing | S (`io.inputProfile.*`) |
| Per-patch output blackout | missing | S (`io.patch.output.setState`) |
| Audio input/output devices | missing | S |

### Show Manager

Entirely missing: no timeline, tracks, items, cursor or playback position. Blocker: S
(`functions.show.*` not registered, no playback-position event) and U. A Show function can be
started/stopped from the F&F tree.

## Ranking for a usable remote control surface

1. **VC live interaction** - S: `vc.button.press`, `vc.slider.setValue`, `vc.button.stateChanged`,
   `vc.slider.valueChanged`. UI already sends/listens; zero UI work to light up.
2. **Function running state** - S: `functions.status.changed` (or `running` in list/get). UI: swap
   the local guess for the event (small).
3. **Cue list control** - S: `vc.cueList.play/stop/next/previous/setPlaybackIndex` +
   `playbackChanged`. UI: a cue list widget (medium).
4. **Stop all** - S: one method. UI trivial.
5. **Live fixture control from F&F** (intensity/colour/position for a selection) - S preferred; a
   UI-side stopgap via `fixtures.get` channel addresses + `io.simpleDesk.setChannels` is possible.
6. **XY pad / speed dial / tap BPM** - S.
7. **I/O patching** - S: `io.plugin.list/getLines`, `io.patch.set/remove`, `io.universe.update/
   delete`. UI: combos already exist in mock form.
8. **Add fixture** - S: `fixtures.defs.*` browsing (`fixtures.patch` exists).
9. **Scene / chaser editors** - U (server ready).
10. **VC layout editing, page CRUD** - U (server ready for Button/Slider).
11. **Function pause / create / move / timing** - U (server ready).
12. **Undo / redo** - S.
13. **Workspace new/open/save-as** - U (needs a picker over `core.project.recentFiles`).
14. **Show Manager** - S + U, large.
