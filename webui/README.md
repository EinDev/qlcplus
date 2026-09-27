# QLC+ 5 web UI (`webui/`)

A browser front end for QLC+ 5 that talks to the app's **Control API** (the WebSocket JSON
protocol in `controlapi/`, spec in `docs/api-spec/`). It is a set of static files: QLC+ serves
this directory over plain HTTP, the browser compiles the JSX on the fly, and the page opens a
WebSocket back to the same QLC+ instance. No Node toolchain, no build step.

Server side (HTTP file server, install rules, CLI flags) is documented in `docs/webui.md`.

## Running it

```
qlcplus5 --webui                       # serve the installed WebUI/ directory on http://localhost:9011/
qlcplus5 --webui --webui-port 8080     # another port
qlcplus5 --webui --webui-root D:\src\qlcplus\webui   # serve this source directory while developing
```

Open `http://<host>:9011/` in a browser. The page connects on its own: it fetches
`qlcplus-config.json` (served by QLC+ next to `index.html` as
`{"apiPort": <n>, "apiHost": null}`), and targets `ws://<page host>:<apiPort>/`. If that file
cannot be fetched (the directory is served by some other static server, e.g.
`python -m http.server 8099 --directory webui`), it falls back to the page's hostname and port
9010. Both are overridable in the **network button** popover in the main toolbar; a host/port
typed there is remembered in `localStorage` (`qlc.host` / `qlc.port`) and wins over the server
default until "Use server default" is pressed.

- **Auto-connect** happens only when the page is served over http(s). Opened via `file://` the
  page stays offline until Connect is pressed - and note that Chrome refuses to run it from
  `file://` at all (Babel loads the `.jsx` files via XHR, which Chrome blocks for `file:` origins);
  serve it over HTTP.
- **https pages cannot use `ws://`** - browsers block it. Serve the web UI over plain http as long
  as the Control API has no TLS.
- The network button's corner lamp shows the state: grey offline, blinking amber connecting /
  reconnecting, green live, red failed. An unexpected drop reconnects automatically with
  backoff (1 s -> 10 s) until Disconnect is pressed. A first attempt that never opens (QLC+'s
  API not running, wrong port) is *not* retried - the lamp turns red and the page waits for
  Connect. Disconnected, every screen keeps working on its built-in mock data (`data.js`),
  clearly labelled as such.

## Layout

| Path | What it is |
| --- | --- |
| `index.html` | Entry point. Loads styles, the vendored React/Babel, the design-system bundle, the API client and the screens. |
| `api/qlcplus-api.js` | Transport: envelopes, `hello`, reconnect, unsupported-method tracking, Simple Desk / DMX merging. |
| `api/domains/*.js` | One namespace per spec fragment (`qlc.core`, `qlc.io`, `qlc.vc`, `qlc.fixtures`, ...), thin wrappers over `call()`. |
| `Connection.jsx` | `QLCConnectionProvider` / `useQLC()` / the network button popover. |
| `App.jsx` | Main toolbar: actions menu (New / Open / Save / Save as / Undo / Redo / About), context switching, blackout, Stop all, BPM + beat indicator, mode. |
| `FixturesFunctions.jsx` | Fixture tree (per universe) + function tree (per folder), multi-select, context menus, start/pause/stop, create/rename/move/delete, fixture re-addressing. |
| `ff/ff-core.jsx` | Shared F&F plumbing: serial revision-gated mutation queue (CONFLICT retry, per-key coalescing), cached `fixtures.get`, capability lookup via `fixtures.defs.*`, channel classification and colour/position DMX maths, own-echo event filter. |
| `ff/FixtureTools.jsx` | Live fixture tools for the selected fixtures (intensity, colour, pan/tilt, capability presets) writing through `io.simpleDesk.setChannels`, or into the open Scene. |
| `ff/FunctionEditors.jsx` | Scene editor (channel console, members, palettes), Chaser/Sequence step editor, timing/run-order editor, the shared picker dialog. |
| `ff/CollectionEditor.jsx` | Collection editor (`window.QLCEditors.Collection`): ordered member list, add via picker, remove, move up/down over `functions.collection.*`. |
| `ff/EfxEditor.jsx` | EFX editor (`window.QLCEditors.EFX`): live preview canvas fed by `functions.efx.getPreview`, fixture heads (mode / reverse / start offset, add / remove / reorder, offset on all), pattern parameters, propagation, timing. |
| `tools/e2e/*.js` | Headless-Chrome end-to-end drivers per slice (`node webui/tools/e2e/efx-collection.js` against a `dev-webui-sandbox.ps1` instance). |
| `tools/coverage/` | Dev-only JS coverage of this directory: `hook.js` (preloaded into an e2e driver, records V8 coverage through `cdp.js`) and `report.js` (maps it back to the `.jsx` sources via Babel's inline source maps). Run `.\dev-webui-coverage.ps1 -Open` from the repo root: every browser driver against its own sandbox, report in `coverage/webui/index.html`. |
| `ff/FixtureDialogs.jsx` | Add Fixtures dialog (`fixtures.defs.*` + `fixtures.patch`), Fixture Groups panel, Palettes panel (create/edit/apply). |
| `ff/View2D.jsx`, `ff/ViewDMX.jsx`, `ff/ViewUniverseGrid.jsx` | F&F centre-area views registered in `window.QLCFFViews` (2D stage over `fixtures.monitor.*`, per-fixture DMX values, 512-cell address grid); `View2D.jsx` also exports `FF.useMonitor` and the placement block of the fixture detail. |
| `ff/FixtureRemap.jsx` | Fixture Remap dialog over `fixtures.remap.suggestChannelMap` / `apply`. |
| `VirtualConsole.jsx` | Pages + widgets at their real geometry, live interaction, Design-mode layout editing, Grand Master. |
| `vc/vc-shared.jsx`, `vc/vc-widgets.jsx`, `vc/vc-edit.jsx` | VC context + pointer-event fader/knob; one body per widget type (button, slider/knob, cue list, XY pad, speed dial, frame, label); selection/move/resize wrapper, widget palette and properties panel. |
| `SimpleDesk.jsx` | 512 channel strips per universe, live DMX values + overrides, keypad, dump to scene. |
| `InputOutput.jsx` | Universe table: rename, passthrough, monitor, add / remove (last universe only), input / output (several per universe) / feedback / profile pickers over `io.plugin.list` + `io.patch.*`, per-output pause / blackout, blackout; right panel with plugins (rescan / configure), input profiles, Grand Master, audio devices. |
| `io/io-shared.jsx`, `io/PatchProperties.jsx`, `io/InputProfileEditor.jsx`, `io/GrandMasterPanel.jsx`, `io/AudioDevices.jsx` | The I/O screen's sub-components: shared row / field helpers, the per-patch plugin parameter dialog (`io.patch.setParameters`, `io.plugin.configure/rescan`), the input profile editor with channel detection (`io.inputProfile.*`), Grand Master level + modes, host audio device pickers (`io.audio.*`). |
| `ShowManager.jsx` | Show Manager (Ctrl+5): show picker / create / rename, tracks (add, rename, mute, solo, move, delete), the timeline with time or BPM markers, zoom and grid, items (drag across time and tracks, resize, lock, colour, copy / paste, delete, function picker adding at the cursor), alignment and timing panel, ripple insert / cut, transport with the playhead cursor. Registers `window.QLCScreens.show`. |
| `data.js` | Mock workspace used while offline. |
| `_ds_bundle.js`, `styles.css`, `tokens/` | The compiled QLC+ design-system components and their CSS tokens. |
| `assets/icons/`, `assets/fonts/` | SVG icons (the qmlui icon set) and Roboto Condensed / Roboto Mono / Font Awesome. |
| `vendor/` | React 18, ReactDOM and Babel standalone - vendored so the page is fully offline. |

Everything is referenced relative to `index.html`, so the directory can be mounted at any URL
path. `?ctx=fx|vc|sd|io` in the URL picks the initial screen.

## What is live and what is not

Everything below was exercised end to end in a real browser against a real QLC+ 5 instance
(`qlcplus5 --api --webui`, SF3 workspace, no IO plugins) on 2026-09-26, with every mutation
verified by reading the server back (`functions.get`, `vc.widget.get`, `io.dmx.universe.get`, ...)
and a second tab checked for the pushed event:

- **Connection**: auto-connect from `qlcplus-config.json`, lamp states, automatic reconnect after
  the server dies and comes back (re-`hello`, docRevision re-synced, Simple Desk DMX stream
  re-subscribed).
- **Fixtures & Functions**: fixture and function trees; fixture tools (intensity, colour incl. the
  typed hex field, pan/tilt spin boxes and XY pad, presets) write real DMX through Simple Desk
  overrides, *Release fixtures* clears them; multi-select; re-addressing; Add Fixtures over
  `fixtures.defs.*` + `fixtures.patch` (Add is blocked until the range is free); fixture groups
  (create / assign / unassign / delete); palettes (create / apply / delete); unpatch. Functions:
  create of every type (Scene, Chaser, Sequence, EFX, Collection, RGB Matrix, Show, Script, Audio,
  Video) in a folder, rename, move to folder, multi-select delete, context menu; start / pause /
  resume / stop with server-reported running state; Scene editor (add fixtures, inline values,
  console faders, remove channel, add palette, fade times); Chaser editor (add / move / remove steps,
  run order, common duration - per-step times are governed by the speed modes exactly like the
  desktop app); RGB Matrix editor (2026-09-27: fixture group, algorithm incl. every installed RGB
  script, colour slots per `acceptedColors`, script properties, Text and Image parameters, blend /
  control mode, animated preview polled from `functions.rgbmatrix.getPreview` - all verified by
  `functions.get` read-backs and the saved `.qxw`, driver `tools/e2e/rgbmatrix.js`). Collection
  editor (add members through the picker, remove, move up / down; loops and self-membership refused
  by the server) and EFX editor (add every head of a fixture, per-head mode / reverse / start offset,
  reorder, "set an offset on all fixtures", algorithm, relative, width / height / offsets / rotation /
  start offset, Lissajous frequency and phase, propagation, dimmer control, duration; the preview
  canvas draws the server-computed pattern and animates the heads along it) - both exercised end to
  end on 2026-09-27 in a sandbox instance by `tools/e2e/efx-collection.js`, with the saved `.qxw`
  checked. Not in the EFX editor: the fake-3D sphere preview and adding one specific head of a
  multi-head fixture (every head is added). A Show is edited on the Show Manager screen (below).
- **Fixture views and placement** (added 2026-09-27, verified against a `fixtures` sandbox on
  ports 9210/9211 with the SF3 project by `webui/tools/e2e/fixtures-views.js`): a view switcher in
  the F&F centre area (Details | 2D | DMX | Universe grid). 2D view: the stage in the project's point
  of view with every fixture at its monitor position, live head colours from the watched universe,
  click / ctrl-click / alt-click (single head) / rubber-band selection wired to the tree, drag to
  move, zoom, align left / top, distribute, arrange circle / grid / line (with detect from placement
  and face centre), rotate around the centroid, move to centre, gel colour, select all / odd / even /
  every Nth, invert selection in group(s), stage settings (point of view, units, size, labels, fixture
  groups overlay, background reset) and the initial point-of-view prompt, and "Pick a 3D point"
  (`fixtures.monitor.aimAt` writes Pan/Tilt as desk overrides). Fixture detail: position / rotation /
  gel colour, invert pan / tilt, lock, hide, linked copies. DMX view: per-fixture channel values from
  the live stream, absolute / relative addresses, DMX / percent, double-click to set a value. Universe
  grid: address map with hover details, click to select, cut / paste into the first free block of
  the shown universe, drag a fixture to a new address. Fixture Remap: clone or retarget fixtures to
  another definition / mode / universe / address, auto-connect channels, apply - Scenes, groups,
  2D positions and VC widgets follow. Fixture Tools gained a Highlight (locate) toggle. Not in the
  browser: the Qt3D view itself (position / rotation editing is the parity), uploading a background
  picture (the file must already be on the server), DMX-driven position / rotation per axis.
- **Fixture-side leftovers** (added 2026-09-27, `webui/ff/FixtureMisc.jsx`, verified against an
  `fxmisc` sandbox on ports 9250/9251 started with `-UserModifiersDir C:\qlcsandbox\fxmisc\UserModifiers`
  by `webui/tools/e2e/fixtures-misc.js`): fixture detail - mode combo (`fixtures.update {mode}`,
  atomic, overlap-checked), per-channel Fade / Behaviour (auto, forced HTP or LTP) / Modifier with
  "Apply changes to fixtures of the same type" (`fixtures.channel.setBehaviour`), a Channel Modifiers
  Editor (template list, SVG curve with draggable handlers, add / remove handler, save as user
  template, rename, delete; `fixtures.modifiers.*`), a printable Fixture summary (definition,
  addressing, physical block, channels). Toolbar: Add an RGB panel (`fixtures.createRgbPanel`:
  columns, rows, components, size, start corner, snake / zig-zag, direction) and a printable
  Universe summary (channels used, weight, power, DIP switches). Fixture Groups: "Edit layout..."
  opens the grid editor (size, drag a head to move / swap, select + Swap / Remove, rotate 90 / 180 /
  270, flip, regenerate in DMX order, reset, place any head by picking it and clicking a cell).
  Fixture Tools: a Color filters section (`fixtures.colorFilters.list`, read-only), a single-axis
  position tool for pan-only / tilt-only fixtures, and a Channels console (fader window with page
  shift, pan / tilt mode, multiple channel selection, copy to all fixtures of the same type) on the
  live / Scene target. Printing uses the browser's print dialog on a print-only copy of the
  summary. Not in the browser: editing colour filter files, and mapping an external controller onto
  the console (the Qt feature lives in qmlui's SceneEditor, there is no engine / API hook).
- **Script / Audio / Video editors** (added 2026-09-27, verified against a `media` sandbox on ports
  9140/9141 by `webui/tools/e2e/media.js`): Script - line-numbered editor, insert-method menu from
  `functions.script.listCommands`, function / fixture ID pickers, server-side syntax check with the
  error line marked in the gutter, Save / Ctrl+S via `functions.script.setSource`. Audio - Replace
  file through the server-side file browser (`functions.audio.setSource`, the file is copied into the
  project's media store and the origin recorded), Reload (`functions.media.reload`), duration,
  playback mode, output device (from `functions.audio.listCapabilities`), volume, fade in / out.
  Video - Replace file / URL, output screen and mode (windowed / fullscreen / Spout), custom
  geometry, rotation, layer. Read-only there, no API setter yet: mute, Detect BPM, video volume,
  Spout sender size. Media info (duration, sample rate, resolution, codecs) comes from the engine's
  decoders / the desktop probe; an instance started without its `Plugins` directory reports none.
- **Server-side file browser** (`webui/ff/ServerFileBrowser.jsx`, `window.ServerFileBrowser`):
  drives / home, path crumbs, a typed path, glob filters over `core.fs.list`. Used by the Audio and
  Video editors; the Open-project dialog still takes a typed path plus recent files.
- **Virtual Console**: page switch; Toggle and Flash buttons with state colouring from
  `vc.button.stateChanged`; slider and knob with the value pushed to every tab; cue list
  play / next / previous / stop / jump with the current step highlighted; XY pad; speed dial
  set / tap; multipage frame next / previous (the frame page flip bumps docRevision - the event
  carries the new one); Grand Master. Design-mode editing: add page / rename / delete, add widgets
  from the palette, move, resize, caption, attach function, Flash action, copy / paste, delete,
  all persisted (reload shows the same layout). Widget configuration (2026-09-27, verified with
  `webui/tools/e2e/vc-layout.js` against a sandbox): the complete Frame / Solo Frame panel
  (header, enable button, collapse, multipage with page count / loop / page labels / "clone first
  page", solo mixing and exclude-monitored, PIN setup) and the complete Button (attached function
  with a Usage popup, pressure behaviour, startup intensity, stop-all fade, flash priority flags)
  and Slider panels (display style, mode, function control with attribute picker and flash
  button, Level-mode channel picker over the fixture list, click & go type, monitoring, value
  range, catch-up, grand master modes); a Label has no settings beyond its style. PIN-protected
  pages and frames ask for the PIN (unlocked per browser session). Edit-mode layout tools: align
  left / right / top / bottom to the first selected widget, distribute horizontally / vertically
  (3+ widgets in one frame), style a multi-selection in one `vc.widget.bulkStyle`, "Add widgets
  from functions" (a button, adjust slider or cue list per picked function), "Create a widget
  matrix" (buttons or sliders in a new frame / solo frame) and the Usage popup.
- **XY Pad, Clock, Animation, Audio Triggers** (`webui/vc/vc-props-live.jsx`, added 2026-09-27,
  verified against a `vclive` sandbox on ports 9180/9181 by `webui/tools/e2e/vc-live.js`): XY pad
  fixtures (fixture / single head / fixture group / universe picker, per-head Pan/Tilt range and
  reverse in degrees, % or DMX), Pan/Tilt window, inverted Y, floor control (stage-grid pad plus a
  height fader), presets (position from the cursor, Scene/EFX function, fixture group or head;
  rename, reorder, remove, apply from the body) - the pad drives real DMX; clock type, countdown
  target and schedules (function, start / stop time, weekdays, repeat) with the day-time spin boxes,
  play / pause / reset, a live countdown (`vc.clock.timeChanged`) and the local wall clock;
  animation RGB Matrix, visibility, instant changes, colour swatches, algorithm combo, colour /
  R-G-B knob / text / script-algorithm presets (with the script's parameters), fader and preset
  buttons / knobs in the body; audio triggers bar count, volume, per-bar type (DMX with the channel
  picker, Function, VC widget) and thresholds, capture toggle and the live bars meter
  (`vc.audioTriggers.levelsChanged`; capture runs on the QLC+ host). Opening the page on
  `http://[::1]:<port>/` now works (the connection split IPv6 literals at their first colon).
- **External controls + key bindings** (`webui/vc/vc-external.jsx`, added 2026-09-27, verified
  against a `vcinput` sandbox on ports 9220/9221 by `webui/tools/e2e/vc-input.js`): every widget's
  property panel has an "External controls" section (ExternalControls.qml): input sources picked
  by hand (universe + channel, or a channel of the input profile patched on that universe), the
  control each one drives, custom feedback values (lower / upper / monitor, plus the profile's
  colour table and MIDI channel routing when it has them), remove; keyboard combinations recorded
  by pressing them in the browser (re-record, change control, remove); auto-detection of a
  controller input (`vc.widget.inputDetect.*` - arming, the single server-wide slot and cancel were
  exercised; a real controller signal cannot reach the plugin-less sandbox, so the binding itself
  is only unit-tested). All of it is saved in the `.qxw` (`<Input .../>`, `<Key>...</Key>`).
  **Key bindings are honoured by the browser**: the server cannot see the browser's keyboard, so
  while the Virtual Console screen is shown, edit mode is off and no text field has focus, a key
  combination bound on a widget of the current page triggers it through the normal live methods -
  button press / release (`vc.button.press`), cue list next / previous / play / stop, frame
  next / previous page, page shortcut, enable, collapse, speed dial tap / factor / reset / apply /
  preset, slider flash, XY pad / animation presets. A matched key is swallowed, so a widget
  binding wins over the App shortcuts (Ctrl+1..5, Space tap, Ctrl+B/S/Z/Y), like the desktop VC;
  unbound keys fall through. A small key-cast strip shows which combination fired what. Not
  mapped in the browser: animation intensity and audio-trigger capture bindings (no live method
  yet), and VC page activation keys (no API for page bindings).
- **Cue List + Speed dial** (`webui/vc/vc-props-cue.jsx`, added 2026-09-27, verified against a
  `vccue` sandbox on ports 9170/9171 by `webui/tools/e2e/vc-cue.js`): cue list properties (attach /
  detach a Chaser or Sequence, Play/Pause+Stop vs Play/Stop+Pause layout, next/previous behaviour,
  side fader None / Crossfade / Steps) and the live side fader with the crossfade step labels
  (`vc.cueList.setSideFaderLevel` / `sideFaderChanged`); speed dial properties (controlled
  functions with per-function fade in / fade out / duration factors, tap-controls-BPM, reset factor
  on dial change, dial time range, visible parts, presets add / rename / retime / remove through
  `vc.widget.preset.add/remove` + `vc.speedDial.preset.update`) and the live body per the
  visibility mask: dial knob, 1/16..16 factor buttons and -/+/x (`vc.speedDial.setFactor`), TAP
  with right-click reset (`vc.speedDial.resetTap`, blinking at the tapped interval), h/m/s/ms
  fields, Apply (`vc.speedDial.apply`) and preset buttons (`vc.widget.preset.apply`). The Steps
  side-fader mode is wired but was only exercised in Crossfade mode; XY pad / Animation presets are
  server-side stubs (INVALID_STATE) until their own slice lands.
- **Simple Desk**: universe tabs, live values + overrides, faders, keypad (`1 THRU 12 AT 128`,
  `+% 20`, `FULL`, `ZERO`, `CLR`, ...), per-channel reset, reset universe, dump to a new Scene,
  fixture list panel. Since 2026-09-27 the keypad goes through the engine's own parser
  (`io.simpleDesk.sendKeypadCommand`; the channel selection is remembered across commands and
  the commands history is the server's, shared with every client - "server" in the header);
  the browser parser (`io/keypad-parser.js`) stays as the fallback for servers without it.
- **Input / Output**: universe rename / passthrough / add / delete (last universe only), patch
  pickers (an instance without IO plugins shows just "None"), input profiles list, blackout.
  Added 2026-09-27 (verified against an `io` sandbox on ports 9190/9191 by
  `webui/tools/e2e/io.js`): universe monitor toggle; Grand Master level + channel / value mode
  in the right panel; the host's audio input / output device pickers (`io.audio.*`); the input
  profile editor (`io/InputProfileEditor.jsx`: manufacturer, model, type, MIDI note-off, channel
  table with the MIDI channel / message / parameter mapping, behaviour, sensitivity, custom
  feedback, colour table, MIDI channel labels, save / reopen / delete - the `.qxi` lands in the
  QLC+ host's user profile folder, `QLCPLUS_USER_INPUTPROFILE_DIR` in a sandbox) with channel
  auto-detection over `io.inputProfile.learn.*`. Built but only unit-tested (the sandbox has no
  IO plugins): the per-patch parameter editor (`io/PatchProperties.jsx`, `io.patch.setParameters`,
  plus "Configure plugin" which opens the plugin's native dialog on the QLC+ host and "Rescan"),
  per-output pause / blackout, extra output lines per universe, feedback lines, profile assignment.
- **Toolbar**: Stop all with the running count, BPM set / tap / off with the beat pulse, Undo /
  Redo (desktop history), Design / Operate mode, New / Open / Save / Save as (server-side paths,
  discard prompt when the project is modified).

- **Show Manager** (2026-09-27, driver `tools/e2e/show.js` on the SF3 Show "Midnight City"): show
  picker and creation, rename; add / rename / mute / solo / move / delete tracks; two Scenes added
  back to back at the cursor from the function picker; drag move (same track, across tracks, an
  occupied spot slides to the nearest free one exactly like the desktop drop, and the server's
  own `INVALID_PARAMS` overlap refusal with `suggestedStartTime` was checked directly); resize by
  the right edge; lock (a locked item ignores the drag) / unlock; colour; ripple insert at the
  cursor (covering item grows, later items shift); Markers BPM 4/4 -> Time -> BPM 4/4; play from
  the cursor with the cursor following `functions.show.<id>.playhead`, pause / resume with Space,
  stop; copy / paste at the cursor; delete with the confirmation; all read back through
  `functions.get` and the saved `.qxw`. Ripple cut, align start / end to cursor and the typed
  start / end / duration fields use the same `item.resize` / `item.move` / `rippleCutTime` calls
  but were not driven by the script. Not in the web UI: the preview-at-cursor scrub mode, the
  stretch-function resize mode, track Spout output size, the legacy timing conversion dialog,
  waveforms / beat markers inside items.

- **Fixture Editor** (2026-09-27, `FixtureEditor.jsx` + `fixtureeditor/*.jsx`, Ctrl+6, over
  `fixturedefs.*`; driver `tools/e2e/fixture-editor.js` against a `fixdefs` sandbox on ports
  9200/9201 started with `dev-webui-sandbox.ps1 -UserFixtureDir C:\qlcsandbox\fixdefs\UserFixtures`,
  which keeps every saved / deleted `.qxf` out of the real `%UserProfile%\QLC+\Fixtures` - the
  driver checks that folder is byte-for-byte untouched): one tab per open definition with the
  modified marker and a Save / Discard / Cancel prompt on close, surviving a page reload
  (`session.list` + the new `session.get`); New; Open from a manufacturer -> model picker with a
  user / system badge (never the unfiltered `fixturedefs.list`); Save (into the host's user fixture
  folder, `defRevision` handled, "someone else saved it" asks before overwriting); Save as user copy
  for a bundled definition (`session.forkToUser`, with the read-only banner and the prompt when
  saving a bundled one); Import (a `.qxf` uploaded from this computer) and Export (downloaded);
  Delete a user definition (a bundled one it shadowed comes back); Validate. General (manufacturer,
  model, author, type), Channels (add from a preset or Custom, remove, the channel wizard; name,
  preset, type, colour, coarse / fine, default value; capabilities with inline range /
  description, warnings, add / remove, the capability wizard, automatic colour assignment on
  Colour channels, per-capability preset with its colours / values / picture path, the alias
  editor with apply-to-all-modes), Modes (add / remove / rename, the slot list with drag or up /
  down ordering, acts-on, emitters from ticked channels, global-or-override physical), Physical,
  Aliases. Every session edit sends `baseRevision` and rebases on `CONFLICT`; a foreign edit from a
  second client was checked. Opened from Fixtures & Functions too: Add Fixtures -> "New definition"
  / "Edit this definition" (`window.QLCOpenFixtureEditor`). Not in the browser: uploading a gobo
  picture (the capability takes the path of a picture on the QLC+ machine), the desktop's free
  "Save as <path>" (definitions always land in the user fixture folder as
  `<Manufacturer>-<Model>.qxf`, which is where QLC+ looks for them) and Avolites D4 import.
  Exercised by the driver: everything above except drag-reordering of mode channels (the up /
  down arrows were driven; the drag uses native HTML5 drag-and-drop, which the headless driver
  does not synthesise) and Ctrl+S (the toolbar Save was driven).

Still not available in the web UI: the 2D / 3D / DMX monitor views, fixture-address
remap, UI settings, audio sample rate / channels / buffer size and the input
level check, the input signal indicator on a patch. Disconnected, every screen keeps
working on its built-in mock data (`data.js`), clearly labelled as such.

The source design system (component sources, guidelines, templates) lives outside this repo;
only runtime files are vendored here. When re-importing from it, keep the load order in
`index.html`: `_ds_bundle.js` contains stale compiled copies of the screens that the real files
loaded afterwards overwrite. Three components were patched locally in `_ds_bundle.js` after the
live pass and must be carried over (or fixed at the source) on a re-import, or the bugs come
back: `CustomSlider` (handle drifted past the track end at high values), `CustomSpinBox` (a
controlled input that snapped back on every non-numeric keystroke, so typing a value was
impossible) and `CustomPopupDialog` (new `disabledButtons` prop, used to block Add Fixtures while
the address range overlaps).
