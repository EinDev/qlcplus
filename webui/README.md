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
| `ff/FixtureDialogs.jsx` | Add Fixtures dialog (`fixtures.defs.*` + `fixtures.patch`), Fixture Groups panel, Palettes panel (create/edit/apply). |
| `VirtualConsole.jsx` | Pages + widgets at their real geometry, live interaction, Design-mode layout editing, Grand Master. |
| `vc/vc-shared.jsx`, `vc/vc-widgets.jsx`, `vc/vc-edit.jsx` | VC context + pointer-event fader/knob; one body per widget type (button, slider/knob, cue list, XY pad, speed dial, frame, label); selection/move/resize wrapper, widget palette and properties panel. |
| `SimpleDesk.jsx` | 512 channel strips per universe, live DMX values + overrides, keypad, dump to scene. |
| `InputOutput.jsx` | Universe table: rename, passthrough, add / remove (last universe only), input / output / feedback / profile pickers over `io.plugin.list` + `io.patch.*`, blackout. |
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
  desktop app). Collection editor (add members through the picker, remove, move up / down; loops
  and self-membership refused by the server) and EFX editor (add every head of a fixture, per-head
  mode / reverse / start offset, reorder, "set an offset on all fixtures", algorithm, relative,
  width / height / offsets / rotation / start offset, Lissajous frequency and phase, propagation,
  dimmer control, duration; the preview canvas draws the server-computed pattern and animates the
  heads along it) - both exercised end to end on 2026-09-27 in a sandbox instance by
  `tools/e2e/efx-collection.js`, with the saved `.qxw` checked. Not in the EFX editor: the fake-3D
  sphere preview and adding one specific head of a multi-head fixture (every head is added).
  Editors for RGB Matrix, Script, Audio, Video and Show are still placeholders.
- **Virtual Console**: page switch; Toggle and Flash buttons with state colouring from
  `vc.button.stateChanged`; slider and knob with the value pushed to every tab; cue list
  play / next / previous / stop / jump with the current step highlighted; XY pad; speed dial
  set / tap; multipage frame next / previous (the frame page flip bumps docRevision - the event
  carries the new one); Grand Master. Design-mode editing: add page / rename / delete, add widgets
  from the palette, move, resize, caption, attach function, Flash action, copy / paste, delete,
  all persisted (reload shows the same layout). Widget *configuration* beyond Button and Slider
  is still view-only (server `vc.widget.setConfig` covers those two types).
- **Simple Desk**: universe tabs, live values + overrides, faders, keypad (`1 THRU 12 AT 128`,
  `+% 20`, `FULL`, `ZERO`, `CLR`, ...), per-channel reset, reset universe, dump to a new Scene,
  fixture list panel.
- **Input / Output**: universe rename / passthrough / add / delete (last universe only), patch
  pickers (an instance without IO plugins shows just "None"), input profiles list, blackout.
  Plugin configuration dialogs and audio devices remain desktop-only.
- **Toolbar**: Stop all with the running count, BPM set / tap / off with the beat pulse, Undo /
  Redo (desktop history), Design / Operate mode, New / Open / Save / Save as (server-side paths,
  discard prompt when the project is modified).

Still not available in the web UI: Show Manager, the 2D / 3D / DMX monitor views, fixture-address
remap, plugin configuration, the fixture editor, UI settings. Disconnected, every screen keeps
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
