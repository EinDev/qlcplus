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

#include <QDateTime>
#include <QJsonArray>
#include <QJsonValue>
#include <QSet>

#include "fakevchost.h"

const QStringList FakeVcHost::ContainerWidgetTypes = { QStringLiteral("Frame"), QStringLiteral("SoloFrame") };
const QStringList FakeVcHost::PresetWidgetTypes = { QStringLiteral("Speed"), QStringLiteral("XYPad"), QStringLiteral("Animation") };
const int FakeVcHost::FakeCueListStepCount = 3;

FakeVcHost::FakeVcHost(QObject *parent)
    : QObject(parent)
    , m_selectedPage(0)
    , m_nextWidgetId(0)
    , m_liveListener(nullptr)
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
    appendLiveStateToJson(w, obj);
    return obj;
}

void FakeVcHost::appendLiveStateToJson(const VcWidgetState &w, QJsonObject &obj) const
{
    // Same additive, type-dependent live fields qmlui's App::vcWidgetSnapshot() exposes - see
    // VcWidgetSummary in docs/api-spec/fragments/virtualconsole.yaml.
    const QString &t = w.widgetType;
    if (t == QStringLiteral("Button"))
    {
        obj.insert(QStringLiteral("state"), w.buttonState);
    }
    else if (t == QStringLiteral("Slider"))
    {
        obj.insert(QStringLiteral("value"), w.sliderValue);
        obj.insert(QStringLiteral("min"), w.typeConfig.value(QStringLiteral("rangeLowLimit")).toDouble(0.0));
        obj.insert(QStringLiteral("max"), w.typeConfig.value(QStringLiteral("rangeHighLimit")).toDouble(255.0));
    }
    else if (t == QStringLiteral("CueList"))
    {
        obj.insert(QStringLiteral("playbackIndex"), w.playbackIndex);
        obj.insert(QStringLiteral("running"), w.running);
        obj.insert(QStringLiteral("paused"), w.paused);
        obj.insert(QStringLiteral("sideFaderLevel"), w.sideFaderLevel);
        obj.insert(QStringLiteral("nextStepIndex"), -1);
        obj.insert(QStringLiteral("primaryTop"), true);
    }
    else if (t == QStringLiteral("XYPad"))
    {
        obj.insert(QStringLiteral("x"), w.x);
        obj.insert(QStringLiteral("y"), w.y);
    }
    else if (t == QStringLiteral("Speed"))
    {
        obj.insert(QStringLiteral("ms"), w.speedMs);
        obj.insert(QStringLiteral("factor"), w.factor);
        obj.insert(QStringLiteral("tapTimeValue"), w.tapTimeValue);
    }
    else if (ContainerWidgetTypes.contains(t))
    {
        obj.insert(QStringLiteral("currentPage"), w.currentPage);
        obj.insert(QStringLiteral("pages"), frameTotalPages(w));
        obj.insert(QStringLiteral("multipage"), w.typeConfig.value(QStringLiteral("multiPageMode")).toBool(false));
    }
}

int FakeVcHost::frameTotalPages(const VcWidgetState &w)
{
    // VCFrame::totalPagesNumber() defaults to 1 and is what gotoPage() range-checks against, whether
    // or not multiPageMode is on.
    return qMax(1, w.typeConfig.value(QStringLiteral("totalPagesNumber")).toInt(1));
}

QJsonObject FakeVcHost::widgetDetailToJson(const VcWidgetState &w) const
{
    QJsonObject obj = widgetSummaryToJson(w);
    QJsonObject typeConfig = w.typeConfig;
    // Like App's speedDialConfigToJson(): the preset list rides along read-only inside typeConfig.
    if (PresetWidgetTypes.contains(w.widgetType))
        typeConfig.insert(QStringLiteral("presets"), w.presets);
    obj.insert(QStringLiteral("typeConfig"), typeConfig);
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

/*****************************************************************************
 * Widgets - live interaction
 *****************************************************************************/

void FakeVcHost::vcSetLiveListener(ApiVcLiveListener *listener)
{
    m_liveListener = listener;
}

void FakeVcHost::notifyCueListPlayback(const VcWidgetState &w) const
{
    if (m_liveListener != nullptr)
        m_liveListener->vcCueListPlaybackChanged(w.id, w.playbackIndex, w.running, w.paused);
}

bool FakeVcHost::vcButtonPress(quint32 id, bool pressed, QString *error)
{
    auto it = m_widgets.find(id);
    if (it == m_widgets.end())
        return false;
    VcWidgetState &w = it.value();

    // Mirrors App::vcButtonPress() / VCButton::requestStateChange(): Flash follows both edges, every
    // other action type acts on the down-edge only.
    QString action = w.typeConfig.value(QStringLiteral("actionType")).toString(QStringLiteral("Toggle"));
    QString newState = w.buttonState;

    if (action == QStringLiteral("Flash"))
    {
        if (w.typeConfig.value(QStringLiteral("functionID")).toString().isEmpty())
        {
            if (error) *error = QStringLiteral("No function attached to button");
            return false;
        }
        newState = pressed ? QStringLiteral("active") : QStringLiteral("inactive");
    }
    else if (pressed == false)
    {
        return true; // release edge: nothing to do for Toggle/Blackout/StopAll
    }
    else if (action == QStringLiteral("StopAll"))
    {
        return true; // fires, but has no observable button state of its own
    }
    else // Toggle, Blackout
    {
        if (action == QStringLiteral("Toggle") && w.typeConfig.value(QStringLiteral("functionID")).toString().isEmpty())
        {
            if (error) *error = QStringLiteral("No function attached to button");
            return false;
        }
        newState = w.buttonState == QStringLiteral("active") ? QStringLiteral("inactive") : QStringLiteral("active");
    }

    if (newState != w.buttonState)
    {
        w.buttonState = newState;
        if (m_liveListener != nullptr)
            m_liveListener->vcButtonStateChanged(id, newState);
    }
    return true;
}

bool FakeVcHost::vcSliderSetValue(quint32 id, int value, QString *error)
{
    Q_UNUSED(error)
    auto it = m_widgets.find(id);
    if (it == m_widgets.end())
        return false;
    VcWidgetState &w = it.value();

    // Confine to the slider's own range like the on-screen fader (and App::vcSliderSetValue()) do.
    int lo = qRound(w.typeConfig.value(QStringLiteral("rangeLowLimit")).toDouble(0.0));
    int hi = qRound(w.typeConfig.value(QStringLiteral("rangeHighLimit")).toDouble(255.0));
    value = qBound(qMin(lo, hi), value, qMax(lo, hi));

    if (value != w.sliderValue)
    {
        w.sliderValue = value;
        if (m_liveListener != nullptr)
            m_liveListener->vcSliderValueChanged(id, value);
    }
    return true;
}

bool FakeVcHost::vcCueListAction(quint32 id, CueListAction action, QString *error)
{
    auto it = m_widgets.find(id);
    if (it == m_widgets.end())
        return false;
    VcWidgetState &w = it.value();

    if (w.typeConfig.value(QStringLiteral("chaserID")).toString().isEmpty())
    {
        if (error) *error = QStringLiteral("No Chaser attached to cue list");
        return false;
    }

    // A simplified VCCueList in its default PlayPauseStop layout with DefaultRunFirst next/prev.
    switch (action)
    {
        case CueListPlay:
            if (w.running == false)
            {
                w.running = true;
                w.paused = false;
                if (w.playbackIndex < 0)
                    w.playbackIndex = 0;
            }
            else
            {
                w.paused = !w.paused;
            }
        break;
        case CueListStop:
            if (w.running)
            {
                w.running = false;
                w.paused = false;
            }
            else
            {
                w.playbackIndex = -1;
            }
        break;
        case CueListNext:
            w.playbackIndex = (w.playbackIndex + 1) % FakeCueListStepCount;
            w.running = true;
            w.paused = false;
        break;
        case CueListPrevious:
            w.playbackIndex = w.playbackIndex <= 0 ? FakeCueListStepCount - 1 : w.playbackIndex - 1;
            w.running = true;
            w.paused = false;
        break;
    }

    notifyCueListPlayback(w);
    return true;
}

bool FakeVcHost::vcCueListSetPlaybackIndex(quint32 id, int index, QString *error)
{
    auto it = m_widgets.find(id);
    if (it == m_widgets.end())
        return false;
    VcWidgetState &w = it.value();

    if (w.typeConfig.value(QStringLiteral("chaserID")).toString().isEmpty())
    {
        if (error) *error = QStringLiteral("No Chaser attached to cue list");
        return false;
    }

    w.playbackIndex = index;
    if (index >= 0)
    {
        w.running = true;
        w.paused = false;
    }
    notifyCueListPlayback(w);
    return true;
}

QJsonObject FakeVcHost::vcCueListSnapshot(quint32 id) const
{
    auto it = m_widgets.constFind(id);
    if (it == m_widgets.constEnd())
        return QJsonObject();
    const VcWidgetState &w = it.value();

    QJsonArray steps;
    if (w.typeConfig.value(QStringLiteral("chaserID")).toString().isEmpty() == false)
    {
        for (int i = 0; i < FakeCueListStepCount; i++)
        {
            QJsonObject step;
            step.insert(QStringLiteral("index"), i);
            step.insert(QStringLiteral("name"), QStringLiteral("Step %1").arg(i + 1));
            step.insert(QStringLiteral("functionId"), QString::number(100 + i));
            step.insert(QStringLiteral("fadeIn"), 0);
            step.insert(QStringLiteral("fadeOut"), 0);
            step.insert(QStringLiteral("hold"), 1000);
            step.insert(QStringLiteral("notes"), QString());
            steps.append(step);
        }
    }

    QJsonObject obj;
    obj.insert(QStringLiteral("steps"), steps);
    obj.insert(QStringLiteral("playbackIndex"), w.playbackIndex);
    obj.insert(QStringLiteral("running"), w.running);
    obj.insert(QStringLiteral("paused"), w.paused);
    return obj;
}

bool FakeVcHost::vcXyPadSetPosition(quint32 id, double x, double y, QString *error)
{
    Q_UNUSED(error)
    auto it = m_widgets.find(id);
    if (it == m_widgets.end())
        return false;
    VcWidgetState &w = it.value();

    if (x != w.x || y != w.y)
    {
        w.x = x;
        w.y = y;
        if (m_liveListener != nullptr)
            m_liveListener->vcXyPadPositionChanged(id, x, y);
    }
    return true;
}

bool FakeVcHost::vcSpeedDialSetValue(quint32 id, int ms, QString *error)
{
    Q_UNUSED(error)
    auto it = m_widgets.find(id);
    if (it == m_widgets.end())
        return false;
    VcWidgetState &w = it.value();

    if (ms != w.speedMs)
    {
        w.speedMs = ms;
        if (m_liveListener != nullptr)
            m_liveListener->vcSpeedDialValueChanged(id, ms);
    }
    return true;
}

bool FakeVcHost::vcSpeedDialTap(quint32 id, QString *error)
{
    auto it = m_widgets.find(id);
    if (it == m_widgets.end())
        return false;
    VcWidgetState &w = it.value();

    // VCSpeedDial::tap(): the first tap only arms; a second tap within 1.5 s sets the interval.
    qint64 now = QDateTime::currentMSecsSinceEpoch();
    if (w.lastTapMs != 0 && now - w.lastTapMs < 1500)
    {
        int interval = int(now - w.lastTapMs);
        if (interval < 1)
            interval = 1;
        w.lastTapMs = now;
        bool ok = vcSpeedDialSetValue(id, interval, error);
        // VCSpeedDial::tap(): tapTimeValue follows the computed interval and notifies on change.
        if (interval != w.tapTimeValue)
        {
            w.tapTimeValue = interval;
            if (m_liveListener != nullptr)
                m_liveListener->vcSpeedDialTapChanged(id, interval, w.speedMs);
        }
        return ok;
    }
    w.lastTapMs = now;
    return true;
}

bool FakeVcHost::vcFrameGotoPage(quint32 id, int page, QString *error)
{
    auto it = m_widgets.find(id);
    if (it == m_widgets.end())
        return false;
    VcWidgetState &w = it.value();

    int pages = frameTotalPages(w);
    if (page < 0 || page >= pages)
    {
        if (error) *error = QStringLiteral("page must be 0..%1").arg(pages - 1);
        return false;
    }
    if (page == w.currentPage)
        return true;

    w.currentPage = page;
    if (m_liveListener != nullptr)
        m_liveListener->vcFramePageChanged(id, page);
    return true;
}

QJsonObject FakeVcHost::vcFrameSnapshot(quint32 id) const
{
    auto it = m_widgets.constFind(id);
    if (it == m_widgets.constEnd())
        return QJsonObject();
    const VcWidgetState &w = it.value();

    QJsonObject obj;
    obj.insert(QStringLiteral("pages"), frameTotalPages(w));
    obj.insert(QStringLiteral("currentPage"), w.currentPage);
    obj.insert(QStringLiteral("multipage"), w.typeConfig.value(QStringLiteral("multiPageMode")).toBool(false));
    return obj;
}

/*****************************************************************************
 * Test hooks
 *****************************************************************************/

void FakeVcHost::simulateButtonState(quint32 id, const QString &state)
{
    auto it = m_widgets.find(id);
    if (it == m_widgets.end())
        return;
    it.value().buttonState = state;
    if (m_liveListener != nullptr)
        m_liveListener->vcButtonStateChanged(id, state);
}

void FakeVcHost::simulateSliderValue(quint32 id, int value)
{
    auto it = m_widgets.find(id);
    if (it == m_widgets.end())
        return;
    it.value().sliderValue = value;
    if (m_liveListener != nullptr)
        m_liveListener->vcSliderValueChanged(id, value);
}

void FakeVcHost::simulateCueListAdvance(quint32 id, int playbackIndex)
{
    auto it = m_widgets.find(id);
    if (it == m_widgets.end())
        return;
    it.value().playbackIndex = playbackIndex;
    it.value().running = true;
    it.value().paused = false;
    notifyCueListPlayback(it.value());
}

/*****************************************************************************
 * Cue list side fader / speed dial extras
 *****************************************************************************/

bool FakeVcHost::vcCueListSetSideFaderLevel(quint32 id, int level, QString *error)
{
    auto it = m_widgets.find(id);
    if (it == m_widgets.end())
        return false;
    VcWidgetState &w = it.value();

    // Mirrors App: the fader is hidden in None mode, confined to 0..100 in Crossfade mode.
    QString mode = w.typeConfig.value(QStringLiteral("sideFaderMode")).toString(QStringLiteral("None"));
    if (mode == QStringLiteral("None"))
    {
        if (error) *error = QStringLiteral("Side fader mode is None");
        return false;
    }
    if (mode == QStringLiteral("Crossfade"))
        level = qMin(level, 100);

    if (level != w.sideFaderLevel)
    {
        w.sideFaderLevel = level;
        if (m_liveListener != nullptr)
            m_liveListener->vcCueListSideFaderChanged(id, level, -1, true);
    }
    return true;
}

bool FakeVcHost::vcSpeedDialSetFactor(quint32 id, const QString &factor, QString *error)
{
    Q_UNUSED(error)
    auto it = m_widgets.find(id);
    if (it == m_widgets.end())
        return false;
    VcWidgetState &w = it.value();

    if (factor != w.factor)
    {
        w.factor = factor;
        if (m_liveListener != nullptr)
            m_liveListener->vcSpeedDialFactorChanged(id, factor);
    }
    return true;
}

bool FakeVcHost::vcSpeedDialApply(quint32 id, QString *error)
{
    Q_UNUSED(error)
    // VCSpeedDial::applyFunctionsTime() only touches the attached Functions - nothing observable here.
    return m_widgets.contains(id);
}

bool FakeVcHost::vcSpeedDialResetTap(quint32 id, QString *error)
{
    Q_UNUSED(error)
    auto it = m_widgets.find(id);
    if (it == m_widgets.end())
        return false;
    VcWidgetState &w = it.value();

    w.lastTapMs = 0;
    if (w.tapTimeValue != 0)
    {
        w.tapTimeValue = 0;
        if (m_liveListener != nullptr)
            m_liveListener->vcSpeedDialTapChanged(id, 0, w.speedMs);
    }
    return true;
}

/*****************************************************************************
 * Widget presets
 *****************************************************************************/

bool FakeVcHost::vcWidgetSupportsPresets(quint32 id) const
{
    auto it = m_widgets.constFind(id);
    return it != m_widgets.constEnd() && PresetWidgetTypes.contains(it.value().widgetType);
}

QJsonArray FakeVcHost::vcWidgetPresets(quint32 id) const
{
    auto it = m_widgets.constFind(id);
    return it != m_widgets.constEnd() ? it.value().presets : QJsonArray();
}

int FakeVcHost::vcWidgetPresetAdd(quint32 id, const QJsonObject &preset, QString *error)
{
    auto it = m_widgets.find(id);
    if (it == m_widgets.end())
        return -1;
    VcWidgetState &w = it.value();

    QJsonObject stored = preset;
    if (w.widgetType == QStringLiteral("Speed"))
    {
        // VcSpeedDialPresetData: name + valueMs, both required - same checks as App.
        if (preset.value(QStringLiteral("name")).isString() == false || preset.value(QStringLiteral("name")).toString().isEmpty())
        {
            if (error) *error = QStringLiteral("preset.name must be a non-empty string");
            return -1;
        }
        if (preset.value(QStringLiteral("valueMs")).isDouble() == false || preset.value(QStringLiteral("valueMs")).toInt() < 0)
        {
            if (error) *error = QStringLiteral("preset.valueMs must be a non-negative integer");
            return -1;
        }
        stored = QJsonObject();
        stored.insert(QStringLiteral("name"), preset.value(QStringLiteral("name")));
        stored.insert(QStringLiteral("valueMs"), preset.value(QStringLiteral("valueMs")).toInt());
    }

    int presetId = w.nextPresetId++;
    stored.insert(QStringLiteral("presetId"), presetId);
    w.presets.append(stored);
    return presetId;
}

bool FakeVcHost::vcWidgetPresetRemove(quint32 id, int presetId, QString *error)
{
    Q_UNUSED(error)
    auto it = m_widgets.find(id);
    if (it == m_widgets.end())
        return false;
    QJsonArray &presets = it.value().presets;
    for (int i = 0; i < presets.size(); i++)
    {
        if (presets.at(i).toObject().value(QStringLiteral("presetId")).toInt(-1) == presetId)
        {
            presets.removeAt(i);
            return true;
        }
    }
    return false;
}

bool FakeVcHost::vcWidgetPresetApply(quint32 id, int presetId, QString *error)
{
    auto it = m_widgets.find(id);
    if (it == m_widgets.end())
        return false;
    VcWidgetState &w = it.value();

    for (const QJsonValue &v : w.presets)
    {
        QJsonObject p = v.toObject();
        if (p.value(QStringLiteral("presetId")).toInt(-1) != presetId)
            continue;
        // Speed: the preset button's `speedObj.currentTime = preset.value`; the others have no
        // observable effect in this fake.
        if (w.widgetType == QStringLiteral("Speed"))
            return vcSpeedDialSetValue(id, p.value(QStringLiteral("valueMs")).toInt(), error);
        return true;
    }
    return false;
}

bool FakeVcHost::vcSpeedDialPresetUpdate(quint32 id, int presetId, const QJsonObject &patch, QString *error)
{
    Q_UNUSED(error)
    auto it = m_widgets.find(id);
    if (it == m_widgets.end())
        return false;
    QJsonArray &presets = it.value().presets;
    for (int i = 0; i < presets.size(); i++)
    {
        QJsonObject p = presets.at(i).toObject();
        if (p.value(QStringLiteral("presetId")).toInt(-1) != presetId)
            continue;
        if (patch.contains(QStringLiteral("name")))
            p.insert(QStringLiteral("name"), patch.value(QStringLiteral("name")));
        if (patch.contains(QStringLiteral("valueMs")))
            p.insert(QStringLiteral("valueMs"), patch.value(QStringLiteral("valueMs")).toInt());
        presets.replace(i, p);
        return true;
    }
    return false;
}
