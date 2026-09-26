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
| `FixturesFunctions.jsx` | Fixture tree (per universe) + function tree (per folder), detail panes, start/stop, rename, delete. |
| `VirtualConsole.jsx` | Pages + widgets at their real geometry, live interaction, Design-mode layout editing, Grand Master. |
| `vc/vc-shared.jsx`, `vc/vc-widgets.jsx`, `vc/vc-edit.jsx` | VC context + pointer-event fader/knob; one body per widget type (button, slider/knob, cue list, XY pad, speed dial, frame, label); selection/move/resize wrapper, widget palette and properties panel. |
| `SimpleDesk.jsx` | 512 channel strips per universe, live DMX values + overrides, keypad, dump to scene. |
| `InputOutput.jsx` | Universe / patch table (read-only against today's server). |
| `data.js` | Mock workspace used while offline. |
| `_ds_bundle.js`, `styles.css`, `tokens/` | The compiled QLC+ design-system components and their CSS tokens. |
| `assets/icons/`, `assets/fonts/` | SVG icons (the qmlui icon set) and Roboto Condensed / Roboto Mono / Font Awesome. |
| `vendor/` | React 18, ReactDOM and Babel standalone - vendored so the page is fully offline. |

Everything is referenced relative to `index.html`, so the directory can be mounted at any URL
path. `?ctx=fx|vc|sd|io` in the URL picks the initial screen.

## What is live and what is not

Live against the Control API when connected: fixture and function trees and details, function
start/stop (running state is *not* reported back by the server), rename/delete/unpatch, Simple
Desk values/overrides/reset/keypad/dump, Virtual Console pages and widget layout, Grand Master,
blackout, engine mode, project name and save, universe table.

Not yet possible because the server lacks the methods: Virtual Console button presses and slider
moves (`vc.button.press` / `vc.slider.setValue` are in the spec but not registered by the
server - the UI detects the `Unknown method` reply and switches to view-only), cue lists / XY
pads / speed dials, I/O patching and plugin enumeration, adding fixtures (no fixture-definition
browsing), stop-all, BPM. Not yet built in the UI although the server could do it: function
editors (scene values, chaser steps), fixture re-addressing, VC layout editing, Show Manager.

The source design system (component sources, guidelines, templates) lives outside this repo;
only runtime files are vendored here. When re-importing from it, keep the load order in
`index.html`: `_ds_bundle.js` contains stale compiled copies of the screens that the real files
loaded afterwards overwrite.
