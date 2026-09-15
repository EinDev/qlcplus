# Spout video output for QLC+ 5 (qmlui) — design report

Date: 2026-09-15. Scope per user decisions: Spout is a **new output mode of the Video function** (next to Windowed / Fullscreen), per-Video resolution, no on-screen window in Spout mode, opaque frames while playing and a fully transparent frame while idle, **sender name = "QLC+ <Show Manager track name>"** when run from a Show, fallback "QLC+ <video name>".

## 1. How Video playback works today

- **Engine** (`engine/src/video.{h,cpp}`): `Video` is a `Function` with `m_screen`, `m_fullscreen`, `m_customGeometry`, `m_rotation`, `m_zIndex` (video.h:172-193). Lifecycle hooks: `Video::preRun` emits `requestPlayback()` (video.cpp:735-739), `setPause` emits `requestPause` (741-748), `postRun` emits `requestStop()` (758-762). `write()` only increments elapsed (750-756). XML: screen/fullscreen/geometry/rotation/zIndex are attributes on `<Source>` (video.cpp:604-623 save, 656-706 load). `stopFromUI()` (578-582) stops via `FunctionParent::VideoWindowClosed`.
- **GUI bridge** (`qmlui/videoprovider.{h,cpp}`): `VideoProvider` is created per document at `qmlui/app.cpp:1120` and `:1407`, deleted in `clearDocument` (:1136-1139), `shutdown()` on stop-all (:1173). It builds one `VideoContent` per Video function (videoprovider.cpp:195-210) and connects the three request signals (207-209; queued, since `preRun/postRun` run on the MasterTimer thread).
- **Window**: `VideoContent::playContent()` (videoprovider.cpp:294-387) picks `QGuiApplication::screens()[m_video->screen()]` (301-303), creates a `QQuickView` loading `qrc:/VideoContext.qml` (318), positions it on that screen (321-334), then invokes QML `addVideo`/`addPicture` (354-368) and `show()`/`showFullScreen()` (372-386). Windowed videos get **one window each**; fullscreen videos share one `m_fullscreenContext` (298-299, 374). Stop → `removeContent` (403-414) → QML fade-out → `cleanupItem` → `videoContent.destroyContext()` (VideoContext.qml:97-98 → videoprovider.cpp:276-292) closes the window.
- **Frames**: `VideoContext.qml` uses `MediaPlayer { videoOutput: pVideoOutput }` + `VideoOutput` (VideoContext.qml:310-351). No `QVideoSink`/frame callback exists in C++ today; fades/intensity are QML `opacity` (VideoContext.qml:180, 194-196), EndOfMedia → loop or `stopFromUI()` (333-343). Resolution is probed with a temporary `QMediaPlayer` (videoprovider.cpp:436-446, 554-569).
- **Show Manager**: `ShowRunner::write` (engine/src/showrunner.cpp:184-224 time-based, 228-269 beat-based) finds the owning Track for each `ShowFunction` (207-216) to apply the per-track intensity override, then calls `f->start(timer, functionParent(), offset)` (218/263) with `FunctionParent(Function, showId)` (139-142) — **no track/ShowFunction context reaches the Video**.
- Backend: MSYS2 Qt 6.11 ships `plugins/multimedia/ffmpegmediaplugin.dll` (FFmpeg backend, confirmed). Renderer forced to `OpenGLRhi` at `qmlui/main.cpp:84`.

## 2. Producing a Spout sender with alpha — options (ranked)

| # | Path | Robustness | Effort | Notes |
|---|---|---|---|---|
| **A (recommended)** | CPU: C++ `QMediaPlayer` + `QVideoSink` (no QML, no window) → `QVideoFrame::toImage()` → paint into `QImage::Format_ARGB32_Premultiplied` canvas of the sender size → `spoutDX::SendImage` | High: no GL context, no render thread; SpoutDX owns its own D3D11 device | Medium | `SendImage` uploads the buffer as-is via `UpdateSubresource` into a `DXGI_FORMAT_B8G8R8A8_UNORM` texture — `ARGB32_Premultiplied` is BGRA in memory on Windows, so **no swizzle pass**. Cost at 1080p: hw-frame download (D3D11VA → CPU via `map()`) ~2-3 ms + NV12→RGB ~2-4 ms + draw/upload ~1-2 ms ≈ 6-9 ms/frame on the GUI thread: fine at 30 fps, borderline at 60 fps → move convert+send to a worker thread (QVideoFrame is ref-counted; SpoutDX is Qt-independent). Fallback: `QT_FFMPEG_DECODING_HW_DEVICE_TYPES=""` forces SW decode (no download step). |
| B | GPU: `QQuickRenderControl` rendering `VideoContext.qml` offscreen (transparent clear colour) into a GL texture → SpoutGL `SendTexture` (WGL_NV_DX_interop) | Medium: we own the context (no fight with Qt's render thread), but needs SpoutGL + interop + a manual render loop | High | Best fidelity (reuses fades/geometry/rotation/z-order/multi-item composition). Keep as upgrade if A's CPU cost bites (4K, 60 fps). Interim B': `QQuickRenderControl::grab()` + SpoutDX `SendImage` (glReadPixels ≈ 8 MB/frame). |
| C | Make the existing windowed `QQuickView` translucent (`setColor(Qt::transparent)`, VideoContext.qml:32 black → transparent) and use OBS Window Capture (WGC, "Allow Transparency") | Low | Trivial | Window is created on play and destroyed on stop (videoprovider.cpp:276-292), so OBS loses/re-finds it each clip (0.5-1 s black/blank), window must stay on screen, WGC latency. Useful only as a zero-code experiment. |

## 3. Spout SDK + MinGW feasibility

- Spout2 (leadedge/Spout2) is **BSD-2-Clause** — compatible with Apache-2.0; keep LICENSE + notice in the vendored dir.
- Upstream has explicit MinGW support: PR #93 (SPOUT_DLLEXP without `_MSC_VER`), PR #114 (MinGW fixes in SpoutUtils), PR #122 (merged 2025-08-14: mingw-w64 cross build, `_uuidof`→`__uuidof`, lowercase headers, forward slashes). `SpoutGL/CMakeLists.txt` has `if (MINGW AND NOT SPOUT_BUILD_ARM) target_compile_options(... -msse4)` (SpoutCopy uses SSE intrinsics). Maintainer verified builds with VS2022 and mingw-w64.
- We need only **SpoutDX** (DirectX 11; `SPOUT_BUILD_SPOUTDX=ON` upstream): `SpoutDX.cpp` + `SpoutCopy/SpoutDirectX/SpoutFrameCount/SpoutSenderNames/SpoutSharedMemory/SpoutUtils.cpp`. No OpenGL, no `SpoutGL.cpp`.
- MinGW64 already provides everything: `libd3d11.a`, `libdxgi.a`, `libd3d9.a`, `libopengl32.a`, `d3d11.h`, `d3d11_1.h`, `dxgi1_2.h`, `d3d9.h` (checked in `C:\msys64\mingw64\{lib,include}`). GCC ignores `#pragma comment(lib,...)`, so link explicitly: `d3d11 dxgi d3d9 shell32 advapi32 version comctl32 psapi ole32 user32 gdi32 winmm`.
- Vendoring shape (like FTDI in `plugins/dmxusb/src/CMakeLists.txt`): `qmlui/spout/` (it is not an IO plugin) containing the SDK subset + `CMakeLists.txt` with `add_library(spoutdx STATIC ...)`, `SPOUT_BUILD_STATIC`, `-msse4`, guarded by `if(WIN32)`; qmlui links it and defines `QLC_SPOUT` so Linux/macOS CI compile the mode out (UI still shows it disabled).
- Residual risk: `strcpy_s`/`sprintf_s` (mingw-w64 has them via `MINGW_HAS_SECURE_API`), `_MSC_VER >= 1900` blocks, `MessageBoxTimeoutA`. Resolve with a compile smoke test first (milestone 1).

## 4. Alpha semantics

- No alpha from content (decided). Qt FFmpeg backend only preserves alpha for packed RGBA/BGRA/ARGB/ABGR AV formats; YUVA is converted to YUV420P (alpha dropped) — so VP9-alpha WebM would not work anyway.
- Idle: canvas cleared to (0,0,0,0) and sent once at stop (after fade-out) and once at sender creation. Playing: alpha 255. Fade in/out and intensity: `QPainter::setOpacity(intensity × fade)` over a transparent canvas → **premultiplied** output; tell the user to select the OBS Spout source composite mode "Premultiplied alpha". Send with `HoldFps` off; send on every `videoFrameChanged` and on every fade tick.
- Keep senders alive (transparent) rather than `ReleaseSender()`: the OBS plugin's `win_spout_sender_has_changed()` de-inits/re-inits the source when a sender disappears or changes size, rendering nothing in between.

## 5. Where it lives / model changes

- **engine `Video`**: replace `bool m_fullscreen` semantics with `enum OutputMode { Windowed, Fullscreen, Spout }` (keep `fullscreen()` for VideoContext.qml:272 compat), add `QSize m_spoutSize` (0×0 = native `resolution()`), `QString m_runtimeSenderName`, `spoutSenderName()` = runtime name or `"QLC+ " + name()`. XML: new `Output="spout"` and `SpoutSize="w,h"` attributes on `<Source>` (video.cpp:604-623, 656-706).
- **Track → name**: in `ShowRunner::write` inside the existing track loop (showrunner.cpp:207-216 and 252-261), before `f->start()`: `if (Video *v = qobject_cast<Video*>(f)) v->setRuntimeSenderName("QLC+ " + track->name());` and pass it along as an argument of `requestPlayback(QString)` (video.cpp:737) so the GUI thread never reads a cross-thread member; clear it in `postRun`. Precedent: the same loop already sets `sf->setIntensityOverrideId`. Started from Function Manager/VC there is no track → fallback name.
- **Same Video on two Spout tracks at once**: `Function::start` ignores a second start with the same `FunctionParent(Function, showId)` (function.cpp:1146-1150) — only the first track's sender plays; document as limitation. Same-track overlap is prevented by `ShowManager::checkOverlapping` (qmlui/showmanager.h:425), but a fade-out tail can overlap the next clip.
- **Sender pool**: `VideoProvider` owns `QHash<QString, SpoutSender*>` (lifetime matches doc load/clear). Spout auto-renames duplicate sender names (`name_1`), which would silently break "one OBS source per track", so contents **claim/release** a shared per-name sender; the sender composites all active layers (≤2 during fade tails) on each update. Resize: if a claimant's size differs, the sender resizes; OBS re-inits the source (transform kept, base size changes) — recommend one size per track.
- **`VideoContent` Spout branch**: `playContent()` (videoprovider.cpp:294) → no `QQuickView`; new `qmlui/spoutvideoplayer.{h,cpp}` (QMediaPlayer + QAudioOutput + QVideoSink, fade animation, EndOfMedia loop/`stopFromUI` mirroring VideoContext.qml:321-343, picture branch via `QImage` for `isPicture()`), `pauseContent`/`stopContent` (389-414), volume/intensity (472-479, video.cpp:562). `shutdown()` (159-175) releases senders.
- **Editor**: `VideoEditor.qml` "Output mode" row (lines 249-280) gets a third `CustomCheckBox` "Spout" in `outputModeGroup`; new "Spout size" spin boxes; screen combo disabled in Spout mode. `videoeditor.{h,cpp}` properties + Tardis actions `VideoSetOutputMode`, `VideoSetSpoutSize` next to `VideoSetFullscreen` (qmlui/tardis/tardis.h:253-254, tardis.cpp:1338-1349).

## 6. Effort, risks, first milestone

- **Sessions (~4-5)**: (1) vendor SpoutDX + CMake + MinGW compile + debug hook sending a transparent then a solid frame → visible in OBS; (2) engine model, XML, runner name plumbing, editor UI + Tardis; (3) `SpoutVideoPlayer` path (frames, idle, loop, pause, volume, fades, pictures); (4) sender pool/compositing, resize policy, perf measurement, worker thread if needed, docs.
- **Risks**: Spout+MinGW compile (medium, mitigated upstream); frame-copy CPU at 1080p60 (medium — measure, worker thread, GPU path B as escape hatch); teardown races (existing code already fights them, videoprovider.cpp:571-590); OBS source re-init on size change.
- **Milestone 1**: `qmlui/spout/` builds under MinGW; a temporary Video-independent hook (e.g. on app start) creates sender "QLC+ test" 1280×720, sends a transparent frame, then a red frame — OBS shows nothing, then red. Proves toolchain, linking, Spout↔OBS, alpha handling before touching the engine.

### Critical files for implementation
- `qmlui/videoprovider.cpp`
- `engine/src/video.cpp`
- `engine/src/showrunner.cpp`
- `qmlui/qml/fixturesfunctions/VideoEditor.qml`
- `qmlui/CMakeLists.txt` (+ new `qmlui/spout/CMakeLists.txt`)

## Summary

Recommended approach: add "Spout" as a third output mode of the Video function and implement it as a pure-C++ CPU path — `QMediaPlayer` + `QVideoSink` (no QQuickView at all), frames converted with `QVideoFrame::toImage()` and painted into an `ARGB32_Premultiplied` canvas of the configured sender size, pushed with vendored **SpoutDX** (`SendImage`, D3D11, own device, BGRA as-is). This avoids Qt's OpenGL render thread entirely; the QQuickRenderControl/SpoutGL GPU path stays as a later upgrade if 1080p60 CPU cost (~6-9 ms/frame) proves too high. Senders are pooled per name in `VideoProvider`, kept alive with a transparent frame while idle (OBS re-inits on sender loss/resize), and shared by clips on the same track. `ShowRunner::write` already resolves the owning Track per ShowFunction (showrunner.cpp:207-216); it passes "QLC+ <track name>" to the Video before `start()`, carried through `requestPlayback(QString)`; outside a Show the name falls back to "QLC+ <video name>". Spout2 is BSD-2 and builds with MinGW (upstream PRs #93/#114/#122, `-msse4`); MinGW64 has d3d11/dxgi libs. Milestone 1: vendored SpoutDX compiles and a debug hook shows transparent-then-red in OBS.

Open questions for the user: (1) Create senders eagerly at document load for every Spout-mode video's track (so OBS can pick them before anything plays) or lazily on first play? (2) Should rotation/position/scale attributes apply in Spout mode, or be ignored (YAGNI)? (3) Audio in Spout mode: keep playing locally through QLC+'s audio output? (4) Accept the limitation that one Video function cannot play on two tracks simultaneously? (5) Default sender size: native video resolution, or fixed 1920×1080 per track?

## Decisions (user, 2026-09-15)

1. Senders are created at document load for every Spout-mode Video (OBS can pick them before anything plays).
2. Rotation/position/scale attributes are ignored in Spout mode.
3. Audio keeps playing locally through QLC+'s audio output in Spout mode.
4. Accepted limitation: one Video function cannot play on two tracks at the same time.
5. Default sender size: the video's native resolution.
