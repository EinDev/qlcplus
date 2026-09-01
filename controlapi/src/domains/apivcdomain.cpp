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

        // Note: docs/api-spec/fragments/virtualconsole.yaml defines no broadcast event for
        // vc.page.setPin (unlike create/delete/rename) - see apivcdomain-notes.md. Other clients
        // observe the new hasPin flag on their next vc.page.list.
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
