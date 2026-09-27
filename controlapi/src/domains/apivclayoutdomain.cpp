/*
  Q Light Controller Plus - Control API
  apivclayoutdomain.cpp

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

#include <QJsonArray>
#include <QJsonValue>
#include <QPointF>
#include <QRectF>
#include <QSet>
#include <algorithm>

#include "apivclayoutdomain.h"
#include "apivcpagestyledomain.h"
#include "apivchost.h"
#include "apiserver.h"
#include "apisession.h"
#include "apidispatcher.h"
#include "apienvelope.h"
#include "doc.h"
#include "fixture.h"
#include "function.h"

namespace {

const QString kHostUnavailable = QStringLiteral("App instance not available");
const QString kWidgetId = QStringLiteral("widgetId");
const QString kWidgetIds = QStringLiteral("widgetIds");
const QString kBaseRevision = QStringLiteral("baseRevision");
const QString kDocRevision = QStringLiteral("docRevision");
const QStringList kFrameTypes = { QStringLiteral("Frame"), QStringLiteral("SoloFrame") };
const QStringList kSliderTypes = { QStringLiteral("Slider") };

/** Strict whole-number check (see ApiVcDomain's jsonIsInteger): QJsonValue::toInt() would turn
 *  "abc"/true/1.5 into 0/1/1. */
bool jsonIsInteger(const QJsonValue &v)
{
    if (v.isDouble() == false)
        return false;
    double d = v.toDouble();
    if (d < -2147483648.0 || d > 2147483647.0)
        return false;
    return d == double(qint32(d));
}

QRectF rectFromJson(const QJsonObject &o)
{
    return QRectF(o.value(QStringLiteral("x")).toDouble(), o.value(QStringLiteral("y")).toDouble(),
                  o.value(QStringLiteral("width")).toDouble(), o.value(QStringLiteral("height")).toDouble());
}

QJsonObject rectToJson(const QRectF &r)
{
    QJsonObject o;
    o.insert(QStringLiteral("x"), r.x());
    o.insert(QStringLiteral("y"), r.y());
    o.insert(QStringLiteral("width"), r.width());
    o.insert(QStringLiteral("height"), r.height());
    return o;
}

/** A PIN must be either empty (clears protection) or exactly 4 digits - mirrors VirtualConsole::
 *  setPagePIN()'s own validation, same rule vc.page.setPin applies. */
bool validPin(const QString &pin)
{
    if (pin.isEmpty())
        return true;
    if (pin.size() != 4)
        return false;
    for (const QChar &c : pin)
    {
        if (c.isDigit() == false)
            return false;
    }
    return true;
}

} // namespace

ApiVcLayoutDomain::ApiVcLayoutDomain(Doc *doc, ApiServer *server, QObject *parent)
    : QObject(parent)
    , m_doc(doc)
    , m_server(server)
{
    Q_ASSERT(m_doc != nullptr);
    Q_ASSERT(m_server != nullptr);

    ApiDispatcher *d = m_server->dispatcher();
    registerFrameMethods(d);
    registerSliderMethods(d);
    registerLayoutMethods(d);
    registerCreationMethods(d);
}

/*****************************************************************************
 * Shared helpers
 *****************************************************************************/

ApiVcHost *ApiVcLayoutDomain::vcHost() const
{
    return dynamic_cast<ApiVcHost *>(m_server->parent());
}

ApiVcHost *ApiVcLayoutDomain::requireHost(ApiSession *session, const QString &id)
{
    ApiVcHost *host = vcHost();
    if (host == nullptr)
        session->send(ApiEnvelope::buildErrorResponse(id, ApiEnvelope::ErrInternal, kHostUnavailable));
    return host;
}

bool ApiVcLayoutDomain::parseWidgetId(const QString &s, quint32 &outId)
{
    bool ok = false;
    quint32 v = s.toUInt(&ok);
    if (ok == false)
        return false;
    outId = v;
    return true;
}

QJsonArray ApiVcLayoutDomain::idsToJson(const QList<quint32> &ids)
{
    QJsonArray arr;
    for (quint32 wid : ids)
        arr.append(QString::number(wid));
    return arr;
}

bool ApiVcLayoutDomain::checkRevision(ApiSession *session, const QString &id, const QJsonObject &params)
{
    quint32 baseRevision = quint32(params.value(kBaseRevision).toInt());
    if (baseRevision == m_doc->docRevision())
        return true;

    QJsonObject details;
    details.insert(kDocRevision, int(m_doc->docRevision()));
    session->send(ApiEnvelope::buildErrorResponse(id, ApiEnvelope::ErrConflict,
                                                    QStringLiteral("baseRevision is stale"), details));
    return false;
}

bool ApiVcLayoutDomain::resolveWidget(ApiSession *session, const QString &id, const QJsonObject &params,
                                      const QStringList &allowedTypes, ApiVcHost *host, quint32 *outId)
{
    quint32 wid = ApiVcHost::InvalidWidgetId;
    if (parseWidgetId(params.value(kWidgetId).toString(), wid) == false || host->vcWidgetExists(wid) == false)
    {
        session->send(ApiEnvelope::buildErrorResponse(id, ApiEnvelope::ErrNotFound, QStringLiteral("No such widget")));
        return false;
    }

    QString type = host->vcWidgetType(wid);
    if (allowedTypes.isEmpty() == false && allowedTypes.contains(type) == false)
    {
        QJsonObject details;
        details.insert(QStringLiteral("widgetType"), type);
        session->send(ApiEnvelope::buildErrorResponse(id, ApiEnvelope::ErrInvalidParams,
                                                        QStringLiteral("Widget %1 is a %2, not a %3")
                                                            .arg(QString::number(wid), type, allowedTypes.join(QStringLiteral("/"))),
                                                        details));
        return false;
    }

    *outId = wid;
    return true;
}

bool ApiVcLayoutDomain::resolveWidgetIds(ApiSession *session, const QString &id, const QJsonObject &params, int minCount,
                                         ApiVcHost *host, QList<quint32> *outIds)
{
    QJsonValue v = params.value(kWidgetIds);
    if (v.isArray() == false)
    {
        session->send(ApiEnvelope::buildErrorResponse(id, ApiEnvelope::ErrInvalidParams,
                                                        QStringLiteral("widgetIds must be an array of widget ids")));
        return false;
    }

    QList<quint32> ids;
    for (const QJsonValue &item : v.toArray())
    {
        quint32 wid = ApiVcHost::InvalidWidgetId;
        if (parseWidgetId(item.toString(), wid) == false || host->vcWidgetExists(wid) == false)
        {
            session->send(ApiEnvelope::buildErrorResponse(id, ApiEnvelope::ErrNotFound,
                                                            QStringLiteral("No such widget '%1'").arg(item.toString())));
            return false;
        }
        if (ids.contains(wid) == false)
            ids.append(wid);
    }

    if (ids.size() < minCount)
    {
        session->send(ApiEnvelope::buildErrorResponse(id, ApiEnvelope::ErrInvalidParams,
                                                        QStringLiteral("widgetIds needs at least %1 distinct widget(s)").arg(minCount)));
        return false;
    }

    *outIds = ids;
    return true;
}

bool ApiVcLayoutDomain::resolveTarget(ApiSession *session, const QString &id, const QJsonObject &params, ApiVcHost *host,
                                      int *outPage, quint32 *outParentId)
{
    QJsonValue pageValue = params.value(QStringLiteral("page"));
    if (jsonIsInteger(pageValue) == false || pageValue.toInt() < 0 || pageValue.toInt() >= host->vcPageCount())
    {
        session->send(ApiEnvelope::buildErrorResponse(id, ApiEnvelope::ErrNotFound, QStringLiteral("No such page")));
        return false;
    }

    quint32 parentId = ApiVcHost::InvalidWidgetId;
    if (params.contains(QStringLiteral("parentId")))
    {
        if (parseWidgetId(params.value(QStringLiteral("parentId")).toString(), parentId) == false || host->vcWidgetExists(parentId) == false)
        {
            session->send(ApiEnvelope::buildErrorResponse(id, ApiEnvelope::ErrNotFound, QStringLiteral("No such parent widget")));
            return false;
        }
        if (host->vcIsContainerWidget(parentId) == false)
        {
            session->send(ApiEnvelope::buildErrorResponse(id, ApiEnvelope::ErrInvalidParams,
                                                            QStringLiteral("parentId must be a Frame or SoloFrame widget")));
            return false;
        }
    }

    *outPage = pageValue.toInt();
    *outParentId = parentId;
    return true;
}

void ApiVcLayoutDomain::replyRevision(ApiSession *session, const QString &id)
{
    QJsonObject result;
    result.insert(kDocRevision, int(m_doc->docRevision()));
    session->send(ApiEnvelope::buildOkResponse(id, result));
}

void ApiVcLayoutDomain::broadcastConfigChanged(ApiVcHost *host, quint32 widgetId, ApiSession *session)
{
    QJsonObject data;
    data.insert(QStringLiteral("widget"), host->vcWidgetSnapshot(widgetId));
    data.insert(kDocRevision, int(m_doc->docRevision()));
    m_server->broadcast(QStringLiteral("vc.widget.configChanged"), data, session->clientId(), false);
}

void ApiVcLayoutDomain::broadcastBulkUpdated(ApiVcHost *host, const QList<quint32> &widgetIds, ApiSession *session)
{
    QJsonArray widgets;
    for (quint32 wid : widgetIds)
        widgets.append(host->vcWidgetSnapshot(wid));

    QJsonObject data;
    data.insert(QStringLiteral("widgets"), widgets);
    data.insert(kDocRevision, int(m_doc->docRevision()));
    m_server->broadcast(QStringLiteral("vc.widget.bulkUpdated"), data, session->clientId(), false);
}

/*****************************************************************************
 * vc.frame.setPin / validatePin / cloneFirstPage
 *****************************************************************************/

void ApiVcLayoutDomain::registerFrameMethods(ApiDispatcher *d)
{
    d->registerMethod(QStringLiteral("vc.frame.setPin"), [this](ApiSession *session, const QString &id, const QJsonObject &params)
    {
        ApiVcHost *host = requireHost(session, id);
        if (host == nullptr || checkRevision(session, id, params) == false)
            return;

        quint32 wid = ApiVcHost::InvalidWidgetId;
        if (resolveWidget(session, id, params, kFrameTypes, host, &wid) == false)
            return;

        QString currentPin = params.value(QStringLiteral("currentPIN")).toString();
        QString newPin = params.value(QStringLiteral("newPIN")).toString();
        if (validPin(newPin) == false)
        {
            session->send(ApiEnvelope::buildErrorResponse(id, ApiEnvelope::ErrInvalidParams,
                                                            QStringLiteral("newPIN must be empty or exactly 4 digits")));
            return;
        }

        if (host->vcFrameSetPin(wid, currentPin, newPin) == false)
        {
            session->send(ApiEnvelope::buildErrorResponse(id, ApiEnvelope::ErrInvalidParams,
                                                            QStringLiteral("currentPIN does not match")));
            return;
        }

        m_doc->setModified();
        // The spec defines no dedicated event for a PIN change (same as vc.page.setPin); the frame's
        // typeConfig.hasPin flips, so report it as a config change.
        broadcastConfigChanged(host, wid, session);
        replyRevision(session, id);
    });

    d->registerMethod(QStringLiteral("vc.frame.validatePin"), [this](ApiSession *session, const QString &id, const QJsonObject &params)
    {
        ApiVcHost *host = requireHost(session, id);
        if (host == nullptr)
            return;

        quint32 wid = ApiVcHost::InvalidWidgetId;
        if (resolveWidget(session, id, params, kFrameTypes, host, &wid) == false)
            return;

        // Live/session-scoped and stateless server-side: the unlock is remembered by the client for
        // its own session, never broadcast (see virtualconsole-notes.md).
        QJsonObject result;
        result.insert(QStringLiteral("valid"), host->vcFrameValidatePin(wid, params.value(QStringLiteral("pin")).toString()));
        session->send(ApiEnvelope::buildOkResponse(id, result));
    });

    d->registerMethod(QStringLiteral("vc.frame.cloneFirstPage"), [this](ApiSession *session, const QString &id, const QJsonObject &params)
    {
        ApiVcHost *host = requireHost(session, id);
        if (host == nullptr || checkRevision(session, id, params) == false)
            return;

        quint32 wid = ApiVcHost::InvalidWidgetId;
        if (resolveWidget(session, id, params, kFrameTypes, host, &wid) == false)
            return;

        QString error;
        QJsonArray createdIds;
        if (host->vcFrameCloneFirstPage(wid, createdIds, &error) == false)
        {
            session->send(ApiEnvelope::buildErrorResponse(id, ApiEnvelope::ErrInvalidState,
                                                            error.isEmpty() ? QStringLiteral("Unable to clone the first page") : error));
            return;
        }

        m_doc->setModified();

        // The copies are new widgets: report them the way the other bulk creators do, on
        // vc.widget.bulkUpdated, with the frame itself first.
        QList<quint32> affected;
        affected.append(wid);
        for (const QJsonValue &v : createdIds)
        {
            quint32 created = ApiVcHost::InvalidWidgetId;
            if (parseWidgetId(v.toString(), created))
                affected.append(created);
        }
        broadcastBulkUpdated(host, affected, session);
        replyRevision(session, id);
    });
}

/*****************************************************************************
 * vc.slider.setLevelChannels / flash
 *****************************************************************************/

void ApiVcLayoutDomain::registerSliderMethods(ApiDispatcher *d)
{
    d->registerMethod(QStringLiteral("vc.slider.setLevelChannels"), [this](ApiSession *session, const QString &id, const QJsonObject &params)
    {
        ApiVcHost *host = requireHost(session, id);
        if (host == nullptr || checkRevision(session, id, params) == false)
            return;

        quint32 wid = ApiVcHost::InvalidWidgetId;
        if (resolveWidget(session, id, params, kSliderTypes, host, &wid) == false)
            return;

        QJsonValue channelsValue = params.value(QStringLiteral("channels"));
        if (channelsValue.isArray() == false)
        {
            session->send(ApiEnvelope::buildErrorResponse(id, ApiEnvelope::ErrInvalidParams,
                                                            QStringLiteral("channels must be an array of {fixtureId, channel}")));
            return;
        }

        // Validate every entry against the document before touching the slider: the fixture must
        // exist and the channel index be within it (VCSlider::addLevelChannel() stores anything).
        QList<QPair<quint32, quint32> > channels;
        for (const QJsonValue &v : channelsValue.toArray())
        {
            QJsonObject entry = v.toObject();
            bool ok = false;
            quint32 fixtureId = entry.value(QStringLiteral("fixtureId")).toString().toUInt(&ok);
            Fixture *fixture = ok ? m_doc->fixture(fixtureId) : nullptr;
            if (fixture == nullptr)
            {
                session->send(ApiEnvelope::buildErrorResponse(id, ApiEnvelope::ErrNotFound,
                                                                QStringLiteral("No such fixture '%1'").arg(entry.value(QStringLiteral("fixtureId")).toString())));
                return;
            }
            QJsonValue ch = entry.value(QStringLiteral("channel"));
            if (jsonIsInteger(ch) == false || ch.toInt() < 0 || quint32(ch.toInt()) >= fixture->channels())
            {
                QJsonObject details;
                details.insert(QStringLiteral("fixtureId"), QString::number(fixtureId));
                details.insert(QStringLiteral("channels"), int(fixture->channels()));
                session->send(ApiEnvelope::buildErrorResponse(id, ApiEnvelope::ErrInvalidParams,
                                                                QStringLiteral("Fixture %1 has no channel %2").arg(fixtureId).arg(ch.toDouble()),
                                                                details));
                return;
            }
            QPair<quint32, quint32> pair(fixtureId, quint32(ch.toInt()));
            if (channels.contains(pair) == false)
                channels.append(pair);
        }

        QString error;
        if (host->vcSliderSetLevelChannels(wid, channels, &error) == false)
        {
            session->send(ApiEnvelope::buildErrorResponse(id, ApiEnvelope::ErrInvalidParams,
                                                            error.isEmpty() ? QStringLiteral("Unable to set level channels") : error));
            return;
        }

        m_doc->setModified();
        broadcastConfigChanged(host, wid, session);
        replyRevision(session, id);
    });

    d->registerMethod(QStringLiteral("vc.slider.flash"), [this](ApiSession *session, const QString &id, const QJsonObject &params)
    {
        QJsonValue onValue = params.value(QStringLiteral("on"));
        if (onValue.isBool() == false)
        {
            session->send(ApiEnvelope::buildErrorResponse(id, ApiEnvelope::ErrInvalidParams,
                                                            QStringLiteral("on must be a boolean")));
            return;
        }

        ApiVcHost *host = requireHost(session, id);
        if (host == nullptr)
            return;

        quint32 wid = ApiVcHost::InvalidWidgetId;
        if (resolveWidget(session, id, params, kSliderTypes, host, &wid) == false)
            return;

        // Live (§4b): same disabled-widget refusal as the other live methods.
        if (host->vcWidgetSnapshot(wid).value(QStringLiteral("isDisabled")).toBool())
        {
            session->send(ApiEnvelope::buildErrorResponse(id, ApiEnvelope::ErrInvalidState, QStringLiteral("Widget is disabled")));
            return;
        }

        QString error;
        if (host->vcSliderFlash(wid, onValue.toBool(), &error) == false)
        {
            session->send(ApiEnvelope::buildErrorResponse(id, ApiEnvelope::ErrInvalidState, error));
            return;
        }
        session->send(ApiEnvelope::buildOkResponse(id, QJsonObject()));
    });
}

/*****************************************************************************
 * vc.widget.align / distribute / bulkStyle
 *****************************************************************************/

void ApiVcLayoutDomain::registerLayoutMethods(ApiDispatcher *d)
{
    // Geometry is parent-relative (VcGeometry), so aligning or distributing widgets that live in
    // different containers has no meaning in page space - the on-screen tool works on a selection
    // that is normally within one frame too ("ignorant alignment", VirtualConsole::setWidgetsAlignment()
    // says). Require one common parent and answer INVALID_PARAMS otherwise.
    auto sameParent = [](ApiVcHost *host, const QList<quint32> &ids)
    {
        quint32 parent = host->vcWidgetParentId(ids.first());
        for (quint32 wid : ids)
        {
            if (host->vcWidgetParentId(wid) != parent)
                return false;
        }
        return true;
    };

    d->registerMethod(QStringLiteral("vc.widget.align"), [this, sameParent](ApiSession *session, const QString &id, const QJsonObject &params)
    {
        ApiVcHost *host = requireHost(session, id);
        if (host == nullptr || checkRevision(session, id, params) == false)
            return;

        QList<quint32> ids;
        if (resolveWidgetIds(session, id, params, 1, host, &ids) == false)
            return;

        quint32 refId = ApiVcHost::InvalidWidgetId;
        if (parseWidgetId(params.value(QStringLiteral("referenceWidgetId")).toString(), refId) == false || host->vcWidgetExists(refId) == false)
        {
            session->send(ApiEnvelope::buildErrorResponse(id, ApiEnvelope::ErrNotFound, QStringLiteral("No such reference widget")));
            return;
        }

        static const QStringList alignments = { QStringLiteral("left"), QStringLiteral("hcenter"), QStringLiteral("right"),
                                                QStringLiteral("top"), QStringLiteral("vcenter"), QStringLiteral("bottom") };
        QString alignment = params.value(QStringLiteral("alignment")).toString();
        if (alignments.contains(alignment) == false)
        {
            session->send(ApiEnvelope::buildErrorResponse(id, ApiEnvelope::ErrInvalidParams,
                                                            QStringLiteral("alignment must be one of %1").arg(alignments.join(QStringLiteral("/")))));
            return;
        }

        QList<quint32> all = ids;
        if (all.contains(refId) == false)
            all.append(refId);
        if (sameParent(host, all) == false)
        {
            session->send(ApiEnvelope::buildErrorResponse(id, ApiEnvelope::ErrInvalidParams,
                                                            QStringLiteral("Every widget and the reference must share the same parent")));
            return;
        }

        // Same arithmetic as VirtualConsole::setWidgetsAlignment(), plus the two centre alignments
        // the spec adds. Sizes never change.
        QRectF ref = rectFromJson(host->vcWidgetSnapshot(refId).value(QStringLiteral("geometry")).toObject());
        QList<QPair<quint32, QJsonObject> > updates;
        for (quint32 wid : ids)
        {
            if (wid == refId)
                continue;
            QRectF g = rectFromJson(host->vcWidgetSnapshot(wid).value(QStringLiteral("geometry")).toObject());
            if (alignment == QStringLiteral("left"))
                g.moveLeft(ref.left());
            else if (alignment == QStringLiteral("hcenter"))
                g.moveLeft(ref.center().x() - g.width() / 2.0);
            else if (alignment == QStringLiteral("right"))
                g.moveLeft(ref.right() - g.width());
            else if (alignment == QStringLiteral("top"))
                g.moveTop(ref.top());
            else if (alignment == QStringLiteral("vcenter"))
                g.moveTop(ref.center().y() - g.height() / 2.0);
            else
                g.moveTop(ref.bottom() - g.height());
            updates.append(qMakePair(wid, rectToJson(g)));
        }

        if (updates.isEmpty() == false)
            host->vcRepositionWidgets(updates);
        m_doc->setModified();
        broadcastBulkUpdated(host, ids, session);
        replyRevision(session, id);
    });

    d->registerMethod(QStringLiteral("vc.widget.distribute"), [this, sameParent](ApiSession *session, const QString &id, const QJsonObject &params)
    {
        ApiVcHost *host = requireHost(session, id);
        if (host == nullptr || checkRevision(session, id, params) == false)
            return;

        QList<quint32> ids;
        if (resolveWidgetIds(session, id, params, 3, host, &ids) == false)
            return;

        QString direction = params.value(QStringLiteral("direction")).toString();
        if (direction != QStringLiteral("horizontal") && direction != QStringLiteral("vertical"))
        {
            session->send(ApiEnvelope::buildErrorResponse(id, ApiEnvelope::ErrInvalidParams,
                                                            QStringLiteral("direction must be horizontal or vertical")));
            return;
        }
        if (sameParent(host, ids) == false)
        {
            session->send(ApiEnvelope::buildErrorResponse(id, ApiEnvelope::ErrInvalidParams,
                                                            QStringLiteral("Every widget must share the same parent")));
            return;
        }

        // VirtualConsole::setWidgetsDistribution(): sort along the axis, keep the outermost two in
        // place, spread the others so every gap is equal.
        bool horizontal = direction == QStringLiteral("horizontal");
        struct Entry { quint32 id; QRectF geom; qreal pos; qreal size; };
        QList<Entry> entries;
        qreal min = 0, max = 0, total = 0;
        for (int i = 0; i < ids.size(); i++)
        {
            QRectF g = rectFromJson(host->vcWidgetSnapshot(ids.at(i)).value(QStringLiteral("geometry")).toObject());
            Entry e { ids.at(i), g, horizontal ? g.x() : g.y(), horizontal ? g.width() : g.height() };
            if (i == 0 || e.pos < min)
                min = e.pos;
            if (i == 0 || e.pos + e.size > max)
                max = e.pos + e.size;
            total += e.size;
            entries.append(e);
        }
        std::stable_sort(entries.begin(), entries.end(), [](const Entry &a, const Entry &b) { return a.pos < b.pos; });

        qreal gap = ((max - min) - total) / qreal(entries.size() - 1);
        qreal next = min;
        QList<QPair<quint32, QJsonObject> > updates;
        for (int i = 0; i < entries.size(); i++)
        {
            Entry &e = entries[i];
            if (i > 0 && i < entries.size() - 1)
            {
                if (horizontal)
                    e.geom.moveLeft(next);
                else
                    e.geom.moveTop(next);
                updates.append(qMakePair(e.id, rectToJson(e.geom)));
            }
            next += e.size + gap;
        }

        if (updates.isEmpty() == false)
            host->vcRepositionWidgets(updates);
        m_doc->setModified();
        broadcastBulkUpdated(host, ids, session);
        replyRevision(session, id);
    });

    d->registerMethod(QStringLiteral("vc.widget.bulkStyle"), [this](ApiSession *session, const QString &id, const QJsonObject &params)
    {
        ApiVcHost *host = requireHost(session, id);
        if (host == nullptr || checkRevision(session, id, params) == false)
            return;

        QList<quint32> ids;
        if (resolveWidgetIds(session, id, params, 1, host, &ids) == false)
            return;

        // VcWidgetStyle's fields, lifted straight into a vc.widget.update-style patch: null resets a
        // colour / clears the image, {} resets the font (see App::applyStyleToWidget()).
        QJsonObject style;
        for (const QString &key : { QStringLiteral("caption"), QStringLiteral("foregroundColor"), QStringLiteral("backgroundColor"),
                                     QStringLiteral("backgroundImage"), QStringLiteral("font") })
        {
            if (params.contains(key))
                style.insert(key, params.value(key));
        }
        if (style.isEmpty())
        {
            session->send(ApiEnvelope::buildErrorResponse(id, ApiEnvelope::ErrInvalidParams,
                                                            QStringLiteral("At least one of caption/foregroundColor/backgroundColor/backgroundImage/font is required")));
            return;
        }
        QString styleError;
        if (ApiVcPageStyleDomain::checkStyle(style, &styleError) == false)
        {
            session->send(ApiEnvelope::buildErrorResponse(id, ApiEnvelope::ErrInvalidParams, styleError));
            return;
        }

        QJsonObject fields;
        fields.insert(QStringLiteral("style"), style);
        for (quint32 wid : ids)
        {
            QString error;
            host->vcUpdateWidgetCommon(wid, fields, &error);
        }

        m_doc->setModified();
        broadcastBulkUpdated(host, ids, session);
        replyRevision(session, id);
    });
}

/*****************************************************************************
 * vc.widget.createFromFunctions / createMatrix / usage
 *****************************************************************************/

void ApiVcLayoutDomain::registerCreationMethods(ApiDispatcher *d)
{
    d->registerMethod(QStringLiteral("vc.widget.createFromFunctions"), [this](ApiSession *session, const QString &id, const QJsonObject &params)
    {
        ApiVcHost *host = requireHost(session, id);
        if (host == nullptr || checkRevision(session, id, params) == false)
            return;

        int page = 0;
        quint32 parentId = ApiVcHost::InvalidWidgetId;
        if (resolveTarget(session, id, params, host, &page, &parentId) == false)
            return;

        static const QStringList hints = { QStringLiteral("button"), QStringLiteral("adjustSlider"), QStringLiteral("cueList") };
        QString hint = params.value(QStringLiteral("widgetHint")).toString();
        if (hints.contains(hint) == false)
        {
            session->send(ApiEnvelope::buildErrorResponse(id, ApiEnvelope::ErrInvalidParams,
                                                            QStringLiteral("widgetHint must be one of %1").arg(hints.join(QStringLiteral("/")))));
            return;
        }

        // VCFrame::addFunctions() silently skips unknown ids and (for cue lists) non-Chasers - check
        // up front so the request is all-or-nothing and the error says why.
        QJsonValue idsValue = params.value(QStringLiteral("functionIds"));
        QList<quint32> functionIds;
        for (const QJsonValue &v : idsValue.toArray())
        {
            bool ok = false;
            quint32 fid = v.toString().toUInt(&ok);
            Function *function = ok ? m_doc->function(fid) : nullptr;
            if (function == nullptr)
            {
                session->send(ApiEnvelope::buildErrorResponse(id, ApiEnvelope::ErrNotFound,
                                                                QStringLiteral("No such function '%1'").arg(v.toString())));
                return;
            }
            if (hint == QStringLiteral("cueList") && function->type() != Function::ChaserType)
            {
                session->send(ApiEnvelope::buildErrorResponse(id, ApiEnvelope::ErrInvalidParams,
                                                                QStringLiteral("widgetHint cueList requires Chasers only - function %1 is a %2")
                                                                    .arg(fid).arg(Function::typeToString(function->type()))));
                return;
            }
            if (functionIds.contains(fid) == false)
                functionIds.append(fid);
        }
        if (idsValue.isArray() == false || functionIds.isEmpty())
        {
            session->send(ApiEnvelope::buildErrorResponse(id, ApiEnvelope::ErrInvalidParams,
                                                            QStringLiteral("functionIds must be a non-empty array of function ids")));
            return;
        }

        QJsonObject position = params.value(QStringLiteral("position")).toObject();
        QPointF pos(position.value(QStringLiteral("x")).toDouble(), position.value(QStringLiteral("y")).toDouble());

        QString error;
        QList<quint32> created = host->vcCreateWidgetsFromFunctions(page, parentId, functionIds, pos, hint, &error);
        if (created.isEmpty())
        {
            session->send(ApiEnvelope::buildErrorResponse(id, ApiEnvelope::ErrInvalidParams,
                                                            error.isEmpty() ? QStringLiteral("No widget was created") : error));
            return;
        }

        m_doc->setModified();
        broadcastBulkUpdated(host, created, session);

        QJsonObject result;
        result.insert(kDocRevision, int(m_doc->docRevision()));
        result.insert(kWidgetIds, idsToJson(created));
        session->send(ApiEnvelope::buildOkResponse(id, result));
    });

    d->registerMethod(QStringLiteral("vc.widget.createMatrix"), [this](ApiSession *session, const QString &id, const QJsonObject &params)
    {
        ApiVcHost *host = requireHost(session, id);
        if (host == nullptr || checkRevision(session, id, params) == false)
            return;

        int page = 0;
        quint32 parentId = ApiVcHost::InvalidWidgetId;
        if (resolveTarget(session, id, params, host, &page, &parentId) == false)
            return;

        QString matrixType = params.value(QStringLiteral("matrixType")).toString();
        if (matrixType != QStringLiteral("Button") && matrixType != QStringLiteral("Slider"))
        {
            session->send(ApiEnvelope::buildErrorResponse(id, ApiEnvelope::ErrInvalidParams,
                                                            QStringLiteral("matrixType must be Button or Slider")));
            return;
        }

        QJsonObject matrixSize = params.value(QStringLiteral("matrixSize")).toObject();
        QJsonObject widgetSize = params.value(QStringLiteral("widgetSize")).toObject();
        QJsonValue columns = matrixSize.value(QStringLiteral("columns")), rows = matrixSize.value(QStringLiteral("rows"));
        QJsonValue width = widgetSize.value(QStringLiteral("width")), height = widgetSize.value(QStringLiteral("height"));
        // PopupCreateMatrix.qml's spin boxes: 1..100 per axis, widget size 1..1000 px.
        if (jsonIsInteger(columns) == false || jsonIsInteger(rows) == false ||
            columns.toInt() < 1 || columns.toInt() > 100 || rows.toInt() < 1 || rows.toInt() > 100)
        {
            session->send(ApiEnvelope::buildErrorResponse(id, ApiEnvelope::ErrInvalidParams,
                                                            QStringLiteral("matrixSize.columns/rows must be integers 1..100")));
            return;
        }
        if (jsonIsInteger(width) == false || jsonIsInteger(height) == false ||
            width.toInt() < 1 || width.toInt() > 1000 || height.toInt() < 1 || height.toInt() > 1000)
        {
            session->send(ApiEnvelope::buildErrorResponse(id, ApiEnvelope::ErrInvalidParams,
                                                            QStringLiteral("widgetSize.width/height must be integers 1..1000")));
            return;
        }

        QJsonObject position = params.value(QStringLiteral("position")).toObject();
        QPointF pos(position.value(QStringLiteral("x")).toDouble(), position.value(QStringLiteral("y")).toDouble());
        bool soloFrame = params.value(QStringLiteral("soloFrame")).toBool(false);

        QString error;
        QList<quint32> created = host->vcCreateWidgetMatrix(page, parentId, matrixType, pos, columns.toInt(), rows.toInt(),
                                                            width.toInt(), height.toInt(), soloFrame, &error);
        if (created.isEmpty())
        {
            session->send(ApiEnvelope::buildErrorResponse(id, ApiEnvelope::ErrInvalidParams,
                                                            error.isEmpty() ? QStringLiteral("No widget was created") : error));
            return;
        }

        m_doc->setModified();
        broadcastBulkUpdated(host, created, session);

        QJsonObject result;
        result.insert(kDocRevision, int(m_doc->docRevision()));
        result.insert(kWidgetIds, idsToJson(created));
        session->send(ApiEnvelope::buildOkResponse(id, result));
    });

    d->registerMethod(QStringLiteral("vc.widget.usage"), [this](ApiSession *session, const QString &id, const QJsonObject &params)
    {
        ApiVcHost *host = requireHost(session, id);
        if (host == nullptr)
            return;

        bool ok = false;
        quint32 fid = params.value(QStringLiteral("functionId")).toString().toUInt(&ok);
        if (ok == false)
        {
            session->send(ApiEnvelope::buildErrorResponse(id, ApiEnvelope::ErrInvalidParams,
                                                            QStringLiteral("functionId must be a function id string")));
            return;
        }
        // An id that no longer exists is not an error: the answer is simply "no widget uses it"
        // (a client may ask right after functions.delete to find stale references).

        QJsonArray widgets;
        for (quint32 wid : host->vcWidgetsUsingFunction(fid))
            widgets.append(host->vcWidgetSnapshot(wid));

        QJsonObject result;
        result.insert(QStringLiteral("widgets"), widgets);
        session->send(ApiEnvelope::buildOkResponse(id, result));
    });
}
