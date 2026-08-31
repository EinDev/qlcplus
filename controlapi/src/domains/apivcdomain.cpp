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
#include <QSet>
#include <QPair>
#include <algorithm>
#include <limits>

#include "apivcdomain.h"
#include "apiserver.h"
#include "apisession.h"
#include "apidispatcher.h"
#include "apienvelope.h"
#include "doc.h"

const quint32 ApiVcDomain::InvalidWidgetId = std::numeric_limits<quint32>::max();
const QStringList ApiVcDomain::ContainerWidgetTypes = { QStringLiteral("Frame"), QStringLiteral("SoloFrame") };

ApiVcDomain::ApiVcDomain(Doc *doc, ApiServer *server, QObject *parent)
    : QObject(parent)
    , m_doc(doc)
    , m_server(server)
    , m_selectedPage(0)
    , m_nextWidgetId(0)
{
    Q_ASSERT(m_doc != nullptr);
    Q_ASSERT(m_server != nullptr);

    // Start with a single page - mirrors VirtualConsole's own invariant that at least one page
    // always exists (see VirtualConsole::deletePage()'s refusal to go below one). See this class's
    // own header comment for why this is a disconnected in-memory model rather than the real,
    // running VirtualConsole.
    VcPageState page;
    page.name = QStringLiteral("Page 1");
    m_pages.append(page);

    ApiDispatcher *d = m_server->dispatcher();
    registerPageMethods(d);
    registerWidgetMethods(d);
}

/*****************************************************************************
 * Model <-> JSON
 *****************************************************************************/

QJsonObject ApiVcDomain::pageToJson(int index) const
{
    QJsonObject obj;
    obj.insert(QStringLiteral("index"), index);
    obj.insert(QStringLiteral("name"), m_pages.at(index).name);
    obj.insert(QStringLiteral("hasPin"), m_pages.at(index).pin.isEmpty() == false);
    return obj;
}

QJsonObject ApiVcDomain::geometryToJson(const QRectF &geom) const
{
    QJsonObject obj;
    obj.insert(QStringLiteral("x"), geom.x());
    obj.insert(QStringLiteral("y"), geom.y());
    obj.insert(QStringLiteral("width"), geom.width());
    obj.insert(QStringLiteral("height"), geom.height());
    return obj;
}

QRectF ApiVcDomain::geometryFromJson(const QJsonObject &geom) const
{
    return QRectF(geom.value(QStringLiteral("x")).toDouble(),
                  geom.value(QStringLiteral("y")).toDouble(),
                  geom.value(QStringLiteral("width")).toDouble(),
                  geom.value(QStringLiteral("height")).toDouble());
}

QJsonObject ApiVcDomain::styleToJson(const VcWidgetState &w) const
{
    QJsonObject obj;
    obj.insert(QStringLiteral("caption"), w.caption);
    obj.insert(QStringLiteral("backgroundColor"), w.backgroundColor.isEmpty() ? QJsonValue() : QJsonValue(w.backgroundColor));
    obj.insert(QStringLiteral("backgroundImage"), w.backgroundImage.isEmpty() ? QJsonValue() : QJsonValue(w.backgroundImage));
    obj.insert(QStringLiteral("foregroundColor"), w.foregroundColor.isEmpty() ? QJsonValue() : QJsonValue(w.foregroundColor));
    obj.insert(QStringLiteral("font"), w.font);
    return obj;
}

void ApiVcDomain::applyStyleFromJson(VcWidgetState &w, const QJsonObject &style) const
{
    if (style.contains(QStringLiteral("caption")))
        w.caption = style.value(QStringLiteral("caption")).toString();

    if (style.contains(QStringLiteral("backgroundColor")))
    {
        QJsonValue v = style.value(QStringLiteral("backgroundColor"));
        w.backgroundColor = v.isNull() ? QString() : v.toString();
    }
    if (style.contains(QStringLiteral("backgroundImage")))
    {
        QJsonValue v = style.value(QStringLiteral("backgroundImage"));
        w.backgroundImage = v.isNull() ? QString() : v.toString();
    }
    if (style.contains(QStringLiteral("foregroundColor")))
    {
        QJsonValue v = style.value(QStringLiteral("foregroundColor"));
        w.foregroundColor = v.isNull() ? QString() : v.toString();
    }
    if (style.contains(QStringLiteral("font")))
        w.font = style.value(QStringLiteral("font")).toObject();
}

QJsonObject ApiVcDomain::widgetSummaryToJson(const VcWidgetState &w) const
{
    QJsonObject obj;
    obj.insert(QStringLiteral("id"), QString::number(w.id));
    obj.insert(QStringLiteral("widgetType"), w.widgetType);
    obj.insert(QStringLiteral("page"), w.page);
    if (w.parentId != InvalidWidgetId)
        obj.insert(QStringLiteral("parentId"), QString::number(w.parentId));
    obj.insert(QStringLiteral("geometry"), geometryToJson(w.geometry));
    obj.insert(QStringLiteral("zIndex"), w.zIndex);
    obj.insert(QStringLiteral("allowResize"), w.allowResize);
    obj.insert(QStringLiteral("isDisabled"), w.isDisabled);
    obj.insert(QStringLiteral("isVisible"), w.isVisible);
    obj.insert(QStringLiteral("style"), styleToJson(w));
    return obj;
}

QJsonObject ApiVcDomain::widgetDetailToJson(const VcWidgetState &w) const
{
    QJsonObject obj = widgetSummaryToJson(w);
    obj.insert(QStringLiteral("typeConfig"), w.typeConfig);
    // Out of scope for this pass (see this class's header comment) - always present, always empty.
    obj.insert(QStringLiteral("inputSources"), QJsonArray());
    obj.insert(QStringLiteral("keySequences"), QJsonArray());
    obj.insert(QStringLiteral("externalControls"), QJsonArray());
    return obj;
}

/*****************************************************************************
 * Model helpers
 *****************************************************************************/

bool ApiVcDomain::parseWidgetId(const QString &s, quint32 &outId) const
{
    bool ok = false;
    quint32 v = s.toUInt(&ok);
    if (ok == false)
        return false;
    outId = v;
    return true;
}

ApiVcDomain::VcWidgetState *ApiVcDomain::findWidget(const QString &widgetIdStr)
{
    quint32 wid;
    if (parseWidgetId(widgetIdStr, wid) == false)
        return nullptr;

    auto it = m_widgets.find(wid);
    if (it == m_widgets.end())
        return nullptr;

    return &it.value();
}

bool ApiVcDomain::isContainerWidget(quint32 id) const
{
    auto it = m_widgets.constFind(id);
    if (it == m_widgets.constEnd())
        return false;
    return ContainerWidgetTypes.contains(it.value().widgetType);
}

QList<quint32> ApiVcDomain::collectDescendants(quint32 id) const
{
    QList<quint32> result;
    QList<quint32> queue;
    queue.append(id);

    while (queue.isEmpty() == false)
    {
        quint32 current = queue.takeFirst();
        for (auto it = m_widgets.constBegin(); it != m_widgets.constEnd(); ++it)
        {
            if (it.value().parentId == current)
            {
                result.append(it.key());
                queue.append(it.key());
            }
        }
    }

    return result;
}

bool ApiVcDomain::isSelfOrAncestorOf(quint32 ancestorCandidate, quint32 id) const
{
    quint32 current = id;
    while (current != InvalidWidgetId)
    {
        if (current == ancestorCandidate)
            return true;

        auto it = m_widgets.constFind(current);
        if (it == m_widgets.constEnd())
            break;
        current = it.value().parentId;
    }
    return false;
}

void ApiVcDomain::setWidgetPageRecursive(quint32 id, int newPage)
{
    auto it = m_widgets.find(id);
    if (it == m_widgets.end())
        return;

    it.value().page = newPage;
    for (quint32 childId : collectDescendants(id))
        m_widgets[childId].page = newPage;
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
        QJsonArray pages;
        for (int i = 0; i < m_pages.size(); i++)
            pages.append(pageToJson(i));

        QJsonObject result;
        result.insert(QStringLiteral("pages"), pages);
        result.insert(QStringLiteral("selectedPage"), m_selectedPage);
        session->send(ApiEnvelope::buildOkResponse(id, result));
    });

    d->registerMethod(QStringLiteral("vc.page.create"), [this, doc](ApiSession *session, const QString &id, const QJsonObject &params)
    {
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
        if (index < 0 || index > m_pages.size())
            index = m_pages.size();

        VcPageState page;
        page.name = QStringLiteral("Page %1").arg(index + 1);
        m_pages.insert(index, page);

        // Shift every widget on a page at or after the insertion point down by one, and bump the
        // selection if the insertion happened at or before it - extends VirtualConsole::addPage()'s
        // own selectedPage-bump-on-exact-match to also cover insertion strictly before the selection.
        for (auto it = m_widgets.begin(); it != m_widgets.end(); ++it)
        {
            if (it.value().page >= index)
                it.value().page++;
        }
        if (index <= m_selectedPage)
            m_selectedPage++;

        doc->setModified();

        QJsonObject data;
        data.insert(QStringLiteral("page"), pageToJson(index));
        data.insert(QStringLiteral("docRevision"), int(doc->docRevision()));
        m_server->broadcast(QStringLiteral("vc.page.created"), data, session->clientId(), false);

        QJsonObject result;
        result.insert(QStringLiteral("docRevision"), int(doc->docRevision()));
        session->send(ApiEnvelope::buildOkResponse(id, result));
    });

    d->registerMethod(QStringLiteral("vc.page.delete"), [this, doc](ApiSession *session, const QString &id, const QJsonObject &params)
    {
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
        if (index < 0 || index >= m_pages.size())
        {
            session->send(ApiEnvelope::buildErrorResponse(id, ApiEnvelope::ErrNotFound,
                                                            QStringLiteral("No such page")));
            return;
        }

        // Mirrors VirtualConsole::deletePage()'s own refusal - at least one page must always exist.
        if (m_pages.size() == 1)
        {
            session->send(ApiEnvelope::buildErrorResponse(id, ApiEnvelope::ErrInvalidState,
                                                            QStringLiteral("Cannot delete the last page")));
            return;
        }

        // Deleting a page recursively deletes every widget on it - since a widget's own .page
        // field is kept in sync with its top-level ancestor's page on every create/reparent/update
        // (see setWidgetPageRecursive()), filtering by .page == index already captures the whole
        // subtree without needing a separate parent-chain walk here.
        QJsonArray deletedIds;
        QList<quint32> toRemove;
        for (auto it = m_widgets.constBegin(); it != m_widgets.constEnd(); ++it)
        {
            if (it.value().page == index)
                toRemove.append(it.key());
        }
        for (quint32 wid : toRemove)
        {
            deletedIds.append(QString::number(wid));
            m_widgets.remove(wid);
        }

        // Renumber every remaining widget on a later page down by one.
        for (auto it = m_widgets.begin(); it != m_widgets.end(); ++it)
        {
            if (it.value().page > index)
                it.value().page--;
        }

        m_pages.removeAt(index);

        if (index < m_selectedPage)
            m_selectedPage--;
        else if (index == m_selectedPage)
            m_selectedPage = qMin(m_selectedPage, m_pages.size() - 1);

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
        if (index < 0 || index >= m_pages.size())
        {
            session->send(ApiEnvelope::buildErrorResponse(id, ApiEnvelope::ErrNotFound,
                                                            QStringLiteral("No such page")));
            return;
        }

        QString name = params.value(QStringLiteral("name")).toString();
        m_pages[index].name = name;
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
        if (index < 0 || index >= m_pages.size())
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

        VcPageState &page = m_pages[index];
        if (page.pin.isEmpty() == false && page.pin != currentPin)
        {
            session->send(ApiEnvelope::buildErrorResponse(id, ApiEnvelope::ErrInvalidParams,
                                                            QStringLiteral("currentPIN does not match")));
            return;
        }

        page.pin = newPin;
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
        int index = params.value(QStringLiteral("index")).toInt();
        QString pin = params.value(QStringLiteral("pin")).toString();

        bool valid = false;
        if (index >= 0 && index < m_pages.size())
        {
            const VcPageState &page = m_pages.at(index);
            valid = page.pin.isEmpty() || page.pin == pin;
        }

        QJsonObject result;
        result.insert(QStringLiteral("valid"), valid);
        session->send(ApiEnvelope::buildOkResponse(id, result));
    });

    d->registerMethod(QStringLiteral("vc.page.select"), [this](ApiSession *session, const QString &id, const QJsonObject &params)
    {
        int index = params.value(QStringLiteral("index")).toInt();
        if (index < 0 || index >= m_pages.size())
        {
            session->send(ApiEnvelope::buildErrorResponse(id, ApiEnvelope::ErrNotFound,
                                                            QStringLiteral("No such page")));
            return;
        }

        // Live/runtime (§4b) - no baseRevision, and must NOT touch docRevision (doc.h's own comment
        // on docRevision() is explicit that live/runtime state must never bump it).
        m_selectedPage = index;
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
        bool hasPageFilter = params.contains(QStringLiteral("page"));
        int pageFilter = params.value(QStringLiteral("page")).toInt();

        bool hasParentFilter = params.contains(QStringLiteral("parentId"));
        quint32 parentFilter = InvalidWidgetId;
        if (hasParentFilter && parseWidgetId(params.value(QStringLiteral("parentId")).toString(), parentFilter) == false)
        {
            session->send(ApiEnvelope::buildErrorResponse(id, ApiEnvelope::ErrInvalidParams,
                                                            QStringLiteral("Invalid parentId")));
            return;
        }

        QSet<QString> typeFilters;
        for (const QJsonValue &v : params.value(QStringLiteral("typeFilters")).toArray())
            typeFilters.insert(v.toString());

        QList<quint32> ids = m_widgets.keys();
        std::sort(ids.begin(), ids.end());

        QJsonArray widgets;
        for (quint32 wid : ids)
        {
            const VcWidgetState &w = m_widgets.value(wid);
            if (hasPageFilter && w.page != pageFilter)
                continue;
            if (hasParentFilter && w.parentId != parentFilter)
                continue;
            if (typeFilters.isEmpty() == false && typeFilters.contains(w.widgetType) == false)
                continue;
            widgets.append(widgetSummaryToJson(w));
        }

        QJsonObject result;
        result.insert(QStringLiteral("widgets"), widgets);
        session->send(ApiEnvelope::buildOkResponse(id, result));
    });

    d->registerMethod(QStringLiteral("vc.widget.get"), [this](ApiSession *session, const QString &id, const QJsonObject &params)
    {
        VcWidgetState *w = findWidget(params.value(QStringLiteral("widgetId")).toString());
        if (w == nullptr)
        {
            session->send(ApiEnvelope::buildErrorResponse(id, ApiEnvelope::ErrNotFound,
                                                            QStringLiteral("No such widget")));
            return;
        }
        session->send(ApiEnvelope::buildOkResponse(id, widgetDetailToJson(*w)));
    });

    d->registerMethod(QStringLiteral("vc.widget.create"), [this, doc](ApiSession *session, const QString &id, const QJsonObject &params)
    {
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
        if (page < 0 || page >= m_pages.size())
        {
            session->send(ApiEnvelope::buildErrorResponse(id, ApiEnvelope::ErrNotFound,
                                                            QStringLiteral("No such page")));
            return;
        }

        quint32 parentId = InvalidWidgetId;
        if (params.contains(QStringLiteral("parentId")))
        {
            QString parentIdStr = params.value(QStringLiteral("parentId")).toString();
            if (parseWidgetId(parentIdStr, parentId) == false || m_widgets.contains(parentId) == false)
            {
                session->send(ApiEnvelope::buildErrorResponse(id, ApiEnvelope::ErrNotFound,
                                                                QStringLiteral("No such parent widget")));
                return;
            }
            if (isContainerWidget(parentId) == false)
            {
                session->send(ApiEnvelope::buildErrorResponse(id, ApiEnvelope::ErrInvalidParams,
                                                                QStringLiteral("parentId must be a Frame or SoloFrame widget")));
                return;
            }
        }

        VcWidgetState w;
        w.id = m_nextWidgetId++;
        w.widgetType = widgetType;
        w.page = page;
        w.parentId = parentId;
        w.geometry = geometryFromJson(params.value(QStringLiteral("geometry")).toObject());
        if (params.contains(QStringLiteral("style")))
            applyStyleFromJson(w, params.value(QStringLiteral("style")).toObject());
        if (params.contains(QStringLiteral("typeConfig")))
            w.typeConfig = params.value(QStringLiteral("typeConfig")).toObject();

        m_widgets.insert(w.id, w);
        doc->setModified();

        QJsonObject data;
        data.insert(QStringLiteral("widget"), widgetDetailToJson(w));
        data.insert(QStringLiteral("docRevision"), int(doc->docRevision()));
        m_server->broadcast(QStringLiteral("vc.widget.created"), data, session->clientId(), false);

        QJsonObject result;
        result.insert(QStringLiteral("docRevision"), int(doc->docRevision()));
        result.insert(QStringLiteral("widgetId"), QString::number(w.id));
        session->send(ApiEnvelope::buildOkResponse(id, result));
    });

    d->registerMethod(QStringLiteral("vc.widget.update"), [this, doc](ApiSession *session, const QString &id, const QJsonObject &params)
    {
        quint32 baseRevision = quint32(params.value(QStringLiteral("baseRevision")).toInt());
        if (baseRevision != doc->docRevision())
        {
            QJsonObject details;
            details.insert(QStringLiteral("docRevision"), int(doc->docRevision()));
            session->send(ApiEnvelope::buildErrorResponse(id, ApiEnvelope::ErrConflict,
                                                            QStringLiteral("baseRevision is stale"), details));
            return;
        }

        VcWidgetState *w = findWidget(params.value(QStringLiteral("widgetId")).toString());
        if (w == nullptr)
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

        // Validate everything BEFORE mutating anything below - this handler must be all-or-nothing
        // like vc.widget.reposition, not leave a partial write behind an error response with no
        // setModified()/broadcast/revision bump to signal it (a real bug caught in review: the page
        // check used to run last, after geometry/style/etc. had already been applied).
        bool hasPage = params.contains(QStringLiteral("page"));
        int newPage = hasPage ? params.value(QStringLiteral("page")).toInt() : w->page;
        if (hasPage)
        {
            if (newPage < 0 || newPage >= m_pages.size())
            {
                session->send(ApiEnvelope::buildErrorResponse(id, ApiEnvelope::ErrNotFound,
                                                                QStringLiteral("No such page")));
                return;
            }
            // A nested widget's page always mirrors its containing top-level ancestor's (see
            // vc.widget.reparent) - changing it directly here would desync it from its parent
            // (which stays behind) and leave a dangling parentId behind after a future
            // vc.page.delete on either page. Reparent the widget instead if it needs to move.
            if (w->parentId != InvalidWidgetId)
            {
                session->send(ApiEnvelope::buildErrorResponse(id, ApiEnvelope::ErrInvalidParams,
                                                                QStringLiteral("Cannot change page directly on a nested widget - use vc.widget.reparent")));
                return;
            }
        }

        if (params.contains(QStringLiteral("geometry")))
            w->geometry = geometryFromJson(params.value(QStringLiteral("geometry")).toObject());
        if (params.contains(QStringLiteral("zIndex")))
            w->zIndex = params.value(QStringLiteral("zIndex")).toInt();
        if (params.contains(QStringLiteral("allowResize")))
            w->allowResize = params.value(QStringLiteral("allowResize")).toBool();
        if (params.contains(QStringLiteral("isDisabled")))
            w->isDisabled = params.value(QStringLiteral("isDisabled")).toBool();
        if (params.contains(QStringLiteral("isVisible")))
            w->isVisible = params.value(QStringLiteral("isVisible")).toBool();
        if (params.contains(QStringLiteral("style")))
            applyStyleFromJson(*w, params.value(QStringLiteral("style")).toObject());
        if (hasPage)
        {
            // Moving a Frame/SoloFrame moves its whole subtree with it, matching how "page" is
            // treated as a single shared value across a container's descendants elsewhere in
            // this model (see vc.widget.reparent). Already validated above: w has no parent.
            setWidgetPageRecursive(w->id, newPage);
        }

        doc->setModified();

        QJsonObject data;
        data.insert(QStringLiteral("widget"), widgetDetailToJson(*w));
        data.insert(QStringLiteral("docRevision"), int(doc->docRevision()));
        m_server->broadcast(QStringLiteral("vc.widget.updated"), data, session->clientId(), false);

        QJsonObject result;
        result.insert(QStringLiteral("docRevision"), int(doc->docRevision()));
        session->send(ApiEnvelope::buildOkResponse(id, result));
    });

    d->registerMethod(QStringLiteral("vc.widget.setConfig"), [this, doc](ApiSession *session, const QString &id, const QJsonObject &params)
    {
        quint32 baseRevision = quint32(params.value(QStringLiteral("baseRevision")).toInt());
        if (baseRevision != doc->docRevision())
        {
            QJsonObject details;
            details.insert(QStringLiteral("docRevision"), int(doc->docRevision()));
            session->send(ApiEnvelope::buildErrorResponse(id, ApiEnvelope::ErrConflict,
                                                            QStringLiteral("baseRevision is stale"), details));
            return;
        }

        VcWidgetState *w = findWidget(params.value(QStringLiteral("widgetId")).toString());
        if (w == nullptr)
        {
            session->send(ApiEnvelope::buildErrorResponse(id, ApiEnvelope::ErrNotFound,
                                                            QStringLiteral("No such widget")));
            return;
        }

        // Partial patch merged onto the existing typeConfig, per the spec's own description of
        // vc.widget.setConfig - NOT a replace.
        QJsonObject patch = params.value(QStringLiteral("config")).toObject();
        for (auto it = patch.constBegin(); it != patch.constEnd(); ++it)
            w->typeConfig.insert(it.key(), it.value());

        doc->setModified();

        QJsonObject data;
        data.insert(QStringLiteral("widget"), widgetDetailToJson(*w));
        data.insert(QStringLiteral("docRevision"), int(doc->docRevision()));
        m_server->broadcast(QStringLiteral("vc.widget.configChanged"), data, session->clientId(), false);

        QJsonObject result;
        result.insert(QStringLiteral("docRevision"), int(doc->docRevision()));
        session->send(ApiEnvelope::buildOkResponse(id, result));
    });

    d->registerMethod(QStringLiteral("vc.widget.delete"), [this, doc](ApiSession *session, const QString &id, const QJsonObject &params)
    {
        quint32 baseRevision = quint32(params.value(QStringLiteral("baseRevision")).toInt());
        if (baseRevision != doc->docRevision())
        {
            QJsonObject details;
            details.insert(QStringLiteral("docRevision"), int(doc->docRevision()));
            session->send(ApiEnvelope::buildErrorResponse(id, ApiEnvelope::ErrConflict,
                                                            QStringLiteral("baseRevision is stale"), details));
            return;
        }

        // Deleting a Frame/SoloFrame recursively deletes its children too (mirrors
        // VirtualConsole::deleteVCWidgets()). Unknown ids are silently skipped, same leniency as
        // the real engine method (it just `continue`s past a lookup miss).
        QSet<quint32> toDelete;
        for (const QJsonValue &v : params.value(QStringLiteral("widgetIds")).toArray())
        {
            quint32 wid;
            if (parseWidgetId(v.toString(), wid) == false || m_widgets.contains(wid) == false)
                continue;

            toDelete.insert(wid);
            for (quint32 descendant : collectDescendants(wid))
                toDelete.insert(descendant);
        }

        if (toDelete.isEmpty())
        {
            session->send(ApiEnvelope::buildErrorResponse(id, ApiEnvelope::ErrNotFound,
                                                            QStringLiteral("No such widget(s)")));
            return;
        }

        QJsonArray deletedIds;
        for (quint32 wid : toDelete)
        {
            deletedIds.append(QString::number(wid));
            m_widgets.remove(wid);
        }

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
        quint32 baseRevision = quint32(params.value(QStringLiteral("baseRevision")).toInt());
        if (baseRevision != doc->docRevision())
        {
            QJsonObject details;
            details.insert(QStringLiteral("docRevision"), int(doc->docRevision()));
            session->send(ApiEnvelope::buildErrorResponse(id, ApiEnvelope::ErrConflict,
                                                            QStringLiteral("baseRevision is stale"), details));
            return;
        }

        VcWidgetState *w = findWidget(params.value(QStringLiteral("widgetId")).toString());
        if (w == nullptr)
        {
            session->send(ApiEnvelope::buildErrorResponse(id, ApiEnvelope::ErrNotFound,
                                                            QStringLiteral("No such widget")));
            return;
        }

        quint32 newParentId = InvalidWidgetId;
        if (params.contains(QStringLiteral("newParentId")))
        {
            QString newParentStr = params.value(QStringLiteral("newParentId")).toString();
            if (parseWidgetId(newParentStr, newParentId) == false || m_widgets.contains(newParentId) == false)
            {
                session->send(ApiEnvelope::buildErrorResponse(id, ApiEnvelope::ErrNotFound,
                                                                QStringLiteral("No such parent widget")));
                return;
            }
            if (isContainerWidget(newParentId) == false)
            {
                session->send(ApiEnvelope::buildErrorResponse(id, ApiEnvelope::ErrInvalidParams,
                                                                QStringLiteral("newParentId must be a Frame or SoloFrame widget")));
                return;
            }
            // Reject moving a widget into itself, or into one of its own descendants - would
            // otherwise create a cycle in the parent chain (and infinite-loop a future recursive
            // delete/collectDescendants() walk).
            if (isSelfOrAncestorOf(w->id, newParentId))
            {
                session->send(ApiEnvelope::buildErrorResponse(id, ApiEnvelope::ErrInvalidParams,
                                                                QStringLiteral("Cannot reparent a widget into its own descendant")));
                return;
            }
        }

        w->parentId = newParentId;
        // No "page" param on this message: reparenting onto the page root leaves the widget's
        // current top-level page unchanged (matches this being a same-page drag gesture); moving
        // into a container on a different page adopts that container's page instead.
        if (newParentId != InvalidWidgetId)
            setWidgetPageRecursive(w->id, m_widgets.value(newParentId).page);

        QJsonObject position = params.value(QStringLiteral("position")).toObject();
        QRectF geom = w->geometry;
        geom.moveTopLeft(QPointF(position.value(QStringLiteral("x")).toDouble(),
                                  position.value(QStringLiteral("y")).toDouble()));
        w->geometry = geom;

        doc->setModified();

        // Single-widget move - broadcast on vc.widget.updated, not bulkUpdated, per the spec's own
        // note on VcWidgetUpdatedEvent.
        QJsonObject data;
        data.insert(QStringLiteral("widget"), widgetDetailToJson(*w));
        data.insert(QStringLiteral("docRevision"), int(doc->docRevision()));
        m_server->broadcast(QStringLiteral("vc.widget.updated"), data, session->clientId(), false);

        QJsonObject result;
        result.insert(QStringLiteral("docRevision"), int(doc->docRevision()));
        session->send(ApiEnvelope::buildOkResponse(id, result));
    });

    d->registerMethod(QStringLiteral("vc.widget.reposition"), [this, doc](ApiSession *session, const QString &id, const QJsonObject &params)
    {
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
        QList<QPair<VcWidgetState *, QRectF> > updates;
        for (const QJsonValue &v : items)
        {
            QJsonObject item = v.toObject();
            VcWidgetState *w = findWidget(item.value(QStringLiteral("widgetId")).toString());
            if (w == nullptr)
            {
                session->send(ApiEnvelope::buildErrorResponse(id, ApiEnvelope::ErrNotFound,
                                                                QStringLiteral("No such widget")));
                return;
            }
            updates.append(qMakePair(w, geometryFromJson(item.value(QStringLiteral("geometry")).toObject())));
        }

        QJsonArray widgetsData;
        for (auto &pair : updates)
        {
            pair.first->geometry = pair.second;
            QJsonObject entry;
            entry.insert(QStringLiteral("widgetId"), QString::number(pair.first->id));
            entry.insert(QStringLiteral("geometry"), geometryToJson(pair.second));
            widgetsData.append(entry);
        }

        doc->setModified();

        QJsonObject data;
        data.insert(QStringLiteral("widgets"), widgetsData);
        data.insert(QStringLiteral("docRevision"), int(doc->docRevision()));
        m_server->broadcast(QStringLiteral("vc.widget.repositioned"), data, session->clientId(), false);

        QJsonObject result;
        result.insert(QStringLiteral("docRevision"), int(doc->docRevision()));
        session->send(ApiEnvelope::buildOkResponse(id, result));
    });
}
