/*
  Q Light Controller Plus
  show.cpp

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

#include <QXmlStreamReader>
#include <QXmlStreamWriter>
#include <QString>
#include <QThread>
#include <QDebug>
#include <QFile>
#include <QList>

#include <algorithm>

#include "showrunner.h"
#include "function.h"
#include "show.h"
#include "doc.h"

#define KXMLQLCShowTimeDivision QStringLiteral("TimeDivision")
#define KXMLQLCShowTimeType     QStringLiteral("Type")
#define KXMLQLCShowTimeBPM      QStringLiteral("BPM")

/*****************************************************************************
 * Initialization
 *****************************************************************************/

Show::Show(Doc* doc) : Function(doc, Function::ShowType)
    , m_timeDivisionType(Time)
    , m_timeDivisionBPM(120)
    , m_latestTrackId(0)
    , m_latestShowFunctionID(0)
    , m_scheduleDirty(true)
    , m_rebuildQueued(false)
    , m_runner(NULL)
{
    setName(tr("New Show"));

    // Clear attributes here. I want attributes to be mapped
    // exactly like the Show tracks
    unregisterAttribute(tr("Intensity"));

    // The schedule splits clips by their Function's tempo type and resolves a
    // zero duration to the Function's own; both can change outside this Show.
    connect(this, &Function::tempoTypeChanged, this, &Show::markScheduleDirty);
    if (doc != NULL)
        connect(doc, &Doc::functionChanged, this, &Show::slotFunctionChanged);
}

Show::~Show()
{
    m_tracks.clear();
}

QIcon Show::getIcon() const
{
    return QIcon(":/show.png");
}

quint32 Show::totalDuration()
{
    quint32 totalDuration = 0;

    foreach (Track *track, m_tracks)
    {
        foreach (ShowFunction *sf, track->showFunctions())
        {
            if (sf->startTime() + sf->duration(doc()) > totalDuration)
                totalDuration = sf->startTime() + sf->duration(doc());
        }
    }

    return totalDuration;
}

/*****************************************************************************
 * Copying
 *****************************************************************************/

Function* Show::createCopy(Doc* doc, bool addToDoc)
{
    Q_ASSERT(doc != NULL);

    Function* copy = new Show(doc);
    if (copy->copyFrom(this) == false)
    {
        delete copy;
        copy = NULL;
    }
    if (addToDoc == true && doc->addFunction(copy) == false)
    {
        delete copy;
        copy = NULL;
    }

    return copy;
}

bool Show::copyFrom(const Function* function)
{
    const Show* show = qobject_cast<const Show*> (function);
    if (show == NULL)
        return false;

    m_timeDivisionType = show->m_timeDivisionType;
    m_timeDivisionBPM = show->m_timeDivisionBPM;
    m_latestTrackId = show->m_latestTrackId;
    m_latestShowFunctionID = show->m_latestShowFunctionID;

    // create a copy of each track
    foreach (Track *track, show->tracks())
    {
        quint32 sceneID = track->getSceneID();
        Track* newTrack = new Track(sceneID, this);
        newTrack->setName(track->name());
        addTrack(newTrack);

        // create a copy of each sequence/audio in a track
        foreach (ShowFunction *sfunc, track->showFunctions())
        {
            Function* function = doc()->function(sfunc->functionID());
            if (function == NULL)
                continue;

            /* Attempt to create a copy of the function to Doc */
            Function* copy = function->createCopy(doc());
            if (copy != NULL)
            {
                copy->setName(tr("Copy of %1").arg(function->name()));
                ShowFunction *showFunc = newTrack->createShowFunction(copy->id());
                showFunc->setStartTime(sfunc->startTime());
                showFunc->setDuration(sfunc->duration());
                showFunc->setColor(sfunc->color());
                showFunc->setLocked(sfunc->isLocked());
            }
        }
    }

    return Function::copyFrom(function);
}

/*********************************************************************
 * Time division
 *********************************************************************/

void Show::setTimeDivision(Show::TimeDivision type, int BPM)
{
    qDebug() << "[setTimeDivision] type:" << type << ", BPM:" << BPM;
    m_timeDivisionType = type;
    m_timeDivisionBPM = BPM;
}

Show::TimeDivision Show::timeDivisionType() const
{
    return m_timeDivisionType;
}

int Show::beatsDivision() const
{
    switch(m_timeDivisionType)
    {
        case BPM_2_4: return 2;
        case BPM_3_4: return 3;
        case BPM_4_4: return 4;
        default: return 0;
    }
}

void Show::setTimeDivisionType(TimeDivision type)
{
    m_timeDivisionType = type;
}

int Show::timeDivisionBPM() const
{
    return m_timeDivisionBPM;
}

void Show::setTimeDivisionBPM(int BPM)
{
    m_timeDivisionBPM = BPM;
}

QString Show::tempoToString(Show::TimeDivision type)
{
    switch(type)
    {
        case Time: return QString("Time"); break;
        case BPM_4_4: return QString("BPM_4_4"); break;
        case BPM_3_4: return QString("BPM_3_4"); break;
        case BPM_2_4: return QString("BPM_2_4"); break;
        case Invalid:
        default:
            return QString("Invalid"); break;
    }
    return QString();
}

Show::TimeDivision Show::stringToTempo(const QString& tempo)
{
    if (tempo == "Time")
        return Time;
    else if (tempo == "BPM_4_4")
        return BPM_4_4;
    else if (tempo == "BPM_3_4")
        return BPM_3_4;
    else if (tempo == "BPM_2_4")
        return BPM_2_4;
    else
        return Invalid;
}

/*****************************************************************************
 * Tracks
 *****************************************************************************/

bool Show::addTrack(Track *track, quint32 id)
{
    Q_ASSERT(track != NULL);

    // No ID given, this method can assign one
    if (id == Track::invalidId())
        id = createTrackId();

     track->setId(id);
     track->setShowId(this->id());
     m_tracks[id] = track;

     registerAttribute(QString("%1-%2").arg(track->name()).arg(track->id()));

     connect(track, &Track::changed, this, &Show::slotTrackChanged, Qt::UniqueConnection);
     markScheduleDirty();

     return true;
}

bool Show::removeTrack(quint32 id)
{
    if (m_tracks.contains(id) == true)
    {
        Track* track = m_tracks.take(id);
        Q_ASSERT(track != NULL);

        unregisterAttribute(QString("%1-%2").arg(track->name()).arg(track->id()));

        //emit trackRemoved(id);
        delete track;
        markScheduleDirty();

        return true;
    }
    else
    {
        qWarning() << Q_FUNC_INFO << "No track found with id" << id;
        return false;
    }
}

Track* Show::track(quint32 id) const
{
    return m_tracks.value(id, NULL);
}

Track* Show::getTrackFromSceneID(quint32 id) const
{
    foreach (Track *track, m_tracks)
    {
        if (track->getSceneID() == id)
            return track;
    }
    return NULL;
}

Track *Show::getTrackFromShowFunctionID(quint32 id) const
{
    foreach (Track *track, m_tracks)
        if (track->showFunction(id) != NULL)
            return track;

    return NULL;
}

int Show::getTracksCount() const
{
    return m_tracks.size();
}

void Show::moveTrack(Track *track, int direction)
{
    if (track == NULL)
        return;

    qint32 trkID = track->id();
    if (trkID == 0 && direction == -1)
        return;
    qint32 maxID = -1;
    Track *swapTrack = NULL;
    qint32 swapID = -1;
    if (direction > 0) swapID = INT_MAX;

    foreach (quint32 id, m_tracks.keys())
    {
        qint32 signedID = (qint32)id;
        if (signedID > maxID) maxID = signedID;
        if (direction == -1 && signedID > swapID && signedID < trkID)
            swapID = signedID;
        else if (direction == 1 && signedID < swapID && signedID > trkID)
            swapID = signedID;
    }

    qDebug() << Q_FUNC_INFO << "Direction:" << direction << ", trackID:" << trkID << ", swapID:" << swapID;
    if (swapID == trkID || (direction > 0 && trkID == maxID))
        return;

    swapTrack = m_tracks[swapID];
    m_tracks[swapID] = track;
    m_tracks[trkID] = swapTrack;
    track->setId(swapID);
    swapTrack->setId(trkID);
    markScheduleDirty();
}

QList <Track*> Show::tracks() const
{
    return m_tracks.values();
}

quint32 Show::createTrackId()
{
    while (m_tracks.contains(m_latestTrackId) == true ||
           m_latestTrackId == Track::invalidId())
    {
        m_latestTrackId++;
    }

    return m_latestTrackId;
}

/*********************************************************************
 * Show Functions
 *********************************************************************/

quint32 Show::getLatestShowFunctionId()
{
    return m_latestShowFunctionID++;
}

ShowFunction *Show::showFunction(quint32 id) const
{
    foreach (Track *track, m_tracks)
    {
        ShowFunction *sf = track->showFunction(id);
        if (sf != NULL)
            return sf;
    }

    return NULL;
}

/*****************************************************************************
 * Load & Save
 *****************************************************************************/

bool Show::saveXML(QXmlStreamWriter *doc) const
{
    Q_ASSERT(doc != NULL);

    /* Function tag */
    doc->writeStartElement(KXMLQLCFunction);

    /* Common attributes */
    saveXMLCommon(doc);

    doc->writeStartElement(KXMLQLCShowTimeDivision);
    doc->writeAttribute(KXMLQLCShowTimeType, tempoToString(m_timeDivisionType));
    doc->writeAttribute(KXMLQLCShowTimeBPM, QString::number(m_timeDivisionBPM));
    doc->writeEndElement();

    foreach (Track *track, m_tracks)
        track->saveXML(doc);

    /* End the <Function> tag */
    doc->writeEndElement();

    return true;
}

bool Show::loadXML(QXmlStreamReader &root)
{
    if (root.name() != KXMLQLCFunction)
    {
        qWarning() << Q_FUNC_INFO << "Function node not found";
        return false;
    }

    if (root.attributes().value(KXMLQLCFunctionType).toString() != typeToString(Function::ShowType))
    {
        qWarning() << Q_FUNC_INFO << root.attributes().value(KXMLQLCFunctionType).toString()
                   << "is not a show";
        return false;
    }

    while (root.readNextStartElement())
    {
        if (root.name() == KXMLQLCShowTimeDivision)
        {
            QString type = root.attributes().value(KXMLQLCShowTimeType).toString();
            int bpm = root.attributes().value(KXMLQLCShowTimeBPM).toString().toInt();
            setTimeDivision(stringToTempo(type), bpm);
            root.skipCurrentElement();
        }
        else if (root.name() == KXMLQLCTrack)
        {
            Track *trk = new Track(Function::invalidId(), this);
            if (trk->loadXML(root) == true)
                addTrack(trk, trk->id());
        }
        else
        {
            qWarning() << Q_FUNC_INFO << "Unknown Show tag:" << root.name();
            root.skipCurrentElement();
        }
    }

    return true;
}

void Show::postLoad()
{
    foreach (Track* track, m_tracks)
    {
        if (track->postLoad(doc()))
            doc()->setModified();
    }

    // A loaded Show can be started from anywhere (Virtual Console, autostart,
    // Function Manager) without going through ShowManager, so make sure a
    // snapshot exists before the MasterTimer thread ever needs one.
    rebuildSchedule();
}

bool Show::contains(quint32 functionId) const
{
    Doc *doc = this->doc();
    Q_ASSERT(doc != NULL);

    if (functionId == id())
        return true;

    foreach (Track* track, m_tracks)
    {
        if (track->contains(doc, functionId))
            return true;
    }

    return false;
}

QList<quint32> Show::components() const
{
    QList<quint32> ids;

    foreach (Track* track, m_tracks)
        ids.append(track->components());

    return ids;
}

/*****************************************************************************
 * Schedule
 *****************************************************************************/

void Show::rebuildSchedule()
{
    QSharedPointer<const ShowSchedule> schedule = buildSchedule();

    {
        QMutexLocker locker(&m_scheduleMutex);
        m_currentSchedule = schedule;
        m_pendingSchedule = schedule;
        m_scheduleDirty = false;
    }

    emit scheduleChanged();
}

QSharedPointer<const ShowSchedule> Show::currentSchedule()
{
    bool rebuild = false;
    {
        QMutexLocker locker(&m_scheduleMutex);
        // Never built: build now even off the owner thread, as the runner
        // cannot work without one (same live walk the old runner did).
        // Stale: only the owner thread may walk the live tracks; the queued
        // rebuild will deliver a pending snapshot to the runner shortly.
        rebuild = m_currentSchedule.isNull() ||
                  (m_scheduleDirty && QThread::currentThread() == thread());
    }

    if (rebuild)
        rebuildSchedule();

    QMutexLocker locker(&m_scheduleMutex);
    m_pendingSchedule.clear();
    return m_currentSchedule;
}

QSharedPointer<const ShowSchedule> Show::takePendingSchedule()
{
    QMutexLocker locker(&m_scheduleMutex);
    QSharedPointer<const ShowSchedule> schedule = m_pendingSchedule;
    m_pendingSchedule.clear();
    return schedule;
}

bool Show::isScheduleDirty() const
{
    QMutexLocker locker(&m_scheduleMutex);
    return m_scheduleDirty;
}

void Show::markScheduleDirty()
{
    {
        QMutexLocker locker(&m_scheduleMutex);
        m_scheduleDirty = true;
        if (m_rebuildQueued)
            return;
        m_rebuildQueued = true;
    }

    // Coalesce bursts (undo walking many steps, a drag emitting per frame)
    // into a single rebuild per event-loop turn.
    QMetaObject::invokeMethod(this, &Show::slotRebuildScheduleIfDirty, Qt::QueuedConnection);
}

void Show::slotRebuildScheduleIfDirty()
{
    bool dirty = false;
    {
        QMutexLocker locker(&m_scheduleMutex);
        m_rebuildQueued = false;
        dirty = m_scheduleDirty;
    }

    if (dirty)
        rebuildSchedule();
}

void Show::slotTrackChanged(quint32 trackId)
{
    Q_UNUSED(trackId);
    markScheduleDirty();
}

void Show::slotFunctionChanged(quint32 fid)
{
    QSharedPointer<const ShowSchedule> schedule;
    {
        QMutexLocker locker(&m_scheduleMutex);
        if (m_scheduleDirty)
            return;
        schedule = m_currentSchedule;
    }

    if (schedule.isNull() || schedule->functionIds.contains(fid))
        markScheduleDirty();
}

QSharedPointer<const ShowSchedule> Show::buildSchedule() const
{
    QSharedPointer<ShowSchedule> schedule(new ShowSchedule);
    schedule->showTempo = tempoType();

    // Attributes are registered one per track, in track order (see addTrack)
    int attributeIndex = 0;
    foreach (Track *track, m_tracks)
    {
        qreal intensity = getAttributeValue(attributeIndex++);

        if (track == NULL || track->id() == Track::invalidId())
            continue;

        schedule->intensity[track->id()] = intensity;

        if (track->isMute())
            continue;

        foreach (ShowFunction *sf, track->showFunctions())
        {
            Function *f = doc()->function(sf->functionID());
            if (f == NULL)
                continue;

            ScheduledClip clip;
            clip.sfId = sf->id();
            clip.functionId = f->id();
            clip.trackId = track->id();
            clip.start = sf->startTime();
            clip.end = sf->startTime() + sf->duration(doc());
            clip.tempo = f->tempoType();
            clip.type = f->type();

            if (clip.tempo == Function::Time)
                schedule->timeClips.append(clip);
            else
                schedule->beatClips.append(clip);

            schedule->functionIds.insert(f->id());

            if (clip.end > schedule->totalRunTime)
                schedule->totalRunTime = clip.end;
        }
    }

    auto byStart = [](const ScheduledClip &a, const ScheduledClip &b) { return a.start < b.start; };
    std::stable_sort(schedule->timeClips.begin(), schedule->timeClips.end(), byStart);
    std::stable_sort(schedule->beatClips.begin(), schedule->beatClips.end(), byStart);

    return schedule;
}

/*****************************************************************************
 * Running
 *****************************************************************************/

void Show::preRun(MasterTimer* timer)
{
    Function::preRun(timer);
    m_runningChildren.clear();
    if (m_runner != NULL)
    {
        m_runner->stop();
        delete m_runner;
    }

    m_runner = new ShowRunner(doc(), this->id(), elapsed());
    int i = 0;
    foreach (Track *track, m_tracks)
        m_runner->adjustIntensity(getAttributeValue(i++), track);

    connect(m_runner, SIGNAL(timeChanged(quint32)), this, SIGNAL(timeChanged(quint32)));
    connect(m_runner, SIGNAL(showFinished()), this, SIGNAL(showFinished()));
    m_runner->start();
}

void Show::setPause(bool enable)
{
    if (m_runner != NULL)
        m_runner->setPause(enable);
    Function::setPause(enable);
}

void Show::write(MasterTimer* timer, QList<Universe *> universes)
{
    Q_UNUSED(universes);

    if (isPaused())
        return;

    m_runner->write(timer);
}

void Show::postRun(MasterTimer* timer, QList<Universe *> universes)
{
    if (m_runner != NULL)
    {
        m_runner->stop();
        delete m_runner;
        m_runner = NULL;
    }
    Function::postRun(timer, universes);
}

void Show::slotChildStopped(quint32 fid)
{
    Q_UNUSED(fid);
}

/*****************************************************************************
 * Attributes
 *****************************************************************************/

int Show::adjustAttribute(qreal fraction, int attributeId)
{
    int attrIndex = Function::adjustAttribute(fraction, attributeId);

    if (m_runner != NULL)
    {
        QList<Track*> trkList = m_tracks.values();
        if (trkList.isEmpty() == false &&
            attrIndex >= 0 && attrIndex < trkList.count())
        {
            Track *track = trkList.at(attrIndex);
            if (track != NULL)
                m_runner->adjustIntensity(getAttributeValue(attrIndex), track);
        }
    }

    return attrIndex;
}

