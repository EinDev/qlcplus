# Show Manager: live rescheduling of a playing Show - design

Read-only investigation, 2026-09-15. All paths under `<repo>\`.

Bug: while a Show plays, timeline edits (resize/move/add/delete clip, mute track, undo/redo) never reach the running `ShowRunner`. Root cause: the runner is a one-shot snapshot taken at `Show::preRun` and never re-read.

## 1. How the runner works today

| Concern | Where |
|---|---|
| Snapshot of tracks/clips, skipping muted tracks and clips already ended before `startTime`; `m_totalRunTime` = max end | `engine/src/showrunner.cpp:54-85` |
| Split into `m_timeFunctions` / `m_beatFunctions` by `Function::tempoType()`, sorted by `startTime` | `showrunner.cpp:74-77, 87-88` |
| Phase 1: start every clip with `startTime <= elapsed`; mid-start offset = `elapsed - startTime` passed as `Function::start(timer, parent, offset)`; queue `(Function*, start+duration)`; index advances. **Does not check whether the clip's end has already passed** | `showrunner.cpp:184-224` (beat twin `229-269`) |
| Intensity: `requestAttributeOverride` with `m_intensityMap[trackId]`, found by scanning `m_show->tracks()` for the clip | `showrunner.cpp:207-215`, `adjustIntensity` `308-329` |
| Phase 2: stop when `currTime >= stopTime` | `showrunner.cpp:275-289` |
| Phase 3: show ends when `m_elapsedTime >= m_totalRunTime` | `showrunner.cpp:292-298` |
| Beat clock: reconcile must sit after the `beatSynced` early return | `showrunner.cpp:154-181` |
| Runner created in `Show::preRun` with `elapsed()`, per-track intensity seeded by attribute index; deleted in `postRun` | `engine/src/show.cpp:467-485, 504-513` |
| `Show::write` skips the runner while paused | `show.cpp:494-502` |
| Track mute only read in the ctor; `Track::setMute` emits `muteChanged` but nothing engine-side listens | `showrunner.cpp:61`, `engine/src/track.cpp:111-119` |
| `ShowFunction` is already a QObject with `startTimeChanged`/`durationChanged`/`functionIDChanged` signals (no listener) | `engine/src/showfunction.h:38-91`, `showfunction.cpp:65-86` |
| `ShowFunction::duration(doc)` falls back to `Function::totalDuration()` when 0 | `showfunction.cpp:93-106` |
| `Track::changed(quint32)` exists but is only emitted by `setName` | `track.h:58`, `track.cpp:84-88` |
| `Show::moveTrack` swaps track **ids** (breaks `m_intensityMap` keyed by id) | `show.cpp:315-319` |

Mid-start is honoured by the child functions: `Function::start` sets `m_elapsed = startTime` (`engine/src/function.cpp:1165`); Audio seeks the decoder to `elapsed()` (`engine/audio/src/audio.cpp:464`); Chaser builds its runner from `elapsed()` (`engine/src/chaser.cpp:636` -> `chaserrunner.cpp:55-68`); Video passes `elapsed()` to QML which sets `player.position` (`qmlui/videoprovider.cpp:258`, `qmlui/qml/fixturesfunctions/VideoContext.qml:326-330`). Scene ignores elapsed and just fades in (`engine/src/scene.cpp:863-887`) - exactly what the headline case (Scene clip extended past the playhead) needs.

Restart semantics already exist in the timer: stop+start of an already-listed function goes through the start queue and is handled as `postRun` then `preRun` in the same tick (`engine/src/mastertimer.cpp:294-307`); `postRun` resets attributes (`function.cpp:1059-1060`), so the intensity override must be re-requested on restart.

Pre-existing limitation: the same `Function` in two overlapping clips is broken regardless of this change - `Function::start` dedups by `FunctionParent` (`function.cpp:1148-1155`) and the runner uses one parent for all clips (`showrunner.cpp:139-142`), so the second start is a no-op and the first clip's end stops both (`function.cpp:1198-1205`).

## 2. qmlui operations that mutate a playing Show

None of these consult `isRunning()`/the runner - the only `isRunning` uses in `qmlui/showmanager.cpp` are `setCurrentShowID` (131), `playShow` (1451) and `stopShow` (1483).

- `checkAndMoveItem` 700-782: `setStartTime` 770, cross-track move 773-777.
- `setShowItemStartTime` 817-834, `setShowItemDuration` 836-853, `setShowItemDurationWithUndo` 969-976.
- `insertShowItemTime(At)` 1019-1108, `cutShowItemTime(At)` 1110-1265 (also edit Chaser steps), `insertTimeAtCursor` 1267-1325, `cutTimeAtCursor` 1327-1355, `moveAllItemsAfterCursor` 978-1017 (bursts of N setter calls).
- `addItems` 483-602 (also `pasteFromClipboard` 1831-1869): `createShowFunction` 555, `setDuration`/`setStartTime` 574-575.
- `deleteShowItems` 632-677: `removeShowFunction(sf, true)` **deletes the ShowFunction** 664.
- `deleteSelectedTrack` 445-477: `removeTrack` 470 **deletes the Track**. `moveTrack` 432-443. `setTrackSolo` 416-430.
- `convertLegacyBeatShow` 1941-1986.
- Bypassing ShowManager entirely: the mute button writes `trackRef.mute` straight into `Track::setMute` (`qmlui/qml/showmanager/TrackDelegate.qml:104`); Tardis undo/redo calls `Show::addTrack/removeTrack`, `Track::addShowFunction/removeShowFunction` and the `ShowFunction` setters directly (`qmlui/tardis/tardis.cpp:598-652, 1388-1408`), re-creating a deleted clip with its **original UID** (623-626). Tardis undo runs on the main thread (`tardis.h:71-79`).
- ShowItem resize handles commit only `onReleased` (`ShowItem.qml:789-790, 929`), not per frame; `stretchFunctions` additionally writes `funcRef.totalDuration` (795, 933), which matters for clips with duration 0 (fallback above).

Conclusion: notification must be model-level (Track/Show), not per UI call site.

## 3. Concurrency

`ShowRunner::write` runs on the MasterTimer thread: Win32 timer-queue callback `mastertimer-win32.cpp:39-46` -> `MasterTimer::timerTick` `mastertimer.cpp:110` -> `timerTickFunctions` `252` -> `Show::write` -> runner. All edits in section 2 run on the GUI thread. Existing locks: `MasterTimer::m_functionListMutex` (start queue only), `Function::m_sourcesMutex`/`m_stopMutex` (start/stop bookkeeping). Nothing guards `Show::m_tracks`, `Track::m_functions` or the `ShowFunction` fields.

Consequences today: `write()` dereferences `ShowFunction*` from its snapshot (`showrunner.cpp:189-190`) after `deleteShowItems` may have deleted it (use-after-free); it copies `m_show->tracks()`/`track->showFunctions()` (`207-209`) while the GUI thread appends/removes (QList copy during reallocation is UB); `Show::preRun` walks the same lists on the timer thread. A "dirty flag consumed on the next tick" cannot close these windows on its own - whichever thread rebuilds must not read live lists that the other thread mutates without a lock.

## 4. Proposed design

**Immutable schedule snapshot, built on the GUI thread, consumed on the timer thread.**

1. `engine/src/showschedule.h` (new, value types only):
   `struct ScheduledClip { quint32 sfId, functionId, trackId, start, end; Function::TempoType tempo; }` and `struct ShowSchedule { QVector<ScheduledClip> timeClips, beatClips /*sorted by start*/; quint32 totalRunTime; QVector<quint32> trackOrder; }`. Duration is resolved at build time (`duration(doc)` fallback), so the runner never touches `ShowFunction`/`Track` again.

2. Model notification (engine):
   - `Track`: on `createShowFunction`/`addShowFunction`/`loadXML` connect the clip's `startTimeChanged`, `durationChanged`, `functionIDChanged` to `Track::changed`; disconnect in `removeShowFunction`; emit `changed` from add/remove/`setMute`.
   - `Show`: connect `Track::changed` in `addTrack`; call the same slot from `removeTrack`/`moveTrack`. The slot sets `m_scheduleDirty` and, if not already queued, `QMetaObject::invokeMethod(this, &Show::rebuildSchedule, Qt::QueuedConnection)`. Bursts (undo walking 20 steps, `insertTimeAtCursor`) collapse into one rebuild per event-loop pass; per-edit cost is one bool store. Use a dedicated signal `Show::scheduleChanged()` - do not reuse `Function::changed`, which Doc listens to (`doc.cpp:1070`).
   - `Show::rebuildSchedule()` (public, synchronous, also usable by tests): builds `QSharedPointer<const ShowSchedule>` in O(n log n), stores it under `QMutex m_scheduleMutex` as `m_pendingSchedule`, emits `scheduleChanged()` (ShowManager uses it to refresh `showDurationChanged`, which today is only emitted by `addItems`/`refreshView`).
   - `Show::preRun` / the runner ctor consume `Show::currentSchedule()` (built on demand if none) instead of walking tracks - removes the preRun-side race too.

3. `ShowRunner` (timer thread):
   - Replace `QList<ShowFunction*>` with the snapshot vectors and `m_runningQueue` with `struct RunningClip { quint32 sfId, trackId; Function *f; quint32 stopTime, start; int overrideId; }` keyed by `sfId` (Tardis preserves UIDs, so undo-of-delete matches).
   - At the top of `write()`, after the beat-sync return (`179-180`) and before Phase 1: `if (auto s = m_show->takePendingSchedule()) reconcile(s)`.
   - `reconcile(now = m_elapsedTime / m_elapsedBeats per tempo)`:
     a. For each running clip, look up its `sfId` in the new schedule. Missing (deleted, track deleted/muted) or `now` outside `[start,end)` -> stop, drop. `start` or `functionId` changed -> restart (stop, then start at `now-start`, re-request intensity override). Only `end` changed -> update `stopTime` in place (no glitch).
     b. For each schedule clip with `start <= now < end` and no running entry -> start at offset `now-start` (the existing Phase 1 code path, factored into `startClip()`).
     c. Replace the sorted lists; set each index to `upper_bound(now)` - clips that already ended are skipped by index, since Phase 1 never checks `end`. Recompute `m_totalRunTime` (mandatory for the headline bug: extending the last clip otherwise trips Phase 3 and ends the show). Rebuild `m_intensityMap` from Show attributes by track index as `preRun` does (`show.cpp:478-480`), fixing `moveTrack` id swaps.
     d. Same-`Function` guard: if another running clip references the same `Function*`, do bookkeeping only, no `stop()`/`start()`.
   - Restart is safe from the timer thread: `Function::stop` then `start` -> `timer->startFunction` locks `m_functionListMutex`, which `timerTickFunctions` does not hold during `write()` (`mastertimer.cpp:242-253` vs `287`); the queue drains at `294-307`.

4. Pause: `Show::write` returns early while paused (`498-499`). Apply the swap and **stops** while paused (a deleted clip must release its faders), defer **starts** to the first unpaused tick - `Function::setPause(true)` on a not-yet-`preRun` function is a no-op (`function.cpp:1178-1179`), so a fresh start cannot be paused in the same tick. Keep a `m_startPassPending` flag.

5. Stopped playhead: nothing needed - no runner exists, `preRun` builds fresh from the current schedule.

6. Undo/redo: covered automatically because Tardis goes through the same model mutators. `ShowManager` needs no per-call-site changes; optional: connect `Show::scheduleChanged` -> `showDurationChanged`.

## 5. Tests

Existing: `engine/test/showrunner/showrunner_test.cpp` (appless, drives `runner.write(timer)` directly, `#define private public` for `m_runningQueue`/indices; `beatTempoUsesRealMilliseconds` 77-145 is the template). Registered in `engine/test/CMakeLists.txt:71,139`. Function-level assertions need `timer.timerTick()` to drain the start queue (pattern `engine/test/chaser/chaser_test.cpp:908-934`). Because the rebuild is queued, tests call `show->rebuildSchedule()` explicitly.

Proposed cases (Scene clip 0-10 s, runner started at 0, tick to 12 s):
- `extendEndPastPlayhead`: clip ended, queue empty; `setDuration(15000)` + rebuild + tick -> queue has the clip, `Function::elapsed() == 12000` after `timerTick()`, `m_totalRunTime == 15000`, show not finished.
- `shrinkEndBeforePlayhead`: running clip 0-20 s at 12 s; `setDuration(5000)` -> function stopped, queue empty.
- `moveStartEarlierUnderPlayhead`: clip 15-25 s at 12 s; `setStartTime(5000)` -> started with offset 7000; then `setStartTime(8000)` while running -> restarted, elapsed 4000.
- `endOnlyChangeDoesNotRestart`: running clip, `setDuration` larger -> same `preRun` count (spy on `Function::running`), `stopTime` updated.
- `deleteRunningClip` / `undoRestoresIt`: `removeShowFunction(sf, true)` -> stopped, no UAF (run under ASan in CI); re-add with same UID -> restarted at offset.
- `muteTrackStopsUnmuteResumes`, `deleteTrack`, `moveTrackKeepsIntensity`, `beatClipReschedule`, `sameFunctionTwoClipsGuard`, `pausedAppliesStopsDefersStarts`.

## 6. Effort and risks

Effort: engine ~350 lines (`showschedule.h`, Track/Show signal plumbing, runner rewrite of ~150 lines), qmlui ~15 lines, tests ~250 lines: 2-3 days including manual verification against `SF3.qxw`.

Risks: (1) restart of Audio recreates the renderer (`audio.cpp:458-487`) - a brief gap when a running audio clip's start is dragged; acceptable, same as the resume-after-cursor-move path (`showmanager.cpp:1461-1467`). (2) Scene restart re-runs fade-in; could special-case `SceneType` to never restart. (3) `Show::adjustAttribute` already touches `m_runner` from the GUI thread (`show.cpp:528-537`) - pre-existing race, out of scope but worth a note. (4) Beat-tempo clips are hard to test manually. (5) `Show::totalDuration()` (`show.cpp:64-78`) and the QML duration binding become live - UI-only behaviour change.
