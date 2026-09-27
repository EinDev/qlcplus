# Web UI parity slices: common brief for worktree agents

You are one of several agents each landing one slice of "the browser web UI (`webui/`) can do
everything the Qt/QML UI (`qmlui/`) can". Read this file completely before touching anything;
your own task prompt names the slice. Everything here is mandatory.

## What a slice consists of

1. **Spec first.** `docs/api-spec/fragments/*.yaml` is the source of truth for the Control API
   (AsyncAPI; message per request/response/event; `docs/api-spec/00-conventions.md` explains the
   envelope, `docRevision`/`baseRevision` optimistic concurrency for document edits, no revision for
   live/runtime control, subscribe-gated event topics, error codes). Most methods you need are
   already specified but not implemented: implement them *as specified*. If the spec is wrong or
   missing something (say so in your report), extend the fragment + its `-notes.md` in the same
   style, then re-run the merge: `C:\msys64\mingw64\bin\python3.exe docs/api-spec/_tools/merge.py`
   (needs PyYAML in MSYS2 python; if it is missing, note it and skip the merge, do not pip-install).
2. **Server.** `controlapi/src/domains/api<domain>domain.{h,cpp}` registers methods with
   `dispatcher->registerMethod(QStringLiteral("x.y"), lambda)` and broadcasts events with
   `m_server->broadcast(topic, data, originClientId, gated)`. Read one existing domain end to end
   first (`apifunctionsdomain.cpp` for document edits with `baseRevision`, `apiiodomain.cpp` for live
   state and a DMXSource, `apivcdomain.cpp` + `apivchost.h` for the "engine-agnostic domain calls a
   host interface implemented by qmlui's App" pattern - anything needing `qmlui/` objects (Virtual
   Console widgets, Show Manager helpers, monitor views, input profile editor) MUST go through a host
   interface in `controlapi/src/` implemented in `qmlui/app*.cpp`; `controlapi` never links `qmlui`).
   Prefer a NEW domain file for your slice over growing an existing one (e.g.
   `apiefxdomain.cpp`, registered in `apiserver.cpp`'s constructor and listed in
   `controlapi/src/CMakeLists.txt`) so parallel agents don't collide. `functions.get`'s per-type
   `typeDetail` is supplied through `ApiFunctionsDomain::setTypeDetailProvider(Function::XType, fn)`
   from your domain's constructor. Engine signals from `qlcplusengine.dll` must be connected with the
   string-based `connect(obj, SIGNAL(...), this, SLOT(...))` form: pointer-to-member connects across
   this MinGW DLL boundary silently never fire.
3. **Server tests.** Every new method gets a QTest case in `controlapi/test/api<domain>domain/`
   (copy the layout of `controlapi/test/apifunctionsdomain/`: `<name>_test.{h,cpp}`, `CMakeLists.txt`,
   `add_subdirectory` + the `QLCPLUS_CONTROLAPI_TESTS` list in `controlapi/test/CMakeLists.txt`). Tests
   drive a real `ApiServer` over a real `QWebSocket`; assert on responses, events and engine state.
   Write the failing test before the implementation where practical.
4. **Client wrapper.** `webui/api/domains/<domain>.js` already has thin wrappers for most specced
   methods; add missing ones in the same style. `webui/api/qlcplus-api.js` is the transport: read
   its header (docRevision tracking, `isUnsupported()`, event subscription, own-echo filtering).
5. **UI.** Plain React 18 + JSX compiled in the browser by the vendored Babel - no build step, no
   npm, no imports: files are `<script type="text/babel">` tags in `webui/index.html` (order
   matters; add yours after the file whose globals you use) and share globals via
   `window.<Name>` / `Object.assign(window, {...})`. Components come from the design-system bundle:
   `const { RobotoText, IconButton, GenericButton, CustomSpinBox, CustomComboBox, CustomCheckBox,
   CustomTextInput, CustomSlider, SectionBox, CustomPopupDialog, ... } = window.PatchDesignSystem_5432c9;`
   (see how `webui/ff/FunctionEditors.jsx` and `webui/vc/vc-edit.jsx` use them; `_ds_bundle.js` is
   compiled output - do not edit it except for a documented bug fix, listed in `webui/README.md`).
   Icons: `window.QLCData.icon('<name>')` over `webui/assets/icons/`. Look at the matching
   `qmlui/qml/**/*.qml` for what the Qt editor offers and mirror its *capabilities*; layout may
   differ. Register into the extension points instead of editing shared files:
   - a function editor: `window.QLCEditors = Object.assign(window.QLCEditors || {}, { EFX: EfxEditor })`
     (props: `qlc, detail, reload, setDetail, functions, fixtures, selectedFixtureIds, palettes,
     onSelectFixtures`; `window.FF` in `webui/ff/ff-core.jsx` has the mutation queue, event helpers,
     fixture/channel utilities; `FF.TimingEditor` is the shared speed/run-order block).
   - a top-level screen: `window.QLCScreens.show = { id:'show', icon:'showmanager', label:'Show Manager',
     keys:'Ctrl 5', hotkey:'5', component: ShowManager, order: 10 }`.
   - Virtual Console widget body / properties: `window.QLCVCBodies[widgetType]` /
     `window.QLCVCProperties[widgetType]` (props documented in `webui/vc/vc-widgets.jsx` and
     `webui/vc/vc-edit.jsx`; shared context via `useVC()` from `webui/vc/vc-shared.jsx`).
   - toolbar buttons / actions-menu entries / UI settings: `window.QLCToolbarItems`,
     `window.QLCMenuItems`, `window.QLCUISettingsDialog` (see the comment at the top of `webui/App.jsx`).
   Offline (disconnected) the screens fall back to `webui/data.js` mock data labelled "(mock)"; a new
   screen may simply render "connect first" offline.
   Run `node webui/tools/check-jsx.js` after every edit: it must print `ok` for every file.
6. **Verify end to end in a sandbox, not against mocks** (see below), then update docs:
   `webui/README.md` ("What is live and what is not"), the row(s) for your slice in
   `docs/webui-parity.md` (status vocabulary is defined there; `live` only for things you actually
   exercised in the browser against your sandbox server, write "sandbox" in the notes column), and
   the fragment's `-notes.md` with an "Implemented <date>" section like the existing ones.

## Environment and build (Windows, MSYS2 MinGW64, Qt 6)

Your worktree has no `build/`. Use PowerShell (the `PowerShell` tool), never a Git Bash `timeout`
wrapper (it corrupts TMP for the compiler). Set, in every PowerShell call that builds or runs:

```powershell
$env:PATH = "C:\msys64\mingw64\bin;C:\msys64\usr\bin;$env:PATH"
$env:MSYSTEM = "MINGW64"; $env:MSYSTEM_CARCH = "x86_64"
$env:TMP = "<your scratchpad dir>"; $env:TEMP = $env:TMP
```

Configure once (about 10 s):
```powershell
cmake.exe -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug -DCMAKE_INSTALL_PREFIX=C:/qlcplus -Dqmlui=ON
```
Build only what you need, `-j 8` (several agents share this 24-core box):
```powershell
cmake.exe --build build --target qlcplus5 -j 8          # app (engine + controlapi + qmlui), ~4-6 min fresh
cmake.exe --build build --target <yourdomain>_test -j 8 # your test binary
```
Give the tool a 600000 ms timeout; a build that is still running after ~8 minutes is a problem to
diagnose (re-run and read the actual compiler error - never call a failure "OOM" or "flaky" without
the error text), not something to wait out. Incremental rebuilds take well under a minute.

Run a test binary directly, never through `ctest`. They are WIN32-subsystem (no console output):
```powershell
$env:PATH = "C:\msys64\mingw64\bin;$PWD\build\engine\src;$PWD\build\engine\audio;$env:PATH"
.\build\controlapi\test\<name>\<name>_test.exe -o result.txt,txt; Get-Content result.txt
```
Engine sources are CRLF: use the Edit tool for C++ edits, not scripted exact-string patches. The
sandbox refuses some compound bash forms (`export X=$(...)`, heredocs, `source`); keep shell calls
simple, prefer Glob/Grep/Read over `find`, and PowerShell for anything involving env vars.

## End-to-end verification (mandatory before you report done)

`dev-webui-sandbox.ps1` (repo root) starts a throwaway plugin-less copy of the app from YOUR build
on YOUR ports, serving the web UI from YOUR worktree, with the SF3 test project (its patches
stripped, so no DMX can leave the machine). It only *reads* `C:\qlcplus` (to copy it once into
`C:\qlcsandbox\<Name>`); that read is allowed. Your task prompt gives you `<Name>`, `<ApiPort>`,
`<WebUiPort>`; never use other ports and NEVER 9010/9011 (the user's live rig).

```powershell
.\dev-webui-sandbox.ps1 -Name <Name> -BuildDir .\build -WebUiRoot .\webui -ApiPort <ApiPort> -WebUiPort <WebUiPort>
# ... test ...
.\dev-webui-sandbox.ps1 -Name <Name> -Stop
```
It prints the log paths (`-d` output, so `qDebug`/`qWarning` from the server are in there - read
them when something misbehaves). Re-run it after every rebuild (it kills its own previous instance).
Drive headless Chrome with `webui/tools/cdp.js` from a Node script in your scratchpad (Node 22 is on
PATH; `launch()`, `open(url)`, `page.eval(fn)`, `page.waitFor(fn)`, `page.click(selector)`,
`page.type(text)`, `page.screenshot(file)`, `page.consoleErrors`). URL query `?ctx=fx|vc|sd|io|<id>`
picks the screen. After the page connects (the network lamp turns green; `window.__QLC` is not
exported, so assert on rendered content), exercise every new control the way an operator would,
then confirm the effect by reading the server back (a second `functions.get`, `vc.widget.get`, the
`.qxw` written by `core.project.saveAs` into the sandbox dir, ...), and check `page.consoleErrors`
is empty. Take a screenshot of the finished screen and Read it: it must look like the rest of the UI.
Every previous UI agent that tested only against mocks shipped bugs that the integration pass
found; do not be that agent. Save your driver script under `webui/tools/e2e/<slice>.js` and commit
it so the integration pass can re-run it.

## Hard rules

- Never run `dev-build-run.ps1` or `dev-ui-drive.ps1`. Never write under `C:\qlcplus`. Never touch a
  running `qlcplus5.exe` (the user's live show). Never run `ctest`. Never use ports 9010/9011. Never
  send a mutating API call anywhere but your own sandbox. Stay inside your worktree for edits.
- Only make `core.project.saveAs` write into your sandbox directory (`C:\qlcsandbox\<Name>\...`).
- Commit before every build. Conventional Commits (`feat(webui): ...`, `feat(controlapi): ...`,
  `test(controlapi): ...`, `docs(api-spec): ...`); several small commits are fine, never amend, no
  debug-only logging in a commit. End each commit message with
  `Co-Authored-By: Claude Fable 5.1 <noreply@anthropic.com>`.
- Do not edit `CLAUDE.md`, memory files, or the other agents' slices. If you must touch a shared
  file (`index.html`, `apiserver.cpp/.h`, the two `CMakeLists.txt` lists, `webui/README.md`,
  `docs/webui-parity.md`), keep the edit to the minimal added lines at the natural place; the
  coordinator resolves merge conflicts.
- Push back in your report if a requested feature cannot map onto a browser (say why) rather than
  faking it; deliver everything else in full.

## Report back (the coordinator sees only your final message)

- Commit hashes with one line each; configure/build wall-clock; the test binary's full pass/fail
  summary (paste failures verbatim).
- Exactly what you exercised in the browser against the sandbox (bullet per control) and what you
  did NOT get to, honestly.
- Spec changes made; anything you found wrong in the spec, engine or existing web UI.
- Rows of `docs/webui-parity.md` you changed and to what.
