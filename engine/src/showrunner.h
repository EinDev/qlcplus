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

    void write(MasterTimer *timer);

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

private:
    QMap<quint32, qreal> m_intensityMap;

};

/** @} */

#endif
