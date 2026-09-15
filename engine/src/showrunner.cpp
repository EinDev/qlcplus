/*
  Q Light Controller
  showrunner.cpp

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

#include <QMutex>
#include <QDebug>

#include <algorithm>

#include "showrunner.h"
#include "function.h"
#include "track.h"
#include "show.h"
#include "video.h"

#define TIMER_INTERVAL 50

ShowRunner::ShowRunner(const Doc* doc, quint32 showID, quint32 startTime)
    : QObject(NULL)
    , m_doc(doc)
    , m_currentClipIndex(0)
    , m_elapsedTime(startTime)
    , m_elapsedBeats(startTime)
    , beatSynced(false)
    , m_totalRunTime(0)
    , m_startPassPending(false)
{
    Q_ASSERT(m_doc != NULL);
    Q_ASSERT(showID != Show::invalidId());

    m_show = qobject_cast<Show*>(m_doc->function(showID));
    if (m_show == NULL)
    {
        m_schedule = QSharedPointer<const ShowSchedule>(new ShowSchedule);
        return;
    }

    m_schedule = m_show->currentSchedule();
    m_totalRunTime = m_schedule->totalRunTime;

    for (auto it = m_schedule->intensity.constBegin(); it != m_schedule->intensity.constEnd(); ++it)
        m_intensityMap[it.key()] = it.value();

#if 1
    qDebug() << "Ordered list of ShowFunctions (clock:"
             << (m_schedule->showTempo == Function::Beats ? "beats" : "time") << "):";
    foreach (const ScheduledClip &clip, m_schedule->clips)
        qDebug() << "[Show] Function ID:" << clip.functionId << "start time:" << clip.start << "end time:" << clip.end;
#endif
    m_runningQueue.clear();

    qDebug() << "ShowRunner created";
}

ShowRunner::~ShowRunner()
{
}

void ShowRunner::start()
{
    qDebug() << "ShowRunner started";
}

void ShowRunner::setPause(bool enable)
{
    for (int i = 0; i < m_runningQueue.count(); i++)
        m_runningQueue.at(i).function->setPause(enable);
}

void ShowRunner::stop()
{
    m_elapsedTime = 0;
    m_elapsedBeats = 0;
    m_currentClipIndex = 0;
    m_startPassPending = false;

    for (int i = 0; i < m_runningQueue.count(); i++)
        m_runningQueue.at(i).function->stop(functionParent());

    m_runningQueue.clear();
    qDebug() << "ShowRunner stopped";
}

FunctionParent ShowRunner::functionParent() const
{
    return FunctionParent(FunctionParent::Function, m_show->id());
}

quint32 ShowRunner::now() const
{
    return m_schedule->showTempo == Function::Beats ? m_elapsedBeats : m_elapsedTime;
}

bool ShowRunner::isOffsetSensitive(Function::Type type)
{
    // These honour the start offset handed to Function::start(): moving their
    // clip's start while they play changes what should be heard/seen now, so
    // they get restarted at the new offset. Scenes, matrices, EFX etc. only
    // (re)fade in and would just glitch.
    switch (type)
    {
        case Function::AudioType:
        case Function::VideoType:
        case Function::ChaserType:
        case Function::SequenceType:
            return true;
        default:
            return false;
    }
}

int ShowRunner::runningIndex(quint32 sfId) const
{
    for (int i = 0; i < m_runningQueue.count(); i++)
        if (m_runningQueue.at(i).sfId == sfId)
            return i;
    return -1;
}

bool ShowRunner::isFunctionShared(const Function *function, quint32 sfId) const
{
    for (int i = 0; i < m_runningQueue.count(); i++)
    {
        const RunningClip &rc = m_runningQueue.at(i);
        if (rc.function == function && rc.sfId != sfId)
            return true;
    }
    return false;
}

int ShowRunner::indexAfter(const QVector<ScheduledClip> &clips, quint32 now)
{
    auto it = std::upper_bound(clips.constBegin(), clips.constEnd(), now,
                               [](quint32 value, const ScheduledClip &clip) { return value < clip.start; });
    return int(it - clips.constBegin());
}

/**
 * A Video in Spout mode publishes under "QLC+ <track name>" when it is
 * played from a Show, so that one OBS/receiver source per track keeps
 * working whichever clip is playing on it. The name has to be set before
 * Function::start(), because Video::preRun() hands it to the GUI thread.
 */
static void applySpoutSenderName(Function *f, const Track *track)
{
    Video *video = qobject_cast<Video *>(f);
    if (video == nullptr || track == nullptr || video->outputMode() != Video::Spout)
        return;

    video->setRuntimeSenderName(Video::spoutSenderNameForTrack(track->name()));
}

void ShowRunner::startClip(const ScheduledClip &clip, quint32 now)
{
    Function *f = m_doc->function(clip.functionId);
    if (f == NULL)
        return;

    // this should happen only when a Show is not started from 0,
    // or when the clip was (re)scheduled under the playhead
    quint32 functionTimeOffset = now > clip.start ? now - clip.start : 0;

    RunningClip rc;
    rc.sfId = clip.sfId;
    rc.functionId = clip.functionId;
    rc.trackId = clip.trackId;
    rc.start = clip.start;
    rc.stopTime = clip.end;
    rc.function = f;
    rc.overrideId = f->requestAttributeOverride(Function::Intensity, m_intensityMap.value(clip.trackId, 1.0));

    applySpoutSenderName(f, m_show->track(clip.trackId));
    f->start(m_doc->masterTimer(), functionParent(), functionTimeOffset);
    m_runningQueue.append(rc);
}

void ShowRunner::stopClip(int index)
{
    RunningClip rc = m_runningQueue.takeAt(index);

    // Function::start() dedups by parent, so two overlapping clips of the same
    // Function share one run: leave it to the other clip.
    if (isFunctionShared(rc.function, rc.sfId) == false)
        rc.function->stop(functionParent());
}

void ShowRunner::startDueClips()
{
    const QVector<ScheduledClip> &clips = m_schedule->clips;
    quint32 now = this->now();

    // clips are ordered by start time, so when we find an entry with start
    // time greater than now, this phase is over
    while (m_currentClipIndex < clips.count())
    {
        const ScheduledClip &clip = clips.at(m_currentClipIndex);
        if (clip.start > now)
            break;

        // a clip that already ended (runner started past it, or it was
        // rescheduled behind the playhead) is skipped, not started
        if (clip.end > now && runningIndex(clip.sfId) == -1)
            startClip(clip, now);

        m_currentClipIndex++;
    }
}

void ShowRunner::runStartPass()
{
    m_startPassPending = false;
    quint32 now = this->now();

    foreach (const ScheduledClip &clip, m_schedule->clips)
    {
        if (clip.start > now)
            break;
        if (clip.isActiveAt(now) == false || runningIndex(clip.sfId) != -1)
            continue;

        Function *f = m_doc->function(clip.functionId);
        if (f == NULL)
            continue;

        // A stop requested earlier this tick (restart, or an edit that
        // moved the clip away and back) is completed by the MasterTimer
        // only after this write(): starting now would have that postRun
        // wipe the elapsed offset again. Retry on the next tick.
        if (f->isRunning() && f->stopped())
        {
            m_startPassPending = true;
            continue;
        }

        startClip(clip, now);
    }
}

void ShowRunner::reconcile(const QSharedPointer<const ShowSchedule> &schedule)
{
    const ShowSchedule &s = *schedule;

    // 0. The Show's tempo decides which clock the clips are judged against.
    //    Switching to Beats mid-run: the beat clock has not been advancing,
    //    so align it to the real playhead here (for the checks below) and
    //    again on the next beat pulse, which re-establishes beat sync.
    if (s.showTempo != m_schedule->showTempo && s.showTempo == Function::Beats)
    {
        m_elapsedBeats = m_elapsedTime;
        beatSynced = false;
    }
    m_schedule = schedule;
    quint32 now = this->now();

    // 1. Running clips: stop what is gone or no longer under the playhead,
    //    restart what moved (only if the offset matters), update the rest.
    for (int i = m_runningQueue.count() - 1; i >= 0; i--)
    {
        RunningClip &rc = m_runningQueue[i];
        const ScheduledClip *clip = s.clip(rc.sfId);

        if (clip == NULL || clip->isActiveAt(now) == false)
        {
            stopClip(i);
            continue;
        }

        bool restart = clip->functionId != rc.functionId ||
                       (clip->start != rc.start && isOffsetSensitive(clip->type));
        if (restart)
        {
            // the start pass below re-adds it at the new offset once the
            // MasterTimer has completed the stop
            stopClip(i);
            continue;
        }

        rc.start = clip->start;
        rc.stopTime = clip->end;
        rc.trackId = clip->trackId;
    }

    // 2. Track intensity: attribute values by track ID (track IDs are
    //    swapped by Show::moveTrack, so the old map may be stale).
    QMap<quint32, qreal> intensityMap;
    for (auto it = s.intensity.constBegin(); it != s.intensity.constEnd(); ++it)
        intensityMap[it.key()] = it.value();

    for (int i = 0; i < m_runningQueue.count(); i++)
    {
        RunningClip &rc = m_runningQueue[i];
        qreal intensity = intensityMap.value(rc.trackId, 1.0);
        if (rc.overrideId != Function::invalidAttributeId() &&
            m_intensityMap.value(rc.trackId, 1.0) != intensity)
                rc.function->adjustAttribute(intensity, rc.overrideId);
    }
    m_intensityMap = intensityMap;

    // 3. Phase 1 continues from the first clip starting after the playhead;
    //    everything at or before it is handled by the start pass, which
    //    starts the active clips that are not running.
    m_totalRunTime = s.totalRunTime;
    m_currentClipIndex = indexAfter(s.clips, now);
    m_startPassPending = true;
}

void ShowRunner::applyPendingSchedule()
{
    if (m_show == NULL)
        return;

    QSharedPointer<const ShowSchedule> schedule = m_show->takePendingSchedule();
    if (schedule.isNull() == false)
        reconcile(schedule);
}

void ShowRunner::write(MasterTimer *timer)
{
    //qDebug() << Q_FUNC_INFO << "elapsed:" << m_elapsedTime << ", total:" << m_totalRunTime;

    // Phase 0. Pick up timeline edits made since the last tick
    applyPendingSchedule();

    // check synchronization to beats (if show is beat-based)
    if (m_schedule->showTempo == Function::Beats)
    {
        //qDebug() << Q_FUNC_INFO << "isBeat:" << timer->isBeat() << ", elapsed beats:" << m_elapsedBeats;

        if (timer->isBeat())
        {
            if (beatSynced == false)
            {
                // The beat clock starts from wherever the real playhead is
                // (the Show's start position, or the point at which the Show
                // was switched to Beats while playing) and steps from here.
                m_elapsedBeats = m_elapsedTime;
                beatSynced = true;
                qDebug() << "Beat synced";
            }
            else
            {
                // m_elapsedBeats tracks real elapsed milliseconds (see showrunner.h),
                // not a beat count, so advance it by the actual duration of one beat
                // at the current BPM rather than a fixed pseudo-unit step. With no
                // known BPM there is no way to convert a beat pulse into a real-ms
                // increment, so leave it unchanged instead of guessing - the
                // clips simply won't start/stop until BPM is known.
                int bpmNumber = m_doc->inputOutputMap()->bpmNumber();
                if (bpmNumber > 0)
                    m_elapsedBeats += qRound(60000.0 / bpmNumber);
            }
        }

        if (beatSynced == false)
            return;
    }

    // Clips (re)scheduled under the playhead by an edit, or whose start was
    // deferred while the Show was paused
    if (m_startPassPending)
        runStartPass();

    // Phase 1. Check all the Functions that need to be started
    startDueClips();

    // Phase 2. Check if we need to stop some running Functions
    // It is done in reverse order for two reasons:
    // 1- m_runningQueue is not ordered by stop time
    // 2- to avoid messing up with indices when an entry is removed
    quint32 currTime = now();
    for (int i = m_runningQueue.count() - 1; i >= 0; i--)
    {
        const RunningClip &rc = m_runningQueue.at(i);

        // if we passed the function stop time
        if (currTime >= rc.stopTime)
        {
            // stop the function
            rc.function->stop(functionParent());
            // remove it from the running queue
            m_runningQueue.removeAt(i);
        }
    }

    // Phase 3. Check if this is the end of the Show
    if (m_elapsedTime >= m_totalRunTime)
    {
        if (m_show != NULL)
            m_show->stop(functionParent());
        emit showFinished();
        return;
    }

    m_elapsedTime += MasterTimer::tick();
    emit timeChanged(m_elapsedTime);
}

/************************************************************************
 * Intensity
 ************************************************************************/

void ShowRunner::adjustIntensity(qreal fraction, const Track *track)
{
    if (track == NULL)
        return;

    qDebug() << Q_FUNC_INFO << "Track ID: " << track->id() << ", val:" << fraction;
    m_intensityMap[track->id()] = fraction;

    for (int i = 0; i < m_runningQueue.count(); i++)
    {
        const RunningClip &rc = m_runningQueue.at(i);
        if (rc.trackId == track->id() && rc.overrideId != Function::invalidAttributeId())
            rc.function->adjustAttribute(fraction, rc.overrideId);
    }
}
