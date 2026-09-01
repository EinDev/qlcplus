/*
  Q Light Controller Plus - Control API unit test
  fakevchost.cpp

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

#include "fakevchost.h"

const QStringList FakeVcHost::ContainerWidgetTypes = { QStringLiteral("Frame"), QStringLiteral("SoloFrame") };

FakeVcHost::FakeVcHost(QObject *parent)
    : QObject(parent)
    , m_selectedPage(0)
    , m_nextWidgetId(0)
{
    // At least one page always exists - mirrors VirtualConsole's own invariant (see
    // VirtualConsole::deletePage()'s refusal to go below one).
    VcPageState page;
    page.name = QStringLiteral("Page 1");
    m_pages.append(page);
}

/*****************************************************************************
 * Model <-> JSON
 *****************************************************************************/

QRectF FakeVcHost::geometryFromJson(const QJsonObject &geom)
{
    return QRectF(geom.value(QStringLiteral("x")).toDouble(),
                  geom.value(QStringLiteral("y")).toDouble(),
                  geom.value(QStringLiteral("width")).toDouble(),
                  geom.value(QStringLiteral("height")).toDouble());
}

QJsonObject FakeVcHost::geometryToJson(const QRectF &geom)
{
    QJsonObject obj;
    obj.insert(QStringLiteral("x"), geom.x());
    obj.insert(QStringLiteral("y"), geom.y());
    obj.insert(QStringLiteral("width"), geom.width());
    obj.insert(QStringLiteral("height"), geom.height());
    return obj;
}

QJsonObject FakeVcHost::styleToJson(const VcWidgetState &w) const
{
    QJsonObject obj;
    obj.insert(QStringLiteral("caption"), w.caption);
    obj.insert(QStringLiteral("backgroundColor"), w.backgroundColor.isEmpty() ? QJsonValue() : QJsonValue(w.backgroundColor));
    obj.insert(QStringLiteral("backgroundImage"), w.backgroundImage.isEmpty() ? QJsonValue() : QJsonValue(w.backgroundImage));
    obj.insert(QStringLiteral("foregroundColor"), w.foregroundColor.isEmpty() ? QJsonValue() : QJsonValue(w.foregroundColor));
    obj.insert(QStringLiteral("font"), w.font);
    return obj;
}

void FakeVcHost::applyStyleFromJson(VcWidgetState &w, const QJsonObject &style) const
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

QJsonObject FakeVcHost::widgetSummaryToJson(const VcWidgetState &w) const
{
    QJsonObject obj;
    obj.insert(QStringLiteral("id"), QString::number(w.id));
    obj.insert(QStringLiteral("widgetType"), w.widgetType);
    obj.insert(QStringLiteral("page"), w.page);
    if (w.parentId != ApiVcHost::InvalidWidgetId)
        obj.insert(QStringLiteral("parentId"), QString::number(w.parentId));
    obj.insert(QStringLiteral("geometry"), geometryToJson(w.geometry));
    obj.insert(QStringLiteral("zIndex"), w.zIndex);
    obj.insert(QStringLiteral("allowResize"), w.allowResize);
    obj.insert(QStringLiteral("isDisabled"), w.isDisabled);
    obj.insert(QStringLiteral("isVisible"), w.isVisible);
    obj.insert(QStringLiteral("style"), styleToJson(w));
    return obj;
}

QJsonObject FakeVcHost::widgetDetailToJson(const VcWidgetState &w) const
{
    QJsonObject obj = widgetSummaryToJson(w);
    obj.insert(QStringLiteral("typeConfig"), w.typeConfig);
    obj.insert(QStringLiteral("inputSources"), QJsonArray());
    obj.insert(QStringLiteral("keySequences"), QJsonArray());
    obj.insert(QStringLiteral("externalControls"), QJsonArray());
    return obj;
}

QList<quint32> FakeVcHost::collectDescendants(quint32 id) const
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

void FakeVcHost::setWidgetPageRecursive(quint32 id, int newPage)
{
    auto it = m_widgets.find(id);
    if (it == m_widgets.end())
        return;

    it.value().page = newPage;
    for (quint32 childId : collectDescendants(id))
        m_widgets[childId].page = newPage;
}

/*****************************************************************************
 * Pages
 *****************************************************************************/

int FakeVcHost::vcPageCount() const
{
    return m_pages.size();
}

QJsonObject FakeVcHost::vcPageSnapshot(int index) const
{
    QJsonObject obj;
    obj.insert(QStringLiteral("index"), index);
    obj.insert(QStringLiteral("name"), m_pages.at(index).name);
    obj.insert(QStringLiteral("hasPin"), m_pages.at(index).pin.isEmpty() == false);
    return obj;
}

int FakeVcHost::vcSelectedPage() const
{
    return m_selectedPage;
}

void FakeVcHost::vcSetSelectedPage(int index)
{
    m_selectedPage = index;
}

void FakeVcHost::vcAddPage(int index)
{
    VcPageState page;
    page.name = QStringLiteral("Page %1").arg(index + 1);
    m_pages.insert(index, page);

    // Shift every widget on a page at or after the insertion point down by one, and bump the
    // selection if the insertion happened at or before it.
    for (auto it = m_widgets.begin(); it != m_widgets.end(); ++it)
    {
        if (it.value().page >= index)
            it.value().page++;
    }
    if (index <= m_selectedPage)
        m_selectedPage++;
}

bool FakeVcHost::vcDeletePage(int index, QJsonArray &deletedWidgetIds)
{
    if (index < 0 || index >= m_pages.size() || m_pages.size() == 1)
        return false;

    // Deleting a page recursively deletes every widget on it - since a widget's own .page field is
    // kept in sync with its top-level ancestor's page on every create/reparent/update (see
    // setWidgetPageRecursive()), filtering by .page == index already captures the whole subtree
    // without needing a separate parent-chain walk here.
    QList<quint32> toRemove;
    for (auto it = m_widgets.constBegin(); it != m_widgets.constEnd(); ++it)
    {
        if (it.value().page == index)
            toRemove.append(it.key());
    }
    for (quint32 wid : toRemove)
    {
        deletedWidgetIds.append(QString::number(wid));
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

    return true;
}

void FakeVcHost::vcRenamePage(int index, const QString &name)
{
    m_pages[index].name = name;
}

bool FakeVcHost::vcSetPagePin(int index, const QString &currentPin, const QString &newPin)
{
    VcPageState &page = m_pages[index];
    if (page.pin.isEmpty() == false && page.pin != currentPin)
        return false;

    page.pin = newPin;
    return true;
}

bool FakeVcHost::vcValidatePagePin(int index, const QString &pin) const
{
    const VcPageState &page = m_pages.at(index);
    return page.pin.isEmpty() || page.pin == pin;
}

/*****************************************************************************
 * Widgets - queries
 *****************************************************************************/

bool FakeVcHost::vcWidgetExists(quint32 id) const
{
    return m_widgets.contains(id);
}

QString FakeVcHost::vcWidgetType(quint32 id) const
{
    auto it = m_widgets.constFind(id);
    return it == m_widgets.constEnd() ? QString() : it.value().widgetType;
}

int FakeVcHost::vcWidgetPage(quint32 id) const
{
    auto it = m_widgets.constFind(id);
    return it == m_widgets.constEnd() ? -1 : it.value().page;
}

quint32 FakeVcHost::vcWidgetParentId(quint32 id) const
{
    auto it = m_widgets.constFind(id);
    return it == m_widgets.constEnd() ? ApiVcHost::InvalidWidgetId : it.value().parentId;
}

bool FakeVcHost::vcIsContainerWidget(quint32 id) const
{
    auto it = m_widgets.constFind(id);
    if (it == m_widgets.constEnd())
        return false;
    return ContainerWidgetTypes.contains(it.value().widgetType);
}

QList<quint32> FakeVcHost::vcWidgetIds() const
{
    return m_widgets.keys();
}

QJsonObject FakeVcHost::vcWidgetSnapshot(quint32 id) const
{
    auto it = m_widgets.constFind(id);
    if (it == m_widgets.constEnd())
        return QJsonObject();
    return widgetDetailToJson(it.value());
}

/*****************************************************************************
 * Widgets - mutations
 *****************************************************************************/

quint32 FakeVcHost::vcCreateWidget(const QString &widgetType, int page, quint32 parentId,
                                    const QJsonObject &geometry, const QJsonObject &style,
                                    const QJsonObject &typeConfig, QString *error)
{
    Q_UNUSED(error)

    VcWidgetState w;
    w.id = m_nextWidgetId++;
    w.widgetType = widgetType;
    w.page = page;
    w.parentId = parentId;
    w.geometry = geometryFromJson(geometry);
    if (style.isEmpty() == false)
        applyStyleFromJson(w, style);
    w.typeConfig = typeConfig;

    m_widgets.insert(w.id, w);
    return w.id;
}

void FakeVcHost::vcDeleteWidgets(const QList<quint32> &ids, QJsonArray &deletedIds)
{
    QSet<quint32> toDelete;
    for (quint32 wid : ids)
    {
        if (m_widgets.contains(wid) == false)
            continue;
        toDelete.insert(wid);
        for (quint32 descendant : collectDescendants(wid))
            toDelete.insert(descendant);
    }

    for (quint32 wid : toDelete)
    {
        deletedIds.append(QString::number(wid));
        m_widgets.remove(wid);
    }
}

bool FakeVcHost::vcUpdateWidgetCommon(quint32 id, const QJsonObject &fields, QString *error)
{
    Q_UNUSED(error)

    auto it = m_widgets.find(id);
    if (it == m_widgets.end())
        return false;
    VcWidgetState &w = it.value();

    if (fields.contains(QStringLiteral("geometry")))
        w.geometry = geometryFromJson(fields.value(QStringLiteral("geometry")).toObject());
    if (fields.contains(QStringLiteral("zIndex")))
        w.zIndex = fields.value(QStringLiteral("zIndex")).toInt();
    if (fields.contains(QStringLiteral("allowResize")))
        w.allowResize = fields.value(QStringLiteral("allowResize")).toBool();
    if (fields.contains(QStringLiteral("isDisabled")))
        w.isDisabled = fields.value(QStringLiteral("isDisabled")).toBool();
    if (fields.contains(QStringLiteral("isVisible")))
        w.isVisible = fields.value(QStringLiteral("isVisible")).toBool();
    if (fields.contains(QStringLiteral("style")))
        applyStyleFromJson(w, fields.value(QStringLiteral("style")).toObject());

    return true;
}

void FakeVcHost::vcMoveTopLevelWidgetToPage(quint32 id, int newPage)
{
    setWidgetPageRecursive(id, newPage);
}

bool FakeVcHost::vcSetWidgetConfig(quint32 id, const QJsonObject &configPatch, QString *error)
{
    Q_UNUSED(error)

    auto it = m_widgets.find(id);
    if (it == m_widgets.end())
        return false;

    for (auto patchIt = configPatch.constBegin(); patchIt != configPatch.constEnd(); ++patchIt)
        it.value().typeConfig.insert(patchIt.key(), patchIt.value());

    return true;
}

bool FakeVcHost::vcReparentWidget(quint32 id, quint32 newParentId, QPointF newTopLeft, QString *error)
{
    Q_UNUSED(error)

    auto it = m_widgets.find(id);
    if (it == m_widgets.end())
        return false;
    VcWidgetState &w = it.value();

    w.parentId = newParentId;
    // No explicit page argument: reparenting onto the page root leaves the widget's current
    // top-level page unchanged (matches this being a same-page drag gesture); moving into a
    // container on a different page adopts that container's page instead.
    if (newParentId != ApiVcHost::InvalidWidgetId)
        setWidgetPageRecursive(id, m_widgets.value(newParentId).page);

    QRectF geom = w.geometry;
    geom.moveTopLeft(newTopLeft);
    w.geometry = geom;

    return true;
}

void FakeVcHost::vcRepositionWidgets(const QList<QPair<quint32, QJsonObject> > &updates)
{
    for (const auto &pair : updates)
    {
        auto it = m_widgets.find(pair.first);
        if (it != m_widgets.end())
            it.value().geometry = geometryFromJson(pair.second);
    }
}
