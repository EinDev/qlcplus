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

#include "showrunner.h"
#include "function.h"
#include "track.h"
#include "show.h"

#define TIMER_INTERVAL 50

static bool compareShowFunctions(const ShowFunction *sf1, const ShowFunction *sf2)
{
    if (sf1->startTime() < sf2->startTime())
        return true;
    return false;
}

ShowRunner::ShowRunner(const Doc* doc, quint32 showID, quint32 startTime)
    : QObject(NULL)
    , m_doc(doc)
    , m_currentTimeFunctionIndex(0)
    , m_elapsedTime(startTime)
    , m_currentBeatFunctionIndex(0)
    , m_elapsedBeats(0)
    , beatSynced(false)
    , m_totalRunTime(0)
{
    Q_ASSERT(m_doc != NULL);
    Q_ASSERT(showID != Show::invalidId());

    m_show = qobject_cast<Show*>(m_doc->function(showID));
    if (m_show == NULL)
        return;

    foreach (Track *track, m_show->tracks())
    {
        // some sanity checks
        if (track == NULL ||
            track->id() == Track::invalidId())
                continue;

        if (track->isMute())
            continue;

        // get all the functions of the track and append them to the runner queue
        foreach (ShowFunction *sfunc, track->showFunctions())
        {
            if (sfunc->startTime() + sfunc->duration(m_doc) <= startTime)
                continue;

            Function *f = m_doc->function(sfunc->functionID());
            if (f == NULL)
                continue;

            if (f->tempoType() == Function::Time)
                m_timeFunctions.append(sfunc);
            else
                m_beatFunctions.append(sfunc);

            if (sfunc->startTime() + sfunc->duration(m_doc) > m_totalRunTime)
                m_totalRunTime = sfunc->startTime() + sfunc->duration(m_doc);
        }

        // Initialize the intensity map
        m_intensityMap[track->id()] = 1.0;
    }

    std::sort(m_timeFunctions.begin(), m_timeFunctions.end(), compareShowFunctions);
    std::sort(m_beatFunctions.begin(), m_beatFunctions.end(), compareShowFunctions);

#if 1
    qDebug() << "[DBG SHOWRUN] Show tempoType:" << (int)m_show->tempoType() << "startTime:" << startTime;
    qDebug() << "Ordered list of ShowFunctions (time):";
    foreach (ShowFunction *sfunc, m_timeFunctions)
    {
        Function *dbgF = m_doc->function(sfunc->functionID());
        qDebug() << "[Show] Function ID:" << sfunc->functionID() << "start time:" << sfunc->startTime() << "duration:" << sfunc->duration(m_doc)
                 << "[DBG] type:" << (dbgF ? dbgF->type() : -1) << "tempoType:" << (dbgF ? (int)dbgF->tempoType() : -1)
                 << "func totalDuration:" << (dbgF ? dbgF->totalDuration() : 0);
    }

    qDebug() << "Ordered list of ShowFunctions (beats):";
    foreach (ShowFunction *sfunc, m_beatFunctions)
    {
        Function *dbgF = m_doc->function(sfunc->functionID());
        qDebug() << "[Show] Function ID:" << sfunc->functionID() << "start time:" << sfunc->startTime() << "duration:" << sfunc->duration(m_doc)
                 << "[DBG] type:" << (dbgF ? dbgF->type() : -1) << "tempoType:" << (dbgF ? (int)dbgF->tempoType() : -1)
                 << "func totalDuration:" << (dbgF ? dbgF->totalDuration() : 0);
    }
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
    {
        Function *f = m_runningQueue.at(i).first;
        f->setPause(enable);
    }
}

void ShowRunner::stop()
{
    m_elapsedTime = 0;
    m_elapsedBeats = 0;
    m_currentTimeFunctionIndex = 0;
    m_currentBeatFunctionIndex = 0;

    for (int i = 0; i < m_runningQueue.count(); i++)
    {
        Function *f = m_runningQueue.at(i).first;
        f->stop(functionParent());
    }

    m_runningQueue.clear();
    qDebug() << "ShowRunner stopped";
}

FunctionParent ShowRunner::functionParent() const
{
    return FunctionParent(FunctionParent::Function, m_show->id());
}

void ShowRunner::write(MasterTimer *timer)
{
    //qDebug() << Q_FUNC_INFO << "elapsed:" << m_elapsedTime << ", total:" << m_totalRunTime;

    // Phase 1. Check all the Functions that need to be started
    // m_timeFunctions is ordered by startup time, so when we found an entry
    // with start time greater than m_elapsed, this phase is over
    bool startFunctionsDone = false;

    // check synchronization to beats (if show is beat-based)
    if (m_show->tempoType() == Function::Beats)
    {
        //qDebug() << Q_FUNC_INFO << "isBeat:" << timer->isBeat() << ", elapsed beats:" << m_elapsedBeats;

        if (timer->isBeat())
        {
            if (beatSynced == false)
            {
                beatSynced = true;
                qDebug() << "Beat synced";
            }
            else
                m_elapsedBeats += 1000;
        }

        if (beatSynced == false)
            return;
    }

    // check if there are time-based functions to start
    while (startFunctionsDone == false)
    {
        if (m_currentTimeFunctionIndex == m_timeFunctions.count())
            break;

        ShowFunction *sf = m_timeFunctions.at(m_currentTimeFunctionIndex);
        quint32 funcStartTime = sf->startTime();
        quint32 functionTimeOffset = 0;
        Function *f = m_doc->function(sf->functionID());
        if (f == nullptr)
        {
            m_currentTimeFunctionIndex++;
            continue;
        }

        // this should happen only when a Show is not started from 0
        if (m_elapsedTime > funcStartTime)
        {
            functionTimeOffset = m_elapsedTime - funcStartTime;
            funcStartTime = m_elapsedTime;
        }
        if (m_elapsedTime >= funcStartTime)
        {
            foreach (Track *track, m_show->tracks())
            {
                if (track->showFunctions().contains(sf))
                {
                    int intOverrideId = f->requestAttributeOverride(Function::Intensity, m_intensityMap[track->id()]);
                    //f->adjustAttribute(m_intensityMap[track->id()], Function::Intensity);
                    sf->setIntensityOverrideId(intOverrideId);
                    break;
                }
            }

            f->start(m_doc->masterTimer(), functionParent(), functionTimeOffset);
            m_runningQueue.append(QPair<Function *, quint32>(f, sf->startTime() + sf->duration(m_doc)));
            qDebug() << "[DBG SHOWRUN] START (time-path) functionID:" << f->id() << "type:" << f->type()
                     << "tempoType:" << (int)f->tempoType() << "m_elapsedTime:" << m_elapsedTime
                     << "sf->startTime:" << sf->startTime() << "sf->duration:" << sf->duration(m_doc)
                     << "-> stopTime:" << (sf->startTime() + sf->duration(m_doc)) << "offset:" << functionTimeOffset;
            m_currentTimeFunctionIndex++;
        }
        else
            startFunctionsDone = true;
    }

    startFunctionsDone = false;

    // check if there are beat-based functions to start
    while (startFunctionsDone == false)
    {
        if (m_currentBeatFunctionIndex == m_beatFunctions.count())
            break;

        ShowFunction *sf = m_beatFunctions.at(m_currentBeatFunctionIndex);
        quint32 funcStartTime = sf->startTime();
        quint32 functionTimeOffset = 0;
        Function *f = m_doc->function(sf->functionID());
        if (f == nullptr)
        {
            m_currentBeatFunctionIndex++;
            continue;
        }

        // this should happen only when a Show is not started from 0
        if (m_elapsedBeats > funcStartTime)
        {
            functionTimeOffset = m_elapsedBeats - funcStartTime;
            funcStartTime = m_elapsedBeats;
        }
        if (m_elapsedBeats >= funcStartTime)
        {
            foreach (Track *track, m_show->tracks())
            {
                if (track->showFunctions().contains(sf))
                {
                    int intOverrideId = f->requestAttributeOverride(Function::Intensity, m_intensityMap[track->id()]);
                    //f->adjustAttribute(m_intensityMap[track->id()], Function::Intensity);
                    sf->setIntensityOverrideId(intOverrideId);
                    break;
                }
            }

            f->start(m_doc->masterTimer(), functionParent(), functionTimeOffset);
            m_runningQueue.append(QPair<Function *, quint32>(f, sf->startTime() + sf->duration(m_doc)));
            qDebug() << "[DBG SHOWRUN] START (beat-path) functionID:" << f->id() << "type:" << f->type()
                     << "tempoType:" << (int)f->tempoType() << "m_elapsedBeats:" << m_elapsedBeats
                     << "sf->startTime:" << sf->startTime() << "sf->duration:" << sf->duration(m_doc)
                     << "-> stopTime:" << (sf->startTime() + sf->duration(m_doc)) << "offset:" << functionTimeOffset;
            m_currentBeatFunctionIndex++;
        }
        else
            startFunctionsDone = true;
    }

    // Phase 2. Check if we need to stop some running Functions
    // It is done in reverse order for two reasons:
    // 1- m_runningQueue is not ordered by stop time
    // 2- to avoid messing up with indices when an entry is removed
    for (int i = m_runningQueue.count() - 1; i >= 0; i--)
    {
        Function *func = m_runningQueue.at(i).first;
        quint32 stopTime = m_runningQueue.at(i).second;
        quint32 currTime = func->tempoType() == Function::Time ? m_elapsedTime : m_elapsedBeats;

        // if we passed the function stop time
        if (currTime >= stopTime)
        {
            qDebug() << "[DBG SHOWRUN] STOP functionID:" << func->id() << "type:" << func->type()
                     << "tempoType:" << (int)func->tempoType() << "currTime:" << currTime << "stopTime:" << stopTime
                     << "m_elapsedTime:" << m_elapsedTime << "m_elapsedBeats:" << m_elapsedBeats;
            // stop the function
            func->stop(functionParent());
            // remove it from the running queue
            m_runningQueue.removeAt(i);
        }
    }

    // Phase 3. Check if this is the end of the Show
    if (m_elapsedTime >= m_totalRunTime)
    {
        qDebug() << "[DBG SHOWRUN] Show finished. m_elapsedTime:" << m_elapsedTime << "m_totalRunTime:" << m_totalRunTime;
        if (m_show != NULL)
            m_show->stop(functionParent());
        emit showFinished();
        return;
    }

    static quint32 dbgLastHeartbeat = 0;
    if (m_elapsedTime - dbgLastHeartbeat >= 1000 || m_elapsedTime < dbgLastHeartbeat)
    {
        dbgLastHeartbeat = m_elapsedTime;
        qDebug() << "[DBG SHOWRUN-TICK] m_elapsedTime:" << m_elapsedTime << "m_elapsedBeats:" << m_elapsedBeats
                  << "isBeat:" << timer->isBeat() << "runningQueue size:" << m_runningQueue.count()
                  << "totalRunTime:" << m_totalRunTime;
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

    foreach (ShowFunction *sf, track->showFunctions())
    {
        Function *f = m_doc->function(sf->functionID());
        if (f == NULL)
            continue;

        for (int i = 0; i < m_runningQueue.count(); i++)
        {
            Function *rf = m_runningQueue.at(i).first;
            if (f == rf)
                f->adjustAttribute(fraction, sf->intensityOverrideId());
        }
    }
}

