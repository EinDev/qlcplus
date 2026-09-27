/*
  Q Light Controller Plus - Control API
  apishowdomain.cpp

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

#include <algorithm>

#include <QJsonArray>
#include <QColor>
#include <QSet>

#include "apishowdomain.h"
#include "apishowpreviewdomain.h"
#include "apifunctionsdomain.h"
#include "apiserver.h"
#include "apisession.h"
#include "apienvelope.h"
#include "showmovehelper.h"
#include "showfunction.h"
#include "chaserstep.h"
#include "chaser.h"
#include "track.h"
#include "show.h"
#include "doc.h"

/*****************************************************************************
 * JSON helpers (file-local)
 *****************************************************************************/

namespace
{

QString timeDivisionToString(Show::TimeDivision type)
{
    switch (type)
    {
        case Show::BPM_4_4: return QStringLiteral("bpm_4_4");
        case Show::BPM_3_4: return QStringLiteral("bpm_3_4");
        case Show::BPM_2_4: return QStringLiteral("bpm_2_4");
        case Show::Time:
        default:            return QStringLiteral("time");
    }
}

bool timeDivisionFromString(const QString &str, Show::TimeDivision &type)
{
    if (str == QStringLiteral("time"))         type = Show::Time;
    else if (str == QStringLiteral("bpm_4_4")) type = Show::BPM_4_4;
    else if (str == QStringLiteral("bpm_3_4")) type = Show::BPM_3_4;
    else if (str == QStringLiteral("bpm_2_4")) type = Show::BPM_2_4;
    else return false;
    return true;
}

QJsonObject conflictDetails(Doc *doc)
{
    QJsonObject details;
    details.insert(QStringLiteral("docRevision"), int(doc->docRevision()));
    return details;
}

QJsonObject docRevisionResult(Doc *doc)
{
    QJsonObject result;
    result.insert(QStringLiteral("docRevision"), int(doc->docRevision()));
    return result;
}

bool checkBaseRevision(Doc *doc, const QJsonObject &params, ApiSession *session, const QString &id)
{
    quint32 baseRevision = quint32(params.value(QStringLiteral("baseRevision")).toInt());
    if (baseRevision != doc->docRevision())
    {
        session->send(ApiEnvelope::buildErrorResponse(id, ApiEnvelope::ErrConflict,
                                                        QStringLiteral("baseRevision is stale"), conflictDetails(doc)));
        return false;
    }
    return true;
}

/** params.showId (or params.functionId, the spelling functions.show.setTimeDivision
 *  uses) resolved to a Show, or NOT_FOUND/INVALID_PARAMS answered and null. */
Show *findShowOrRespond(Doc *doc, const QJsonObject &params, ApiSession *session, const QString &id)
{
    QJsonValue v = params.contains(QStringLiteral("showId")) ? params.value(QStringLiteral("showId"))
                                                             : params.value(QStringLiteral("functionId"));
    QString idStr = v.isDouble() ? QString::number(v.toInt()) : v.toString();
    bool ok = false;
    quint32 fid = idStr.toUInt(&ok);
    Function *function = ok ? doc->function(fid) : nullptr;
    if (function == nullptr)
    {
        session->send(ApiEnvelope::buildErrorResponse(id, ApiEnvelope::ErrNotFound,
                                                        QStringLiteral("No function with id %1").arg(idStr)));
        return nullptr;
    }
    if (function->type() != Function::ShowType)
    {
        session->send(ApiEnvelope::buildErrorResponse(id, ApiEnvelope::ErrInvalidParams,
                                                        QStringLiteral("Function %1 is not a Show").arg(idStr)));
        return nullptr;
    }
    return qobject_cast<Show *>(function);
}

Track *findTrackOrRespond(Show *show, const QJsonValue &v, ApiSession *session, const QString &id)
{
    QString idStr = v.isDouble() ? QString::number(v.toInt()) : v.toString();
    bool ok = false;
    quint32 trackId = idStr.toUInt(&ok);
    Track *track = ok ? show->track(trackId) : nullptr;
    if (track == nullptr)
    {
        session->send(ApiEnvelope::buildErrorResponse(id, ApiEnvelope::ErrNotFound,
                                                        QStringLiteral("Show %1 has no track with id %2").arg(show->id()).arg(idStr)));
        return nullptr;
    }
    return track;
}

ShowFunction *findItemOrRespond(Show *show, const QJsonValue &v, ApiSession *session, const QString &id)
{
    QString idStr = v.isDouble() ? QString::number(v.toInt()) : v.toString();
    bool ok = false;
    quint32 sfId = idStr.toUInt(&ok);
    ShowFunction *sf = ok ? show->showFunction(sfId) : nullptr;
    if (sf == nullptr)
    {
        session->send(ApiEnvelope::buildErrorResponse(id, ApiEnvelope::ErrNotFound,
                                                        QStringLiteral("Show %1 has no item with id %2").arg(show->id()).arg(idStr)));
        return nullptr;
    }
    return sf;
}

/** ShowManager::minimumTimelineDuration */
int minimumDuration(Show *show)
{
    return show->timeDivisionType() == Show::Time ? 1 : 125;
}

/** The clips of $track as plain spans. Like ShowManager::trackSpans() /
 *  checkOverlapping(), an item whose Function is gone blocks nothing. */
QList<ShowClipSpan> spansOf(Doc *doc, Track *track)
{
    QList<ShowClipSpan> clips;
    for (ShowFunction *sf : track->showFunctions())
    {
        if (doc->function(sf->functionID()) == nullptr)
            continue;
        ShowClipSpan clip;
        clip.id = sf->id();
        clip.startTime = sf->startTime();
        clip.duration = sf->duration();
        clips.append(clip);
    }
    return clips;
}

/** True when [$startTime, +$duration) on $track hits a clip other than $exceptId;
 *  $blockingId gets the first one in the way. */
bool overlapsOnTrack(Doc *doc, Track *track, quint32 exceptId, qint64 startTime, qint64 duration, quint32 *blockingId)
{
    QSet<quint32> moving;
    if (exceptId != UINT_MAX)
        moving.insert(exceptId);
    return ShowMoveHelper::firstBlocker(spansOf(doc, track), startTime, duration, moving, blockingId);
}

QJsonObject blockedDetails(Doc *doc, Track *track, quint32 exceptId, quint32 blockingId, qint64 startTime, qint64 duration)
{
    QSet<quint32> moving;
    if (exceptId != UINT_MAX)
        moving.insert(exceptId);
    QJsonObject details;
    details.insert(QStringLiteral("blockingItemId"), QString::number(blockingId));
    details.insert(QStringLiteral("suggestedStartTime"),
                   double(ShowMoveHelper::resolveCollision(spansOf(doc, track), startTime, duration, moving)));
    return details;
}

/*****************************************************************************
 * Ripple edits - a host-free mirror of ShowManager::insertTimeAtCursor /
 * cutTimeAtCursor and the helpers they use (qmlui/showmanager.cpp), without
 * the Tardis undo bookkeeping. Kept in the same order and with the same
 * type-specific rules so a client sees exactly what the Qt editor does.
 *****************************************************************************/

quint32 itemRelativeTimeFromCursor(const ShowFunction *sf, int cursorTime)
{
    const quint32 currentTimeValue = quint32(qMax(0, cursorTime));
    if (currentTimeValue <= sf->startTime())
        return 0;
    return qMin(sf->duration(), currentTimeValue - sf->startTime());
}

quint32 mapCursorToChaserTime(const ShowFunction *sf, Chaser *chaser, int cursorTime)
{
    quint32 itemRelativeTime = itemRelativeTimeFromCursor(sf, cursorTime);
    quint32 chaserRelativeTime = itemRelativeTime;
    quint32 chaserTotal = chaser->totalDuration();
    if (sf->duration() > 0 && chaserTotal > 0)
        chaserRelativeTime = quint32(qRound((double(itemRelativeTime) * double(chaserTotal)) / double(sf->duration())));
    return chaserRelativeTime;
}

quint32 chaserStepDuration(Chaser *chaser, int index)
{
    if (index < 0 || index >= chaser->stepsCount())
        return 0;
    if (chaser->durationMode() == Chaser::Common)
        return chaser->duration();
    ChaserStep *step = chaser->stepAt(index);
    return step ? step->duration : 0;
}

int chaserStepIndexFromTime(Chaser *chaser, quint32 timeValue)
{
    if (chaser->stepsCount() == 0)
        return -1;
    quint32 elapsed = 0;
    for (int i = 0; i < chaser->stepsCount(); ++i)
    {
        quint32 stepDuration = chaserStepDuration(chaser, i);
        if (stepDuration == 0)
            stepDuration = 1;
        if (timeValue < elapsed + stepDuration)
            return i;
        elapsed += stepDuration;
    }
    return chaser->stepsCount() - 1;
}

bool setChaserStepDuration(Chaser *chaser, int stepIndex, quint32 newDuration)
{
    if (stepIndex < 0 || stepIndex >= chaser->stepsCount())
        return false;
    ChaserStep *stepRef = chaser->stepAt(stepIndex);
    if (stepRef == nullptr)
        return false;
    ChaserStep step = *stepRef;
    newDuration = qMax(quint32(1), newDuration);
    if (step.duration == newDuration)
        return true;
    step.duration = newDuration;
    step.hold = Function::speedSubtract(step.duration, step.fadeIn);
    chaser->replaceStep(step, stepIndex);
    return true;
}

void convertChaserCommonToPerStep(Chaser *chaser)
{
    if (chaser->durationMode() != Chaser::Common)
        return;
    quint32 commonDuration = qMax(quint32(1), chaser->duration());
    chaser->setDurationMode(Chaser::PerStep);
    for (int i = 0; i < chaser->stepsCount(); ++i)
        setChaserStepDuration(chaser, i, commonDuration);
}

/** ShowManager::setShowItemStartTime without the undo entry */
bool setItemStartTimeChecked(Doc *doc, Show *show, ShowFunction *sf, int startTime)
{
    Track *track = show->getTrackFromShowFunctionID(sf->id());
    if (track == nullptr)
        return false;
    if (overlapsOnTrack(doc, track, sf->id(), startTime, sf->duration(), nullptr))
        return false;
    sf->setStartTime(quint32(startTime));
    return true;
}

bool moveAllItemsAfterCursor(Doc *doc, Show *show, int cursorTime, int delta)
{
    if (delta == 0)
        return true;

    QList<ShowFunction *> itemsToMove;
    for (Track *track : show->tracks())
    {
        for (ShowFunction *sf : track->showFunctions())
        {
            if (int(sf->startTime()) <= cursorTime)
                continue;
            itemsToMove.append(sf);
        }
    }

    std::sort(itemsToMove.begin(), itemsToMove.end(), [delta](ShowFunction *a, ShowFunction *b)
    {
        if (delta > 0)
            return a->startTime() > b->startTime();
        return a->startTime() < b->startTime();
    });

    for (ShowFunction *sf : itemsToMove)
    {
        int newStart = int(sf->startTime()) + delta;
        if (newStart < 0)
            newStart = 0;
        if (setItemStartTimeChecked(doc, show, sf, newStart) == false)
            return false;
    }
    return true;
}

bool insertItemTimeAt(Doc *doc, Show *show, ShowFunction *sf, int length, int cursorTime)
{
    if (length <= 0)
        return false;
    Function *func = doc->function(sf->functionID());
    if (func == nullptr)
        return false;
    Track *track = show->getTrackFromShowFunctionID(sf->id());
    if (track == nullptr)
        return false;

    int minDuration = minimumDuration(show);

    switch (func->type())
    {
        case Function::AudioType:
        case Function::VideoType:
        {
            if (func->runOrder() != Function::Loop)
                return false;
        }
        Q_FALLTHROUGH();
        case Function::SceneType:
        case Function::CollectionType:
        case Function::EFXType:
        case Function::RGBMatrixType:
        {
            int newDuration = int(sf->duration()) + length;
            if (newDuration < minDuration)
                newDuration = minDuration;
            if (overlapsOnTrack(doc, track, sf->id(), sf->startTime(), newDuration, nullptr))
                return false;
            sf->setDuration(quint32(newDuration));
            return true;
        }
        case Function::ChaserType:
        case Function::SequenceType:
        {
            Chaser *chaser = qobject_cast<Chaser *>(func);
            if (chaser == nullptr)
                return false;
            if (chaser->stepsCount() == 0 && func->type() != Function::SequenceType)
                return false;

            int newItemDuration = int(sf->duration()) + length;
            if (newItemDuration < minDuration)
                newItemDuration = minDuration;
            if (overlapsOnTrack(doc, track, sf->id(), sf->startTime(), newItemDuration, nullptr))
                return false;

            quint32 chaserRelativeTime = mapCursorToChaserTime(sf, chaser, cursorTime);
            int insertIndex = chaserStepIndexFromTime(chaser, chaserRelativeTime);
            if (insertIndex < 0)
                return false;

            convertChaserCommonToPerStep(chaser);

            ChaserStep *targetStepRef = chaser->stepAt(insertIndex);
            if (targetStepRef == nullptr)
                return false;
            if (setChaserStepDuration(chaser, insertIndex, targetStepRef->duration + quint32(length)) == false)
                return false;

            sf->setDuration(quint32(newItemDuration));
            return true;
        }
        default:
        break;
    }
    return false;
}

bool cutItemTimeAt(Doc *doc, Show *show, ShowFunction *sf, int length, int cursorTime)
{
    if (length <= 0)
        return false;
    Function *func = doc->function(sf->functionID());
    if (func == nullptr)
        return false;

    int minDuration = minimumDuration(show);
    int maxCutDuration = int(sf->duration()) - minDuration;
    if (maxCutDuration <= 0)
        return false;
    int targetCutDuration = qMin(length, maxCutDuration);

    switch (func->type())
    {
        case Function::AudioType:
        case Function::VideoType:
        {
            if (func->runOrder() != Function::Loop)
                return false;
        }
        Q_FALLTHROUGH();
        case Function::SceneType:
        case Function::CollectionType:
        case Function::EFXType:
        case Function::RGBMatrixType:
        {
            int newDuration = int(sf->duration()) - targetCutDuration;
            if (newDuration < minDuration)
                newDuration = minDuration;
            sf->setDuration(quint32(newDuration));
            return true;
        }
        case Function::ChaserType:
        case Function::SequenceType:
        {
            Chaser *chaser = qobject_cast<Chaser *>(func);
            if (chaser == nullptr || chaser->stepsCount() == 0)
                return false;

            convertChaserCommonToPerStep(chaser);

            quint32 chaserRelativeTime = mapCursorToChaserTime(sf, chaser, cursorTime);
            int cutStartIndex = chaserStepIndexFromTime(chaser, chaserRelativeTime);
            if (cutStartIndex < 0)
                return false;

            quint32 stepStartTime = 0;
            for (int i = 0; i < cutStartIndex; ++i)
                stepStartTime += qMax(quint32(1), chaserStepDuration(chaser, i));

            int cutRemaining = targetCutDuration;
            int cutDuration = 0;
            int stepIndex = cutStartIndex;
            quint32 cursorOffset = chaserRelativeTime > stepStartTime ? (chaserRelativeTime - stepStartTime) : 0;

            while (cutRemaining > 0 && stepIndex < chaser->stepsCount())
            {
                ChaserStep *stepRef = chaser->stepAt(stepIndex);
                if (stepRef == nullptr)
                    break;

                ChaserStep step = *stepRef;
                quint32 stepDuration = qMax(quint32(1), step.duration);
                quint32 offset = qMin(cursorOffset, stepDuration);
                int removable = (stepIndex == cutStartIndex) ? int(stepDuration - offset) : int(stepDuration);
                if (removable <= 0)
                {
                    cursorOffset = 0;
                    stepIndex++;
                    continue;
                }

                int consume = qMin(cutRemaining, removable);

                if (stepIndex == cutStartIndex && offset > 0)
                {
                    quint32 newStepDuration = stepDuration;
                    if (consume < removable)
                        newStepDuration = qMax(quint32(1), quint32(int(stepDuration) - consume));
                    else
                        newStepDuration = qMax(quint32(1), quint32(offset));
                    setChaserStepDuration(chaser, stepIndex, newStepDuration);

                    cutDuration += consume;
                    cutRemaining -= consume;
                    cursorOffset = 0;
                    if (consume < removable)
                        break;
                    stepIndex++;
                    continue;
                }

                if (consume < removable)
                {
                    setChaserStepDuration(chaser, stepIndex, qMax(quint32(1), quint32(int(stepDuration) - consume)));
                    cutDuration += consume;
                    cutRemaining = 0;
                    break;
                }

                if (chaser->stepsCount() <= 1)
                {
                    quint32 newStepDuration = 1;
                    int actualConsume = int(stepDuration - newStepDuration);
                    if (actualConsume <= 0)
                        break;
                    setChaserStepDuration(chaser, stepIndex, newStepDuration);
                    cutDuration += actualConsume;
                    cutRemaining -= actualConsume;
                    break;
                }

                if (chaser->removeStep(stepIndex) == false)
                    break;

                cutDuration += consume;
                cutRemaining -= consume;
            }

            if (cutDuration <= 0)
                return false;

            int newDuration = int(sf->duration()) - cutDuration;
            if (newDuration < minDuration)
                newDuration = minDuration;
            sf->setDuration(quint32(newDuration));
            return true;
        }
        default:
        break;
    }
    return false;
}

/** ShowManager::insertTimeAtCursor: returns true when the timeline changed */
bool insertTimeAtCursor(Doc *doc, Show *show, int length, int cursorTime)
{
    if (length <= 0)
        return false;

    bool hasTarget = false;
    bool hasItemsAfterCursor = false;
    for (Track *track : show->tracks())
    {
        for (ShowFunction *sf : track->showFunctions())
        {
            if (sf->isLocked())
                continue;
            int startTime = int(sf->startTime());
            if (startTime > cursorTime)
                hasItemsAfterCursor = true;
            int endTime = startTime + int(sf->duration());
            if (cursorTime < startTime || cursorTime > endTime)
                continue;
            hasTarget = true;
            if (hasItemsAfterCursor)
                break;
        }
        if (hasTarget && hasItemsAfterCursor)
            break;
    }

    if (!hasTarget && !hasItemsAfterCursor)
        return false;

    if (moveAllItemsAfterCursor(doc, show, cursorTime, length) == false)
        return false;

    bool changed = false;
    for (Track *track : show->tracks())
    {
        for (ShowFunction *sf : track->showFunctions())
        {
            if (sf->isLocked())
                continue;
            int startTime = int(sf->startTime());
            int endTime = startTime + int(sf->duration());
            if (cursorTime < startTime || cursorTime > endTime)
                continue;
            changed |= insertItemTimeAt(doc, show, sf, length, cursorTime);
        }
    }

    if (hasTarget && !hasItemsAfterCursor && !changed)
        moveAllItemsAfterCursor(doc, show, cursorTime, -length);

    return changed || hasItemsAfterCursor;
}

/** ShowManager::cutTimeAtCursor: returns true when the timeline changed */
bool cutTimeAtCursor(Doc *doc, Show *show, int length, int cursorTime)
{
    if (length <= 0)
        return false;

    bool changed = false;
    for (Track *track : show->tracks())
    {
        for (ShowFunction *sf : track->showFunctions())
        {
            if (sf->isLocked())
                continue;
            int startTime = int(sf->startTime());
            int endTime = startTime + int(sf->duration());
            if (cursorTime < startTime || cursorTime > endTime)
                continue;
            changed |= cutItemTimeAt(doc, show, sf, length, cursorTime);
        }
    }

    if (!changed)
        return false;

    moveAllItemsAfterCursor(doc, show, cursorTime, -length);
    return changed;
}

/** RFC 6902 patch of every startTime/duration that differs between two
 *  snapshots of the same track/item layout (what the ripples change). */
QJsonArray timelinePatch(const QJsonArray &before, const QJsonArray &after)
{
    QJsonArray patch;
    for (int t = 0; t < after.count() && t < before.count(); t++)
    {
        QJsonArray itemsBefore = before.at(t).toObject().value(QStringLiteral("items")).toArray();
        QJsonArray itemsAfter = after.at(t).toObject().value(QStringLiteral("items")).toArray();
        for (int i = 0; i < itemsAfter.count() && i < itemsBefore.count(); i++)
        {
            QJsonObject a = itemsBefore.at(i).toObject();
            QJsonObject b = itemsAfter.at(i).toObject();
            for (const QString &key : { QStringLiteral("startTime"), QStringLiteral("duration") })
            {
                if (a.value(key) == b.value(key))
                    continue;
                QJsonObject op;
                op.insert(QStringLiteral("op"), QStringLiteral("replace"));
                op.insert(QStringLiteral("path"), QStringLiteral("/tracks/%1/items/%2/%3").arg(t).arg(i).arg(key));
                op.insert(QStringLiteral("value"), b.value(key));
                patch.append(op);
            }
        }
    }
    return patch;
}

/** RFC 6902 "replace the whole tracks array" - for the edits that reorder
 *  tracks or touch every one of them. */
QJsonArray replaceTracksPatch(const QJsonArray &tracks)
{
    QJsonObject op;
    op.insert(QStringLiteral("op"), QStringLiteral("replace"));
    op.insert(QStringLiteral("path"), QStringLiteral("/tracks"));
    op.insert(QStringLiteral("value"), tracks);
    QJsonArray patch;
    patch.append(op);
    return patch;
}

} // namespace

/*****************************************************************************
 * ApiShowDomain
 *****************************************************************************/

ApiShowDomain::ApiShowDomain(Doc *doc, ApiServer *server, QObject *parent)
    : QObject(parent)
    , m_doc(doc)
    , m_server(server)
{
    Q_ASSERT(m_doc != nullptr);
    Q_ASSERT(m_server != nullptr);

    m_clock.start();

    Doc *d = m_doc;
    ApiFunctionsDomain::setTypeDetailProvider(int(Function::ShowType), [d](Function *function)
    {
        return detailToJson(d, qobject_cast<Show *>(function));
    });

    // engine-DLL objects: string-based connections (see apiiodomain.cpp)
    connect(m_doc, SIGNAL(functionAdded(quint32)), this, SLOT(slotFunctionAdded(quint32)));
    for (Function *function : m_doc->functions())
        watchShow(function);

    registerMethods();
}

QJsonObject ApiShowDomain::itemToJson(Doc *doc, ShowFunction *sf)
{
    QJsonObject obj;
    obj.insert(QStringLiteral("id"), QString::number(sf->id()));
    obj.insert(QStringLiteral("functionId"), QString::number(sf->functionID()));
    Function *function = doc->function(sf->functionID());
    if (function != nullptr)
    {
        obj.insert(QStringLiteral("functionType"), Function::typeToString(function->type()));
        obj.insert(QStringLiteral("functionName"), function->name());
    }
    obj.insert(QStringLiteral("startTime"), double(sf->startTime()));
    obj.insert(QStringLiteral("duration"), double(sf->duration()));
    // FunctionsShowItem.color is a required string: a ShowFunction loaded
    // without a Color attribute has an invalid QColor, shown by the Qt
    // editor with the type's default colour - report that one.
    QColor color = sf->color();
    if (color.isValid() == false)
        color = ShowFunction::defaultColor(function != nullptr ? function->type() : Function::Undefined);
    obj.insert(QStringLiteral("color"), color.name());
    obj.insert(QStringLiteral("locked"), sf->isLocked());
    return obj;
}

QJsonObject ApiShowDomain::trackToJson(Doc *doc, Track *track)
{
    QJsonObject obj;
    obj.insert(QStringLiteral("id"), QString::number(track->id()));
    obj.insert(QStringLiteral("name"), track->name());
    if (track->getSceneID() != Function::invalidId())
        obj.insert(QStringLiteral("sceneId"), QString::number(track->getSceneID()));
    obj.insert(QStringLiteral("mute"), track->isMute());
    QJsonArray items;
    for (ShowFunction *sf : track->showFunctions())
        items.append(itemToJson(doc, sf));
    obj.insert(QStringLiteral("items"), items);
    // Spout output size block (apishowpreviewdomain.cpp), only on tracks
    // holding Spout-mode Videos or a fixed size
    QJsonObject spout = ApiShowPreviewDomain::trackSpoutJson(doc, track);
    if (spout.isEmpty() == false)
        obj.insert(QStringLiteral("spout"), spout);
    return obj;
}

QJsonArray ApiShowDomain::tracksToJson(Doc *doc, Show *show)
{
    QJsonArray tracks;
    for (Track *track : show->tracks())
        tracks.append(trackToJson(doc, track));
    return tracks;
}

QJsonObject ApiShowDomain::detailToJson(Doc *doc, Show *show)
{
    QJsonObject obj;
    if (show == nullptr)
        return obj;
    obj.insert(QStringLiteral("functionId"), QString::number(show->id()));
    obj.insert(QStringLiteral("timeDivisionType"), timeDivisionToString(show->timeDivisionType()));
    obj.insert(QStringLiteral("timeDivisionBPM"), show->timeDivisionBPM());
    obj.insert(QStringLiteral("tracks"), tracksToJson(doc, show));
    obj.insert(QStringLiteral("totalDuration"), double(show->totalDuration()));
    // runtime: frozen at the cursor (functions.show.preview or the desktop's preview)
    obj.insert(QStringLiteral("previewing"), show->isScrubMode());
    obj.insert(QStringLiteral("docRevision"), int(doc->docRevision()));
    return obj;
}

/*****************************************************************************
 * Playhead
 *****************************************************************************/

void ApiShowDomain::watchShow(Function *function)
{
    if (function == nullptr || function->type() != Function::ShowType)
        return;
    connect(function, SIGNAL(timeChanged(quint32)), this, SLOT(slotShowTimeChanged(quint32)), Qt::UniqueConnection);
    connect(function, SIGNAL(stopped(quint32)), this, SLOT(slotShowStopped(quint32)), Qt::UniqueConnection);
}

void ApiShowDomain::slotFunctionAdded(quint32 id)
{
    watchShow(m_doc->function(id));
}

void ApiShowDomain::slotShowTimeChanged(quint32 ms)
{
    Show *show = qobject_cast<Show *>(sender());
    if (show == nullptr)
        return;

    quint32 showId = show->id();
    qint64 now = m_clock.elapsed();
    bool first = m_playheadSentAt.contains(showId) == false;
    bool rewound = !first && ms < m_playheadSentTime.value(showId);
    if (!first && !rewound && now - m_playheadSentAt.value(showId) < PlayheadIntervalMs)
        return;

    m_playheadSentAt.insert(showId, now);
    m_playheadSentTime.insert(showId, ms);

    QJsonObject data;
    data.insert(QStringLiteral("functionId"), QString::number(showId));
    data.insert(QStringLiteral("time"), double(ms));
    m_server->broadcast(QStringLiteral("functions.show.%1.playhead").arg(showId), data, QString(), true);
}

void ApiShowDomain::slotShowStopped(quint32 id)
{
    // the next start sends its first tick right away again
    m_playheadSentAt.remove(id);
    m_playheadSentTime.remove(id);
}

/*****************************************************************************
 * Methods
 *****************************************************************************/

void ApiShowDomain::registerMethods()
{
    ApiDispatcher *dispatcher = m_server->dispatcher();
    Doc *doc = m_doc;
    ApiServer *server = m_server;

    /********************* time division *********************/

    dispatcher->registerMethod(QStringLiteral("functions.show.setTimeDivision"), [doc, server](ApiSession *session, const QString &id, const QJsonObject &params)
    {
        Show *show = findShowOrRespond(doc, params, session, id);
        if (show == nullptr)
            return;
        if (checkBaseRevision(doc, params, session, id) == false)
            return;

        Show::TimeDivision type;
        QString typeStr = params.value(QStringLiteral("timeDivisionType")).toString();
        if (timeDivisionFromString(typeStr, type) == false)
        {
            session->send(ApiEnvelope::buildErrorResponse(id, ApiEnvelope::ErrInvalidParams,
                                                            QStringLiteral("Invalid timeDivisionType '%1'").arg(typeStr)));
            return;
        }
        int bpm = show->timeDivisionBPM();
        if (params.contains(QStringLiteral("bpm")))
        {
            bpm = params.value(QStringLiteral("bpm")).toInt();
            if (bpm <= 0)
            {
                session->send(ApiEnvelope::buildErrorResponse(id, ApiEnvelope::ErrInvalidParams,
                                                                QStringLiteral("bpm must be a positive integer")));
                return;
            }
        }
        else if (type != Show::Time)
        {
            session->send(ApiEnvelope::buildErrorResponse(id, ApiEnvelope::ErrInvalidParams,
                                                            QStringLiteral("bpm is required for a BPM time division")));
            return;
        }

        // ShowManager::setTimeDivision + setTimeDivisionBPM: the Show's own
        // tempo follows the division so ShowRunner schedules by beats
        show->setTimeDivisionType(type);
        show->setTempoType(type == Show::Time ? Function::Time : Function::Beats);
        show->setTimeDivisionBPM(bpm);
        doc->setModified();
        session->send(ApiEnvelope::buildOkResponse(id, docRevisionResult(doc)));

        QJsonObject data;
        data.insert(QStringLiteral("functionId"), QString::number(show->id()));
        data.insert(QStringLiteral("timeDivisionType"), timeDivisionToString(show->timeDivisionType()));
        data.insert(QStringLiteral("timeDivisionBPM"), show->timeDivisionBPM());
        data.insert(QStringLiteral("docRevision"), int(doc->docRevision()));
        server->broadcast(QStringLiteral("functions.show.timeDivisionChanged"), data, session->clientId(), false);
    });

    /********************* tracks *********************/

    dispatcher->registerMethod(QStringLiteral("functions.show.track.add"), [doc, server](ApiSession *session, const QString &id, const QJsonObject &params)
    {
        Show *show = findShowOrRespond(doc, params, session, id);
        if (show == nullptr)
            return;
        if (checkBaseRevision(doc, params, session, id) == false)
            return;

        quint32 sceneId = Function::invalidId();
        if (params.contains(QStringLiteral("sceneId")) && params.value(QStringLiteral("sceneId")).isNull() == false)
        {
            QString sceneStr = params.value(QStringLiteral("sceneId")).toString();
            bool ok = false;
            sceneId = sceneStr.toUInt(&ok);
            Function *scene = ok ? doc->function(sceneId) : nullptr;
            if (scene == nullptr)
            {
                session->send(ApiEnvelope::buildErrorResponse(id, ApiEnvelope::ErrNotFound,
                                                                QStringLiteral("No function with id %1").arg(sceneStr)));
                return;
            }
            if (scene->type() != Function::SceneType)
            {
                session->send(ApiEnvelope::buildErrorResponse(id, ApiEnvelope::ErrInvalidParams,
                                                                QStringLiteral("Function %1 is not a Scene").arg(sceneStr)));
                return;
            }
        }

        QString name = params.value(QStringLiteral("name")).toString();
        if (name.isEmpty())
            name = QStringLiteral("Track %1").arg(show->tracks().count() + 1);

        // parented to the Show like ShowManager::addItems does, so that
        // createShowFunction() draws its item ids from the Show; named before
        // addTrack(), which registers the Show attribute from the name
        Track *track = new Track(sceneId, show);
        track->setName(name);
        show->addTrack(track);
        doc->setModified();

        QJsonObject result = docRevisionResult(doc);
        result.insert(QStringLiteral("trackId"), QString::number(track->id()));
        session->send(ApiEnvelope::buildOkResponse(id, result));

        QJsonObject data;
        data.insert(QStringLiteral("showId"), QString::number(show->id()));
        data.insert(QStringLiteral("track"), trackToJson(doc, track));
        data.insert(QStringLiteral("docRevision"), int(doc->docRevision()));
        server->broadcast(QStringLiteral("functions.show.track.added"), data, session->clientId(), false);
    });

    dispatcher->registerMethod(QStringLiteral("functions.show.track.remove"), [doc, server](ApiSession *session, const QString &id, const QJsonObject &params)
    {
        Show *show = findShowOrRespond(doc, params, session, id);
        if (show == nullptr)
            return;
        Track *track = findTrackOrRespond(show, params.value(QStringLiteral("trackId")), session, id);
        if (track == nullptr)
            return;
        if (checkBaseRevision(doc, params, session, id) == false)
            return;

        quint32 trackId = track->id();
        show->removeTrack(trackId); // deletes the Track and its ShowFunctions
        doc->setModified();
        session->send(ApiEnvelope::buildOkResponse(id, docRevisionResult(doc)));

        QJsonObject data;
        data.insert(QStringLiteral("showId"), QString::number(show->id()));
        data.insert(QStringLiteral("trackId"), QString::number(trackId));
        data.insert(QStringLiteral("docRevision"), int(doc->docRevision()));
        server->broadcast(QStringLiteral("functions.show.track.removed"), data, session->clientId(), false);
    });

    dispatcher->registerMethod(QStringLiteral("functions.show.track.rename"), [doc, server](ApiSession *session, const QString &id, const QJsonObject &params)
    {
        Show *show = findShowOrRespond(doc, params, session, id);
        if (show == nullptr)
            return;
        Track *track = findTrackOrRespond(show, params.value(QStringLiteral("trackId")), session, id);
        if (track == nullptr)
            return;
        if (checkBaseRevision(doc, params, session, id) == false)
            return;
        QString name = params.value(QStringLiteral("name")).toString();
        if (name.isEmpty())
        {
            session->send(ApiEnvelope::buildErrorResponse(id, ApiEnvelope::ErrInvalidParams,
                                                            QStringLiteral("name must be a non-empty string")));
            return;
        }

        track->setName(name);
        doc->setModified();
        session->send(ApiEnvelope::buildOkResponse(id, docRevisionResult(doc)));

        QJsonObject data;
        data.insert(QStringLiteral("showId"), QString::number(show->id()));
        data.insert(QStringLiteral("trackId"), QString::number(track->id()));
        data.insert(QStringLiteral("name"), track->name());
        data.insert(QStringLiteral("docRevision"), int(doc->docRevision()));
        server->broadcast(QStringLiteral("functions.show.track.renamed"), data, session->clientId(), false);
    });

    dispatcher->registerMethod(QStringLiteral("functions.show.track.setMute"), [doc, server](ApiSession *session, const QString &id, const QJsonObject &params)
    {
        Show *show = findShowOrRespond(doc, params, session, id);
        if (show == nullptr)
            return;
        Track *track = findTrackOrRespond(show, params.value(QStringLiteral("trackId")), session, id);
        if (track == nullptr)
            return;
        if (checkBaseRevision(doc, params, session, id) == false)
            return;
        QJsonValue mute = params.value(QStringLiteral("mute"));
        if (mute.isBool() == false)
        {
            session->send(ApiEnvelope::buildErrorResponse(id, ApiEnvelope::ErrInvalidParams,
                                                            QStringLiteral("mute must be a boolean")));
            return;
        }

        track->setMute(mute.toBool());
        doc->setModified();
        session->send(ApiEnvelope::buildOkResponse(id, docRevisionResult(doc)));

        QJsonObject data;
        data.insert(QStringLiteral("showId"), QString::number(show->id()));
        data.insert(QStringLiteral("trackId"), QString::number(track->id()));
        data.insert(QStringLiteral("mute"), track->isMute());
        data.insert(QStringLiteral("docRevision"), int(doc->docRevision()));
        server->broadcast(QStringLiteral("functions.show.track.muteChanged"), data, session->clientId(), false);
    });

    dispatcher->registerMethod(QStringLiteral("functions.show.track.setSolo"), [doc, server](ApiSession *session, const QString &id, const QJsonObject &params)
    {
        Show *show = findShowOrRespond(doc, params, session, id);
        if (show == nullptr)
            return;
        Track *track = findTrackOrRespond(show, params.value(QStringLiteral("trackId")), session, id);
        if (track == nullptr)
            return;
        if (checkBaseRevision(doc, params, session, id) == false)
            return;
        QJsonValue solo = params.value(QStringLiteral("solo"));
        if (solo.isBool() == false)
        {
            session->send(ApiEnvelope::buildErrorResponse(id, ApiEnvelope::ErrInvalidParams,
                                                            QStringLiteral("solo must be a boolean")));
            return;
        }

        // ShowManager::setTrackSolo: the soloed track is unmuted and every
        // other one muted; solo=false unmutes them all
        for (Track *other : show->tracks())
            other->setMute(other == track ? false : solo.toBool());
        doc->setModified();
        session->send(ApiEnvelope::buildOkResponse(id, docRevisionResult(doc)));

        QJsonObject data;
        data.insert(QStringLiteral("showId"), QString::number(show->id()));
        data.insert(QStringLiteral("patch"), replaceTracksPatch(tracksToJson(doc, show)));
        data.insert(QStringLiteral("docRevision"), int(doc->docRevision()));
        server->broadcast(QStringLiteral("functions.show.tracksChanged"), data, session->clientId(), false);
    });

    dispatcher->registerMethod(QStringLiteral("functions.show.track.move"), [doc, server](ApiSession *session, const QString &id, const QJsonObject &params)
    {
        Show *show = findShowOrRespond(doc, params, session, id);
        if (show == nullptr)
            return;
        Track *track = findTrackOrRespond(show, params.value(QStringLiteral("trackId")), session, id);
        if (track == nullptr)
            return;
        if (checkBaseRevision(doc, params, session, id) == false)
            return;
        QString direction = params.value(QStringLiteral("direction")).toString();
        if (direction != QStringLiteral("up") && direction != QStringLiteral("down"))
        {
            session->send(ApiEnvelope::buildErrorResponse(id, ApiEnvelope::ErrInvalidParams,
                                                            QStringLiteral("direction must be 'up' or 'down'")));
            return;
        }

        QList<Track *> tracks = show->tracks();
        int index = tracks.indexOf(track);
        int dir = direction == QStringLiteral("up") ? -1 : 1;
        if ((dir < 0 && index <= 0) || (dir > 0 && index >= tracks.count() - 1))
        {
            session->send(ApiEnvelope::buildErrorResponse(id, ApiEnvelope::ErrInvalidParams,
                                                            QStringLiteral("Track is already at the %1").arg(dir < 0 ? QStringLiteral("top") : QStringLiteral("bottom"))));
            return;
        }

        // Show::moveTrack swaps the ids of the two tracks: the moved Track
        // object keeps its name/items but answers to the other id from now on
        show->moveTrack(track, dir);
        doc->setModified();

        QJsonObject result = docRevisionResult(doc);
        result.insert(QStringLiteral("trackId"), QString::number(track->id()));
        session->send(ApiEnvelope::buildOkResponse(id, result));

        QJsonObject data;
        data.insert(QStringLiteral("showId"), QString::number(show->id()));
        data.insert(QStringLiteral("trackId"), QString::number(track->id()));
        data.insert(QStringLiteral("patch"), replaceTracksPatch(tracksToJson(doc, show)));
        data.insert(QStringLiteral("docRevision"), int(doc->docRevision()));
        server->broadcast(QStringLiteral("functions.show.tracksChanged"), data, session->clientId(), false);
    });

    /********************* items *********************/

    dispatcher->registerMethod(QStringLiteral("functions.show.item.add"), [doc, server](ApiSession *session, const QString &id, const QJsonObject &params)
    {
        Show *show = findShowOrRespond(doc, params, session, id);
        if (show == nullptr)
            return;
        Track *track = findTrackOrRespond(show, params.value(QStringLiteral("trackId")), session, id);
        if (track == nullptr)
            return;
        if (checkBaseRevision(doc, params, session, id) == false)
            return;

        QString fidStr = params.value(QStringLiteral("functionId")).toString();
        bool ok = false;
        quint32 fid = fidStr.toUInt(&ok);
        Function *function = ok ? doc->function(fid) : nullptr;
        if (function == nullptr)
        {
            session->send(ApiEnvelope::buildErrorResponse(id, ApiEnvelope::ErrNotFound,
                                                            QStringLiteral("No function with id %1").arg(fidStr)));
            return;
        }
        // the Show itself, or anything that (transitively) plays it
        if (function->id() == show->id() || function->contains(show->id()))
        {
            session->send(ApiEnvelope::buildErrorResponse(id, ApiEnvelope::ErrInvalidParams,
                                                            QStringLiteral("Cannot place function %1 on a timeline of Show %2: it contains that Show").arg(fidStr).arg(show->id())));
            return;
        }
        if (params.value(QStringLiteral("startTime")).isDouble() == false)
        {
            session->send(ApiEnvelope::buildErrorResponse(id, ApiEnvelope::ErrInvalidParams,
                                                            QStringLiteral("startTime (ms) is required")));
            return;
        }
        qint64 startTime = qMax<qint64>(0, qint64(params.value(QStringLiteral("startTime")).toDouble()));

        // ShowManager::createShowItem: the Function's own length, or a fixed
        // default when it has none (a looping Chaser/Scene reports 0)
        qint64 duration = params.contains(QStringLiteral("duration"))
                ? qint64(params.value(QStringLiteral("duration")).toDouble()) : 0;
        if (duration <= 0)
            duration = function->totalDuration() ? function->totalDuration()
                                                 : (show->timeDivisionType() == Show::Time ? 5000 : 4000);
        if (duration < minimumDuration(show))
        {
            session->send(ApiEnvelope::buildErrorResponse(id, ApiEnvelope::ErrInvalidParams,
                                                            QStringLiteral("duration must be at least %1 ms").arg(minimumDuration(show))));
            return;
        }

        QColor color = ShowFunction::defaultColor(function->type());
        if (params.contains(QStringLiteral("color")))
        {
            color = QColor(params.value(QStringLiteral("color")).toString());
            if (color.isValid() == false)
            {
                session->send(ApiEnvelope::buildErrorResponse(id, ApiEnvelope::ErrInvalidParams,
                                                                QStringLiteral("Invalid color '%1'").arg(params.value(QStringLiteral("color")).toString())));
                return;
            }
        }

        quint32 blockingId = 0;
        if (overlapsOnTrack(doc, track, UINT_MAX, startTime, duration, &blockingId))
        {
            session->send(ApiEnvelope::buildErrorResponse(id, ApiEnvelope::ErrInvalidParams,
                                                            QStringLiteral("The item would overlap item %1 on track %2").arg(blockingId).arg(track->id()),
                                                            blockedDetails(doc, track, UINT_MAX, blockingId, startTime, duration)));
            return;
        }

        ShowFunction *sf = track->createShowFunction(function->id());
        sf->setStartTime(quint32(startTime));
        sf->setDuration(quint32(duration));
        sf->setColor(color);
        doc->setModified();

        QJsonObject result = docRevisionResult(doc);
        result.insert(QStringLiteral("itemId"), QString::number(sf->id()));
        session->send(ApiEnvelope::buildOkResponse(id, result));

        QJsonObject data;
        data.insert(QStringLiteral("showId"), QString::number(show->id()));
        data.insert(QStringLiteral("trackId"), QString::number(track->id()));
        data.insert(QStringLiteral("item"), itemToJson(doc, sf));
        data.insert(QStringLiteral("docRevision"), int(doc->docRevision()));
        server->broadcast(QStringLiteral("functions.show.item.added"), data, session->clientId(), false);
    });

    dispatcher->registerMethod(QStringLiteral("functions.show.item.remove"), [doc, server](ApiSession *session, const QString &id, const QJsonObject &params)
    {
        Show *show = findShowOrRespond(doc, params, session, id);
        if (show == nullptr)
            return;
        if (checkBaseRevision(doc, params, session, id) == false)
            return;

        // all-or-nothing: every id must exist before anything is deleted
        QList<ShowFunction *> items;
        QJsonArray itemIds;
        for (const QJsonValue &v : params.value(QStringLiteral("itemIds")).toArray())
        {
            ShowFunction *sf = findItemOrRespond(show, v, session, id);
            if (sf == nullptr)
                return;
            if (items.contains(sf))
                continue;
            items.append(sf);
            itemIds.append(QString::number(sf->id()));
        }
        if (items.isEmpty())
        {
            session->send(ApiEnvelope::buildErrorResponse(id, ApiEnvelope::ErrInvalidParams,
                                                            QStringLiteral("itemIds must name at least one item")));
            return;
        }

        for (ShowFunction *sf : items)
        {
            Track *track = show->getTrackFromShowFunctionID(sf->id());
            if (track != nullptr)
                track->removeShowFunction(sf, true);
        }
        doc->setModified();
        session->send(ApiEnvelope::buildOkResponse(id, docRevisionResult(doc)));

        QJsonObject data;
        data.insert(QStringLiteral("showId"), QString::number(show->id()));
        data.insert(QStringLiteral("itemIds"), itemIds);
        data.insert(QStringLiteral("docRevision"), int(doc->docRevision()));
        server->broadcast(QStringLiteral("functions.show.item.removed"), data, session->clientId(), false);
    });

    dispatcher->registerMethod(QStringLiteral("functions.show.item.move"), [doc, server](ApiSession *session, const QString &id, const QJsonObject &params)
    {
        Show *show = findShowOrRespond(doc, params, session, id);
        if (show == nullptr)
            return;
        ShowFunction *sf = findItemOrRespond(show, params.value(QStringLiteral("itemId")), session, id);
        if (sf == nullptr)
            return;
        Track *dstTrack = findTrackOrRespond(show, params.value(QStringLiteral("trackId")), session, id);
        if (dstTrack == nullptr)
            return;
        if (checkBaseRevision(doc, params, session, id) == false)
            return;
        if (params.value(QStringLiteral("startTime")).isDouble() == false)
        {
            session->send(ApiEnvelope::buildErrorResponse(id, ApiEnvelope::ErrInvalidParams,
                                                            QStringLiteral("startTime (ms) is required")));
            return;
        }
        if (sf->isLocked())
        {
            session->send(ApiEnvelope::buildErrorResponse(id, ApiEnvelope::ErrInvalidParams,
                                                            QStringLiteral("Item %1 is locked").arg(sf->id())));
            return;
        }

        qint64 startTime = qMax<qint64>(0, qint64(params.value(QStringLiteral("startTime")).toDouble()));
        quint32 blockingId = 0;
        if (overlapsOnTrack(doc, dstTrack, sf->id(), startTime, sf->duration(), &blockingId))
        {
            session->send(ApiEnvelope::buildErrorResponse(id, ApiEnvelope::ErrInvalidParams,
                                                            QStringLiteral("The item would overlap item %1 on track %2").arg(blockingId).arg(dstTrack->id()),
                                                            blockedDetails(doc, dstTrack, sf->id(), blockingId, startTime, sf->duration())));
            return;
        }

        Track *srcTrack = show->getTrackFromShowFunctionID(sf->id());
        sf->setStartTime(quint32(startTime));
        if (srcTrack != nullptr && srcTrack != dstTrack)
        {
            // ShowManager::moveShowItemToTrack
            srcTrack->removeShowFunction(sf, false);
            dstTrack->addShowFunction(sf);
        }
        doc->setModified();
        session->send(ApiEnvelope::buildOkResponse(id, docRevisionResult(doc)));

        QJsonObject data;
        data.insert(QStringLiteral("showId"), QString::number(show->id()));
        data.insert(QStringLiteral("itemId"), QString::number(sf->id()));
        data.insert(QStringLiteral("trackId"), QString::number(dstTrack->id()));
        data.insert(QStringLiteral("startTime"), double(sf->startTime()));
        data.insert(QStringLiteral("docRevision"), int(doc->docRevision()));
        server->broadcast(QStringLiteral("functions.show.item.moved"), data, session->clientId(), false);
    });

    dispatcher->registerMethod(QStringLiteral("functions.show.item.resize"), [doc, server](ApiSession *session, const QString &id, const QJsonObject &params)
    {
        Show *show = findShowOrRespond(doc, params, session, id);
        if (show == nullptr)
            return;
        ShowFunction *sf = findItemOrRespond(show, params.value(QStringLiteral("itemId")), session, id);
        if (sf == nullptr)
            return;
        if (checkBaseRevision(doc, params, session, id) == false)
            return;
        if (sf->isLocked())
        {
            session->send(ApiEnvelope::buildErrorResponse(id, ApiEnvelope::ErrInvalidParams,
                                                            QStringLiteral("Item %1 is locked").arg(sf->id())));
            return;
        }
        qint64 duration = qint64(params.value(QStringLiteral("duration")).toDouble());
        if (duration < minimumDuration(show))
        {
            session->send(ApiEnvelope::buildErrorResponse(id, ApiEnvelope::ErrInvalidParams,
                                                            QStringLiteral("duration must be at least %1 ms").arg(minimumDuration(show))));
            return;
        }

        Track *track = show->getTrackFromShowFunctionID(sf->id());
        quint32 blockingId = 0;
        if (track != nullptr && overlapsOnTrack(doc, track, sf->id(), sf->startTime(), duration, &blockingId))
        {
            // the largest duration that still fits: up to the first clip
            // starting at or after this one (the blocker is one of them)
            qint64 maxDuration = duration;
            for (const ShowClipSpan &clip : spansOf(doc, track))
            {
                if (clip.id == sf->id() || clip.startTime < qint64(sf->startTime()))
                    continue;
                maxDuration = qMin(maxDuration, clip.startTime - qint64(sf->startTime()));
            }
            QJsonObject details;
            details.insert(QStringLiteral("blockingItemId"), QString::number(blockingId));
            details.insert(QStringLiteral("maxDuration"), double(qMax<qint64>(0, maxDuration)));
            session->send(ApiEnvelope::buildErrorResponse(id, ApiEnvelope::ErrInvalidParams,
                                                            QStringLiteral("The item would overlap item %1").arg(blockingId), details));
            return;
        }

        sf->setDuration(quint32(duration));
        doc->setModified();
        session->send(ApiEnvelope::buildOkResponse(id, docRevisionResult(doc)));

        QJsonObject data;
        data.insert(QStringLiteral("showId"), QString::number(show->id()));
        data.insert(QStringLiteral("itemId"), QString::number(sf->id()));
        data.insert(QStringLiteral("duration"), double(sf->duration()));
        data.insert(QStringLiteral("docRevision"), int(doc->docRevision()));
        server->broadcast(QStringLiteral("functions.show.item.resized"), data, session->clientId(), false);
    });

    dispatcher->registerMethod(QStringLiteral("functions.show.item.setColor"), [doc, server](ApiSession *session, const QString &id, const QJsonObject &params)
    {
        Show *show = findShowOrRespond(doc, params, session, id);
        if (show == nullptr)
            return;
        ShowFunction *sf = findItemOrRespond(show, params.value(QStringLiteral("itemId")), session, id);
        if (sf == nullptr)
            return;
        if (checkBaseRevision(doc, params, session, id) == false)
            return;
        QColor color(params.value(QStringLiteral("color")).toString());
        if (color.isValid() == false)
        {
            session->send(ApiEnvelope::buildErrorResponse(id, ApiEnvelope::ErrInvalidParams,
                                                            QStringLiteral("Invalid color '%1'").arg(params.value(QStringLiteral("color")).toString())));
            return;
        }

        sf->setColor(color);
        doc->setModified();
        session->send(ApiEnvelope::buildOkResponse(id, docRevisionResult(doc)));

        QJsonObject data;
        data.insert(QStringLiteral("showId"), QString::number(show->id()));
        data.insert(QStringLiteral("itemId"), QString::number(sf->id()));
        data.insert(QStringLiteral("color"), sf->color().name());
        data.insert(QStringLiteral("docRevision"), int(doc->docRevision()));
        server->broadcast(QStringLiteral("functions.show.item.colorChanged"), data, session->clientId(), false);
    });

    dispatcher->registerMethod(QStringLiteral("functions.show.item.setLocked"), [doc, server](ApiSession *session, const QString &id, const QJsonObject &params)
    {
        Show *show = findShowOrRespond(doc, params, session, id);
        if (show == nullptr)
            return;
        ShowFunction *sf = findItemOrRespond(show, params.value(QStringLiteral("itemId")), session, id);
        if (sf == nullptr)
            return;
        if (checkBaseRevision(doc, params, session, id) == false)
            return;
        QJsonValue locked = params.value(QStringLiteral("locked"));
        if (locked.isBool() == false)
        {
            session->send(ApiEnvelope::buildErrorResponse(id, ApiEnvelope::ErrInvalidParams,
                                                            QStringLiteral("locked must be a boolean")));
            return;
        }

        sf->setLocked(locked.toBool());
        doc->setModified();
        session->send(ApiEnvelope::buildOkResponse(id, docRevisionResult(doc)));

        QJsonObject data;
        data.insert(QStringLiteral("showId"), QString::number(show->id()));
        data.insert(QStringLiteral("itemId"), QString::number(sf->id()));
        data.insert(QStringLiteral("locked"), sf->isLocked());
        data.insert(QStringLiteral("docRevision"), int(doc->docRevision()));
        server->broadcast(QStringLiteral("functions.show.item.lockedChanged"), data, session->clientId(), false);
    });

    /********************* ripple edits *********************/

    // Both ripples share the same shape: validate, snapshot, edit, answer
    // {docRevision, changed} and broadcast itemsChanged with the diff. A
    // ripple that finds nothing at the cursor is not an error (the Qt
    // editor's buttons do nothing then either): changed=false, no bump.
    auto ripple = [doc, server](bool insert, ApiSession *session, const QString &id, const QJsonObject &params)
    {
        Show *show = findShowOrRespond(doc, params, session, id);
        if (show == nullptr)
            return;
        if (checkBaseRevision(doc, params, session, id) == false)
            return;
        if (params.value(QStringLiteral("cursorTime")).isDouble() == false ||
            params.value(QStringLiteral("length")).isDouble() == false)
        {
            session->send(ApiEnvelope::buildErrorResponse(id, ApiEnvelope::ErrInvalidParams,
                                                            QStringLiteral("cursorTime and length (ms) are required")));
            return;
        }
        int cursorTime = qMax(0, params.value(QStringLiteral("cursorTime")).toInt());
        int length = params.value(QStringLiteral("length")).toInt();
        if (length < minimumDuration(show))
        {
            session->send(ApiEnvelope::buildErrorResponse(id, ApiEnvelope::ErrInvalidParams,
                                                            QStringLiteral("length must be at least %1 ms").arg(minimumDuration(show))));
            return;
        }

        QJsonArray before = tracksToJson(doc, show);
        bool changed = insert ? insertTimeAtCursor(doc, show, length, cursorTime)
                              : cutTimeAtCursor(doc, show, length, cursorTime);
        if (changed)
            doc->setModified();

        QJsonObject result = docRevisionResult(doc);
        result.insert(QStringLiteral("changed"), changed);
        session->send(ApiEnvelope::buildOkResponse(id, result));

        if (changed == false)
            return;
        QJsonObject data;
        data.insert(QStringLiteral("showId"), QString::number(show->id()));
        data.insert(QStringLiteral("patch"), timelinePatch(before, tracksToJson(doc, show)));
        data.insert(QStringLiteral("docRevision"), int(doc->docRevision()));
        server->broadcast(QStringLiteral("functions.show.itemsChanged"), data, session->clientId(), false);
    };

    dispatcher->registerMethod(QStringLiteral("functions.show.rippleInsertTime"), [ripple](ApiSession *session, const QString &id, const QJsonObject &params)
    {
        ripple(true, session, id, params);
    });
    dispatcher->registerMethod(QStringLiteral("functions.show.rippleCutTime"), [ripple](ApiSession *session, const QString &id, const QJsonObject &params)
    {
        ripple(false, session, id, params);
    });
}
