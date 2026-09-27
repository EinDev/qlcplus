/*
  Q Light Controller Plus - Control API
  apishowpreviewdomain.cpp

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
#include <QJsonValue>
#include <QSize>

#include "apishowpreviewdomain.h"
#include "apishowhost.h"
#include "apiserver.h"
#include "apisession.h"
#include "apidispatcher.h"
#include "apienvelope.h"
#include "functionparent.h"
#include "showfunction.h"
#include "track.h"
#include "video.h"
#include "show.h"
#include "doc.h"

ApiShowHost *ApiShowPreviewDomain::s_showHost = nullptr;
ApiShowPreviewDomain *ApiShowPreviewDomain::s_instance = nullptr;

namespace {

/** Strict whole-number check: QJsonValue::toInt() would turn "abc"/true/1.5 into 0/1/1. */
bool jsonIsInteger(const QJsonValue &v)
{
    if (v.isDouble() == false)
        return false;
    double d = v.toDouble();
    if (d < -2147483648.0 || d > 2147483647.0)
        return false;
    return d == double(qint32(d));
}

quint32 idFromJson(const QJsonValue &v, bool *ok)
{
    QString s = v.isDouble() ? QString::number(v.toInt()) : v.toString();
    return s.toUInt(ok);
}

/** params.showId (or functionId) resolved to a Show, or NOT_FOUND / INVALID_PARAMS answered. */
Show *findShowOrRespond(Doc *doc, const QJsonObject &params, ApiSession *session, const QString &id)
{
    QJsonValue v = params.contains(QStringLiteral("showId")) ? params.value(QStringLiteral("showId"))
                                                             : params.value(QStringLiteral("functionId"));
    bool ok = false;
    quint32 fid = idFromJson(v, &ok);
    Function *function = ok ? doc->function(fid) : nullptr;
    if (function == nullptr)
    {
        session->send(ApiEnvelope::buildErrorResponse(id, ApiEnvelope::ErrNotFound, QStringLiteral("No such Show")));
        return nullptr;
    }
    if (function->type() != Function::ShowType)
    {
        session->send(ApiEnvelope::buildErrorResponse(id, ApiEnvelope::ErrInvalidParams,
                                                        QStringLiteral("Function %1 is not a Show").arg(fid)));
        return nullptr;
    }
    // the type was checked: static_cast, which unlike qobject_cast never
    // depends on the engine DLL's meta object
    return static_cast<Show *>(function);
}

QJsonValue sizeJson(const QSize &size)
{
    if (size.isEmpty())
        return QJsonValue();
    QJsonObject o;
    o.insert(QStringLiteral("width"), size.width());
    o.insert(QStringLiteral("height"), size.height());
    return o;
}

} // namespace

ApiShowPreviewDomain::ApiShowPreviewDomain(Doc *doc, ApiServer *server, QObject *parent)
    : QObject(parent)
    , m_doc(doc)
    , m_server(server)
{
    Q_ASSERT(m_doc != nullptr);
    Q_ASSERT(m_server != nullptr);

    s_instance = this;
    s_showHost = dynamic_cast<ApiShowHost *>(m_server->parent());

    registerMethods(m_server->dispatcher());
    connect(m_server, &ApiServer::sessionDisconnected, this, &ApiShowPreviewDomain::slotSessionDisconnected);
}

ApiShowPreviewDomain::~ApiShowPreviewDomain()
{
    if (s_instance == this)
    {
        s_instance = nullptr;
        s_showHost = nullptr;
    }
}

/*****************************************************************************
 * Spout
 *****************************************************************************/

QJsonObject ApiShowPreviewDomain::trackSpoutJson(Doc *doc, Track *track)
{
    QJsonObject obj;
    if (doc == nullptr || track == nullptr)
        return obj;

    QList<ShowFunction *> items = track->showFunctions();
    std::stable_sort(items.begin(), items.end(), [](ShowFunction *a, ShowFunction *b)
    {
        return a->startTime() < b->startTime();
    });

    QJsonArray clips;
    QList<QSize> clipSizes;
    for (ShowFunction *sf : items)
    {
        Function *f = doc->function(sf->functionID());
        if (f == nullptr || f->type() != Function::VideoType)
            continue;
        Video *video = static_cast<Video *>(f);
        if (video->outputMode() != Video::Spout)
            continue;

        QSize size = video->spoutSize().isEmpty() ? video->resolution() : video->spoutSize();
        if (size.isEmpty())
            size = QSize(0, 0);
        QJsonObject clip;
        clip.insert(QStringLiteral("itemId"), QString::number(sf->id()));
        clip.insert(QStringLiteral("functionId"), QString::number(video->id()));
        clip.insert(QStringLiteral("name"), video->name());
        clip.insert(QStringLiteral("width"), size.width());
        clip.insert(QStringLiteral("height"), size.height());
        clips.append(clip);
        clipSizes.append(size);
    }

    QSize fixed = track->spoutSize();
    if (clips.isEmpty() && fixed.isEmpty())
        return obj;

    QSize output = s_showHost != nullptr ? s_showHost->showTrackSpoutOutputSize(track) : fixed;
    if (output.isEmpty() && fixed.isEmpty() == false)
        output = fixed;

    bool mismatch = false;
    if (output.isEmpty() == false)
    {
        for (const QSize &s : clipSizes)
        {
            if (s.isEmpty() == false && s != output)
                mismatch = true;
        }
    }

    obj.insert(QStringLiteral("fixedSize"), sizeJson(fixed));
    obj.insert(QStringLiteral("outputSize"), sizeJson(output));
    obj.insert(QStringLiteral("clips"), clips);
    obj.insert(QStringLiteral("mismatch"), mismatch);
    return obj;
}

/*****************************************************************************
 * Preview
 *****************************************************************************/

void ApiShowPreviewDomain::broadcastPreview(Show *show, bool previewing, quint32 time, const QString &originClientId)
{
    QJsonObject data;
    data.insert(QStringLiteral("functionId"), QString::number(show->id()));
    data.insert(QStringLiteral("previewing"), previewing);
    data.insert(QStringLiteral("time"), double(time));
    m_server->broadcast(QStringLiteral("functions.show.previewChanged"), data, originClientId, false);
}

void ApiShowPreviewDomain::slotSessionDisconnected(const QString &clientId)
{
    const QList<quint32> ids = m_previewing.keys(clientId);
    for (quint32 id : ids)
    {
        m_previewing.remove(id);
        Function *f = m_doc->function(id);
        if (f == nullptr || f->type() != Function::ShowType)
            continue;
        Show *show = static_cast<Show *>(f);
        if (show->isScrubMode() == false)
            continue;
        quint32 time = quint32(show->elapsed());
        show->stop(FunctionParent::master(FunctionParent::ControlApi));
        broadcastPreview(show, false, time, QString());
    }
}

void ApiShowPreviewDomain::slotShowStopped(quint32 id)
{
    if (m_previewing.remove(id) == 0)
        return;

    Function *f = m_doc->function(id);
    if (f == nullptr || f->type() != Function::ShowType)
        return;
    broadcastPreview(static_cast<Show *>(f), false, quint32(f->elapsed()), QString());
}

void ApiShowPreviewDomain::registerMethods(ApiDispatcher *d)
{
    Doc *doc = m_doc;

    // functions.show.preview {showId, time} - §4b
    d->registerMethod(QStringLiteral("functions.show.preview"), [this, doc](ApiSession *session, const QString &id, const QJsonObject &params)
    {
        Show *show = findShowOrRespond(doc, params, session, id);
        if (show == nullptr)
            return;

        QJsonValue t = params.value(QStringLiteral("time"));
        if (jsonIsInteger(t) == false || t.toInt() < 0)
        {
            session->send(ApiEnvelope::buildErrorResponse(id, ApiEnvelope::ErrInvalidParams,
                                                            QStringLiteral("time must be an integer >= 0 (ms)")));
            return;
        }
        quint32 position = quint32(t.toInt());

        QJsonObject result;
        result.insert(QStringLiteral("previewing"), true);
        result.insert(QStringLiteral("time"), double(position));

        if (show->isScrubMode())
        {
            // already frozen (by this or another front end): the runner
            // coalesces the seeks posted between two ticks
            show->requestSeek(position);
            // the last client to move an API preview owns it (its disconnect ends it)
            if (m_previewing.contains(show->id()))
                m_previewing.insert(show->id(), session->clientId());
            session->send(ApiEnvelope::buildOkResponse(id, result));
            return;
        }

        if (show->isRunning())
        {
            if (show->stopped())
            {
                session->send(ApiEnvelope::buildErrorResponse(id, ApiEnvelope::ErrInvalidState,
                                                                QStringLiteral("The Show is stopping, try again")));
                return;
            }
            if (show->isPaused() == false)
            {
                session->send(ApiEnvelope::buildErrorResponse(id, ApiEnvelope::ErrInvalidState,
                                                                QStringLiteral("The Show is playing: pause or stop it to preview")));
                return;
            }

            // Paused: hand the runner over to the frozen state at the new
            // cursor (ShowManager::previewAt): scrub mode first, so the
            // runner keeps its clips held through the unpause and seeks them
            // on its first tick.
            show->setScrubMode(true);
            show->requestSeek(position);
            show->setPause(false);
        }
        else
        {
            // Edits queue their schedule rebuild on the event loop: make the
            // first frozen tick show the timeline as it is now.
            show->rebuildSchedule();
            show->setScrubMode(true);
            show->start(doc->masterTimer(), FunctionParent::master(FunctionParent::ControlApi), position);
        }

        m_previewing.insert(show->id(), session->clientId());
        // engine-DLL object: string-based connection (see apiiodomain.cpp)
        connect(show, SIGNAL(stopped(quint32)), this, SLOT(slotShowStopped(quint32)), Qt::UniqueConnection);
        session->send(ApiEnvelope::buildOkResponse(id, result));
        broadcastPreview(show, true, position, session->clientId());
    });

    // functions.show.endPreview {showId, play} - §4b
    d->registerMethod(QStringLiteral("functions.show.endPreview"), [this, doc](ApiSession *session, const QString &id, const QJsonObject &params)
    {
        Show *show = findShowOrRespond(doc, params, session, id);
        if (show == nullptr)
            return;

        QJsonValue playValue = params.value(QStringLiteral("play"));
        if (playValue.isUndefined() == false && playValue.isBool() == false)
        {
            session->send(ApiEnvelope::buildErrorResponse(id, ApiEnvelope::ErrInvalidParams,
                                                            QStringLiteral("play must be a boolean")));
            return;
        }
        bool play = playValue.toBool(false);

        QJsonObject result;
        result.insert(QStringLiteral("previewing"), false);
        result.insert(QStringLiteral("playing"), false);

        if (show->isScrubMode() == false)
        {
            session->send(ApiEnvelope::buildOkResponse(id, result));
            return;
        }

        quint32 time = quint32(show->elapsed());
        m_previewing.remove(show->id());

        if (play)
        {
            // the frozen runner already sits at the cursor with its clips
            // started: leaving scrub mode plays on from there (and starts the
            // Audio it skipped at its offset)
            show->setScrubMode(false);
            result.insert(QStringLiteral("playing"), true);
        }
        else
        {
            show->stop(FunctionParent::master(FunctionParent::ControlApi));
        }

        session->send(ApiEnvelope::buildOkResponse(id, result));
        broadcastPreview(show, false, time, session->clientId());
    });

    // functions.show.track.setSpoutSize {showId, trackId, width, height, baseRevision} - §4a
    d->registerMethod(QStringLiteral("functions.show.track.setSpoutSize"), [this, doc](ApiSession *session, const QString &id, const QJsonObject &params)
    {
        Show *show = findShowOrRespond(doc, params, session, id);
        if (show == nullptr)
            return;

        bool ok = false;
        quint32 trackId = idFromJson(params.value(QStringLiteral("trackId")), &ok);
        Track *track = ok ? show->track(trackId) : nullptr;
        if (track == nullptr)
        {
            session->send(ApiEnvelope::buildErrorResponse(id, ApiEnvelope::ErrNotFound, QStringLiteral("No such track")));
            return;
        }

        quint32 baseRevision = quint32(params.value(QStringLiteral("baseRevision")).toInt());
        if (baseRevision != doc->docRevision())
        {
            QJsonObject details;
            details.insert(QStringLiteral("docRevision"), int(doc->docRevision()));
            session->send(ApiEnvelope::buildErrorResponse(id, ApiEnvelope::ErrConflict,
                                                            QStringLiteral("baseRevision is stale"), details));
            return;
        }

        QJsonValue w = params.value(QStringLiteral("width"));
        QJsonValue h = params.value(QStringLiteral("height"));
        bool valid = jsonIsInteger(w) && jsonIsInteger(h);
        if (valid)
        {
            bool unset = w.toInt() == 0 && h.toInt() == 0;
            bool inRange = w.toInt() >= 1 && w.toInt() <= MaxSpoutSize && h.toInt() >= 1 && h.toInt() <= MaxSpoutSize;
            valid = unset || inRange;
        }
        if (valid == false)
        {
            session->send(ApiEnvelope::buildErrorResponse(id, ApiEnvelope::ErrInvalidParams,
                                                            QStringLiteral("width and height must both be 0 (unset) or integers in 1..%1").arg(MaxSpoutSize)));
            return;
        }

        QSize size(w.toInt(), h.toInt());
        bool changed = track->spoutSize() != size;
        if (changed)
        {
            track->setSpoutSize(size);
            doc->setModified();
        }

        // like ShowManager::setTrackSpoutSize: an unchanged size is still
        // applied to the live sender (it may be elsewhere after a "keep")
        if (s_showHost != nullptr)
            s_showHost->showTrackSpoutSizeChanged(show, track);

        QJsonObject result;
        result.insert(QStringLiteral("docRevision"), int(doc->docRevision()));
        session->send(ApiEnvelope::buildOkResponse(id, result));

        if (changed)
        {
            QJsonObject data;
            data.insert(QStringLiteral("showId"), QString::number(show->id()));
            data.insert(QStringLiteral("trackId"), QString::number(track->id()));
            data.insert(QStringLiteral("spoutSize"), sizeJson(track->spoutSize()));
            data.insert(QStringLiteral("spout"), trackSpoutJson(doc, track));
            data.insert(QStringLiteral("docRevision"), int(doc->docRevision()));
            m_server->broadcast(QStringLiteral("functions.show.track.spoutSizeChanged"), data, session->clientId(), false);
        }
    });
}
