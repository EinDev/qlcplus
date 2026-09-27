/*
  Q Light Controller Plus - Control API
  apivcdomain.cpp

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
#include <QSet>
#include <algorithm>
#include <limits>

#include "apivcdomain.h"
#include "apivchost.h"
#include "apiserver.h"
#include "apisession.h"
#include "apidispatcher.h"
#include "apienvelope.h"
#include "doc.h"

namespace {
const QString kHostUnavailable = QStringLiteral("App instance not available");
}

ApiVcDomain::ApiVcDomain(Doc *doc, ApiServer *server, QObject *parent)
    : QObject(parent)
    , m_doc(doc)
    , m_server(server)
{
    Q_ASSERT(m_doc != nullptr);
    Q_ASSERT(m_server != nullptr);

    ApiDispatcher *d = m_server->dispatcher();
    registerPageMethods(d);
    registerWidgetMethods(d);
    registerLiveMethods(d);
    registerCueSpeedDialMethods(d);
    registerPresetMethods(d);

    if (ApiVcHost *host = vcHost())
        host->vcSetLiveListener(this);
}

ApiVcDomain::~ApiVcDomain()
{
    // ApiServer's parent (the host) outlives the server in both production (App deletes m_apiServer
    // explicitly in ~App()) and the test suites (server-then-host cleanup order) - detach so the host
    // never calls back into a destroyed listener.
    if (ApiVcHost *host = vcHost())
        host->vcSetLiveListener(nullptr);
}

ApiVcHost *ApiVcDomain::vcHost() const
{
    return dynamic_cast<ApiVcHost *>(m_server->parent());
}

bool ApiVcDomain::parseWidgetId(const QString &s, quint32 &outId)
{
    bool ok = false;
    quint32 v = s.toUInt(&ok);
    if (ok == false)
        return false;
    outId = v;
    return true;
}

bool ApiVcDomain::isSelfOrAncestorOf(ApiVcHost *host, quint32 ancestorCandidate, quint32 id) const
{
    quint32 current = id;
    while (current != ApiVcHost::InvalidWidgetId)
    {
        if (current == ancestorCandidate)
            return true;
        current = host->vcWidgetParentId(current);
    }
    return false;
}

/*****************************************************************************
 * vc.page.*
 *****************************************************************************/

void ApiVcDomain::registerPageMethods(ApiDispatcher *d)
{
    Doc *doc = m_doc;

    d->registerMethod(QStringLiteral("vc.page.list"), [this](ApiSession *session, const QString &id, const QJsonObject &params)
    {
        Q_UNUSED(params)
        ApiVcHost *host = vcHost();
        if (host == nullptr)
        {
            session->send(ApiEnvelope::buildErrorResponse(id, ApiEnvelope::ErrInternal, kHostUnavailable));
            return;
        }

        QJsonArray pages;
        for (int i = 0; i < host->vcPageCount(); i++)
            pages.append(host->vcPageSnapshot(i));

        QJsonObject result;
        result.insert(QStringLiteral("pages"), pages);
        result.insert(QStringLiteral("selectedPage"), host->vcSelectedPage());
        session->send(ApiEnvelope::buildOkResponse(id, result));
    });

    d->registerMethod(QStringLiteral("vc.page.create"), [this, doc](ApiSession *session, const QString &id, const QJsonObject &params)
    {
        ApiVcHost *host = vcHost();
        if (host == nullptr)
        {
            session->send(ApiEnvelope::buildErrorResponse(id, ApiEnvelope::ErrInternal, kHostUnavailable));
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

        int index = params.value(QStringLiteral("index")).toInt();
        if (index < 0 || index > host->vcPageCount())
            index = host->vcPageCount();

        host->vcAddPage(index);
        doc->setModified();

        QJsonObject data;
        data.insert(QStringLiteral("page"), host->vcPageSnapshot(index));
        data.insert(QStringLiteral("docRevision"), int(doc->docRevision()));
        m_server->broadcast(QStringLiteral("vc.page.created"), data, session->clientId(), false);

        QJsonObject result;
        result.insert(QStringLiteral("docRevision"), int(doc->docRevision()));
        session->send(ApiEnvelope::buildOkResponse(id, result));
    });

    d->registerMethod(QStringLiteral("vc.page.delete"), [this, doc](ApiSession *session, const QString &id, const QJsonObject &params)
    {
        ApiVcHost *host = vcHost();
        if (host == nullptr)
        {
            session->send(ApiEnvelope::buildErrorResponse(id, ApiEnvelope::ErrInternal, kHostUnavailable));
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

        int index = params.value(QStringLiteral("index")).toInt();
        if (index < 0 || index >= host->vcPageCount())
        {
            session->send(ApiEnvelope::buildErrorResponse(id, ApiEnvelope::ErrNotFound,
                                                            QStringLiteral("No such page")));
            return;
        }

        if (host->vcPageCount() == 1)
        {
            session->send(ApiEnvelope::buildErrorResponse(id, ApiEnvelope::ErrInvalidState,
                                                            QStringLiteral("Cannot delete the last page")));
            return;
        }

        QJsonArray deletedIds;
        host->vcDeletePage(index, deletedIds);
        doc->setModified();

        QJsonObject data;
        data.insert(QStringLiteral("index"), index);
        data.insert(QStringLiteral("deletedWidgetIds"), deletedIds);
        data.insert(QStringLiteral("docRevision"), int(doc->docRevision()));
        m_server->broadcast(QStringLiteral("vc.page.deleted"), data, session->clientId(), false);

        QJsonObject result;
        result.insert(QStringLiteral("docRevision"), int(doc->docRevision()));
        session->send(ApiEnvelope::buildOkResponse(id, result));
    });

    d->registerMethod(QStringLiteral("vc.page.rename"), [this, doc](ApiSession *session, const QString &id, const QJsonObject &params)
    {
        ApiVcHost *host = vcHost();
        if (host == nullptr)
        {
            session->send(ApiEnvelope::buildErrorResponse(id, ApiEnvelope::ErrInternal, kHostUnavailable));
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

        int index = params.value(QStringLiteral("index")).toInt();
        if (index < 0 || index >= host->vcPageCount())
        {
            session->send(ApiEnvelope::buildErrorResponse(id, ApiEnvelope::ErrNotFound,
                                                            QStringLiteral("No such page")));
            return;
        }

        QString name = params.value(QStringLiteral("name")).toString();
        host->vcRenamePage(index, name);
        doc->setModified();

        QJsonObject data;
        data.insert(QStringLiteral("index"), index);
        data.insert(QStringLiteral("name"), name);
        data.insert(QStringLiteral("docRevision"), int(doc->docRevision()));
        m_server->broadcast(QStringLiteral("vc.page.renamed"), data, session->clientId(), false);

        QJsonObject result;
        result.insert(QStringLiteral("docRevision"), int(doc->docRevision()));
        session->send(ApiEnvelope::buildOkResponse(id, result));
    });

    d->registerMethod(QStringLiteral("vc.page.setPin"), [this, doc](ApiSession *session, const QString &id, const QJsonObject &params)
    {
        ApiVcHost *host = vcHost();
        if (host == nullptr)
        {
            session->send(ApiEnvelope::buildErrorResponse(id, ApiEnvelope::ErrInternal, kHostUnavailable));
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

        int index = params.value(QStringLiteral("index")).toInt();
        if (index < 0 || index >= host->vcPageCount())
        {
            session->send(ApiEnvelope::buildErrorResponse(id, ApiEnvelope::ErrNotFound,
                                                            QStringLiteral("No such page")));
            return;
        }

        QString currentPin = params.value(QStringLiteral("currentPIN")).toString();
        QString newPin = params.value(QStringLiteral("newPIN")).toString();

        // A PIN must be either empty (clears protection) or exactly 4 digits - mirrors
        // VirtualConsole::setPagePIN()'s own validation.
        if (newPin.isEmpty() == false)
        {
            bool ok = false;
            newPin.toInt(&ok);
            if (ok == false || newPin.size() != 4)
            {
                session->send(ApiEnvelope::buildErrorResponse(id, ApiEnvelope::ErrInvalidParams,
                                                                QStringLiteral("newPIN must be empty or exactly 4 digits")));
                return;
            }
        }

        if (host->vcSetPagePin(index, currentPin, newPin) == false)
        {
            session->send(ApiEnvelope::buildErrorResponse(id, ApiEnvelope::ErrInvalidParams,
                                                            QStringLiteral("currentPIN does not match")));
            return;
        }

        doc->setModified();

        // vc.page.updated (VcPageUpdatedEvent, added 2026-09-27): the page's hasPin flag is what
        // every client draws on the page tab and checks before showing the page, and nothing else
        // would tell them it changed. The PIN itself is never broadcast.
        QJsonObject data;
        data.insert(QStringLiteral("page"), host->vcPageSnapshot(index));
        data.insert(QStringLiteral("docRevision"), int(doc->docRevision()));
        m_server->broadcast(QStringLiteral("vc.page.updated"), data, session->clientId(), false);

        QJsonObject result;
        result.insert(QStringLiteral("docRevision"), int(doc->docRevision()));
        session->send(ApiEnvelope::buildOkResponse(id, result));
    });

    d->registerMethod(QStringLiteral("vc.page.validatePin"), [this](ApiSession *session, const QString &id, const QJsonObject &params)
    {
        ApiVcHost *host = vcHost();
        if (host == nullptr)
        {
            session->send(ApiEnvelope::buildErrorResponse(id, ApiEnvelope::ErrInternal, kHostUnavailable));
            return;
        }

        int index = params.value(QStringLiteral("index")).toInt();
        QString pin = params.value(QStringLiteral("pin")).toString();

        bool valid = index >= 0 && index < host->vcPageCount() && host->vcValidatePagePin(index, pin);

        QJsonObject result;
        result.insert(QStringLiteral("valid"), valid);
        session->send(ApiEnvelope::buildOkResponse(id, result));
    });

    d->registerMethod(QStringLiteral("vc.page.select"), [this](ApiSession *session, const QString &id, const QJsonObject &params)
    {
        ApiVcHost *host = vcHost();
        if (host == nullptr)
        {
            session->send(ApiEnvelope::buildErrorResponse(id, ApiEnvelope::ErrInternal, kHostUnavailable));
            return;
        }

        int index = params.value(QStringLiteral("index")).toInt();
        if (index < 0 || index >= host->vcPageCount())
        {
            session->send(ApiEnvelope::buildErrorResponse(id, ApiEnvelope::ErrNotFound,
                                                            QStringLiteral("No such page")));
            return;
        }

        // Live/runtime (§4b) - no baseRevision, and must NOT touch docRevision (doc.h's own comment
        // on docRevision() is explicit that live/runtime state must never bump it).
        host->vcSetSelectedPage(index);
        session->send(ApiEnvelope::buildOkResponse(id, QJsonObject()));

        QJsonObject data;
        data.insert(QStringLiteral("index"), index);
        m_server->broadcast(QStringLiteral("vc.page.selected"), data, session->clientId(), false);
    });
}

/*****************************************************************************
 * vc.widget.*
 *****************************************************************************/

void ApiVcDomain::registerWidgetMethods(ApiDispatcher *d)
{
    Doc *doc = m_doc;

    d->registerMethod(QStringLiteral("vc.widget.list"), [this](ApiSession *session, const QString &id, const QJsonObject &params)
    {
        ApiVcHost *host = vcHost();
        if (host == nullptr)
        {
            session->send(ApiEnvelope::buildErrorResponse(id, ApiEnvelope::ErrInternal, kHostUnavailable));
            return;
        }

        bool hasPageFilter = params.contains(QStringLiteral("page"));
        int pageFilter = params.value(QStringLiteral("page")).toInt();

        bool hasParentFilter = params.contains(QStringLiteral("parentId"));
        quint32 parentFilter = ApiVcHost::InvalidWidgetId;
        if (hasParentFilter && parseWidgetId(params.value(QStringLiteral("parentId")).toString(), parentFilter) == false)
        {
            session->send(ApiEnvelope::buildErrorResponse(id, ApiEnvelope::ErrInvalidParams,
                                                            QStringLiteral("Invalid parentId")));
            return;
        }

        QSet<QString> typeFilters;
        for (const QJsonValue &v : params.value(QStringLiteral("typeFilters")).toArray())
            typeFilters.insert(v.toString());

        QList<quint32> ids = host->vcWidgetIds();
        std::sort(ids.begin(), ids.end());

        QJsonArray widgets;
        for (quint32 wid : ids)
        {
            if (hasPageFilter && host->vcWidgetPage(wid) != pageFilter)
                continue;
            if (hasParentFilter && host->vcWidgetParentId(wid) != parentFilter)
                continue;
            if (typeFilters.isEmpty() == false && typeFilters.contains(host->vcWidgetType(wid)) == false)
                continue;
            widgets.append(host->vcWidgetSnapshot(wid));
        }

        QJsonObject result;
        result.insert(QStringLiteral("widgets"), widgets);
        session->send(ApiEnvelope::buildOkResponse(id, result));
    });

    d->registerMethod(QStringLiteral("vc.widget.get"), [this](ApiSession *session, const QString &id, const QJsonObject &params)
    {
        ApiVcHost *host = vcHost();
        if (host == nullptr)
        {
            session->send(ApiEnvelope::buildErrorResponse(id, ApiEnvelope::ErrInternal, kHostUnavailable));
            return;
        }

        quint32 wid;
        if (parseWidgetId(params.value(QStringLiteral("widgetId")).toString(), wid) == false || host->vcWidgetExists(wid) == false)
        {
            session->send(ApiEnvelope::buildErrorResponse(id, ApiEnvelope::ErrNotFound,
                                                            QStringLiteral("No such widget")));
            return;
        }
        session->send(ApiEnvelope::buildOkResponse(id, host->vcWidgetSnapshot(wid)));
    });

    d->registerMethod(QStringLiteral("vc.widget.create"), [this, doc](ApiSession *session, const QString &id, const QJsonObject &params)
    {
        ApiVcHost *host = vcHost();
        if (host == nullptr)
        {
            session->send(ApiEnvelope::buildErrorResponse(id, ApiEnvelope::ErrInternal, kHostUnavailable));
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

        static const QStringList validTypes = {
            QStringLiteral("Button"), QStringLiteral("Slider"), QStringLiteral("XYPad"),
            QStringLiteral("Frame"), QStringLiteral("SoloFrame"), QStringLiteral("Label"),
            QStringLiteral("AudioTriggers"), QStringLiteral("Animation"), QStringLiteral("Clock"),
            QStringLiteral("CueList"), QStringLiteral("Speed")
        };

        QString widgetType = params.value(QStringLiteral("widgetType")).toString();
        if (validTypes.contains(widgetType) == false)
        {
            session->send(ApiEnvelope::buildErrorResponse(id, ApiEnvelope::ErrInvalidParams,
                                                            QStringLiteral("Invalid widgetType")));
            return;
        }

        if (params.contains(QStringLiteral("geometry")) == false)
        {
            session->send(ApiEnvelope::buildErrorResponse(id, ApiEnvelope::ErrInvalidParams,
                                                            QStringLiteral("geometry is required")));
            return;
        }

        int page = params.value(QStringLiteral("page")).toInt();
        if (page < 0 || page >= host->vcPageCount())
        {
            session->send(ApiEnvelope::buildErrorResponse(id, ApiEnvelope::ErrNotFound,
                                                            QStringLiteral("No such page")));
            return;
        }

        quint32 parentId = ApiVcHost::InvalidWidgetId;
        if (params.contains(QStringLiteral("parentId")))
        {
            QString parentIdStr = params.value(QStringLiteral("parentId")).toString();
            if (parseWidgetId(parentIdStr, parentId) == false || host->vcWidgetExists(parentId) == false)
            {
                session->send(ApiEnvelope::buildErrorResponse(id, ApiEnvelope::ErrNotFound,
                                                                QStringLiteral("No such parent widget")));
                return;
            }
            if (host->vcIsContainerWidget(parentId) == false)
            {
                session->send(ApiEnvelope::buildErrorResponse(id, ApiEnvelope::ErrInvalidParams,
                                                                QStringLiteral("parentId must be a Frame or SoloFrame widget")));
                return;
            }
        }

        QString error;
        quint32 wid = host->vcCreateWidget(widgetType, page, parentId,
                                            params.value(QStringLiteral("geometry")).toObject(),
                                            params.value(QStringLiteral("style")).toObject(),
                                            params.value(QStringLiteral("typeConfig")).toObject(),
                                            &error);
        if (wid == ApiVcHost::InvalidWidgetId)
        {
            session->send(ApiEnvelope::buildErrorResponse(id, ApiEnvelope::ErrInvalidParams,
                                                            error.isEmpty() ? QStringLiteral("Unable to create widget") : error));
            return;
        }

        doc->setModified();

        QJsonObject data;
        data.insert(QStringLiteral("widget"), host->vcWidgetSnapshot(wid));
        data.insert(QStringLiteral("docRevision"), int(doc->docRevision()));
        m_server->broadcast(QStringLiteral("vc.widget.created"), data, session->clientId(), false);

        QJsonObject result;
        result.insert(QStringLiteral("docRevision"), int(doc->docRevision()));
        result.insert(QStringLiteral("widgetId"), QString::number(wid));
        session->send(ApiEnvelope::buildOkResponse(id, result));
    });

    d->registerMethod(QStringLiteral("vc.widget.update"), [this, doc](ApiSession *session, const QString &id, const QJsonObject &params)
    {
        ApiVcHost *host = vcHost();
        if (host == nullptr)
        {
            session->send(ApiEnvelope::buildErrorResponse(id, ApiEnvelope::ErrInternal, kHostUnavailable));
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

        quint32 wid;
        if (parseWidgetId(params.value(QStringLiteral("widgetId")).toString(), wid) == false || host->vcWidgetExists(wid) == false)
        {
            session->send(ApiEnvelope::buildErrorResponse(id, ApiEnvelope::ErrNotFound,
                                                            QStringLiteral("No such widget")));
            return;
        }

        static const QStringList optionalFields = {
            QStringLiteral("geometry"), QStringLiteral("zIndex"), QStringLiteral("allowResize"),
            QStringLiteral("isDisabled"), QStringLiteral("isVisible"), QStringLiteral("page"),
            QStringLiteral("style")
        };
        bool hasAny = false;
        for (const QString &f : optionalFields)
        {
            if (params.contains(f))
            {
                hasAny = true;
                break;
            }
        }
        if (hasAny == false)
        {
            session->send(ApiEnvelope::buildErrorResponse(id, ApiEnvelope::ErrInvalidParams,
                                                            QStringLiteral("At least one field must be present")));
            return;
        }

        // Validate everything BEFORE mutating anything below - this handler must be all-or-nothing,
        // not leave a partial write behind an error response with no setModified()/broadcast/
        // revision bump to signal it.
        bool hasPage = params.contains(QStringLiteral("page"));
        int newPage = hasPage ? params.value(QStringLiteral("page")).toInt() : -1;
        if (hasPage)
        {
            if (newPage < 0 || newPage >= host->vcPageCount())
            {
                session->send(ApiEnvelope::buildErrorResponse(id, ApiEnvelope::ErrNotFound,
                                                                QStringLiteral("No such page")));
                return;
            }
            // A nested widget's page always mirrors its containing top-level ancestor's (see
            // vc.widget.reparent) - changing it directly here would desync it from its parent
            // (which stays behind) and leave a dangling parentId behind after a future
            // vc.page.delete on either page. Reparent the widget instead if it needs to move.
            if (host->vcWidgetParentId(wid) != ApiVcHost::InvalidWidgetId)
            {
                session->send(ApiEnvelope::buildErrorResponse(id, ApiEnvelope::ErrInvalidParams,
                                                                QStringLiteral("Cannot change page directly on a nested widget - use vc.widget.reparent")));
                return;
            }
        }

        QJsonObject commonFields;
        for (const QString &f : { QStringLiteral("geometry"), QStringLiteral("zIndex"), QStringLiteral("allowResize"),
                                   QStringLiteral("isDisabled"), QStringLiteral("isVisible"), QStringLiteral("style") })
        {
            if (params.contains(f))
                commonFields.insert(f, params.value(f));
        }

        QString error;
        if (commonFields.isEmpty() == false && host->vcUpdateWidgetCommon(wid, commonFields, &error) == false)
        {
            session->send(ApiEnvelope::buildErrorResponse(id, ApiEnvelope::ErrInvalidParams,
                                                            error.isEmpty() ? QStringLiteral("Unable to update widget") : error));
            return;
        }

        if (hasPage)
        {
            // Moving a Frame/SoloFrame moves its whole subtree with it. Already validated above:
            // $wid has no parent.
            host->vcMoveTopLevelWidgetToPage(wid, newPage);
        }

        doc->setModified();

        QJsonObject data;
        data.insert(QStringLiteral("widget"), host->vcWidgetSnapshot(wid));
        data.insert(QStringLiteral("docRevision"), int(doc->docRevision()));
        m_server->broadcast(QStringLiteral("vc.widget.updated"), data, session->clientId(), false);

        QJsonObject result;
        result.insert(QStringLiteral("docRevision"), int(doc->docRevision()));
        session->send(ApiEnvelope::buildOkResponse(id, result));
    });

    d->registerMethod(QStringLiteral("vc.widget.setConfig"), [this, doc](ApiSession *session, const QString &id, const QJsonObject &params)
    {
        ApiVcHost *host = vcHost();
        if (host == nullptr)
        {
            session->send(ApiEnvelope::buildErrorResponse(id, ApiEnvelope::ErrInternal, kHostUnavailable));
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

        quint32 wid;
        if (parseWidgetId(params.value(QStringLiteral("widgetId")).toString(), wid) == false || host->vcWidgetExists(wid) == false)
        {
            session->send(ApiEnvelope::buildErrorResponse(id, ApiEnvelope::ErrNotFound,
                                                            QStringLiteral("No such widget")));
            return;
        }

        // Partial patch merged onto the existing typeConfig, per the spec's own description of
        // vc.widget.setConfig - NOT a replace.
        QString error;
        if (host->vcSetWidgetConfig(wid, params.value(QStringLiteral("config")).toObject(), &error) == false)
        {
            session->send(ApiEnvelope::buildErrorResponse(id, ApiEnvelope::ErrInvalidParams,
                                                            error.isEmpty() ? QStringLiteral("Unable to update widget config") : error));
            return;
        }

        doc->setModified();

        QJsonObject data;
        data.insert(QStringLiteral("widget"), host->vcWidgetSnapshot(wid));
        data.insert(QStringLiteral("docRevision"), int(doc->docRevision()));
        m_server->broadcast(QStringLiteral("vc.widget.configChanged"), data, session->clientId(), false);

        QJsonObject result;
        result.insert(QStringLiteral("docRevision"), int(doc->docRevision()));
        session->send(ApiEnvelope::buildOkResponse(id, result));
    });

    d->registerMethod(QStringLiteral("vc.widget.delete"), [this, doc](ApiSession *session, const QString &id, const QJsonObject &params)
    {
        ApiVcHost *host = vcHost();
        if (host == nullptr)
        {
            session->send(ApiEnvelope::buildErrorResponse(id, ApiEnvelope::ErrInternal, kHostUnavailable));
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

        // Unknown ids are silently skipped by the host, same leniency as VirtualConsole::
        // deleteVCWidgets() itself - but if NONE of the requested ids resolved to a real widget,
        // that's a NOT_FOUND, not a silent no-op success.
        QList<quint32> ids;
        bool anyKnown = false;
        for (const QJsonValue &v : params.value(QStringLiteral("widgetIds")).toArray())
        {
            quint32 wid;
            if (parseWidgetId(v.toString(), wid) == false)
                continue;
            ids.append(wid);
            if (host->vcWidgetExists(wid))
                anyKnown = true;
        }

        if (anyKnown == false)
        {
            session->send(ApiEnvelope::buildErrorResponse(id, ApiEnvelope::ErrNotFound,
                                                            QStringLiteral("No such widget(s)")));
            return;
        }

        QJsonArray deletedIds;
        host->vcDeleteWidgets(ids, deletedIds);
        doc->setModified();

        QJsonObject data;
        data.insert(QStringLiteral("widgetIds"), deletedIds);
        data.insert(QStringLiteral("docRevision"), int(doc->docRevision()));
        m_server->broadcast(QStringLiteral("vc.widget.deleted"), data, session->clientId(), false);

        QJsonObject result;
        result.insert(QStringLiteral("docRevision"), int(doc->docRevision()));
        session->send(ApiEnvelope::buildOkResponse(id, result));
    });

    d->registerMethod(QStringLiteral("vc.widget.reparent"), [this, doc](ApiSession *session, const QString &id, const QJsonObject &params)
    {
        ApiVcHost *host = vcHost();
        if (host == nullptr)
        {
            session->send(ApiEnvelope::buildErrorResponse(id, ApiEnvelope::ErrInternal, kHostUnavailable));
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

        quint32 wid;
        if (parseWidgetId(params.value(QStringLiteral("widgetId")).toString(), wid) == false || host->vcWidgetExists(wid) == false)
        {
            session->send(ApiEnvelope::buildErrorResponse(id, ApiEnvelope::ErrNotFound,
                                                            QStringLiteral("No such widget")));
            return;
        }

        quint32 newParentId = ApiVcHost::InvalidWidgetId;
        if (params.contains(QStringLiteral("newParentId")))
        {
            QString newParentStr = params.value(QStringLiteral("newParentId")).toString();
            if (parseWidgetId(newParentStr, newParentId) == false || host->vcWidgetExists(newParentId) == false)
            {
                session->send(ApiEnvelope::buildErrorResponse(id, ApiEnvelope::ErrNotFound,
                                                                QStringLiteral("No such parent widget")));
                return;
            }
            if (host->vcIsContainerWidget(newParentId) == false)
            {
                session->send(ApiEnvelope::buildErrorResponse(id, ApiEnvelope::ErrInvalidParams,
                                                                QStringLiteral("newParentId must be a Frame or SoloFrame widget")));
                return;
            }
            // Reject moving a widget into itself, or into one of its own descendants - would
            // otherwise create a cycle in the parent chain.
            if (isSelfOrAncestorOf(host, wid, newParentId))
            {
                session->send(ApiEnvelope::buildErrorResponse(id, ApiEnvelope::ErrInvalidParams,
                                                                QStringLiteral("Cannot reparent a widget into its own descendant")));
                return;
            }
        }

        QJsonObject position = params.value(QStringLiteral("position")).toObject();
        QPointF newTopLeft(position.value(QStringLiteral("x")).toDouble(),
                            position.value(QStringLiteral("y")).toDouble());

        QString error;
        if (host->vcReparentWidget(wid, newParentId, newTopLeft, &error) == false)
        {
            session->send(ApiEnvelope::buildErrorResponse(id, ApiEnvelope::ErrInvalidParams,
                                                            error.isEmpty() ? QStringLiteral("Unable to reparent widget") : error));
            return;
        }

        doc->setModified();

        // Single-widget move - broadcast on vc.widget.updated, not bulkUpdated, per the spec's own
        // note on VcWidgetUpdatedEvent.
        QJsonObject data;
        data.insert(QStringLiteral("widget"), host->vcWidgetSnapshot(wid));
        data.insert(QStringLiteral("docRevision"), int(doc->docRevision()));
        m_server->broadcast(QStringLiteral("vc.widget.updated"), data, session->clientId(), false);

        QJsonObject result;
        result.insert(QStringLiteral("docRevision"), int(doc->docRevision()));
        session->send(ApiEnvelope::buildOkResponse(id, result));
    });

    d->registerMethod(QStringLiteral("vc.widget.reposition"), [this, doc](ApiSession *session, const QString &id, const QJsonObject &params)
    {
        ApiVcHost *host = vcHost();
        if (host == nullptr)
        {
            session->send(ApiEnvelope::buildErrorResponse(id, ApiEnvelope::ErrInternal, kHostUnavailable));
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

        QJsonArray items = params.value(QStringLiteral("widgets")).toArray();
        if (items.isEmpty())
        {
            session->send(ApiEnvelope::buildErrorResponse(id, ApiEnvelope::ErrInvalidParams,
                                                            QStringLiteral("widgets must not be empty")));
            return;
        }

        // Validate every widget exists before applying anything, so this bulk gesture-commit is
        // all-or-nothing rather than partially applied.
        QList<QPair<quint32, QJsonObject> > updates;
        for (const QJsonValue &v : items)
        {
            QJsonObject item = v.toObject();
            quint32 wid;
            if (parseWidgetId(item.value(QStringLiteral("widgetId")).toString(), wid) == false || host->vcWidgetExists(wid) == false)
            {
                session->send(ApiEnvelope::buildErrorResponse(id, ApiEnvelope::ErrNotFound,
                                                                QStringLiteral("No such widget")));
                return;
            }
            updates.append(qMakePair(wid, item.value(QStringLiteral("geometry")).toObject()));
        }

        host->vcRepositionWidgets(updates);
        doc->setModified();

        QJsonArray widgetsData;
        for (const auto &pair : updates)
        {
            QJsonObject entry;
            entry.insert(QStringLiteral("widgetId"), QString::number(pair.first));
            entry.insert(QStringLiteral("geometry"), pair.second);
            widgetsData.append(entry);
        }

        QJsonObject data;
        data.insert(QStringLiteral("widgets"), widgetsData);
        data.insert(QStringLiteral("docRevision"), int(doc->docRevision()));
        m_server->broadcast(QStringLiteral("vc.widget.repositioned"), data, session->clientId(), false);

        QJsonObject result;
        result.insert(QStringLiteral("docRevision"), int(doc->docRevision()));
        session->send(ApiEnvelope::buildOkResponse(id, result));
    });
}

/*****************************************************************************
 * Live interaction - vc.button.* / vc.slider.* / vc.cueList.* / vc.xyPad.* / vc.speedDial.* / vc.frame.*
 *****************************************************************************/

namespace {

const QString kWidgetId = QStringLiteral("widgetId");

/** Strict integer check: QJsonValue::toInt() happily turns "abc"/true/1.5 into 0/1/1, which would let
 *  a wrong-type field pass as a legitimate value. Live methods reject anything that isn't a whole
 *  JSON number instead. Bounded to qint32 because every caller then reads the value with
 *  QJsonValue::toInt(), which returns its default (0) for a whole number outside that range - so
 *  without this bound "value": 4294967296 would silently be applied as 0. */
bool jsonIsInteger(const QJsonValue &v)
{
    if (v.isDouble() == false)
        return false;
    double d = v.toDouble();
    // Range-check before the cast - converting e.g. 1e300 to an integer type is undefined behaviour.
    if (d < double(std::numeric_limits<qint32>::min()) || d > double(std::numeric_limits<qint32>::max()))
        return false;
    return d == double(qint32(d));
}

} // namespace

bool ApiVcDomain::resolveLiveWidget(ApiSession *session, const QString &id, const QJsonObject &params,
                                    const QStringList &allowedTypes, ApiVcHost **outHost, quint32 *outId)
{
    ApiVcHost *host = vcHost();
    if (host == nullptr)
    {
        session->send(ApiEnvelope::buildErrorResponse(id, ApiEnvelope::ErrInternal, kHostUnavailable));
        return false;
    }

    quint32 wid = ApiVcHost::InvalidWidgetId;
    if (parseWidgetId(params.value(kWidgetId).toString(), wid) == false || host->vcWidgetExists(wid) == false)
    {
        session->send(ApiEnvelope::buildErrorResponse(id, ApiEnvelope::ErrNotFound,
                                                        QStringLiteral("No such widget")));
        return false;
    }

    QString type = host->vcWidgetType(wid);
    if (allowedTypes.contains(type) == false)
    {
        QJsonObject details;
        details.insert(QStringLiteral("widgetType"), type);
        session->send(ApiEnvelope::buildErrorResponse(id, ApiEnvelope::ErrInvalidParams,
                                                        QStringLiteral("Widget %1 is a %2, not a %3")
                                                            .arg(QString::number(wid), type, allowedTypes.join(QStringLiteral("/"))),
                                                        details));
        return false;
    }

    // The on-screen widget disables its MouseArea/TouchArea while isDisabled - refuse remote input the
    // same way rather than letting the API bypass a deliberately disabled control.
    if (host->vcWidgetSnapshot(wid).value(QStringLiteral("isDisabled")).toBool())
    {
        session->send(ApiEnvelope::buildErrorResponse(id, ApiEnvelope::ErrInvalidState,
                                                        QStringLiteral("Widget is disabled")));
        return false;
    }

    *outHost = host;
    *outId = wid;
    return true;
}

void ApiVcDomain::broadcastLive(const QString &topic, quint32 widgetId, QJsonObject data)
{
    data.insert(kWidgetId, QString::number(widgetId));
    // Live (§4b) but low enough volume per event (one per discrete gesture/step; a fader drag is
    // already throttled by the sending client) that the contract delivers these to every session
    // rather than subscribe-gating them - see docs/api-spec/fragments/virtualconsole-notes.md.
    m_server->broadcast(topic, data, m_liveOriginClientId, false);
}

void ApiVcDomain::vcButtonStateChanged(quint32 widgetId, const QString &state)
{
    QJsonObject data;
    data.insert(QStringLiteral("state"), state);
    broadcastLive(QStringLiteral("vc.button.stateChanged"), widgetId, data);
}

void ApiVcDomain::vcSliderValueChanged(quint32 widgetId, int value)
{
    QJsonObject data;
    data.insert(QStringLiteral("value"), value);
    broadcastLive(QStringLiteral("vc.slider.valueChanged"), widgetId, data);
}

void ApiVcDomain::vcCueListPlaybackChanged(quint32 widgetId, int playbackIndex, bool running, bool paused)
{
    QJsonObject data;
    data.insert(QStringLiteral("playbackIndex"), playbackIndex);
    data.insert(QStringLiteral("running"), running);
    data.insert(QStringLiteral("paused"), paused);
    broadcastLive(QStringLiteral("vc.cueList.playbackChanged"), widgetId, data);
}

void ApiVcDomain::vcXyPadPositionChanged(quint32 widgetId, double x, double y)
{
    QJsonObject data;
    data.insert(QStringLiteral("x"), x);
    data.insert(QStringLiteral("y"), y);
    broadcastLive(QStringLiteral("vc.xyPad.positionChanged"), widgetId, data);
}

void ApiVcDomain::vcSpeedDialValueChanged(quint32 widgetId, int ms)
{
    QJsonObject data;
    data.insert(QStringLiteral("ms"), ms);
    broadcastLive(QStringLiteral("vc.speedDial.valueChanged"), widgetId, data);
}

void ApiVcDomain::vcFramePageChanged(quint32 widgetId, int page)
{
    QJsonObject data;
    data.insert(QStringLiteral("page"), page);
    // The one live event whose cause bumps docRevision (VCFrame::setCurrentPage() persists the
    // page, see vc.frame.gotoPage's spec note). Carrying the new revision here lets every client
    // re-sync from the event itself instead of paying a CONFLICT round trip on its next structural
    // request - the generic "any event carrying docRevision" client rule (00-conventions §4a) applies.
    data.insert(QStringLiteral("docRevision"), int(m_doc->docRevision()));
    broadcastLive(QStringLiteral("vc.frame.pageChanged"), widgetId, data);
}

void ApiVcDomain::registerLiveMethods(ApiDispatcher *d)
{
    static const QStringList buttonTypes = { QStringLiteral("Button") };
    static const QStringList sliderTypes = { QStringLiteral("Slider") };
    static const QStringList cueListTypes = { QStringLiteral("CueList") };
    static const QStringList xyPadTypes = { QStringLiteral("XYPad") };
    static const QStringList speedTypes = { QStringLiteral("Speed") };
    static const QStringList frameTypes = { QStringLiteral("Frame"), QStringLiteral("SoloFrame") };

    // Every live mutation below follows the same shape: validate params -> set m_liveOriginClientId ->
    // call the host (which reports the resulting change through the ApiVcLiveListener callbacks above,
    // broadcasting with that origin) -> clear the origin -> ack with {} or report the host's refusal.
    // The response is deliberately a bare {} per 00-conventions.md §4b; clients apply state from the
    // event, never from the response.

    // --- vc.button.press ---
    d->registerMethod(QStringLiteral("vc.button.press"), [this](ApiSession *session, const QString &id, const QJsonObject &params)
    {
        QJsonValue pressedValue = params.value(QStringLiteral("pressed"));
        if (pressedValue.isBool() == false)
        {
            session->send(ApiEnvelope::buildErrorResponse(id, ApiEnvelope::ErrInvalidParams,
                                                            QStringLiteral("pressed must be a boolean")));
            return;
        }

        ApiVcHost *host = nullptr;
        quint32 wid = ApiVcHost::InvalidWidgetId;
        if (resolveLiveWidget(session, id, params, buttonTypes, &host, &wid) == false)
            return;

        QString error;
        m_liveOriginClientId = session->clientId();
        bool ok = host->vcButtonPress(wid, pressedValue.toBool(), &error);
        m_liveOriginClientId.clear();

        if (ok == false)
        {
            session->send(ApiEnvelope::buildErrorResponse(id, ApiEnvelope::ErrInvalidState, error));
            return;
        }
        session->send(ApiEnvelope::buildOkResponse(id, QJsonObject()));
    });

    // --- vc.slider.setValue ---
    d->registerMethod(QStringLiteral("vc.slider.setValue"), [this](ApiSession *session, const QString &id, const QJsonObject &params)
    {
        QJsonValue v = params.value(QStringLiteral("value"));
        if (jsonIsInteger(v) == false || v.toInt() < 0 || v.toInt() > 255)
        {
            session->send(ApiEnvelope::buildErrorResponse(id, ApiEnvelope::ErrInvalidParams,
                                                            QStringLiteral("value must be an integer 0..255")));
            return;
        }

        ApiVcHost *host = nullptr;
        quint32 wid = ApiVcHost::InvalidWidgetId;
        if (resolveLiveWidget(session, id, params, sliderTypes, &host, &wid) == false)
            return;

        QString error;
        m_liveOriginClientId = session->clientId();
        bool ok = host->vcSliderSetValue(wid, v.toInt(), &error);
        m_liveOriginClientId.clear();

        if (ok == false)
        {
            session->send(ApiEnvelope::buildErrorResponse(id, ApiEnvelope::ErrInvalidState, error));
            return;
        }
        session->send(ApiEnvelope::buildOkResponse(id, QJsonObject()));
    });

    // --- vc.cueList.play / stop / next / previous ---
    struct CueListMethod { const char *method; ApiVcHost::CueListAction action; };
    static const CueListMethod cueListMethods[] = {
        { "vc.cueList.play", ApiVcHost::CueListPlay },
        { "vc.cueList.stop", ApiVcHost::CueListStop },
        { "vc.cueList.next", ApiVcHost::CueListNext },
        { "vc.cueList.previous", ApiVcHost::CueListPrevious },
    };
    for (const CueListMethod &m : cueListMethods)
    {
        ApiVcHost::CueListAction action = m.action;
        d->registerMethod(QString::fromLatin1(m.method), [this, action](ApiSession *session, const QString &id, const QJsonObject &params)
        {
            ApiVcHost *host = nullptr;
            quint32 wid = ApiVcHost::InvalidWidgetId;
            if (resolveLiveWidget(session, id, params, cueListTypes, &host, &wid) == false)
                return;

            QString error;
            m_liveOriginClientId = session->clientId();
            bool ok = host->vcCueListAction(wid, action, &error);
            m_liveOriginClientId.clear();

            if (ok == false)
            {
                session->send(ApiEnvelope::buildErrorResponse(id, ApiEnvelope::ErrInvalidState, error));
                return;
            }
            session->send(ApiEnvelope::buildOkResponse(id, QJsonObject()));
        });
    }

    // --- vc.cueList.setPlaybackIndex ---
    d->registerMethod(QStringLiteral("vc.cueList.setPlaybackIndex"), [this](ApiSession *session, const QString &id, const QJsonObject &params)
    {
        // "index" per the web UI contract; "playbackIndex" is the spec's original spelling, kept as an
        // accepted alias so a client written against the pre-implementation spec still works.
        QJsonValue v = params.contains(QStringLiteral("index")) ? params.value(QStringLiteral("index"))
                                                                : params.value(QStringLiteral("playbackIndex"));
        if (jsonIsInteger(v) == false)
        {
            session->send(ApiEnvelope::buildErrorResponse(id, ApiEnvelope::ErrInvalidParams,
                                                            QStringLiteral("index must be an integer")));
            return;
        }

        ApiVcHost *host = nullptr;
        quint32 wid = ApiVcHost::InvalidWidgetId;
        if (resolveLiveWidget(session, id, params, cueListTypes, &host, &wid) == false)
            return;

        int index = v.toInt();
        int stepCount = host->vcCueListSnapshot(wid).value(QStringLiteral("steps")).toArray().size();
        if (index < -1 || index >= stepCount)
        {
            QJsonObject details;
            details.insert(QStringLiteral("stepCount"), stepCount);
            session->send(ApiEnvelope::buildErrorResponse(id, ApiEnvelope::ErrInvalidParams,
                                                            QStringLiteral("index must be -1 or 0..%1").arg(stepCount - 1),
                                                            details));
            return;
        }

        QString error;
        m_liveOriginClientId = session->clientId();
        bool ok = host->vcCueListSetPlaybackIndex(wid, index, &error);
        m_liveOriginClientId.clear();

        if (ok == false)
        {
            session->send(ApiEnvelope::buildErrorResponse(id, ApiEnvelope::ErrInvalidState, error));
            return;
        }
        session->send(ApiEnvelope::buildOkResponse(id, QJsonObject()));
    });

    // --- vc.cueList.get ---
    d->registerMethod(QStringLiteral("vc.cueList.get"), [this](ApiSession *session, const QString &id, const QJsonObject &params)
    {
        ApiVcHost *host = vcHost();
        if (host == nullptr)
        {
            session->send(ApiEnvelope::buildErrorResponse(id, ApiEnvelope::ErrInternal, kHostUnavailable));
            return;
        }

        // Read-only: a disabled cue list can still be inspected, so this doesn't go through
        // resolveLiveWidget()'s isDisabled refusal.
        quint32 wid = ApiVcHost::InvalidWidgetId;
        if (parseWidgetId(params.value(kWidgetId).toString(), wid) == false || host->vcWidgetExists(wid) == false)
        {
            session->send(ApiEnvelope::buildErrorResponse(id, ApiEnvelope::ErrNotFound,
                                                            QStringLiteral("No such widget")));
            return;
        }
        if (host->vcWidgetType(wid) != QStringLiteral("CueList"))
        {
            session->send(ApiEnvelope::buildErrorResponse(id, ApiEnvelope::ErrInvalidParams,
                                                            QStringLiteral("Widget is not a CueList")));
            return;
        }
        session->send(ApiEnvelope::buildOkResponse(id, host->vcCueListSnapshot(wid)));
    });

    // --- vc.xyPad.setPosition ---
    d->registerMethod(QStringLiteral("vc.xyPad.setPosition"), [this](ApiSession *session, const QString &id, const QJsonObject &params)
    {
        QJsonValue xv = params.value(QStringLiteral("x"));
        QJsonValue yv = params.value(QStringLiteral("y"));
        if (xv.isDouble() == false || yv.isDouble() == false ||
            xv.toDouble() < 0.0 || xv.toDouble() > 1.0 || yv.toDouble() < 0.0 || yv.toDouble() > 1.0)
        {
            session->send(ApiEnvelope::buildErrorResponse(id, ApiEnvelope::ErrInvalidParams,
                                                            QStringLiteral("x and y must be numbers 0.0..1.0")));
            return;
        }

        ApiVcHost *host = nullptr;
        quint32 wid = ApiVcHost::InvalidWidgetId;
        if (resolveLiveWidget(session, id, params, xyPadTypes, &host, &wid) == false)
            return;

        QString error;
        m_liveOriginClientId = session->clientId();
        bool ok = host->vcXyPadSetPosition(wid, xv.toDouble(), yv.toDouble(), &error);
        m_liveOriginClientId.clear();

        if (ok == false)
        {
            session->send(ApiEnvelope::buildErrorResponse(id, ApiEnvelope::ErrInvalidState, error));
            return;
        }
        session->send(ApiEnvelope::buildOkResponse(id, QJsonObject()));
    });

    // --- vc.speedDial.setValue ---
    d->registerMethod(QStringLiteral("vc.speedDial.setValue"), [this](ApiSession *session, const QString &id, const QJsonObject &params)
    {
        // "ms" per the web UI contract; "valueMs" was the spec's original spelling (vc.speedDial.setCurrentTime).
        QJsonValue v = params.contains(QStringLiteral("ms")) ? params.value(QStringLiteral("ms"))
                                                             : params.value(QStringLiteral("valueMs"));
        if (jsonIsInteger(v) == false || v.toDouble() < 0)
        {
            session->send(ApiEnvelope::buildErrorResponse(id, ApiEnvelope::ErrInvalidParams,
                                                            QStringLiteral("ms must be a non-negative integer")));
            return;
        }

        ApiVcHost *host = nullptr;
        quint32 wid = ApiVcHost::InvalidWidgetId;
        if (resolveLiveWidget(session, id, params, speedTypes, &host, &wid) == false)
            return;

        QString error;
        m_liveOriginClientId = session->clientId();
        bool ok = host->vcSpeedDialSetValue(wid, v.toInt(), &error);
        m_liveOriginClientId.clear();

        if (ok == false)
        {
            session->send(ApiEnvelope::buildErrorResponse(id, ApiEnvelope::ErrInvalidState, error));
            return;
        }
        session->send(ApiEnvelope::buildOkResponse(id, QJsonObject()));
    });

    // --- vc.speedDial.tap ---
    d->registerMethod(QStringLiteral("vc.speedDial.tap"), [this](ApiSession *session, const QString &id, const QJsonObject &params)
    {
        ApiVcHost *host = nullptr;
        quint32 wid = ApiVcHost::InvalidWidgetId;
        if (resolveLiveWidget(session, id, params, speedTypes, &host, &wid) == false)
            return;

        QString error;
        m_liveOriginClientId = session->clientId();
        bool ok = host->vcSpeedDialTap(wid, &error);
        m_liveOriginClientId.clear();

        if (ok == false)
        {
            session->send(ApiEnvelope::buildErrorResponse(id, ApiEnvelope::ErrInvalidState, error));
            return;
        }
        session->send(ApiEnvelope::buildOkResponse(id, QJsonObject()));
    });

    // --- vc.frame.gotoPage ---
    d->registerMethod(QStringLiteral("vc.frame.gotoPage"), [this](ApiSession *session, const QString &id, const QJsonObject &params)
    {
        // "page" per the web UI contract; "pageIndex" was the spec's original spelling.
        QJsonValue v = params.contains(QStringLiteral("page")) ? params.value(QStringLiteral("page"))
                                                               : params.value(QStringLiteral("pageIndex"));
        if (jsonIsInteger(v) == false)
        {
            session->send(ApiEnvelope::buildErrorResponse(id, ApiEnvelope::ErrInvalidParams,
                                                            QStringLiteral("page must be an integer")));
            return;
        }

        ApiVcHost *host = nullptr;
        quint32 wid = ApiVcHost::InvalidWidgetId;
        if (resolveLiveWidget(session, id, params, frameTypes, &host, &wid) == false)
            return;

        // NOTE: unlike every other live method here, VCFrame::setCurrentPage() calls setDocModified()
        // (the frame's current page is saved in the show file), so a successful page flip DOES bump
        // docRevision as a side effect - other clients' next structural request will CONFLICT until
        // they refresh. Documented in the spec; not worked around here because that is the engine's
        // own, deliberate persistence behaviour.
        QString error;
        m_liveOriginClientId = session->clientId();
        bool ok = host->vcFrameGotoPage(wid, v.toInt(), &error);
        m_liveOriginClientId.clear();

        if (ok == false)
        {
            session->send(ApiEnvelope::buildErrorResponse(id, ApiEnvelope::ErrInvalidParams, error));
            return;
        }
        session->send(ApiEnvelope::buildOkResponse(id, QJsonObject()));
    });

    // --- vc.frame.get ---
    d->registerMethod(QStringLiteral("vc.frame.get"), [this](ApiSession *session, const QString &id, const QJsonObject &params)
    {
        ApiVcHost *host = vcHost();
        if (host == nullptr)
        {
            session->send(ApiEnvelope::buildErrorResponse(id, ApiEnvelope::ErrInternal, kHostUnavailable));
            return;
        }

        quint32 wid = ApiVcHost::InvalidWidgetId;
        if (parseWidgetId(params.value(kWidgetId).toString(), wid) == false || host->vcWidgetExists(wid) == false)
        {
            session->send(ApiEnvelope::buildErrorResponse(id, ApiEnvelope::ErrNotFound,
                                                            QStringLiteral("No such widget")));
            return;
        }
        if (frameTypes.contains(host->vcWidgetType(wid)) == false)
        {
            session->send(ApiEnvelope::buildErrorResponse(id, ApiEnvelope::ErrInvalidParams,
                                                            QStringLiteral("Widget is not a Frame or SoloFrame")));
            return;
        }
        session->send(ApiEnvelope::buildOkResponse(id, host->vcFrameSnapshot(wid)));
    });
}

/*****************************************************************************
 * Cue List side fader / Speed Dial extras - vc.cueList.setSideFaderLevel, vc.speedDial.setFactor/apply/resetTap
 *****************************************************************************/

namespace {

// VcSpeedDialMultiplier wire spellings, in VCSpeedDial::SpeedMultiplier order.
const QStringList kSpeedDialMultipliers = {
    QStringLiteral("None"), QStringLiteral("Zero"), QStringLiteral("OneSixteenth"), QStringLiteral("OneEighth"),
    QStringLiteral("OneFourth"), QStringLiteral("Half"), QStringLiteral("One"), QStringLiteral("Two"),
    QStringLiteral("Four"), QStringLiteral("Eight"), QStringLiteral("Sixteen")
};

/** vc.<type>.presetsChanged topic for a preset-capable wire widget type, empty otherwise. */
QString presetsChangedTopic(const QString &widgetType)
{
    if (widgetType == QStringLiteral("Speed"))
        return QStringLiteral("vc.speedDial.presetsChanged");
    if (widgetType == QStringLiteral("XYPad"))
        return QStringLiteral("vc.xyPad.presetsChanged");
    if (widgetType == QStringLiteral("Animation"))
        return QStringLiteral("vc.animation.presetsChanged");
    return QString();
}

bool presetListContains(const QJsonArray &presets, int presetId)
{
    for (const QJsonValue &v : presets)
    {
        if (v.toObject().value(QStringLiteral("presetId")).toInt(-1) == presetId)
            return true;
    }
    return false;
}

} // namespace

void ApiVcDomain::vcCueListSideFaderChanged(quint32 widgetId, int level, int nextStepIndex, bool primaryTop)
{
    QJsonObject data;
    data.insert(QStringLiteral("level"), level);
    data.insert(QStringLiteral("nextStepIndex"), nextStepIndex);
    data.insert(QStringLiteral("primaryTop"), primaryTop);
    broadcastLive(QStringLiteral("vc.cueList.sideFaderChanged"), widgetId, data);
}

void ApiVcDomain::vcSpeedDialFactorChanged(quint32 widgetId, const QString &factor)
{
    QJsonObject data;
    data.insert(QStringLiteral("factor"), factor);
    broadcastLive(QStringLiteral("vc.speedDial.factorChanged"), widgetId, data);
}

void ApiVcDomain::vcSpeedDialTapChanged(quint32 widgetId, int tapTimeValue, int currentTimeMs)
{
    QJsonObject data;
    data.insert(QStringLiteral("tapTimeValue"), tapTimeValue);
    data.insert(QStringLiteral("currentTimeMs"), currentTimeMs);
    broadcastLive(QStringLiteral("vc.speedDial.tapChanged"), widgetId, data);
}

void ApiVcDomain::registerCueSpeedDialMethods(ApiDispatcher *d)
{
    static const QStringList cueListTypes = { QStringLiteral("CueList") };
    static const QStringList speedTypes = { QStringLiteral("Speed") };

    // --- vc.cueList.setSideFaderLevel ---
    d->registerMethod(QStringLiteral("vc.cueList.setSideFaderLevel"), [this](ApiSession *session, const QString &id, const QJsonObject &params)
    {
        QJsonValue v = params.value(QStringLiteral("level"));
        if (jsonIsInteger(v) == false || v.toInt() < 0 || v.toInt() > 255)
        {
            session->send(ApiEnvelope::buildErrorResponse(id, ApiEnvelope::ErrInvalidParams,
                                                            QStringLiteral("level must be an integer 0..255")));
            return;
        }

        ApiVcHost *host = nullptr;
        quint32 wid = ApiVcHost::InvalidWidgetId;
        if (resolveLiveWidget(session, id, params, cueListTypes, &host, &wid) == false)
            return;

        QString error;
        m_liveOriginClientId = session->clientId();
        bool ok = host->vcCueListSetSideFaderLevel(wid, v.toInt(), &error);
        m_liveOriginClientId.clear();

        if (ok == false)
        {
            session->send(ApiEnvelope::buildErrorResponse(id, ApiEnvelope::ErrInvalidState, error));
            return;
        }
        session->send(ApiEnvelope::buildOkResponse(id, QJsonObject()));
    });

    // --- vc.speedDial.setFactor ---
    d->registerMethod(QStringLiteral("vc.speedDial.setFactor"), [this](ApiSession *session, const QString &id, const QJsonObject &params)
    {
        // The dial's own factor is what the on-screen 1/16..16 buttons and +/- set; None/Zero exist in
        // VcSpeedDialMultiplier only as per-function "(Not sent)"/0 overrides (VcSpeedDialConfig.functions)
        // and would multiply every attached Function's speed by 0 here.
        QString factor = params.value(QStringLiteral("factor")).toString();
        int index = kSpeedDialMultipliers.indexOf(factor);
        if (index < kSpeedDialMultipliers.indexOf(QStringLiteral("OneSixteenth")))
        {
            session->send(ApiEnvelope::buildErrorResponse(id, ApiEnvelope::ErrInvalidParams,
                                                            QStringLiteral("factor must be one of OneSixteenth, OneEighth, OneFourth, Half, One, Two, Four, Eight, Sixteen")));
            return;
        }

        ApiVcHost *host = nullptr;
        quint32 wid = ApiVcHost::InvalidWidgetId;
        if (resolveLiveWidget(session, id, params, speedTypes, &host, &wid) == false)
            return;

        QString error;
        m_liveOriginClientId = session->clientId();
        bool ok = host->vcSpeedDialSetFactor(wid, factor, &error);
        m_liveOriginClientId.clear();

        if (ok == false)
        {
            session->send(ApiEnvelope::buildErrorResponse(id, ApiEnvelope::ErrInvalidState, error));
            return;
        }
        session->send(ApiEnvelope::buildOkResponse(id, QJsonObject()));
    });

    // --- vc.speedDial.apply / vc.speedDial.resetTap ---
    struct SpeedDialMethod { const char *method; bool (ApiVcHost::*call)(quint32, QString *); };
    static const SpeedDialMethod speedDialMethods[] = {
        { "vc.speedDial.apply", &ApiVcHost::vcSpeedDialApply },
        { "vc.speedDial.resetTap", &ApiVcHost::vcSpeedDialResetTap },
    };
    for (const SpeedDialMethod &m : speedDialMethods)
    {
        bool (ApiVcHost::*call)(quint32, QString *) = m.call;
        d->registerMethod(QString::fromLatin1(m.method), [this, call](ApiSession *session, const QString &id, const QJsonObject &params)
        {
            ApiVcHost *host = nullptr;
            quint32 wid = ApiVcHost::InvalidWidgetId;
            if (resolveLiveWidget(session, id, params, speedTypes, &host, &wid) == false)
                return;

            QString error;
            m_liveOriginClientId = session->clientId();
            bool ok = (host->*call)(wid, &error);
            m_liveOriginClientId.clear();

            if (ok == false)
            {
                session->send(ApiEnvelope::buildErrorResponse(id, ApiEnvelope::ErrInvalidState, error));
                return;
            }
            session->send(ApiEnvelope::buildOkResponse(id, QJsonObject()));
        });
    }
}

/*****************************************************************************
 * Widget presets - vc.widget.preset.add/apply/remove, vc.speedDial.preset.update
 *****************************************************************************/

bool ApiVcDomain::resolvePresetWidget(ApiSession *session, const QString &id, const QJsonObject &params, bool checkRevision,
                                      ApiVcHost **outHost, quint32 *outId)
{
    ApiVcHost *host = vcHost();
    if (host == nullptr)
    {
        session->send(ApiEnvelope::buildErrorResponse(id, ApiEnvelope::ErrInternal, kHostUnavailable));
        return false;
    }

    if (checkRevision)
    {
        quint32 baseRevision = quint32(params.value(QStringLiteral("baseRevision")).toInt());
        if (baseRevision != m_doc->docRevision())
        {
            QJsonObject details;
            details.insert(QStringLiteral("docRevision"), int(m_doc->docRevision()));
            session->send(ApiEnvelope::buildErrorResponse(id, ApiEnvelope::ErrConflict,
                                                            QStringLiteral("baseRevision is stale"), details));
            return false;
        }
    }

    quint32 wid = ApiVcHost::InvalidWidgetId;
    if (parseWidgetId(params.value(kWidgetId).toString(), wid) == false || host->vcWidgetExists(wid) == false)
    {
        session->send(ApiEnvelope::buildErrorResponse(id, ApiEnvelope::ErrNotFound,
                                                        QStringLiteral("No such widget")));
        return false;
    }

    if (host->vcWidgetSupportsPresets(wid) == false)
    {
        QJsonObject details;
        details.insert(QStringLiteral("widgetType"), host->vcWidgetType(wid));
        session->send(ApiEnvelope::buildErrorResponse(id, ApiEnvelope::ErrInvalidParams,
                                                        QStringLiteral("Widget %1 is a %2, which has no presets (Speed, XYPad and Animation widgets do)")
                                                            .arg(QString::number(wid), host->vcWidgetType(wid)),
                                                        details));
        return false;
    }

    *outHost = host;
    *outId = wid;
    return true;
}

void ApiVcDomain::broadcastPresetsChanged(ApiVcHost *host, quint32 widgetId, const QString &originClientId)
{
    QJsonObject data;
    data.insert(kWidgetId, QString::number(widgetId));
    data.insert(QStringLiteral("presets"), host->vcWidgetPresets(widgetId));
    data.insert(QStringLiteral("docRevision"), int(m_doc->docRevision()));
    // Document-state (§4a) event: always delivered to every session, like vc.widget.configChanged.
    m_server->broadcast(presetsChangedTopic(host->vcWidgetType(widgetId)), data, originClientId, false);
}

void ApiVcDomain::registerPresetMethods(ApiDispatcher *d)
{
    Doc *doc = m_doc;

    // --- vc.widget.preset.add ---
    d->registerMethod(QStringLiteral("vc.widget.preset.add"), [this, doc](ApiSession *session, const QString &id, const QJsonObject &params)
    {
        ApiVcHost *host = nullptr;
        quint32 wid = ApiVcHost::InvalidWidgetId;
        if (resolvePresetWidget(session, id, params, true, &host, &wid) == false)
            return;

        QJsonValue presetValue = params.value(QStringLiteral("preset"));
        if (presetValue.isObject() == false)
        {
            session->send(ApiEnvelope::buildErrorResponse(id, ApiEnvelope::ErrInvalidParams,
                                                            QStringLiteral("preset must be an object")));
            return;
        }

        QString error;
        int presetId = host->vcWidgetPresetAdd(wid, presetValue.toObject(), &error);
        if (presetId < 0)
        {
            session->send(ApiEnvelope::buildErrorResponse(id, ApiEnvelope::ErrInvalidParams,
                                                            error.isEmpty() ? QStringLiteral("Unable to add preset") : error));
            return;
        }

        doc->setModified();
        broadcastPresetsChanged(host, wid, session->clientId());

        QJsonObject result;
        result.insert(QStringLiteral("docRevision"), int(doc->docRevision()));
        result.insert(QStringLiteral("presetId"), presetId);
        session->send(ApiEnvelope::buildOkResponse(id, result));
    });

    // --- vc.widget.preset.remove ---
    d->registerMethod(QStringLiteral("vc.widget.preset.remove"), [this, doc](ApiSession *session, const QString &id, const QJsonObject &params)
    {
        ApiVcHost *host = nullptr;
        quint32 wid = ApiVcHost::InvalidWidgetId;
        if (resolvePresetWidget(session, id, params, true, &host, &wid) == false)
            return;

        QJsonValue pv = params.value(QStringLiteral("presetId"));
        if (jsonIsInteger(pv) == false || presetListContains(host->vcWidgetPresets(wid), pv.toInt()) == false)
        {
            session->send(ApiEnvelope::buildErrorResponse(id, ApiEnvelope::ErrNotFound,
                                                            QStringLiteral("No such preset")));
            return;
        }

        QString error;
        if (host->vcWidgetPresetRemove(wid, pv.toInt(), &error) == false)
        {
            session->send(ApiEnvelope::buildErrorResponse(id, ApiEnvelope::ErrInvalidState,
                                                            error.isEmpty() ? QStringLiteral("Unable to remove preset") : error));
            return;
        }

        doc->setModified();
        broadcastPresetsChanged(host, wid, session->clientId());

        QJsonObject result;
        result.insert(QStringLiteral("docRevision"), int(doc->docRevision()));
        session->send(ApiEnvelope::buildOkResponse(id, result));
    });

    // --- vc.widget.preset.apply (live, §4b) ---
    d->registerMethod(QStringLiteral("vc.widget.preset.apply"), [this](ApiSession *session, const QString &id, const QJsonObject &params)
    {
        ApiVcHost *host = nullptr;
        quint32 wid = ApiVcHost::InvalidWidgetId;
        if (resolvePresetWidget(session, id, params, false, &host, &wid) == false)
            return;

        QJsonValue pv = params.value(QStringLiteral("presetId"));
        if (jsonIsInteger(pv) == false || presetListContains(host->vcWidgetPresets(wid), pv.toInt()) == false)
        {
            session->send(ApiEnvelope::buildErrorResponse(id, ApiEnvelope::ErrNotFound,
                                                            QStringLiteral("No such preset")));
            return;
        }

        // Same refusal as every other live method for a disabled widget (resolveLiveWidget()).
        if (host->vcWidgetSnapshot(wid).value(QStringLiteral("isDisabled")).toBool())
        {
            session->send(ApiEnvelope::buildErrorResponse(id, ApiEnvelope::ErrInvalidState,
                                                            QStringLiteral("Widget is disabled")));
            return;
        }

        QString error;
        m_liveOriginClientId = session->clientId();
        bool ok = host->vcWidgetPresetApply(wid, pv.toInt(), &error);
        m_liveOriginClientId.clear();

        if (ok == false)
        {
            session->send(ApiEnvelope::buildErrorResponse(id, ApiEnvelope::ErrInvalidState,
                                                            error.isEmpty() ? QStringLiteral("Unable to apply preset") : error));
            return;
        }
        session->send(ApiEnvelope::buildOkResponse(id, QJsonObject()));
    });

    // --- vc.speedDial.preset.update ---
    d->registerMethod(QStringLiteral("vc.speedDial.preset.update"), [this, doc](ApiSession *session, const QString &id, const QJsonObject &params)
    {
        ApiVcHost *host = nullptr;
        quint32 wid = ApiVcHost::InvalidWidgetId;
        if (resolvePresetWidget(session, id, params, true, &host, &wid) == false)
            return;

        if (host->vcWidgetType(wid) != QStringLiteral("Speed"))
        {
            session->send(ApiEnvelope::buildErrorResponse(id, ApiEnvelope::ErrInvalidParams,
                                                            QStringLiteral("Widget is not a Speed dial")));
            return;
        }

        QJsonValue pv = params.value(QStringLiteral("presetId"));
        if (jsonIsInteger(pv) == false || presetListContains(host->vcWidgetPresets(wid), pv.toInt()) == false)
        {
            session->send(ApiEnvelope::buildErrorResponse(id, ApiEnvelope::ErrNotFound,
                                                            QStringLiteral("No such preset")));
            return;
        }

        QJsonObject patch;
        if (params.contains(QStringLiteral("name")))
        {
            QJsonValue nv = params.value(QStringLiteral("name"));
            if (nv.isString() == false || nv.toString().isEmpty())
            {
                session->send(ApiEnvelope::buildErrorResponse(id, ApiEnvelope::ErrInvalidParams,
                                                                QStringLiteral("name must be a non-empty string")));
                return;
            }
            patch.insert(QStringLiteral("name"), nv);
        }
        if (params.contains(QStringLiteral("valueMs")))
        {
            QJsonValue mv = params.value(QStringLiteral("valueMs"));
            if (jsonIsInteger(mv) == false || mv.toInt() < 0)
            {
                session->send(ApiEnvelope::buildErrorResponse(id, ApiEnvelope::ErrInvalidParams,
                                                                QStringLiteral("valueMs must be a non-negative integer")));
                return;
            }
            patch.insert(QStringLiteral("valueMs"), mv);
        }
        if (patch.isEmpty())
        {
            session->send(ApiEnvelope::buildErrorResponse(id, ApiEnvelope::ErrInvalidParams,
                                                            QStringLiteral("At least one of name, valueMs is required")));
            return;
        }

        QString error;
        if (host->vcSpeedDialPresetUpdate(wid, pv.toInt(), patch, &error) == false)
        {
            session->send(ApiEnvelope::buildErrorResponse(id, ApiEnvelope::ErrInvalidParams,
                                                            error.isEmpty() ? QStringLiteral("Unable to update preset") : error));
            return;
        }

        doc->setModified();
        broadcastPresetsChanged(host, wid, session->clientId());

        QJsonObject result;
        result.insert(QStringLiteral("docRevision"), int(doc->docRevision()));
        session->send(ApiEnvelope::buildOkResponse(id, result));
    });
}
