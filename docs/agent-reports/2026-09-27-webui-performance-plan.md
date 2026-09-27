# Web UI + Control API performance plan (measured baseline, 2026-09-27)

Scope: the browser web UI (`webui/`) and the WebSocket Control API behind it (`controlapi/`),
running against the real show file (`<test project>` = SF3: 236 fixtures, 6 universes,
956 functions, 15 VC pages, 386 VC widgets). This is a plan; Phase 1 has since been implemented,
see "Implemented (Phase 1)" at the end. Every number in the "Baseline" section was measured.
Anything marked **estimate** was not.

## TL;DR

1. **Live-show safety: DMX output stops whenever the QLC+ main thread is blocked**, and every API
   handler runs on that thread. `Universe::tick()` is a queued slot on the main thread
   (`engine/src/inputoutputmap.cpp:161,168`). While a handler runs, no universe runs its fader
   cycle, so the output freezes. Measured: a 143 ms `fixturedefs.list` call froze all 6 universes
   for 152 ms, and a 1.2 s call froze them for 1.23 s. An unfiltered cold `fixturedefs.list` blocks
   for 2-14 s, which is also what tripped the freeze watchdog today (see "Watchdog incident").
   Every other handler SF3 uses on screen load finishes in under 30 ms.
2. **The Fixtures & Functions screen never stops rendering.** It sits in an endless React render
   loop at 100% of a browser core for as long as the tab is open (`FixturesFunctions.jsx:400-426`).
   If that browser runs on the show machine, it takes CPU from QLC+.
3. **Most of every page load is in-browser Babel.** Babel accounts for 4.0-5.9 s of the 4.4-6.9 s
   until data is shown, on each screen, on every load. At 4x CPU throttling (a stand-in for a
   tablet) the UI appears after 30-44 s. With the same JSX precompiled, it appears after
   **0.3-0.64 s** (x1) and **1.0-3.4 s** (x4). That was measured on a precompiled copy, not estimated.
4. **Nothing is cached over HTTP.** Every load, warm or cold, transfers 5.5-5.7 MB in 89-128
   requests, each on a new TCP connection (`Cache-Control: no-cache`, no validators,
   `Connection: close`).
5. **Simple Desk costs 20% of a desktop core, and 99% at 4x**, following one universe at about
   15 updates/s (8.3k DOM nodes, one React state update per DMX delta). The Virtual Console with
   4 tabs and a 30 Hz fader drag is fine: 3 ms p50 latency, 1-2% CPU per tab, 0.14 core on the
   server.

## Method and environment

- **Machine**: 24 cores / 64 GB, Windows 11, shared with other agents' builds during the
  measurements. Commit charge was 52-56 of 64 GB throughout and CPU load reached 65% in the
  Release run. Absolute server timings are noisy (see the `fixturedefs.list` spread below). Ratios
  measured in a single run are reliable.
- **Server**: `dev-webui-sandbox.ps1 -Name perf` (a plugin-less sandbox with SF3 and all patches
  stripped), running this worktree's **Debug** build. The user's live install is also a Debug
  build: `CMAKE_BUILD_TYPE=Debug` in the main checkout's `build/`, which `dev-build-run.ps1`
  deploys. The Qt window was visible for the first runs and minimized after 14:43, following
  master's new sandbox default. The rate of idle output gaps was similar in both states (see
  Engine interplay).
- **Temporary probes** lived in a throwaway local build and were **not committed**: output-cycle
  gap logging in `Universe::run()`, per-handler timing in `ApiDispatcher::dispatch()`, and per-topic
  fan-out counters in `ApiServer::broadcast()`.
- **Client**: headless Chrome through `webui/tools/cdp.js`, at 1600x1000. "x4" means
  `Emulation.setCPUThrottlingRate(4)`, a rough tablet stand-in (**estimate**: real tablets vary a
  lot). A real tablet over Wi-Fi was not tested.
- **Reusable tools**, committed in `webui/tools/perf/` (each file's header documents it; all take
  sandbox ports and refuse 9010/9011):

  | Tool | Measures |
  |---|---|
  | `page-load.js` | Per-screen load timeline, Babel CPU from a CDP profile, API calls on load, HTTP bytes, long tasks, idle CPU %, console errors |
  | `babel-cost.js` | Offline JSX compile cost with the browser's exact Babel options, and with lean options |
  | `api-cost.js` | Round trip, payload size and main-thread stall per API method; N+1 cost of `functions.get` / `vc.widget.get` over every item |
  | `engine-stall.js` | Gaps in the live DMX stream before, during and after one slow call |
  | `live-screens.js` | Per-screen event rate, bytes and main-thread busy % while functions run |
  | `dmx-fanout.js` | Server cost of N clients following every universe |
  | `multi-client.js` | N VC tabs plus a 30 Hz fader from another client: latency, busy %, server CPU, and the refetch fan-out of one VC edit |
  | `precompile-experiment.js` | Writes a precompiled copy of `webui/`, for measuring the "after" of a build step |

## Baseline

### 1. Page load per screen (x1 = desktop, no throttling)

Times are in ms from navigation start. "Data shown" means the last API response of the initial
burst arrived, with nothing else for 1.5 s. Babel is the self time of `vendor/babel.min.js` in a
CPU profile taken during the load.

| Screen | Run | First render | WS ready | Data shown | Babel CPU | HTTP | API on load | Idle CPU after load | DOM nodes |
|---|---|---|---|---|---|---|---|---|---|
| fx (Fixtures & Functions) | cold | 5287 | 5432 | 5491 | 4864 | 128 req / 5670 KiB | 13 calls / 349 KiB | **100%** | 514 |
| fx | warm | 4484 | 4646 | 4710 | 3965 | 127 / 5666 KiB | 13 / 349 KiB | **100%** | 514 |
| vc (Virtual Console) | cold | 4939 | 5025 | 5047 | 4541 | 89 / 5563 KiB | 11 / 152 KiB | 1% | 324 |
| vc | warm | 5088 | 5154 | 5247 | 4670 | 89 / 5563 KiB | 11 / 152 KiB | 1% | 324 |
| sd (Simple Desk) | cold | 6136 | 6277 | 6643 | 5618 | 103 / 5594 KiB | **53** / 246 KiB | 2% | **8272** |
| sd | warm | 6310 | 6534 | 6889 | 5802 | 103 / 5594 KiB | 53 / 246 KiB | 3% | 8272 |
| io (Inputs/Outputs) | cold | 6200 | 6340 | 6370 | 5721 | 96 / 5588 KiB | 20 / 161 KiB | 1% | 823 |
| show (Show Manager) | cold | 6374 | 6530 | 6619 | 5879 | 100 / 5715 KiB | 12 / **447 KiB** | 2% | 3346 |
| fxeditor (Fixture Editor) | cold | 6219 | 6302 | 6342 | 5784 | 90 / 5583 KiB | 9 / 149 KiB | 1% | 280 |

- Cold and warm transfer identical bytes. That is the caching problem, measured.
- On every screen the connection only opens after all JSX is compiled: "WS ready" comes about
  100 ms after "first render". So nothing loads from the network while Babel runs.
- Long tasks: 15-32 per load, the longest 500-1161 ms. The tab cannot respond during the whole
  compile.
- Duplicate calls on load: fx issues `functions.list` twice (297 KiB), and show issues it
  **three times** (445 KiB of its 447 KiB). sd issues 39 `fixtures.get` calls, one per patched
  fixture (N+1).
- The fx idle CPU of 100% is the render loop (problem P2).

### 2. Page load at 4x CPU throttling (tablet stand-in, cold)

| Screen | First render | Data shown | Babel CPU | Longest task |
|---|---|---|---|---|
| fx | 37,329 | 38,847 | 35,510 | 3629 |
| vc | 40,121 | 41,225 | 38,558 | 3452 |
| sd | 36,975 | 44,139 | 34,944 | 3224 |
| io | 33,802 | 34,548 | 32,331 | 3786 |
| show | 29,465 | 30,551 | 28,221 | 3204 |
| fxeditor | 30,353 | 30,999 | 29,072 | 3941 |

Babel took 7-8 times longer here than unthrottled, not 4. Other load on the machine at the time is
a likely contributor. In any case, at this speed the web UI is not usable.

### 3. The same UI with the JSX precompiled (measured, not estimated)

`precompile-experiment.js` compiled every `text/babel` file ahead of time with the vendored Babel:
the react preset plus block scoping only, no inline source maps, and no `babel.min.js` on the page.
The sandbox served that copy, and `page-load.js` ran unchanged. All six screens rendered with zero
console errors and the same DOM, minus the 46 removed `<script>` tags.

| Screen | Data shown x1 (before → after) | Data shown x4 (before → after) | HTTP per load |
|---|---|---|---|
| fx | 5491 → **638** | 38,847 → **1350** | 5670 → 2826 KiB |
| vc | 5047 → **300** | 41,225 → **1011** | 5563 → 2718 KiB |
| sd | 6643 → **558** | 44,139 → **3404** | 5594 → 2768 KiB |
| io | 6370 → **378** | not run | 5588 → 2743 KiB |
| show | 6619 → **527** | not run | 5715 → 2870 KiB |
| fxeditor | 6342 → **361** | not run | 5583 → 2739 KiB |

Long tasks during load dropped from 15-32 (max 1.2 s) to 0, except sd with 2 (max 82 ms). At x4, sd
still has a 699 ms long task: that is its 8k-node desk, not compilation.

Offline, in Node (`babel-cost.js`, 46 files, 1246 KiB of JSX), the browser's Babel options cost
7.8 s warm and produce 6160 KiB of output including inline source maps. The react-only options
cost 1.25 s and produce 1473 KiB. babel-standalone's defaults for script tags are presets
`react` + `env` with **no targets**, so everything is downleveled to ES5, plus three plugins and
`sourceMaps: "inline"`. Treat the browser numbers above as authoritative; the Node run is serial,
with no idle time and a different warm-up.

### 4. API cost on the server (Debug build, SF3, round trip over loopback)

The handler runs synchronously on the main thread, so the round trip is also how long the QLC+
main thread (desktop UI plus DMX output, see §5) was blocked. The "stall" column is the longest
round trip seen meanwhile by a second connection pinging every 5 ms. A 5-17 ms stall is the idle
noise floor: the main thread also renders the QML UI.

| Method | Median | Max | Response |
|---|---|---|---|
| `functions.list` | 7.7 ms | 10 ms | 148 KiB |
| `fixtures.list` | 13.4 ms | 15.6 ms | 50 KiB |
| `vc.widget.list` (all pages) | 18.2 ms | 20.4 ms | 259 KiB |
| `vc.widget.list` (one page) | 1.3-4.3 ms | 12.8 ms | 0.1-1.4 KiB |
| `fixtures.monitor.get` (2D view) | 21.5 ms | 25.3 ms | 167 KiB |
| `functions.get`, biggest Scene (#121) | 5.4-12.9 ms | 29 ms | 31.5 KiB |
| `functions.get`, biggest Show (#113) | 1.0 ms | 2.9 ms | 10.7 KiB |
| `functions.get` over all 956 functions, sequential | 559-610 ms total | single max 15 ms | 1785 KiB |
| `vc.widget.get` over all 386 widgets, sequential | 82-91 ms total | | |
| everything else used on load (`hello`, `io.*`, `core.*`, `palette.list`, `vc.page.list`, `fixtures.get`, ...) | ≤ 2 ms | | ≤ 4 KiB |
| **`fixturedefs.list` unfiltered, first call** | **14,061 ms** | | 280 KiB, 1782 definitions |
| `fixturedefs.list` unfiltered, warm | 92-118 ms (quiet) / 1.7 s (under load) | | 280 KiB |
| `fixturedefs.list` per manufacturer, cold, all 149 in sequence | 2.0 s (quiet) / 9.8 s (65% CPU load) in total | largest single call 111 ms / 1037 ms (American DJ, 161 defs) | |

- `fixturedefs.list` is the only unbounded handler. It force-loads and parses every definition
  XML on the main thread. The same work took 2 s to 14 s depending on machine state (file cache,
  CPU and memory pressure). On warm calls,
  `QLCFixtureDefCache::fixtureDef()` does a linear scan per entry, which makes the unfiltered list
  O(n²) (`controlapi/src/domains/apifixturedefsdomain.cpp:1054`,
  `engine/src/qlcfixturedefcache.cpp:50`). The web UI itself only calls it filtered by
  manufacturer (`FixtureEditor.jsx:58,251`), but any client can send the unfiltered call.
- A Release build was also measured, but the machine was at 65% CPU load from other work at the
  time, and the same cold load was *slower* than in Debug. That comparison is inconclusive and no
  Debug-vs-Release ratio is claimed.

### 5. Engine interplay: does API traffic delay DMX output?

**Yes, one-for-one.** In code: `MasterTimer::timerTick()` runs on its own thread and emits
`tickReady()`. That signal is connected with `Qt::QueuedConnection` to `Universe::tick()`, and the
`Universe` QObject lives on the main thread, so the slot waits in the main thread's event queue.
`tick()` only releases a semaphore, which the universe's own writer thread (`Universe::run()`)
waits on before every `processFaders()` + `dumpOutput()`. If the main thread is busy, no universe
writes output, on all universes at once. The writer thread's `tryAcquire(2 x tick)` then just loops.

Measured with the temporary `Universe::run()` gap probe and `engine-stall.js` (15 RGB Matrices
running, following the live DMX stream):

| Blocking call | Handler time | DMX stream gap during it (baseline max) | Output-cycle gap, all 6 universes |
|---|---|---|---|
| `fixturedefs.list {manufacturer: Chauvet}`, cold, quiet machine | 143 ms | 152 ms (69 ms) | **152 ms**, same timestamp on u0-u5 |
| same, Release binary, machine under load | 1214 ms | 1258 ms (80 ms) | **1228 ms** |
| per-manufacturer cold loop (149 calls) | up to 1037 ms each | n/a | 1055, 713, 487, 338 ms... one gap per heavy call |

- Background rate: with only measurement traffic, output-cycle gaps above 40 ms (two ticks at
  50 Hz) occurred 64 times in 378 s with the window visible and 44 times in 318 s minimized.
  Typical gaps were 41-48 ms (one dropped tick), up to 162 ms. These gaps are not caused by the
  API alone: the main thread also renders the desktop QML UI.
- Why it matters: in the ArtNet → video chain (~30 fps, flashes need ≥ 2 frames, about 66 ms), a
  150 ms freeze eats 4-5 frames of every running effect. A 1.2 s freeze is plainly visible, and a
  multi-second `fixturedefs.list` is a blackout-length hold of the whole rig.

### 6. Live updates while a show runs (15 RGB Matrices running)

| Screen | Events/s received | KiB/s | Tab main thread busy x1 | x4 | DOM |
|---|---|---|---|---|---|
| sd (follows one universe, the desk's default: id 0, shown as Universe 1) | 16.7 (15 DMX deltas + beat) | 16.4 | **20%** | **99%**, 17 long tasks, max 108 ms | 8273 |
| vc | 1.8 (core.beat) | 0.1 | 1% | 3% | 325 |
| show | 1.7 | 0.1 | 2% | not run | 3347 |
| fx | 1.8 | 0.1 | **100%** (render loop) | 99% | 519 |

DMX stream fan-out on the server (`dmx-fanout.js`, every client subscribed to all 6 universes):

| Clients | Frames/s | Total KiB/s | Bytes per frame (27 changed channels) | QLC+ process CPU | Main-thread ping p99 |
|---|---|---|---|---|---|
| 0 | 0 | 0 | n/a | 0.50 core-s/s | 8.7 ms |
| 1 | 90 | 75 | 855 | 0.48 | 7.3 ms |
| 5 | 452 | 377 | 854 | 0.42 | 8.1 ms |

- Server cost of `ApiServer::broadcast()` (temporary probe): 93 ms per 5 s at about 415
  frames/s, i.e. about 45 µs per frame, or roughly 2% of the main thread. Each event is serialised
  once per session (`apiserver.cpp`, `buildEvent` inside the loop).
- With zero subscribers, `slotUniverseWritten()` still builds the per-channel JSON diff: about
  21 ms per 5 s. Small, but spent for nothing.
- A delta is `{"channel":N,"value":V}` per changed channel, about 32 B each. The payload could be
  3-4 times smaller (**estimate**).

### 7. Several clients on the Virtual Console (`multi-client.js`)

4 VC tabs, each opened after the previous one finished loading. A Node client sent
`vc.slider.setValue` at 30 Hz (the UI's own drag throttle) for 6 s.

| | 1 tab | 4 tabs |
|---|---|---|
| Sender round trip p50 / p95 | 1.5 / 5.3 ms | 2.4 / 9.3 ms |
| Latency send → event in each tab, p50 / p95 / max | 1 / 4 / 6 ms | 3 / 7-8 / 16-19 ms |
| Tab main thread busy | 1% | 1-2% each |
| QLC+ process CPU | 0.11 core-s/s | 0.14 core-s/s |

After one `vc.widget.update` from another client, **every** tab made 4 calls: `core.project.get`
x2, `vc.page.list`, `vc.widget.list` (page) = 3.1 KiB on the page it showed. A 35-widget page
would be about 25 KiB (**estimate** from 690 B per widget).

### 8. Watchdog incident (14:34, sandbox pid 51184): caused by this measurement

The freeze watchdog fired with a 12.7 s heartbeat gap. The cause was this plan's deliberate cold,
unfiltered `fixturedefs.list` (`api-cost.js`, 14,061 ms round trip). The commit charge was
56/65 GB at the time, and the same work later took 2.0 s on a quieter machine, so part of the 14 s
was memory pressure.

The report's main-thread backtrace shows `App::event` → Qt Quick waiting on the render thread
(`wglSwapBuffers`), not an API handler. That is a capture-timing artefact: attaching gdb takes
seconds, and by the time it dumped, the 14 s handler had returned. The snapshot caught the first
scenegraph sync after the stall.

The sandbox recovered on its own: the script's next calls (warm `fixturedefs.list`,
manufacturer-filtered list) succeeded right away. The report's "Recovered after ~30 s" is the
time until the dialog was dismissed, not the stall length: `appendRecoveryNote()` can only run
after `onFreezeDetected()` returns (`qmlui/freezewatchdog.cpp:229-235`).

Severity for a live show, stated separately:

- (a) Any API client can freeze the rig's output for as long as a handler runs (§5).
- (b) The watchdog adds to the outage. `gdb -p` + `thread apply all bt` suspends **every** thread,
  including the Universe writers, for the length of the dump. It then shows a TOPMOST modal on the
  operator's screen.

I did not re-run the unfiltered cold call, to avoid popping that dialog again.

## Problems ranked by user impact, with fixes

Ordering: live-show safety first, then perceived speed, then tablets and multi-client. Effort is
S (hours), M (a day or two) or L (several days). "Verify" names the tool to re-run.

### P1 (safety): slow handlers freeze DMX output

**P1a: make `fixturedefs.list` bounded.**
- Fix: iterate `m_defs` directly instead of `fixtureCache()` + `fixtureDef()` per entry (removes
  the O(n²) warm cost). Either answer the unfiltered list from `FixturesMap.xml` metadata without
  force-loading (drop or lazily fill `channelCount`/`modeCount`), or reject an unfiltered call with
  `INVALID_PARAMS` and require `manufacturer`.
- Gain: unfiltered 2-14 s → about 10-100 ms (**estimate**); the warm case is measured at 92 ms
  today.
- Effort S. Risk: low. The web UI only uses the filtered form, so check spec consumers.
- Files: `controlapi/src/domains/apifixturedefsdomain.cpp`, `engine/src/qlcfixturedefcache.{h,cpp}`
  (optional helper), `docs/api-spec/fragments/fixturedefs*.yaml` + notes, the domain test.
- Verify: `api-cost.js` (drop `--skip-defs`), `engine-stall.js --params '{}'` (unfiltered), on a
  fresh sandbox.

**P1b: decouple DMX output from the main thread (one engine line, needs a soak test).**
- Fix: connect `MasterTimer::tickReady` → `Universe::tick` with `Qt::DirectConnection` instead of
  `Qt::QueuedConnection` (`engine/src/inputoutputmap.cpp:161,168`). `tick()` only touches a
  `QSemaphore`, which is thread-safe, so calling it from the timer thread is safe.
- Gain: output keeps running through **any** main-thread stall: API handlers, the QML UI, project
  load, the watchdog dialog (but not a gdb dump). This is the single biggest live-safety lever.
- Effort S to write, M to validate. Risk: medium. It changes engine timing that upstream relies on,
  and the "at most one pending tick" check becomes racy (harmless: at most one extra cycle). Needs
  a 30-minute soak with a busy show plus the gap probe. **Flag to the user before doing it**: it
  changes existing engine behaviour, even though no functional change is expected.
- Verify: re-add the `Universe::run()` gap probe; `engine-stall.js` must show a DMX *output* gap
  of about 20 ms during a 1 s handler. The WS stream itself will still pause, because it is
  delivered on the main thread.

**P1c: move expensive handlers off the main thread, or make them incremental.**
- Fix: parse fixture definitions with `QtConcurrent` into fresh `QLCFixtureDef` objects and insert
  them on the main thread; reply asynchronously (the dispatcher already allows a late
  `session->send`). Same pattern for future RGB script dry runs and big imports. Add a dispatcher
  budget log (warn above 50 ms) so new slow handlers get noticed.
- Gain: no handler longer than about 50 ms on the main thread (**estimate**).
- Effort M. Risk: medium (engine object thread-affinity).
- Files: `apifixturedefsdomain.cpp`, `apidispatcher.cpp`.
- Verify: the `api-cost.js` "stall" column stays under 30 ms on every row.

**P1d: watchdog behaviour during a show** (outside `webui/`, related).
- Fix: skip the gdb all-threads dump, or cap it, when output is patched; show a non-modal
  notification instead of a TOPMOST dialog.
- Effort S. Risk: low (less diagnostic detail).
- Files: `qmlui/freezewatchdog.cpp`, `qmlui/diagnostics.cpp`.

### P2 (safety and CPU): endless render loop on Fixtures & Functions

- Fix: `fixturesRoot`/`functionsRoot` are new arrays on every render
  (`FixturesFunctions.jsx:400-405`). The effect at `:416-426` depends on them and always calls
  `setSelected(s => s.filter(...))`, which returns a new array, which renders again, forever.
  `useMemo` the two roots on `[live, fixtureTree, functionTree]`, and return the same `s` when
  nothing was filtered out. The static audit found the same pattern in `ShowManager.jsx:234-255`
  (`td.tracks || []` → `itemsById` → `setSelection`). It is **not reproduced**: SF3 has Shows, and
  show idle CPU was 2%. It would fire with no Show loaded, so fix it the same way.
- Gain: 100% → about 1% of a core for every open FF tab (measured before; after is an
  **estimate** from the other screens).
- Effort S. Risk: low.
- Verify: `page-load.js --ctx fx`, idle CPU column.

### P3 (perceived speed, tablets): compiling JSX in the browser on every load

Options, weighed:

| Option | Load time (desktop / x4) | Keeps "no build step"? | Cost |
|---|---|---|---|
| 0. Status quo | 4.4-6.9 s / 30-44 s | yes | none |
| A. Interim: `data-presets="react" data-plugins="transform-block-scoping"` on every `text/babel` tag (drops the ES5 `env` downlevel) | about 1.0-1.5 s / 5-8 s (**estimate**: offline compile 7.8 s → 1.2-1.5 s; the same transforms rendered all 6 screens with 0 errors in the precompiled run) | yes | S: one attribute per tag in `index.html` |
| B. **Checked-in precompiled output**, generated by a Node script that uses the vendored `babel.min.js` (no npm), with a check that fails when `.jsx` and output disagree, plus a `?dev=1` switch that still loads JSX through Babel for editing | **0.3-0.64 s / 1.0-3.4 s (measured)** | the runtime has no build step; developers run one script after an edit, and CI/`check-jsx.js` enforces it | M: script, stale-check, README; `dev-build-run.ps1` or CMake install copies the compiled files |
| C. Cache compiled output in IndexedDB keyed by file hash | first load unchanged; later loads about B | yes | M, plus invalidation bugs, private-mode fallback, and tablets still pay the first 30-44 s |
| D. Service worker that compiles and caches | like C | yes | L, and service workers need https or localhost, which conflicts with plain-http LAN tablets |

- Recommendation: A now (Phase 1, trivial), then B (Phase 2). B keeps the "any agent can edit a
  `.jsx` and reload" workflow through `?dev=1`, and ships the fast path by default.
  `precompile-experiment.js` is the prototype: it already produces a working tree. The real script
  should write next to the sources or into a `webui/dist/` that the web server prefers. The
  `_ds_bundle.js` stale screen copies (see the `index.html` comment) can go at the same time.
- Risk (B): a forgotten regeneration ships stale UI; mitigate with the stale-check in
  `check-jsx.js` and CI.
- Files: `webui/index.html`, new `webui/tools/build.js` (or promote the experiment),
  `webui/tools/check-jsx.js`, `webui/README.md`, `webui/CMakeLists.txt` / install rules.
- Verify: `page-load.js --throttle 1` and `--throttle 4`; `babel-cost.js` for option A.

### P4 (speed, tablets on Wi-Fi): no HTTP caching, no keep-alive

- Fix: add `ETag` (size + mtime, or a hash) and `Last-Modified`, answer `If-None-Match` /
  `If-Modified-Since` with 304, and keep `Cache-Control: no-cache` (revalidate every time, so live
  edits still show up at once). With P3-B, add content-hashed file names plus
  `Cache-Control: max-age=31536000, immutable` for vendor/bundle files. Also support HTTP/1.1
  keep-alive, or at least stop sending `Connection: close` for every one of about 100 requests.
- Gain: warm load transfers about 0 KiB instead of 5.6 MB (2.8 MB after P3). On a 20 Mbit/s
  tablet link that is about 2.2 s saved per load (**estimate**). On localhost the gain is small.
- Effort S (validators) / M (keep-alive). Risk: low. The unit test pins `no-cache` today, so
  extend it rather than drop it.
- Files: `controlapi/src/webserver.{h,cpp}`, `controlapi/test/webserver/webserver_test.cpp`.
- Verify: `page-load.js --runs 2`; the warm run's HTTP KiB column should be about 0.

### P5 (tablets): Simple Desk and the live DMX views re-render per delta

- Fix: coalesce `'channels'` events and apply them once per `requestAnimationFrame` (the stream
  arrives at about 15-50 Hz per universe; the screen and the video pipeline need no more than
  that). Render only the visible strips: 512 strips are always mounted today, about 8.3k nodes.
  Page or virtualise them by the horizontal scroll window. The static audit found the same
  per-delta whole-view re-render in `ff/View2D.jsx:229-261` (new `Uint8Array` per delta, whole SVG)
  and `ff/ViewDMX.jsx:126-135` (memo defeated by a new array per delta). Those were not measured,
  because the FF render loop (P2) masks them; re-measure after P2.
- Gain: Simple Desk 20% → about 5% (x1) and 99% → about 25% (x4) (**estimate**); load DOM
  8.3k → about 1.5k.
- Effort M. Risk: low to medium (desk layout, scroll behaviour).
- Files: `webui/SimpleDesk.jsx`, `webui/ff/View2D.jsx`, `webui/ff/ViewDMX.jsx`,
  `webui/ff/ViewUniverseGrid.jsx`, possibly `webui/api/qlcplus-api.js` (batching in `_routeEvent`).
- Verify: `live-screens.js --ctx sd --throttle 4`, busy % and long tasks.

### P6 (speed, multi-client): duplicate and N+1 fetches, whole-list refetch on events

Measured:
- `functions.list` 2x on fx and 3x on show (297 / 445 KiB).
- 39 `fixtures.get` calls on sd.
- One VC edit → 4 calls in every open tab.

From the static audit (not all measured):
- `App.jsx:72-75` refetches `core.project.get` on 17 topics, without debounce.
- `functions.updated`, which also fires on timing edits such as speed spin boxes
  (`apifunctionsdomain.cpp:1286`), makes every FF tab refetch the whole `functions.list` (148 KiB,
  150 ms debounce).
- `useLiveList` does not skip the client's own echoes.
- `vc.cueList.get` / `vc.widget.get` per CueList / Speed widget.
- `io.universe.get` per universe.

Fixes:
- Share one `functions.list` store app-wide (App already holds one).
- Apply event payloads instead of refetching where the event carries the entity:
  `functions.updated` carries the timing fields, `vc.widget.updated` / `configChanged` carry the
  widget.
- Keep refetch only as the fallback after `core.history.changed` / `core.project.loaded`.
- Batch the N+1 calls behind a `fixtures.get {fixtureIds:[...]}` form (a spec change) or reuse the
  `FF` fixture cache in Simple Desk.
- Debounce `core.project.get`.

Gain: show load −300 KiB, fx load −150 KiB; per remote edit, about 1 call per tab instead of 4-5;
during a speed-box drag, no 148 KiB refetch per tab every 150 ms (**estimate**).
Effort M (L for "apply payloads everywhere"). Risk: medium, because missed updates show stale
data. Keep the refetch fallback on history and load events.
Files: `webui/App.jsx`, `webui/FixturesFunctions.jsx`, `webui/ShowManager.jsx`,
`webui/SimpleDesk.jsx`, `webui/VirtualConsole.jsx`, `webui/ff/ff-core.jsx`, spec fragments if a
batch method is added.
Verify: `page-load.js` (API calls/KiB columns); `multi-client.js` (fan-out lines).

### P7 (tablets): DMX stream payload and subscription hygiene

- Fix:
  - Send deltas as a flat array (`"changes":[ch,val,ch,val,...]`) or base64 of changed ranges,
    under a new versioned topic so old clients keep working.
  - Skip building the diff in `slotUniverseWritten()` when no session subscribes to that universe.
  - Serialise each broadcast event once rather than once per session (`ApiServer::broadcast`).
  - Keep VC live topics ungated; they are cheap (§7). The other ungated live topics, unctions.status.changed (start/stop only) and core.beat (about 2/s), measured at about 1.8 events/s per tab in total (§6), so they need no gating.
- Gain: about 855 → about 250 B per frame (**estimate**); about 75 → about 22 KiB/s for a client
  following all universes; server main-thread cost −0.4% idle and −1-2% with 5 clients (measured
  magnitudes, small).
- Effort S-M. Risk: low, as a spec-versioned addition.
- Files: `controlapi/src/domains/apiiodomain.cpp`, `controlapi/src/apiserver.cpp`,
  `webui/api/qlcplus-api.js`, `docs/api-spec/fragments/io*.yaml` + notes.
- Verify: `dmx-fanout.js`.

### P8 (speed, after P3): code-splitting and lazy screens

- Fix: after precompiling, the page still parses about 1.5 MB of app JS for every screen.
  Load rarely used editors on demand with a tiny loader (`<script>` injection on first open,
  registering into the existing `window.QLCScreens` / `QLCEditors` extension points): Wizard,
  Fixture Editor, Input Profile Editor, tools-misc, Show Manager, RGB/EFX/Video editors.
- Gain: about 100-300 ms (x1) and about 0.5-1 s (x4) on first render (**estimate**).
- Effort M. Risk: low to medium (load order of globals).
- Files: `webui/index.html`, the build script from P3, `webui/App.jsx`.
- Verify: `page-load.js --throttle 4`.

### P9 (smoothness): list virtualisation and render hygiene (static audit, not measured)

- Function and fixture trees: recursive, not memoised, about 7 nodes per row; 956 functions if
  fully expanded.
- VC canvas: no `React.memo` in `vc/*`; `ctx` is not memoised (`VirtualConsole.jsx:413-416`), so
  every live value event re-renders every widget body.
- The Connection context value includes `log`, so each `apiError` re-renders the whole app
  (`Connection.jsx:167-201`).
- `functions.status.changed` / `core.beat` re-render App plus the active screen
  (`App.jsx:146,181`).
- Simple Desk and Grand Master drags send one request per mouse-move event, unthrottled
  (`SimpleDesk.jsx:149`, `VirtualConsole.jsx:84`), unlike VC faders (33 ms).

Fixes:
- Windowed rendering for trees over about 200 visible rows.
- `React.memo` on widget bodies and tree rows, and memoise `ctx`.
- Move `log` out of the context value.
- Throttle desk and GM drags like VC faders.

Effort M. Risk: low. Verify: `live-screens.js` busy %, plus a React Profiler pass.

### P10 (server, unmeasured): run the show on a Release build

The live app is a Debug (`-O0`) build. Server-side handlers are already small (§4), so the web UI
gains little. The engine and QML UI would gain generally. This comparison was inconclusive here
because the machine was loaded (§4). Re-measure on a quiet machine with `api-cost.js` +
`engine-stall.js` against a `build-release/` sandbox before recommending it for shows.

## Phased plan

**Phase 1: quick wins (each S, independent, about 1-2 days total)**

1. P2: fix the FF (and Show Manager) render loop. Measured 100% → expected about 1% idle CPU.
2. P1a: bound `fixturedefs.list` (fix the O(n²) scan; require a filter or answer from map
   metadata).
3. P3-A: `data-presets="react" data-plugins="transform-block-scoping"` on the `text/babel` tags.
   Estimated 4-6 s → about 1-1.5 s load on desktop.
4. P4: `ETag`/`Last-Modified` + 304 in `webserver.cpp` (warm loads stop re-downloading 5.6 MB).
5. P6 (partial): drop the duplicate `functions.list` calls on fx and show; debounce `core.project.get`.
6. P7 (partial): skip the DMX diff when nobody subscribes.
7. P1b: **decide with the user**, then prototype the one-line `DirectConnection` change behind a
   soak test. It is the largest live-safety gain, but it changes existing engine behaviour.

**Phase 2: structural (M each)**

- P3-B: checked-in precompiled output with a `?dev=1` JSX fallback and a stale-check (measured
  0.3-0.64 s / 1.0-3.4 s).
- P5: rAF coalescing plus a windowed Simple Desk; fix per-delta re-rendering in View2D, ViewDMX
  and the grid.
- P6: apply event payloads instead of refetching; batch the N+1 calls.
- P1c: off-main-thread definition parsing, and a dispatcher slow-handler log.
- P4: keep-alive, and immutable caching of hashed bundles.

**Phase 3: polish (M-L)**

- P8: lazy screens and editors.
- P9: virtualised trees, memoised VC widget bodies and contexts, throttled desk/GM drags.
- P7: compact DMX delta topic; serialise each broadcast once.
- P1d: watchdog behaviour during shows.
- P10: Release build for shows, after a clean measurement.

## Not measured / open

- A real tablet over real Wi-Fi. x4 CPU throttling is only a stand-in, and network latency is not
  modelled.
- The 2D view and DMX view under live load: the FF render loop (P2) saturates that tab regardless,
  so re-run `live-screens.js` for them after P2 (the view has to be selected in the UI; there is no
  URL parameter for it yet).
- Show playhead, audio-trigger level streams, a busy chaser's `functions.chaser.currentStepChanged`
  rate, and VC pages with many live widgets (SF3 has 1 slider, 1 speed dial and 333 buttons).
- Clean-machine absolute numbers, and a Debug-vs-Release ratio (§4).
- The unfiltered cold `fixturedefs.list` was not re-run after the watchdog incident. Its range
  (2-14 s) comes from the single call plus the per-manufacturer sums.

## Implemented (Phase 1), 2026-09-27

Items 2-6 of Phase 1. Item 1 (the F&F render loop) landed earlier in `f920175cd`; item 7 (DMX
output off the main thread) is a separate task and was not touched here.

Same method as the baseline: `dev-webui-sandbox.ps1` (SF3, patches stripped), Debug build,
headless Chrome through the tools in `webui/tools/perf/`. "Before" is the commit this work started
from (`2e9d1e239`, with item 1 already in), measured in the same session right before the changes.
The machine was shared with other agents' builds throughout (a first build attempt hit
`cc1plus: out of memory`), so absolute numbers are noisy; the ratios are large enough not to care.

### What changed

| Item | Change | Commit |
|---|---|---|
| P1a `fixturedefs.list` | One pass over the new `QLCFixtureDefCache::fixtureDefs()` (no more `fixtureDef()` linear lookup per row). Unfiltered: nothing is parsed, rows come from what the fixtures map knows; `type`/`author`/`channelCount`/`modeCount` only for already loaded definitions, flagged by a new `loaded` field. Filtered by manufacturer: loads that manufacturer only (`ensureLoaded()`), rows complete as before. Spec + notes + `listUnfilteredDoesNotLoadDefinitions` + engine `fixtureDefsAndEnsureLoaded`. | `d176cf541` |
| P4 HTTP caching | `ETag` (size + mtime, taken before the read) and `Last-Modified` on every file; `If-None-Match` (list, weak, `*`) or, without it, `If-Modified-Since` answers a bodyless `304`. `Cache-Control: no-cache` stays, so every file is revalidated on each load and edits under `--webui-root` show up on a plain reload (verified against the sandbox: a touched file answers 200 to its old tag). 4 new `webserver_test` cases. | `ecc5d274b` |
| P7 (partial) DMX diff | New `ApiServer::hasSubscriber()`; `slotUniverseWritten()` skips the per-channel diff when nobody subscribes to that universe but keeps the diff base current, so the first delta after `subscribe` is unchanged. Test `dmxDiffWithoutSubscriberKeepsSnapshotCurrent` (real ticks). | `066803054` |
| P6 (partial) duplicate calls | `api/qlcplus-api.js` coalesces identical in-flight reads (`COALESCED_READS`), cleared by any non-read request, each sharer gets its own copy; `App.jsx` debounces the `core.project.get` refresh (150 ms trailing); Simple Desk fetches `fixtures.get` once per fixture type instead of once per fixture. | `cad19ce8d` |
| P3-A Babel presets | `data-presets="react" data-plugins="transform-block-scoping"` on all 47 `text/babel` tags; `check-jsx.js` compiles with the same options and fails on a tag without them. No build step. | `daf3b207f` |

### Before / after

`fixturedefs.list` (`api-cost.js`; round trip = main thread blocked):

| Call | Before | After |
|---|---|---|
| unfiltered, first call | **19,968 ms**, 280 KiB (tripped the freeze watchdog, see below) | **16 ms**, 187 KiB |
| unfiltered, warm | 182 ms | 17 ms |
| `manufacturer: "American DJ"` (161 defs), cold | 1691 ms | 122 ms (the machine was much quieter in the after run, so the parsing cost itself is not comparable; this row is the remaining bound per call) |
| `manufacturer: "SF3"` | 4.6 ms | 0.8 ms |
| `engine-stall.js --params '{}'` (DMX stream gap while the unfiltered call runs) | 1.2-20 s (baseline §5) | 63 ms max during vs 64 ms max before = noise floor |

Page load, data shown (`page-load.js`, ms; x1 cold / warm):

| Screen | Before x1 | After x1 | Babel CPU x1 cold | Longest task x1 cold | Before x4 cold | After x4 cold |
|---|---|---|---|---|---|---|
| fx | 6196 / 5339 | **1581 / 1585** | 5493 → 1000 | 759 → 166 | 32,933 | **7531** |
| vc | 5162 / 4754 | **1556 / 1460** | 4713 → 1201 | 607 → 142 | 33,240 | **7900** |
| sd | 4543 / 4534 | **1919 / 1706** | 3908 → 1244 | 477 → 172 | 31,705 | **9446** |
| io | 4571 / 5626 | **1728 / 1383** | 4060 → 1288 | 431 → 160 | 29,352 | **7730** |
| show | 6525 / 6452 | **1230 / 1064** | 5818 → 864 | 688 → 102 | 28,819 | **8117** |
| fxeditor | 6652 / 6070 | **1407 / 1187** | 6021 → 1080 | 970 → 141 | 27,498 | **7707** |

All six screens: 0 console errors before and after, same DOM node counts. The plan's estimate for
P3-A (1.0-1.5 s / 5-8 s) held on desktop; x4 landed at 7.5-9.4 s. babel-standalone still adds
inline source maps (the attributes cannot turn them off), which is part of what separates this
from the precompiled 0.3-0.64 s of P3-B.

HTTP bytes per load (`page-load.js`, run 2 = warm, cache enabled):

| Screen | Before warm | After warm | Cold (unchanged) |
|---|---|---|---|
| fx | 127 req / 5682 KiB | 127 req / **18 KiB** | 5702 KiB |
| vc | 89 / 5579 KiB | 89 / **13 KiB** | 5592 KiB |
| sd | 103 / 5611 KiB | 115 / **16 KiB** | 5625 KiB |
| io | 96 / 5605 KiB | 96 / **14 KiB** | 5618 KiB |
| show | 100 / 5731 KiB | 100 / **14 KiB** | 5745 KiB |
| fxeditor | 90 / 5600 KiB | 90 / **13 KiB** | 5613 KiB |

The request count is unchanged: every file is still revalidated (one 304 each, one TCP connection
each). Keep-alive and immutable hashed bundles remain Phase 2.

API calls on load (`page-load.js`, cold):

| Screen | Before | After | Removed |
|---|---|---|---|
| fx | 13 calls / 349 KiB | 11 / 200 KiB | `functions.list` 2 → 1, `core.project.get` 2 → 1 |
| vc | 11 / 152 KiB | 10 / 152 KiB | `core.mode.get` 2 → 1 |
| sd | **53** / 246 KiB | **16** / 166 KiB | `fixtures.get` 39 → 3, `io.simpleDesk.get` 2 → 1 |
| io | 20 / 161 KiB | 19 / 161 KiB | `io.blackout.get` 2 → 1 |
| show | 12 / **447 KiB** | 10 / **150 KiB** | `functions.list` 3 → 1 |
| fxeditor | 9 / 149 KiB | 9 / 149 KiB | none |

One `vc.widget.update` from another client (`multi-client.js`): each VC tab made 4 calls before
(`core.project.get` x2, `vc.page.list`, `vc.widget.list`) and 3 after (`core.project.get` x1).

DMX diff without subscribers (`dmx-fanout.js`, 15 RGB Matrices running, 0 clients, 20 s windows):
QLC+ process CPU 0.25-0.30 core-s/s before, 0.27-0.35 after. The saving the baseline measured with
a probe (about 21 ms per 5 s, 0.4 % of a core) is below this measurement's noise, so no after
number is claimed; the change is kept because it costs nothing. With 1 subscriber the stream is
unchanged (86 frames/s, 28 changed channels per frame, about 870 B per frame).

### Tests and e2e

- `apifixturedefsdomain_test` 31/31, `webserver_test` 34/34, `apiiodomain_test` 42/42,
  `qlcfixturedefcache_test` 16/16 (engine; needs `resources/fixtures` staged into the build tree).
- Against the sandbox after all changes: `vc-layout.js` 59/59, `show.js` PASS,
  `fixtures-views.js` all passed, `tools-misc.js` all passed, `fixture-editor.js` all passed,
  `check-jsx.js` ok for all 57 files. `fixturedefs-smoke.js` passes its list checks (1782 rows in
  15 ms); its one failure is its own precondition (it expects the user fixture folder redirected
  to a copy named `fixdefs`, which this sandbox did not have).

### Deviations and notes

- **`fixturedefs.list`**: answered from the map instead of rejecting unfiltered calls. Both kept
  every web UI caller working (they all filter); this one also keeps the smoke test and any
  "does this model exist" client working. The filtered form still parses on the main thread
  (P1c, Phase 2).
- **Duplicate calls**: done in the transport (in-flight coalescing) rather than a shared
  app-wide `functions.list` store, because all the duplicates on load were concurrent. It also
  removed duplicates the plan did not list (`core.mode.get`, `io.blackout.get`,
  `io.simpleDesk.get`). Calls that are not concurrent (a refetch after an event) are unaffected;
  applying event payloads instead of refetching is still P6 in Phase 2.
- **Watchdog, again**: the "before" run of the cold unfiltered `fixturedefs.list` (19,968 ms) set
  off the freeze watchdog in the sandbox (13.2 s heartbeat gap, recovered after about 77 s, which
  is the time until its dialog was dismissed). The dialog is TOPMOST on the desktop of whoever is
  at the machine. After the change the same call cannot do that any more.
- **x4 "before"**: the first x4 run stalled on `sd` for over 15 minutes (headless Chrome did not
  return while a build ran next to it) and was stopped. The x4 before numbers above are a re-run
  against the new server serving the unchanged web UI of `2e9d1e239` (cold runs disable the HTTP
  cache, so the server change does not affect them). The two x4 screens that did complete in the
  first attempt (fx 43,575 ms, vc 41,543 ms) show how much machine load moves x4 numbers.
