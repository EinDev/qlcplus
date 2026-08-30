# QLC+ (this checkout: QLC+ 5 / qmlui, Windows)

This checkout builds the **QLC+ 5 QML UI** (`-Dqmlui=ON`), not the QLC+ 4 Qt
Widgets UI. Local dev environment is Windows, built via **MSYS2/MinGW64** — not
the official Qt Online Installer + MSVC, for reasons below. This file exists so a
fresh session doesn't have to rediscover the toolchain setup from scratch.

## Why MSYS2/MinGW, not the Qt installer

`platforms/windows/CMakeLists.txt` hardcodes its bundled-DLL search path to
`C:\msys64\mingw64\bin` — this project's Windows build already assumes MSYS2.
More importantly: **Qt3D was dropped from the official Qt installer starting with
Qt 6.8** (Qt 6.7 was the last version to ship it as a binary component), but
`qmlui` hard-requires the classic Qt3D module (`3DCore 3DInput 3DLogic 3DQuick
3DQuickExtras 3DRender`, used by `qmlui/mainview3d.cpp`). MSYS2's own Qt6
packages still build and ship `qt6-3d`, so that's the only realistic path to a
working Qt3D on a current Windows box.

## One-time environment setup (already done on this machine)

- MSYS2 at `C:\msys64`. Packages installed:
  - `mingw-w64-x86_64-toolchain mingw-w64-x86_64-cmake mingw-w64-x86_64-ninja
    mingw-w64-x86_64-pkgconf mingw-w64-x86_64-libusb mingw-w64-x86_64-libsndfile
    mingw-w64-x86_64-fftw`
  - `mingw-w64-x86_64-qt6-base mingw-w64-x86_64-qt6-declarative
    mingw-w64-x86_64-qt6-svg mingw-w64-x86_64-qt6-multimedia
    mingw-w64-x86_64-qt6-websockets mingw-w64-x86_64-qt6-serialport
    mingw-w64-x86_64-qt6-3d mingw-w64-x86_64-qt6-5compat
    mingw-w64-x86_64-qt6-tools mingw-w64-x86_64-qt6-translations`
- FTDI D2XX SDK (proprietary, not redistributable — must be sourced from
  ftdichip.com by whoever sets this up) at `C:\projects\D2XXSDK`:
  `ftd2xx.h` at the root, `amd64\ftd2xx64.dll`, and `amd64\libftd2xx.a` — the
  `.a` is *not* what FTDI ships; it's generated from the 64-bit DLL via:
  ```
  cd /c/projects/D2XXSDK/amd64
  gendef.exe - ftd2xx64.dll > ftd2xx.def
  dlltool -k --input-def ftd2xx.def --dllname ftd2xx64.dll --output-lib libftd2xx.a
  ```
  (`plugins/dmxusb/src/CMakeLists.txt` links this unconditionally on `WIN32` —
  there's no build flag to skip it.)
- App is deployed/installed to `C:\qlcplus` (flat layout: exe, all DLLs, and
  `Plugins\`, `qml\` subfolders — this is `CMAKE_INSTALL_PREFIX` on Windows per
  `variables.cmake`, not something we chose).
- `C:\qlcplus\qt.conf` exists and is **required**:
  ```
  [Paths]
  Prefix = .
  Imports = qml
  Qml2Imports = qml
  ```
  Without it, `windeployqt` still copies the `qml\` folder correctly, but the
  QML engine keeps using the compiled-in MSYS2 import path instead, every
  `QtQuick.Controls`/`QtQuick.Layouts` import silently fails, and the app opens
  to a **blank white window with no error dialog** (only visible via `-d` +
  stderr capture: `module "QtQuick.Controls" is not installed`). If a fresh
  `windeployqt` run or a from-scratch install dir ever reintroduces this
  symptom, recreate this file first before looking anywhere else.
- `windeployqt` also does **not** bundle several transitive non-Qt MinGW runtime
  DLLs (it only knows about Qt's own libraries). If the app fails with a
  `STATUS_DLL_NOT_FOUND` dialog naming something like `libharfbuzz-0.dll`,
  copy the missing file(s) from `C:\msys64\mingw64\bin\` into `C:\qlcplus\`.
  Known ones already copied once: `libgcc_s_seh-1.dll`, `libstdc++-6.dll`,
  `libwinpthread-1.dll`, `libharfbuzz-0.dll`, `libicudt78.dll`, `libicuin78.dll`,
  `libicuuc78.dll`, `libfreetype-6.dll`, `libglib-2.0-0.dll`,
  `libpcre2-16-0.dll`, `libpcre2-8-0.dll`, `libpng16-16.dll`, `libbz2-1.dll`,
  `libiconv-2.dll`, `libintl-8.dll`, `libgraphite2.dll`, `libb2-1.dll`,
  `libbrotlicommon.dll`, `libbrotlidec.dll`, `libdouble-conversion.dll`,
  `libmd4c.dll`, `zlib1.dll`, `libzstd.dll`.

## Day-to-day build/run

Use `.\dev-build-run.ps1` (repo root, PowerShell) — it kills any running
instance (frees the exe file lock), rebuilds, redeploys the exe into
`C:\qlcplus`, and relaunches:

- `.\dev-build-run.ps1` — incremental build of the `qlcplus5` target only (fast;
  correct for anything under `qmlui/`, which is most day-to-day work since QML
  is embedded via `.qrc` into the exe — there's no hot reload, every QML edit
  needs this rebuild) and normal launch.
- `.\dev-build-run.ps1 -Debug` — same, but launches with QLC+'s own `-d` flag
  and redirects stdout/stderr to timestamped files under
  `%TEMP%\qlcplus-dev-logs\`. **Use this whenever you need to see console
  output** — without `-d`, Qt's default Windows message handler routes
  `qDebug`/`qWarning`/`console.log` to `OutputDebugString`, which a redirected
  stdout/stderr won't capture at all.
- `.\dev-build-run.ps1 -Target ""` — full rebuild (needed if a change touches
  `engine/`, `plugins/`, or anything outside `qmlui/`).
- `.\dev-build-run.ps1 -NoRun` — build + deploy only, don't launch.

Manual equivalent, if ever needed outside the script (from an MSYS2 MinGW64
shell — set `MSYSTEM=MINGW64` and `source /etc/profile` first so `cmake`/
`ninja`/`gcc` on `PATH` resolve to the MinGW64 ones, not any other install):
```
cmake --build build --target qlcplus5 -j$(nproc)
# copy build/qmlui/qlcplus5.exe over C:\qlcplus\qlcplus5.exe (kill the running
# instance first, it locks the file)
```

## Driving the UI yourself instead of asking the user to reproduce

`.\dev-ui-drive.ps1` sends real OS-level mouse input (via `SendInput`, not
`PostMessage` — QtQuick's own drag-active detection needs genuine input, not
just window messages) so you can click/drag inside the running app directly
rather than asking the user to do it for every reproduction:

- `.\dev-ui-drive.ps1 -Action Click -X <screenX> -Y <screenY> -Focus`
- `.\dev-ui-drive.ps1 -Action Drag -X <x1> -Y <y1> -ToX <x2> -ToY <y2>`

Coordinates are **absolute screen pixels**, not window-relative. Workflow:
screenshot the desktop (`System.Windows.Forms.SystemInformation]::VirtualScreen`
+ `CopyFromScreen`, as done throughout this session), read the image to find
what you need, crop/zoom with `System.Drawing.Graphics.DrawImage` at
`InterpolationMode = NearestNeighbor` if a UI element is too small to read
precisely, then convert back to real screen coordinates before calling this
script.

This moves the user's actual cursor — always say so before using it, same as
for screenshots.

**Caution, not yet trustworthy as a verification method**: this was built to
self-check the drag-and-drop ghost fix around commit `867ed7051`. A single
scripted drag + one log trace + one screenshot looked clean and got reported
to the user as "verified end-to-end" — the user then reproduced it manually
and the bug was still there. Don't repeat that mistake: one passing automated
run here is *supporting* evidence at best, not proof a UI bug is fixed. Manual
confirmation from the user is still required before claiming a UI-visible fix
actually works, especially for anything involving object lifetime/timing
(drag gestures, delegate destruction) where a scripted repro may not hit the
same race a real, slower human gesture does.

## Live-inspecting a running instance with GammaRay

GammaRay (KDAB's Qt introspection tool) is built from source at `C:\gr`
(`C:\gr\src` → `C:\gr\build` → installed to `C:\gr\install`) rather than
installed from MSYS2 packages. Reason: MSYS2 only ships `gammaray` for the
`ucrt64`/`clang64`/`clangarm64` environments, not `mingw64`, which is what
this project's Qt6 is built against — GammaRay's probe DLL must be built
against the *exact same* Qt6 binary as the target process, so a ucrt64
GammaRay cannot attach to our mingw64 `qlcplus5.exe`. Building from source
against our own `mingw-w64-x86_64-qt6-base` avoids the ABI mismatch entirely
and just works (confirmed against Qt 6.11.2).

Build recipe, from an MSYS2 MinGW64 shell (`MSYSTEM=MINGW64`, `source
/etc/profile`, same as the manual qlcplus build above):
```
cmake -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=/c/gr/install \
  -DGAMMARAY_MULTI_BUILD=OFF -DGAMMARAY_WITH_KDSME=OFF \
  -DGAMMARAY_BUILD_DOCS=OFF -DGAMMARAY_DISABLE_FEEDBACK=ON /c/gr/src
ninja && ninja install
```
(`GAMMARAY_WITH_KDSME=OFF` skips the state-machine-editor submodule, which
drags in a `graphviz` submodule whose pack files hit Windows path-length
limits when cloned somewhere deeply nested — not needed for QML/property
inspection anyway.) After installing, run `windeployqt.exe` on
`gammaray.exe`/`gammaray-client.exe`/`gammaray-launcher.exe` in
`C:\gr\install\bin` — the freshly-built binaries hit the exact same "no Qt
platform plugin could be initialized" failure as our own app does without
`windeployqt`/`qt.conf` (see above), just never having been deployed yet.

**Launching it properly** (PowerShell, this project's primary shell):
```powershell
Get-Process qlcplus5 -ErrorAction SilentlyContinue | Stop-Process -Force
$env:PATH = "C:\msys64\mingw64\bin;$env:PATH"
Start-Process "C:\gr\install\bin\gammaray.exe" -ArgumentList '"C:\qlcplus\qlcplus5.exe"'
```
Three things that make this "proper" rather than the naive one-liner:
- **Kill any already-running `qlcplus5.exe` first.** GammaRay launches a
  *new* instance rather than attaching to an existing one; skip this and you
  end up with two "Q Light Controller Plus" windows open (one injected, one
  not) and no way to tell them apart at a glance beyond the injected one's
  window title gaining the `(Injected by GammaRay)` suffix.
- **`C:\msys64\mingw64\bin` must be on `PATH`.** `windeployqt` only bundled
  `gammaray.exe`'s direct Qt6 dependencies into `C:\gr\install\bin` — none of
  the low-level MinGW runtime DLLs (`libstdc++-6.dll`, `libwinpthread-1.dll`,
  etc., the same set documented above for `qlcplus5.exe` itself). Without
  this, `gammaray.exe` exits immediately and silently (confirmed: `exit code
  1`, no window, no error dialog, nothing in stdout/stderr) — there's no
  visible symptom pointing at a missing DLL, it just doesn't start.
- **Use `Start-Process`, not a direct call.** `gammaray.exe` runs in the
  foreground and blocks the calling shell until the target process exits;
  `Start-Process` (or backgrounding with `&` from an MSYS2 shell) returns
  immediately so the shell stays usable for `dev-gammaray-inspect.ps1`.

This opens `gammaray-client.exe`, a live Qt-object/QML-tree browser attached
to the running process (title: `GammaRay (Q Light Controller Plus)`).

**`.\dev-gammaray-inspect.ps1`** drives that client window via Windows UI
Automation instead of screenshots — Qt's accessibility bridge exposes
GammaRay's own Widgets UI as a real queryable tree, so property names/values
come back as exact structured text, not pixels to interpret. Dot-source it:
```
. .\dev-gammaray-inspect.ps1
Connect-GammaRay
Select-GammaRayTool -Name Objects
Select-GammaRayTreeNode -Name TreeModel
Get-GammaRayProperties | Format-Table -AutoSize
```
Key functions: `Get-GammaRayToolList`/`Select-GammaRayTool` (sidebar tabs —
Objects, Quick Scenes, Meta Objects, ...), `Get-GammaRayTree`/
`Select-GammaRayTreeNode` (the object/QML tree), `Get-GammaRayProperties`
(the property table for whatever's selected), `Save-GammaRayScreenshot`
(fallback for views with no UIA tree behind them, like the Quick Scenes live
preview or Qt3D Inspector's 3D view).

Gotchas documented in the script's own header — worth knowing before
debugging a "wrong" result rather than the tool:
- The object/QML tree is virtualized like this project's own `TreeModel`
  (see below) — only rows currently scrolled into view/expanded are visible.
- `Get-GammaRayProperties` matches cells to columns by X position, not by
  counting cells per row: Qt's accessibility layer creates no element at all
  for an empty cell, so a row can have 3 visible cells instead of 4.
- A blank `Class` column is expected, not a parsing bug — GammaRay only
  shows the declaring class when it differs from the selected object's own
  runtime type.
- Querying the UI right after selecting a node can throw a transient
  `UnknownError` COMException while the property table repopulates; retry
  after a short `Start-Sleep` rather than treating it as fatal.
- Editing a property value live has not been tested/implemented — treat this
  as read/navigate only until that's proven out.

## Unit testing (qmlui/test) - for TDD work

`qmlui/test/<name>/` holds small, GUI-free QTest suites (see `treeflatmodel/`
and `treemodel/` for the pattern: a `<name>_test.h`/`.cpp` pair, a
`CMakeLists.txt` that either compiles the handful of `.cpp` files under test
directly - `add_executable(foo_test WIN32 foo_test.cpp ../../foo.cpp ...)` -
or links the `qlcplusengine` target for engine/`FixtureUtils`-level code, plus
a trivial `test.sh` that just runs the built binary). Register a new suite
with `add_subdirectory(<name>)` in `qmlui/test/CMakeLists.txt`. Unlike
`engine/test` and `controlapi/test`, **these are not wired into CTest** -
`add_test()` is never called for them; run the binary directly.

Lessons learned so far:

- **What's actually unit-testable here**: pure/static logic with no `Doc*` or
  `QQuickView*` in its call chain - `TreeModel`/`TreeModelItem`/`TreeFlatModel`
  and `FixtureUtils`'s static methods are the reliable examples, plus anything
  built only on a bare `MonitorProperties` (default-constructible). `ContextManager`
  and `FixtureManager`, by contrast, both require a live `QQuickView*` and a real
  `Doc*` just to construct (`ContextManager` also builds real `MainView2D`/
  `MainView3D`/`MainViewDMX` instances, pulling in Qt3D) - there's no lightweight
  fake for that today, so testing fixture-selection/arrange/drag-position logic
  at that layer needs new test infrastructure (an offscreen `QQuickView` + minimal
  `Doc` + stub view backends) as a deliberate separate task, not something to
  improvise while adding a couple of test cases.
- If a lightweight test target suddenly needs a much heavier dependency than
  what it's actually testing (e.g. compiling one file pulls in Qt Quick or the
  engine just to call one unrelated method on another class), treat that as a
  sign of an avoidable coupling in the production code, worth fixing at the
  source - not something to paper over with more libraries in the test's
  `CMakeLists.txt`.
- `TreeModel::addItem(label, data, path, flags)`'s 3rd argument is the *parent
  path*, not a sibling marker - `addItem("Fixture B", ..., "Group")` nests it
  *inside* a folder called "Group" rather than adding a top-level sibling.
  Double-check `tree.index(N)` actually refers to the node you think it does.
- Running a built `_test.exe` from an MSYS2 bash shell: these targets use
  `add_executable(... WIN32 ...)` (no console subsystem), so a failing run can
  produce zero visible stdout with no clue why - pass `-o result.txt,txt` to get
  QTest's real output. `C:\msys64\mingw64\bin` must also be on `PATH` when
  *running* the exe, not just building it, or it fails to even load with a
  misleading exit code.

## Unit testing (engine/test) - resource paths

`engine/test/common/resource_paths.h`'s `INTERNAL_SCRIPTDIR`/`INTERNAL_FIXTUREDIR`/
`INTERNAL_PROFILEDIR` are relative paths (`"../../../resources/rgbscripts/"` etc.),
resolved against the test binary's working directory at runtime - `engine/test/
CMakeLists.txt` sets that to each test's own build output directory
(`WORKING_DIRECTORY $<TARGET_FILE_DIR:...>`), so they resolve to
`<build>/resources/rgbscripts/` etc., not the source tree. Nothing copied those
resources into the build tree for a normal desktop build (only `if(ANDROID)`
branches in `resources/{rgbscripts,fixtures,inputprofiles}/CMakeLists.txt` did
any copying) - `resources/rgbscripts/CMakeLists.txt` now also does an
unconditional `copy_directory` into the build tree, refreshed via a proper
`add_custom_command`/`DEPENDS` whenever a script or that directory's own
`CMakeLists.txt` changes (the latter matters: `RGBScript_Test::scripts()` reads
that file back as a "is this script registered" check, and `QFile::open
(QIODevice::ReadWrite)` on a missing file silently creates an empty one instead
of erroring, so a partial copy missing it fails that check in a confusing way).
Before this fix, a fresh build had no scripts there at all, and if a build
directory happened to acquire a copy some other way, it never got refreshed -
this bit two different overnight test-writing sessions the same way before
being tracked down. `resources/fixtures/`/`resources/inputprofiles/` likely have
the same latent gap (same desktop-skips-the-copy pattern) but weren't touched -
a deliberate follow-up, not verified broken.

**Update - did not reproduce (checked while wiring engine/controlapi tests into
CI, 2026-08-30)**: re-tested this exact scenario - fresh configure/build,
`C:\msys64\mingw64\bin` plus `build/engine/src` and `build/engine/audio` on
`PATH`, `ctest -R rgbscript_test` run from `build/`. This time `ctest` found
`qlcplusengine.dll` fine: it ran the binary and reported `***Failed`, and
re-running the same binary directly with QTestLib's `-o result.txt,txt`
confirmed it's a real, pre-existing test failure (`RGBScript_Test::runScripts()`
- the "3D Starfield" script name fails a `scriptName.toLower() == scriptName`
assumption), not a launch failure - `ctest`'s exit code agreed with the direct
run in both cases. Repeating the full suite (`ctest -j1 --output-on-failure`)
gave 55/62 passing with the rest failing for equally real reasons (e.g.
`doc_test`'s `normalizeComponentPath()` assumes a `/home/user/...`-style Unix
path). `ctest`'s own per-test captured log is still empty even on failure
(`--output-on-failure` prints nothing) - that matches this project's already-
documented WIN32-subsystem-binary behavior (see the `qmlui/test` section above:
these binaries print nothing to stdout without QTestLib's own `-o` flag), which
is what the original note's `<end of output>` was most likely showing, not a
missing-DLL launch failure. Best guess for why the original attempt looked like
a launch failure: unclear, possibly a stale build or a differently-scoped PATH
at the time - not reproducible now. Conclusion: bare `ctest`, run from `build/`
with `build/engine/src` and `build/engine/audio` (not just
`C:\msys64\mingw64\bin`) on `PATH`, is reliable for engine/test and
controlapi/test on this checkout; `dev-test-run.ps1` and
`.github/workflows/build.yml`'s Windows `Test` step both rely on this.

## Git workflow for this project

**Commit before every build**, even for a fix you're not certain worked yet —
this is a standing instruction, not per-request. One commit per fix; never
amend. Strip debug-only logging before committing the real fix (see the
`bugfix-workflow` skill) — never commit instrumentation and the fix together.

## Architecture notes relevant to bug hunting

- `qmlui/treemodel.{h,cpp}` / `treemodelitem.{h,cpp}`: the generic recursive
  tree model (`TreeModel`) backing every tree-style list in the UI (Fixture
  Groups, Function Manager, etc. — anything using `TreeNodeDelegate.qml`).
  Each item's children are themselves a nested `TreeModel*` (role
  `ChildrenModel` / `childrenModel`). `TreeModel::roleChanged` bubbles up from
  every descendant tree to the root tree whenever any role changes anywhere
  (wired in `TreeModel::addItem`), which is useful for reacting to any
  expand/collapse/selection change anywhere in a (sub)tree without walking it
  yourself.
- Tree QML delegates (`TreeNodeDelegate.qml`, and the top-level `ListView`
  delegates in files like `FixtureGroupManager.qml`) only virtualize at
  whatever level the outer `ListView`/`Repeater` operates on — nested children
  render via a plain `Repeater` inside a `Column`, which is **not**
  virtualized. A tree node's rendered height can therefore range from one row
  (collapsed) to thousands of pixels (deeply expanded). Don't trust
  `ListView.contentHeight`'s built-in estimate for these views; if you need an
  exact content height, use/extend `TreeModel::visibleRowCount()`.
