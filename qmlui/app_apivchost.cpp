/*
  Q Light Controller Plus
  app_apivchost.cpp

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

/**
 * App's ApiVcHost implementation (see controlapi/src/apivchost.h and app.h's own "ApiVcHost
 * implementation" section) - drives the real, live VirtualConsole/VCPage/VCWidget object graph on
 * behalf of ApiVcDomain. Kept in its own translation unit, separate from app.cpp, because this is a
 * substantial, self-contained slice of VC-facing code with its own small set of local JSON<->engine
 * conversion helpers.
 *
 * Scope note: vcSetWidgetConfig()/widgetTypeConfigToJson() below only understand VCButton and
 * VCSlider - the two widget types this project's own show file (SF3.qxw) actually uses through the
 * control API today. Every other widget type's type-specific config (VCXYPad, VCLabel, VCCueList,
 * VCAnimation, VCAudioTriggers, VCSpeedDial, VCClock, VCFrame's own multipage/PIN settings) is left
 * for a future pass, same spirit as ApiVcDomain's own header comment about deliberately-unregistered
 * vc.* methods - vcSetWidgetConfig() reports an explicit "not yet supported" error for them rather
 * than silently accepting and discarding the request.
 */

#include <QColor>
#include <QFont>
#include <QHash>
#include <QJsonArray>
#include <QJsonObject>
#include <QJsonValue>
#include <QPointF>
#include <QSet>

#include "app.h"
#include "virtualconsole/virtualconsole.h"
#include "virtualconsole/vcpage.h"
#include "virtualconsole/vcframe.h"
#include "virtualconsole/vcbutton.h"
#include "virtualconsole/vcslider.h"

namespace {

QRectF rectFromJson(const QJsonObject &o)
{
    return QRectF(o.value(QStringLiteral("x")).toDouble(),
                  o.value(QStringLiteral("y")).toDouble(),
                  o.value(QStringLiteral("width")).toDouble(),
                  o.value(QStringLiteral("height")).toDouble());
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

// VcWidgetType wire strings (docs/api-spec/fragments/virtualconsole.yaml) vs VCWidget::WidgetType.
QString wireWidgetType(int type)
{
    switch (type)
    {
        case VCWidget::ButtonWidget: return QStringLiteral("Button");
        case VCWidget::SliderWidget: return QStringLiteral("Slider");
        case VCWidget::XYPadWidget: return QStringLiteral("XYPad");
        case VCWidget::FrameWidget: return QStringLiteral("Frame");
        case VCWidget::SoloFrameWidget: return QStringLiteral("SoloFrame");
        case VCWidget::SpeedWidget: return QStringLiteral("Speed");
        case VCWidget::CueListWidget: return QStringLiteral("CueList");
        case VCWidget::LabelWidget: return QStringLiteral("Label");
        case VCWidget::AudioTriggersWidget: return QStringLiteral("AudioTriggers");
        case VCWidget::AnimationWidget: return QStringLiteral("Animation");
        case VCWidget::ClockWidget: return QStringLiteral("Clock");
        default: return QString();
    }
}

// VCFrame::addWidget()'s $wType parameter goes through VCWidget::stringToType(), whose accepted
// strings ("Solo frame", "Audio Triggers", "Cue list") don't all match the wire VcWidgetType enum's
// ("SoloFrame", "AudioTriggers", "CueList") - translate before calling addWidget().
QString wireTypeToAddWidgetString(const QString &wire)
{
    if (wire == QStringLiteral("SoloFrame"))
        return QStringLiteral("Solo frame");
    if (wire == QStringLiteral("AudioTriggers"))
        return QStringLiteral("Audio Triggers");
    if (wire == QStringLiteral("CueList"))
        return QStringLiteral("Cue list");
    return wire; // Button, Slider, XYPad, Frame, Label, Animation, Clock, Speed match as-is
}

void applyStyleToWidget(VCWidget *w, const QJsonObject &style)
{
    if (style.contains(QStringLiteral("caption")))
        w->setCaption(style.value(QStringLiteral("caption")).toString());

    if (style.contains(QStringLiteral("backgroundColor")))
    {
        QJsonValue v = style.value(QStringLiteral("backgroundColor"));
        if (v.isNull())
            w->resetBackgroundColor();
        else
            w->setBackgroundColor(QColor(v.toString()));
    }
    if (style.contains(QStringLiteral("backgroundImage")))
    {
        QJsonValue v = style.value(QStringLiteral("backgroundImage"));
        w->setBackgroundImage(v.isNull() ? QString() : v.toString());
    }
    if (style.contains(QStringLiteral("foregroundColor")))
    {
        QJsonValue v = style.value(QStringLiteral("foregroundColor"));
        if (v.isNull())
            w->resetForegroundColor();
        else
            w->setForegroundColor(QColor(v.toString()));
    }
    if (style.contains(QStringLiteral("font")))
    {
        QJsonObject f = style.value(QStringLiteral("font")).toObject();
        if (f.isEmpty())
        {
            w->resetFont();
        }
        else
        {
            QFont font = w->font();
            if (f.contains(QStringLiteral("family")))
                font.setFamily(f.value(QStringLiteral("family")).toString());
            if (f.contains(QStringLiteral("pointSize")))
                font.setPointSize(f.value(QStringLiteral("pointSize")).toInt());
            if (f.contains(QStringLiteral("bold")))
                font.setBold(f.value(QStringLiteral("bold")).toBool());
            if (f.contains(QStringLiteral("italic")))
                font.setItalic(f.value(QStringLiteral("italic")).toBool());
            if (f.contains(QStringLiteral("underline")))
                font.setUnderline(f.value(QStringLiteral("underline")).toBool());
            w->setFont(font);
        }
    }
}

QJsonObject widgetStyleToJson(VCWidget *w)
{
    QJsonObject obj;
    obj.insert(QStringLiteral("caption"), w->caption());
    obj.insert(QStringLiteral("backgroundColor"),
               w->hasCustomBackgroundColor() ? QJsonValue(w->backgroundColor().name()) : QJsonValue());
    obj.insert(QStringLiteral("backgroundImage"),
               w->backgroundImage().isEmpty() ? QJsonValue() : QJsonValue(w->backgroundImage()));
    obj.insert(QStringLiteral("foregroundColor"),
               w->hasCustomForegroundColor() ? QJsonValue(w->foregroundColor().name()) : QJsonValue());

    QFont font = w->font();
    QJsonObject fontObj;
    fontObj.insert(QStringLiteral("family"), font.family());
    fontObj.insert(QStringLiteral("pointSize"), font.pointSize());
    fontObj.insert(QStringLiteral("bold"), font.bold());
    fontObj.insert(QStringLiteral("italic"), font.italic());
    fontObj.insert(QStringLiteral("underline"), font.underline());
    obj.insert(QStringLiteral("font"), fontObj);

    return obj;
}

// --- VcButtonConfig ---

bool applyButtonConfig(VCButton *b, const QJsonObject &patch, QString *error)
{
    static const QSet<QString> knownKeys = {
        QStringLiteral("functionID"), QStringLiteral("actionType"), QStringLiteral("stopAllFadeOutTime"),
        QStringLiteral("startupIntensityEnabled"), QStringLiteral("startupIntensity"),
        QStringLiteral("flashOverrides"), QStringLiteral("flashForceLTP")
    };
    static const QHash<QString, VCButton::ButtonAction> actions = {
        { QStringLiteral("Toggle"), VCButton::Toggle }, { QStringLiteral("Flash"), VCButton::Flash },
        { QStringLiteral("Blackout"), VCButton::Blackout }, { QStringLiteral("StopAll"), VCButton::StopAll }
    };

    for (auto it = patch.constBegin(); it != patch.constEnd(); ++it)
    {
        if (knownKeys.contains(it.key()) == false)
        {
            if (error)
                *error = QStringLiteral("Unknown VcButtonConfig key '%1'").arg(it.key());
            return false;
        }
    }

    if (patch.contains(QStringLiteral("functionID")))
    {
        bool ok = false;
        quint32 fid = patch.value(QStringLiteral("functionID")).toString().toUInt(&ok);
        if (ok == false)
        {
            if (error) *error = QStringLiteral("Invalid functionID");
            return false;
        }
        b->setFunctionID(fid);
    }
    if (patch.contains(QStringLiteral("actionType")))
    {
        QString a = patch.value(QStringLiteral("actionType")).toString();
        if (actions.contains(a) == false)
        {
            if (error) *error = QStringLiteral("Invalid actionType '%1'").arg(a);
            return false;
        }
        b->setActionType(actions.value(a));
    }
    if (patch.contains(QStringLiteral("stopAllFadeOutTime")))
        b->setStopAllFadeOutTime(patch.value(QStringLiteral("stopAllFadeOutTime")).toInt());
    if (patch.contains(QStringLiteral("startupIntensityEnabled")))
        b->setStartupIntensityEnabled(patch.value(QStringLiteral("startupIntensityEnabled")).toBool());
    if (patch.contains(QStringLiteral("startupIntensity")))
        b->setStartupIntensity(patch.value(QStringLiteral("startupIntensity")).toDouble());
    if (patch.contains(QStringLiteral("flashOverrides")))
        b->setFlashOverride(patch.value(QStringLiteral("flashOverrides")).toBool());
    if (patch.contains(QStringLiteral("flashForceLTP")))
        b->setFlashForceLTP(patch.value(QStringLiteral("flashForceLTP")).toBool());

    return true;
}

QJsonObject buttonConfigToJson(VCButton *b)
{
    static const QHash<VCButton::ButtonAction, QString> actions = {
        { VCButton::Toggle, QStringLiteral("Toggle") }, { VCButton::Flash, QStringLiteral("Flash") },
        { VCButton::Blackout, QStringLiteral("Blackout") }, { VCButton::StopAll, QStringLiteral("StopAll") }
    };

    QJsonObject obj;
    obj.insert(QStringLiteral("functionID"), QString::number(b->functionID()));
    obj.insert(QStringLiteral("actionType"), actions.value(b->actionType(), QStringLiteral("Toggle")));
    obj.insert(QStringLiteral("stopAllFadeOutTime"), b->stopAllFadeOutTime());
    obj.insert(QStringLiteral("startupIntensityEnabled"), b->startupIntensityEnabled());
    obj.insert(QStringLiteral("startupIntensity"), b->startupIntensity());
    obj.insert(QStringLiteral("flashOverrides"), b->flashOverrides());
    obj.insert(QStringLiteral("flashForceLTP"), b->flashForceLTP());
    return obj;
}

// --- VcSliderConfig ---
// Scoped to the base fields only - levelChannels/controlledFunction/controlledAttribute/
// adjustFlashEnabled/clickAndGoType/cngPrimaryColor/cngSecondaryColor/grandMasterValueMode/
// grandMasterChannelMode/monitorEnabled are left for a future pass (several already have, or the
// spec notes deserve, their own dedicated messages - see VcSliderConfig's own description in
// docs/api-spec/fragments/virtualconsole.yaml).

bool applySliderConfig(VCSlider *s, const QJsonObject &patch, QString *error)
{
    static const QSet<QString> knownKeys = {
        QStringLiteral("widgetStyle"), QStringLiteral("valueDisplayStyle"), QStringLiteral("invertedAppearance"),
        QStringLiteral("sliderMode"), QStringLiteral("catchValues"), QStringLiteral("rangeLowLimit"),
        QStringLiteral("rangeHighLimit")
    };
    static const QHash<QString, VCSlider::SliderWidgetStyle> widgetStyles = {
        { QStringLiteral("Slider"), VCSlider::WSlider }, { QStringLiteral("Knob"), VCSlider::WKnob }
    };
    static const QHash<QString, VCSlider::ValueDisplayStyle> displayStyles = {
        { QStringLiteral("DMXValue"), VCSlider::DMXValue }, { QStringLiteral("PercentageValue"), VCSlider::PercentageValue }
    };
    static const QHash<QString, VCSlider::SliderMode> sliderModes = {
        { QStringLiteral("Level"), VCSlider::Level }, { QStringLiteral("Adjust"), VCSlider::Adjust },
        { QStringLiteral("Submaster"), VCSlider::Submaster }, { QStringLiteral("GrandMaster"), VCSlider::GrandMaster }
    };

    for (auto it = patch.constBegin(); it != patch.constEnd(); ++it)
    {
        if (knownKeys.contains(it.key()) == false)
        {
            if (error)
                *error = QStringLiteral("Unknown or not-yet-supported VcSliderConfig key '%1'").arg(it.key());
            return false;
        }
    }

    if (patch.contains(QStringLiteral("widgetStyle")))
    {
        QString v = patch.value(QStringLiteral("widgetStyle")).toString();
        if (widgetStyles.contains(v) == false)
        {
            if (error) *error = QStringLiteral("Invalid widgetStyle '%1'").arg(v);
            return false;
        }
        s->setWidgetStyle(widgetStyles.value(v));
    }
    if (patch.contains(QStringLiteral("valueDisplayStyle")))
    {
        QString v = patch.value(QStringLiteral("valueDisplayStyle")).toString();
        if (displayStyles.contains(v) == false)
        {
            if (error) *error = QStringLiteral("Invalid valueDisplayStyle '%1'").arg(v);
            return false;
        }
        s->setValueDisplayStyle(displayStyles.value(v));
    }
    if (patch.contains(QStringLiteral("invertedAppearance")))
        s->setInvertedAppearance(patch.value(QStringLiteral("invertedAppearance")).toBool());
    if (patch.contains(QStringLiteral("sliderMode")))
    {
        QString v = patch.value(QStringLiteral("sliderMode")).toString();
        if (sliderModes.contains(v) == false)
        {
            if (error) *error = QStringLiteral("Invalid sliderMode '%1'").arg(v);
            return false;
        }
        s->setSliderMode(sliderModes.value(v));
    }
    if (patch.contains(QStringLiteral("catchValues")))
        s->setCatchValues(patch.value(QStringLiteral("catchValues")).toBool());
    if (patch.contains(QStringLiteral("rangeLowLimit")))
        s->setRangeLowLimit(patch.value(QStringLiteral("rangeLowLimit")).toDouble());
    if (patch.contains(QStringLiteral("rangeHighLimit")))
        s->setRangeHighLimit(patch.value(QStringLiteral("rangeHighLimit")).toDouble());

    return true;
}

QJsonObject sliderConfigToJson(VCSlider *s)
{
    QJsonObject obj;
    obj.insert(QStringLiteral("widgetStyle"), s->widgetStyle() == VCSlider::WKnob ? QStringLiteral("Knob") : QStringLiteral("Slider"));
    obj.insert(QStringLiteral("valueDisplayStyle"),
               s->valueDisplayStyle() == VCSlider::PercentageValue ? QStringLiteral("PercentageValue") : QStringLiteral("DMXValue"));
    obj.insert(QStringLiteral("invertedAppearance"), s->invertedAppearance());
    static const QHash<VCSlider::SliderMode, QString> modes = {
        { VCSlider::Level, QStringLiteral("Level") }, { VCSlider::Adjust, QStringLiteral("Adjust") },
        { VCSlider::Submaster, QStringLiteral("Submaster") }, { VCSlider::GrandMaster, QStringLiteral("GrandMaster") }
    };
    obj.insert(QStringLiteral("sliderMode"), modes.value(s->sliderMode(), QStringLiteral("Level")));
    obj.insert(QStringLiteral("catchValues"), s->catchValues());
    obj.insert(QStringLiteral("rangeLowLimit"), s->rangeLowLimit());
    obj.insert(QStringLiteral("rangeHighLimit"), s->rangeHighLimit());
    return obj;
}

QJsonObject widgetTypeConfigToJson(VCWidget *w)
{
    if (w->type() == VCWidget::ButtonWidget)
        return buttonConfigToJson(qobject_cast<VCButton *>(w));
    if (w->type() == VCWidget::SliderWidget)
        return sliderConfigToJson(qobject_cast<VCSlider *>(w));
    return QJsonObject();
}

} // namespace

VCWidget *App::vcFindWidget(quint32 id) const
{
    return m_virtualConsole != nullptr ? m_virtualConsole->widget(id) : nullptr;
}

static VCPage *vcTopLevelPageOf(VCWidget *w)
{
    VCFrame *frame = qobject_cast<VCFrame *>(w->parent());
    while (frame != nullptr && qobject_cast<VCPage *>(frame) == nullptr)
        frame = qobject_cast<VCFrame *>(frame->parent());
    return qobject_cast<VCPage *>(frame);
}

/*****************************************************************************
 * Pages
 *****************************************************************************/

int App::vcPageCount() const
{
    return m_virtualConsole->pagesCount();
}

QJsonObject App::vcPageSnapshot(int index) const
{
    VCPage *page = m_virtualConsole->page(index);
    QJsonObject obj;
    obj.insert(QStringLiteral("index"), index);
    obj.insert(QStringLiteral("name"), page != nullptr ? page->caption() : QString());
    obj.insert(QStringLiteral("hasPin"), page != nullptr && page->PIN() != 0);
    return obj;
}

int App::vcSelectedPage() const
{
    return m_virtualConsole->selectedPage();
}

void App::vcSetSelectedPage(int index)
{
    m_virtualConsole->setSelectedPage(index);
}

void App::vcAddPage(int index)
{
    m_virtualConsole->addPage(index);
}

bool App::vcDeletePage(int index, QJsonArray &deletedWidgetIds)
{
    if (index < 0 || index >= m_virtualConsole->pagesCount() || m_virtualConsole->pagesCount() == 1)
        return false;

    VCPage *page = m_virtualConsole->page(index);
    if (page != nullptr)
    {
        for (VCWidget *w : page->children(true))
            deletedWidgetIds.append(QString::number(w->id()));
    }

    m_virtualConsole->deletePage(index);
    return true;
}

void App::vcRenamePage(int index, const QString &name)
{
    VCPage *page = m_virtualConsole->page(index);
    if (page != nullptr)
        page->setCaption(name);
}

bool App::vcSetPagePin(int index, const QString &currentPin, const QString &newPin)
{
    return m_virtualConsole->setPagePIN(index, currentPin, newPin);
}

bool App::vcValidatePagePin(int index, const QString &pin) const
{
    // Stateless check only - does NOT call VirtualConsole::validatePagePIN()'s $remember path, since
    // that marks the page validated globally (VCFrame::m_validatedPIN), not per-connection as the
    // spec's own vc.page.validatePin description promises. Implementing real per-session PIN
    // unlocking is left for a future pass.
    VCPage *page = m_virtualConsole->page(index);
    if (page == nullptr)
        return false;
    return page->PIN() == 0 || page->PIN() == pin.toInt();
}

/*****************************************************************************
 * Widgets - queries
 *****************************************************************************/

bool App::vcWidgetExists(quint32 id) const
{
    return vcFindWidget(id) != nullptr;
}

QString App::vcWidgetType(quint32 id) const
{
    VCWidget *w = vcFindWidget(id);
    return w != nullptr ? wireWidgetType(w->type()) : QString();
}

int App::vcWidgetPage(quint32 id) const
{
    VCWidget *w = vcFindWidget(id);
    if (w == nullptr)
        return -1;

    VCPage *page = vcTopLevelPageOf(w);
    if (page == nullptr)
        return -1;

    for (int i = 0; i < m_virtualConsole->pagesCount(); i++)
    {
        if (m_virtualConsole->page(i) == page)
            return i;
    }
    return -1;
}

quint32 App::vcWidgetParentId(quint32 id) const
{
    VCWidget *w = vcFindWidget(id);
    if (w == nullptr)
        return ApiVcHost::InvalidWidgetId;

    VCFrame *parentFrame = qobject_cast<VCFrame *>(w->parent());
    if (parentFrame == nullptr || qobject_cast<VCPage *>(parentFrame) != nullptr)
        return ApiVcHost::InvalidWidgetId; // page root (or, defensively, no frame parent at all)

    return parentFrame->id();
}

bool App::vcIsContainerWidget(quint32 id) const
{
    VCWidget *w = vcFindWidget(id);
    if (w == nullptr)
        return false;
    return w->type() == VCWidget::FrameWidget || w->type() == VCWidget::SoloFrameWidget;
}

QList<quint32> App::vcWidgetIds() const
{
    QList<quint32> ids;
    for (const QVariant &v : m_virtualConsole->widgetsList())
        ids.append(v.toMap().value(QStringLiteral("id")).toUInt());
    return ids;
}

QJsonObject App::vcWidgetSnapshot(quint32 id) const
{
    VCWidget *w = vcFindWidget(id);
    if (w == nullptr)
        return QJsonObject();

    QJsonObject obj;
    obj.insert(QStringLiteral("id"), QString::number(w->id()));
    obj.insert(QStringLiteral("widgetType"), wireWidgetType(w->type()));
    obj.insert(QStringLiteral("page"), vcWidgetPage(id));

    quint32 parentId = vcWidgetParentId(id);
    if (parentId != ApiVcHost::InvalidWidgetId)
        obj.insert(QStringLiteral("parentId"), QString::number(parentId));

    obj.insert(QStringLiteral("geometry"), rectToJson(w->geometry()));
    obj.insert(QStringLiteral("zIndex"), w->zIndex());
    obj.insert(QStringLiteral("allowResize"), w->allowResize());
    obj.insert(QStringLiteral("isDisabled"), w->isDisabled());
    obj.insert(QStringLiteral("isVisible"), w->isVisible());
    obj.insert(QStringLiteral("style"), widgetStyleToJson(w));
    obj.insert(QStringLiteral("typeConfig"), widgetTypeConfigToJson(w));
    obj.insert(QStringLiteral("inputSources"), QJsonArray());
    obj.insert(QStringLiteral("keySequences"), QJsonArray());
    obj.insert(QStringLiteral("externalControls"), QJsonArray());
    return obj;
}

/*****************************************************************************
 * Widgets - mutations
 *****************************************************************************/

quint32 App::vcCreateWidget(const QString &widgetType, int page, quint32 parentId,
                             const QJsonObject &geometry, const QJsonObject &style,
                             const QJsonObject &typeConfig, QString *error)
{
    VCFrame *targetFrame = nullptr;
    if (parentId == ApiVcHost::InvalidWidgetId)
        targetFrame = m_virtualConsole->page(page);
    else
        targetFrame = qobject_cast<VCFrame *>(vcFindWidget(parentId));

    if (targetFrame == nullptr)
    {
        if (error) *error = QStringLiteral("Unable to resolve target container");
        return ApiVcHost::InvalidWidgetId;
    }

    QRectF rect = rectFromJson(geometry);
    // $parentItem may be null if the target page/frame has never been rendered on screen yet (e.g.
    // the user has never opened that Virtual Console page) - every VCWidget::render() override
    // already guards against a null view/parent (see vcwidget.cpp), so the widget is simply created
    // un-rendered and picks up its onscreen item the next time its page is opened, same as a widget
    // loaded from XML with $render=false (VCFrame::loadWidgetXML()).
    QQuickItem *parentItem = targetFrame->renderItem();

    VCWidget *w = targetFrame->addWidget(parentItem, wireTypeToAddWidgetString(widgetType), rect.topLeft().toPoint());
    if (w == nullptr)
    {
        if (error) *error = QStringLiteral("Unable to create widget");
        return ApiVcHost::InvalidWidgetId;
    }

    // addWidget() applies its own type-specific default size - honor the caller's requested size too.
    w->setGeometry(rect);

    if (style.isEmpty() == false)
        applyStyleToWidget(w, style);
    if (typeConfig.isEmpty() == false)
    {
        QString configError;
        vcSetWidgetConfig(w->id(), typeConfig, &configError); // best-effort; creation itself already succeeded
    }

    return w->id();
}

void App::vcDeleteWidgets(const QList<quint32> &ids, QJsonArray &deletedIds)
{
    QVariantList toDelete;
    for (quint32 id : ids)
    {
        VCWidget *w = vcFindWidget(id);
        if (w == nullptr)
            continue;

        deletedIds.append(QString::number(id));
        VCFrame *frame = qobject_cast<VCFrame *>(w);
        if (frame != nullptr)
        {
            for (VCWidget *child : frame->children(true))
                deletedIds.append(QString::number(child->id()));
        }
        toDelete.append(QVariant(id));
    }

    if (toDelete.isEmpty() == false)
        m_virtualConsole->deleteVCWidgets(toDelete);
}

bool App::vcUpdateWidgetCommon(quint32 id, const QJsonObject &fields, QString *error)
{
    VCWidget *w = vcFindWidget(id);
    if (w == nullptr)
    {
        if (error) *error = QStringLiteral("No such widget");
        return false;
    }

    if (fields.contains(QStringLiteral("geometry")))
        w->setGeometry(rectFromJson(fields.value(QStringLiteral("geometry")).toObject()));
    if (fields.contains(QStringLiteral("zIndex")))
        w->setZIndex(fields.value(QStringLiteral("zIndex")).toInt());
    if (fields.contains(QStringLiteral("allowResize")))
        w->setAllowResize(fields.value(QStringLiteral("allowResize")).toBool());
    if (fields.contains(QStringLiteral("isDisabled")))
        w->setDisabled(fields.value(QStringLiteral("isDisabled")).toBool());
    if (fields.contains(QStringLiteral("isVisible")))
        w->setVisible(fields.value(QStringLiteral("isVisible")).toBool());
    if (fields.contains(QStringLiteral("style")))
        applyStyleToWidget(w, fields.value(QStringLiteral("style")).toObject());

    return true;
}

void App::vcMoveTopLevelWidgetToPage(quint32 id, int newPage)
{
    VCWidget *w = vcFindWidget(id);
    if (w == nullptr)
        return;

    VCPage *targetPage = m_virtualConsole->page(newPage);
    if (targetPage == nullptr)
        return;

    m_virtualConsole->moveWidget(w, targetPage, w->geometry().topLeft().toPoint());
}

bool App::vcSetWidgetConfig(quint32 id, const QJsonObject &configPatch, QString *error)
{
    VCWidget *w = vcFindWidget(id);
    if (w == nullptr)
    {
        if (error) *error = QStringLiteral("No such widget");
        return false;
    }

    if (w->type() == VCWidget::ButtonWidget)
        return applyButtonConfig(qobject_cast<VCButton *>(w), configPatch, error);
    if (w->type() == VCWidget::SliderWidget)
        return applySliderConfig(qobject_cast<VCSlider *>(w), configPatch, error);

    if (error)
        *error = QStringLiteral("vc.widget.setConfig is not yet supported for widget type '%1'").arg(wireWidgetType(w->type()));
    return false;
}

bool App::vcReparentWidget(quint32 id, quint32 newParentId, QPointF newTopLeft, QString *error)
{
    VCWidget *w = vcFindWidget(id);
    if (w == nullptr)
    {
        if (error) *error = QStringLiteral("No such widget");
        return false;
    }

    VCFrame *targetFrame = nullptr;
    if (newParentId == ApiVcHost::InvalidWidgetId)
        targetFrame = vcTopLevelPageOf(w); // page root - stays on the widget's own current page
    else
        targetFrame = qobject_cast<VCFrame *>(vcFindWidget(newParentId));

    if (targetFrame == nullptr)
    {
        if (error) *error = QStringLiteral("Unable to resolve target parent");
        return false;
    }

    m_virtualConsole->moveWidget(w, targetFrame, newTopLeft.toPoint());
    return true;
}

void App::vcRepositionWidgets(const QList<QPair<quint32, QJsonObject> > &updates)
{
    for (const auto &pair : updates)
    {
        VCWidget *w = vcFindWidget(pair.first);
        if (w != nullptr)
            w->setGeometry(rectFromJson(pair.second));
    }
}
