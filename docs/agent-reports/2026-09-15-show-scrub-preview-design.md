# Show Manager: scrub preview (state at the playhead while stopped) - design

Read-only investigation, 2026-09-15. Paths under the repo root. Builds on `docs/agent-reports/2026-09-15-show-live-edit-reschedule-design.md` (snapshot runner, now implemented in `engine/src/showrunner.{h,cpp}`, `showschedule.h`, `show.cpp`).

User decisions: scrubbing = fixtures (2D/3D + real DMX) and video follow the playhead while the show is stopped; Audio stays silent while scrubbing.

## 1. Playhead today

| Concern | Where |
|---|---|
| `ShowManager::setCurrentTime`: stores, flags `m_cursorMovedDuringPause` if the Show is paused, emits; no engine effect while stopped | `qmlui/showmanager.cpp:379-389` |
| Cursor set only by **clicks**: header `MouseArea.onClicked` -> `clicked()` -> `showManager.currentTime = posToMs(...)`; items area click likewise. No drag, no keyboard nudge exists | `qmlui/qml/showmanager/HeaderAndCursor.qml:242-247`, `ShowManager.qml:500-507, 593-604` |
| Cursor drawn from `currentTime` (imperative `cursor.x`) | `HeaderAndCursor.qml:102-120` |
| While playing the runner drives it: `Show::timeChanged` -> `slotTimeChanged` -> `m_currentTime` | `showmanager.cpp:1669-1673`, `showrunner.cpp:384-385` |
| `playShow`: stopped -> `rebuildSchedule()` + `Show::start(timer, ShowManagerPlayback, m_currentTime)` (**play starts at the cursor**); paused + cursor moved -> stop, `stopAndWait`, restart at cursor; paused -> `setPause(false)`; playing -> `setPause(true)` | `showmanager.cpp:1448-1485` |
| `stopShow`: running -> `Show::stop`; stopped -> **rewinds cursor to 0** (Home/second press) | `showmanager.cpp:1487-1504`; shortcuts Space/Home `qmlui/app.cpp:622-630` |
| Show start offset -> `Function::start` sets `m_elapsed` -> `Show::preRun` builds runner with `elapsed()` | `engine/src/function.cpp:1165`, `show.cpp:641-658` |
| `resetContents` (document close) nulls `m_currentShow` without stopping it; `setCurrentShowID` swaps without stopping | `showmanager.cpp:1359-1385, 97-135` |

## 2. Engine: state at time T per Function type

Runner: `startClip` computes `offset = now - clip.start` and calls `f->start(timer, parent, offset)` (`showrunner.cpp:171-193`); `runStartPass` starts every clip active at `now` and retries next tick if the Function is still finishing a stop (`227-255`); `isOffsetSensitive` = Audio/Video/Chaser/Sequence (`112-128`).

| Type | Start at offset | Paused |
|---|---|---|
| Scene | ignores elapsed; builds faders with `fadeIn` = `overrideFadeInSpeed()` if set (`scene.cpp:863-887`) | `Scene::setPause` pauses faders (`scene.cpp:914-925`); paused fader skips `nextStep` but keeps writing `fc.current()` (`genericfader.cpp:260-263`) -> **holds** whatever value the fade had reached; `FadeChannel` starts at `m_start` (`fadechannel.cpp:392-394`) |
| Chaser/Sequence | `ChaserRunner` ctor seeks to the step containing `startTime`, `m_startOffset` into it (`chaserrunner.cpp:55-75`, applied `750-776`, step elapsed pre-set `562-568`); step fade-in uses the chaser's override if set, every step (`122-131`) | pause request applied on next write -> pauses step functions + faders (`chaser.cpp:643-666`, `chaserrunner.cpp:724-743`) -> holds the current step |
| RGBMatrix | ignores offset (step handler restarts, `rgbmatrix.cpp:663-709`) | no `setPause` override; `write` skips map update and elapsed (`735-754`) -> faders hold the last frame |
| EFX | ignores offset | `write` returns early (`efx.cpp:1177-1178`) -> fader holds position |
| Collection | children started at 0 (`collection.cpp:317`) | pauses children (`324-335`) |
| Script | ignores offset | no new commands (`script.cpp:362`); already-started children keep running |
| Audio | decoder `seek(elapsed())`, renderer thread started at once (`engine/audio/src/audio.cpp:452-491`) | `suspend()` the sink (`493-507`) - only after it already played |
| Video | `preRun` emits `requestPlayback` (`video.cpp:633-640`); GUI `addVideo(..., elapsed())` / Spout `start(..., elapsed())` (`qmlui/videoprovider.cpp:372-377, 416`); position applied on `LoadedMedia` (`VideoContext.qml:321-331`, `spoutvideoplayer.cpp:219-231`) | `requestPause` -> `player.pause()` / `m_player->pause()` (`VideoContext.qml:244-247`, `spoutvideoplayer.cpp:161-170`); queued in order after `requestPlayback` |

**Why "start at cursor, immediately pause" fails as stated.** `Function::setPause(true)` is a no-op before `preRun` (`function.cpp:1178-1179`), and `preRun` happens only when the start queue drains at the end of a tick (`mastertimer.cpp:288-311`); faders run on the Universe thread (`universe.cpp:391`). A Scene paused before its first fader step holds 0 forever; paused one tick later with fade-in > 0 it freezes partway. Hence: (a) the runner itself must issue the pause, on the timer thread, >= 2 ticks after each clip started; (b) scrub clips must start with `overrideFadeIn = 0` (`Function::start` param; honoured by Scene and Chaser steps as above) so the snap lands first. And the existing pause path *defers* starts (`show.cpp:667-681`, `m_startPassPending`) whereas preview needs starts while frozen - so preview is a distinct runner mode, not `Function::setPause` on the Show.

## 3. Proposed design: a frozen runner + seek requests

Engine (all runner mutation stays on the tick; GUI only posts):

1. `Show`: `void setScrubMode(bool)`, `void requestSeek(quint32 ms)`, backed by `QAtomicInteger` members (the runner is created on the timer thread in `Show::preRun`, `show.cpp:641-658`, so the GUI cannot address it directly - same reason `adjustAttribute`'s direct `m_runner` access at `703-717` is a race). Seek = store value + set flag; rapid drags coalesce into one seek per 50 ms tick for free.
2. `ShowRunner::write` (`showrunner.cpp:328-386`) reads both at the top, after `applyPendingSchedule()` (so mute/edits still apply while frozen, via `buildSchedule` skipping muted tracks `show.cpp:602-603`). While frozen: skip the beat wait (`339-346`), skip Phase 3 (`376-382`: a cursor past the end must show "nothing", not end the show), do **not** advance `m_elapsedTime` or emit `timeChanged`.
3. `startClip` in scrub mode: skip `AudioType` (do not queue it; `runStartPass` picks it up on unfreeze because `runningIndex == -1`); pass `overrideFadeIn = 0`; record `rc.startedAt = tickCounter`.
4. Freeze pass each frozen tick: for every running clip with `f->isRunning() && !f->isPaused()` and `tickCounter - rc.startedAt >= 2` -> `f->setPause(true)`.
5. `seek(ms)`: `m_elapsedTime = ms`; same loop as `reconcile` step 1 (`257-292`): stop clips not `isActiveAt(now)`; restart `isOffsetSensitive` clips (stop now, the start pass re-adds next tick via the `247-251` retry); keep the rest (Scene/RGB/EFX approximated as "held"); `m_currentClipIndex = indexAfter(clips, now)`; `m_startPassPending = true`.
6. Unfreeze: for each running clip `setPause(false)` and `f->setOverrideFadeInSpeed(Function::defaultSpeed())` (`function.h:517`) - otherwise a Chaser keeps stepping with no fade after play (`chaserrunner.cpp:125-131` reads it every step); `m_startPassPending = true` (starts the skipped Audio at its offset); playback continues seamlessly.
7. Video (increment 2): add `Video::seekTo(quint32 ms)` (sets `m_elapsed`, emits `requestSeek`) -> `VideoContent::seekContent` -> `player.position = ms` / `m_player->setPosition(ms)`. The runner uses it instead of restart for Video while frozen: a restart per seek would run `stopContent -> cleanupItem -> destroyContext` and recreate the window/sender at up to 20 Hz (`videoprovider.cpp:272-289, 473-489`, `VideoContext.qml:85-99`). **Verify in the app** that a paused `MediaPlayer` renders the seeked frame in both the QML `VideoOutput` and the `QVideoSink` path (`spoutvideoplayer.cpp:210-217`); fallback: play, then pause on the first frame/position change after the seek.

qmlui:

- `ShowManager`: property `previewEnabled` (toolbar toggle, default on) and read-only `isPreviewing`. `setCurrentTime`: if `previewEnabled` and the Show is not running -> `rebuildSchedule()`, `setScrubMode(true)`, `Show::start(timer, ShowManagerPlayback, t)`; if previewing -> `requestSeek(t)`. Keep the real-pause `m_cursorMovedDuringPause` flow unchanged (candidate for later replacement by seek).
- `playShow`: previewing -> `setScrubMode(false)`, `setPlaybackState(true,false)` (today's code would fall through to `setPause(true)` because the Show is running and not paused, `showmanager.cpp:1483`). `stopShow`: previewing -> `Show::stop` (children stop -> Scene fade-out `scene.cpp:826-847`, Video `requestStop` blank); cursor stays. Buttons: play icon, stop button neutral (not red) while previewing; `timeBox` precision as stopped (`ShowManager.qml:303-338`).
- Leave/close: no hook exists today - `PreviewContext::enableContext` is virtual (`previewcontext.h:66`) but `ShowManager` does not override it, and `MainView.qml` acts only on `checked === true` (`346-350`); `ContextManager::enableContext(name,false)` is only reached from `reattachContext` (`contextmanager.cpp:206-224, 268-273`). Add: `MainView.switchToContext` calls `contextManager.enableContext(previousCtx, false, null)`; `ShowManager::enableContext(false)` stops the preview. Also stop it in `resetContents`, in `setCurrentShowID` before the swap, and clear `isPreviewing` in `slotShowStopped` (covers `App::stopAllFunctions`, `app.cpp:1167-1178`).
- Cursor drag (new): `onPressed/onPositionChanged` on the header MouseArea writing `currentTime`; the engine coalesces.

## 4. Side effects

- **Audio silent**: skipped entirely in scrub mode (renderer would burst before `suspend()` arrives).
- **Real DMX output**: yes, like Function Manager preview which starts Functions for real (`qmlui/functionmanager.cpp:511-545`); a later "preview to output" toggle would need a Universe-level mask, out of scope.
- **Shared Functions**: a Master-type stop clears all sources (`function.cpp:1190-1195`), so a Function Manager preview and a scrub of the same Function kill each other - pre-existing semantics.
- **Doc modified**: nothing in start/stop/`rebuildSchedule`/`setRuntimeSenderName` (`video.cpp:374-377`) calls `setModified`; only `Function::changed` does (`doc.cpp:1316-1320`). Keep it that way.
- Fade-out override deliberately not set (it persists to the clip's natural end); accept a brief fade-out tail when a seek stops a Scene.

## 5. Tests (`engine/test/showrunner`, helpers `LiveShow`/`advanceTo`/`queueHas`, `showrunner_test.cpp:45-156`)

`scrubStartsAndFreezes` (frozen at 12000: two writes -> clip queued, `scene->isPaused()`, `m_elapsedTime` unchanged, `overrideFadeInSpeed()==0`); `scrubSeekStopsAndStarts`; `scrubSeekRestartsChaserAtOffset` (`elapsed()` == new offset); `scrubSkipsAudioUntilUnfreeze` (Audio without source: `audio.cpp:454` guard); `scrubPastEndNoShowFinished`; `scrubHonoursMute`; `unfreezeResumesAndResetsFadeIn`. App-only: video frame rendering, DMX on the rig, context-leave cleanup, drag feel.

## 6. Effort, risks, order

Effort: engine ~200 lines + Video seek ~60, qmlui ~100, QML ~40, tests ~200: 2-3 days plus manual checks on `SF3.qxw`.

Risks: paused-player frame rendering (verify); 100 ms until values freeze (visible as a brief run of Chasers/matrices - acceptable); Scene at T < its fade-in shows full values (approximation); Video/Chaser restart glitch on fast seeks; `Show::adjustAttribute` GUI-thread race pre-exists.

Increments: **1** engine frozen mode + seek + Audio skip + fade-in override/reset + ShowManager auto-preview on click, play/stop/leave handling, tests. **2** `Video::seekTo` in-place seek, frame-while-paused verification (window + Spout). **3** toolbar toggle, cursor drag, button styling, optional replacement of the pause-and-move restart path by seek.

### Critical files for implementation
- `engine/src/showrunner.cpp`
- `engine/src/show.cpp`
- `qmlui/showmanager.cpp`
- `qmlui/videoprovider.cpp`
- `engine/test/showrunner/showrunner_test.cpp`
