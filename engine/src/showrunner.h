/*
  Q Light Controller
  showrunner.h

  Copyright (c) Massimo Callegari

  Licensed under the Apache License, Version 2.0 (the "License");
  you may not use this file except in compliance with the License.
  You may obtain a copy of the License at

      http://www.apache.org/licenses/LICENSE-2.0.txt

  Unless required by applicable law or agreed to in writing, software
  distributed under the License is distributed on an "AS IS" BASIS,
  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
  See the License for the specific language governing permissions and
  limitations under the License.
*/

#ifndef SHOWRUNNER_H
#define SHOWRUNNER_H

#include <QSharedPointer>
#include <QObject>
#include <QVector>
#include <QMutex>
#include <QMap>

#include <function.h>
#include "showschedule.h"

class Function;
class Universe;
class Track;
class Show;
class Doc;

/** @addtogroup engine_functions Functions
 * @{
 */

/**
 * Plays a Show's timeline on the MasterTimer thread.
 *
 * The runner never reads the Show's live Track/ShowFunction objects: it works
 * on an immutable ShowSchedule snapshot obtained from the Show when created,
 * and picks up a rebuilt snapshot (Show::takePendingSchedule) at the top of
 * every tick, reconciling whatever is currently playing against it. That is
 * what lets timeline edits made while the Show plays take effect at the
 * playhead without racing the GUI thread.
 *
 * Scrub mode (Show::setScrubMode / Show::requestSeek, read once per tick)
 * freezes the runner: the playhead only moves by seek, clips under it are
 * started without fade-in and held paused, Audio is skipped, and the Show
 * never ends. Leaving scrub mode resumes normal playback from there.
 */
class ShowRunner final : public QObject
{
    Q_OBJECT

public:
    ShowRunner(const Doc *doc, quint32 showID, quint32 startTime = 0);
    ~ShowRunner();

    /** Start the runner */
    void start();

    /** If running, pauses the runner and all the current running functions. */
    void setPause(bool enable);

    /** Stop the runner */
    void stop();

    /**
     * One tick. $universes are the universes claimed for this tick (as
     * handed to Function::write); the runner only reads their fader cycle
     * counters, to know when a clip started in scrub mode has actually
     * written its values and can be held. Without them (tests driving the
     * runner directly) the hold falls back to counting ticks.
     */
    void write(MasterTimer *timer, const QList<Universe *> &universes = QList<Universe *>());

    /**
     * Consume a schedule rebuilt by the Show since the last tick, if any, and
     * reconcile the running clips against it. Called by write() and, while the
     * Show is paused, by Show::write() so that stops still apply immediately
     * (starts are deferred to the first unpaused tick).
     */
    void applyPendingSchedule();

    /** A clip currently playing under this runner */
    struct RunningClip
    {
        /** ShowFunction ID the clip was started for */
        quint32 sfId;
        quint32 functionId;
        quint32 trackId;
        /** Timeline start the clip was started (or last updated) with */
        quint32 start;
        /** Timeline position at which the clip has to stop */
        quint32 stopTime;
        Function *function;
        /** Intensity attribute override ID on $function, or -1 */
        int overrideId;
        /** Value of m_tickCount when the clip was started (see holdClips) */
        quint32 startedAt;
        /** Universe::faderCycles() of each universe when it was started (see holdClips) */
        QVector<quint32> startCycles;
    };

private:
    /**
     * The playhead position clips are started and stopped against: the wall
     * clock (m_elapsedTime), in a Beats Show as much as in a Time Show - see
     * the definition for why not a beat-stepped clock.
     */
    quint32 now() const;

    /** Whether the timeline start matters to how the Function plays */
    static bool isOffsetSensitive(Function::Type type);

    /** Index of the running clip with the given ShowFunction ID, or -1 */
    int runningIndex(quint32 sfId) const;

    /** True when a running clip other than $sfId plays the same Function */
    bool isFunctionShared(const Function *function, quint32 sfId) const;

    /** Request the track intensity override and start $clip at $now */
    void startClip(const ScheduledClip &clip, quint32 now);

    /** Stop the running clip at $index (unless its Function is shared) */
    void stopClip(int index);

    /** Phase 1: start every not-yet-considered clip whose start time has come */
    void startDueClips();

    /** Start every clip active at the playhead that is not running yet */
    void runStartPass();

    /** Replace the schedule and adjust the running clips to it */
    void reconcile(const QSharedPointer<const ShowSchedule> &schedule);

    /** Move $index past every clip in $clips starting at or before $now */
    static int indexAfter(const QVector<ScheduledClip> &clips, quint32 now);

    /** Enter/leave the frozen (scrub) state, see the class comment */
    void setFrozen(bool frozen);

    /** Move the playhead to $ms and adjust the running clips to it */
    void seek(quint32 ms);

    /** Frozen: pause every running clip whose faders have written its state */
    void holdClips();

    /** True once $rc's faders have run at least twice since it was started */
    bool fadersHaveRun(const RunningClip &rc) const;

private:
    const Doc *m_doc;

    /** The reference of the show to play */
    Show* m_show;

    /** The timeline snapshot currently being played */
    QSharedPointer<const ShowSchedule> m_schedule;

    /** Index of the item in m_schedule->clips to be considered for playback */
    int m_currentClipIndex;

    /** Elapsed time since runner start. Used also to move the cursor in the track view */
    quint32 m_elapsedTime;

    /** True while a Beats Show holds its start for the first beat pulse
     *  (only ever set before the first tick, never re-armed by a live
     *  tempo switch; skipped when no beat source is active) */
    bool m_waitingForBeat;

    /** Total time the runner has to run */
    quint32 m_totalRunTime;

    /** List of the currently running clips and their stop time */
    QList<RunningClip> m_runningQueue;

    /** Set by reconcile(): clips active at the playhead may need starting */
    bool m_startPassPending;

    /** True while the Show is in scrub mode (see Show::setScrubMode) */
    bool m_frozen;

    /** Number of write() calls so far; clips record it when started */
    quint32 m_tickCount;

    /** The universes handed to the current write() */
    QList<Universe *> m_universes;

private:
    FunctionParent functionParent() const;

signals:
    void timeChanged(quint32 time);
    void showFinished();

    /************************************************************************
     * Intensity
     ************************************************************************/
public:
    /**
     * Adjust the intensity of show track
     */
    void adjustIntensity(qreal fraction, const Track *track);
    void adjustIntensity(qreal fraction, quint32 trackId);

private:
    QMap<quint32, qreal> m_intensityMap;

};

/** @} */

#endif
