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
 * Scope note: vcSetWidgetConfig()/widgetTypeConfigToJson() below implement the full VcButtonConfig
 * and VcSliderConfig schemas themselves; every other widget type's type-specific config lives in
 * app_apivcconfig_{layout,cue,live}.cpp (see app_apivcconfig.h) - a type whose slice has not landed
 * yet reports an explicit "not yet supported" error rather than silently accepting and discarding
 * the request. The "layout / configuration slice" section at the very bottom holds the host side of
 * ApiVcLayoutDomain (frame PIN / clone, slider level channels / flash, the bulk creators, usage).
 *
 * Live interaction (the "Widgets - live interaction" section at the bottom) is a separate, wider
 * slice: it drives VCButton/VCSlider/VCCueList/VCXYPad/VCSpeedDial/VCFrame exactly the way their QML
 * items do (VCButtonItem.qml's requestStateChange() calls, VCSliderItem.qml's `sliderObj.value = ...`,
 * VCCueListItem.qml's playClicked()/stopClicked()/..., VCXYPadItem.qml's `currentPosition = ...`,
 * VCSpeedDialItem.qml's `currentTime = ...`/tap(), VCFrame::gotoPage()) and relays the widgets' own
 * change signals to the control API's ApiVcLiveListener, so a change made from ANY source (API, QML,
 * external input, a Function stopping) reaches every connected client the same way.
 */

#include <QColor>
#include <QFont>
#include <QHash>
#include <QJsonArray>
#include <QJsonObject>
#include <QJsonValue>
#include <QPointF>
#include <QPointer>
#include <QSet>
#include <algorithm>

#include "app.h"
#include "app_apivcconfig.h"
#include "chaser.h"
#include "chaserstep.h"
#include "fixture.h"
#include "function.h"
#include "virtualconsole/virtualconsole.h"
#include "virtualconsole/vcpage.h"
#include "virtualconsole/vcframe.h"
#include "virtualconsole/vcbutton.h"
#include "virtualconsole/vcslider.h"
#include "virtualconsole/vccuelist.h"
#include "virtualconsole/vcxypad.h"
#include "virtualconsole/vcspeeddial.h"

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
// The full VcSliderConfig schema (docs/api-spec/fragments/virtualconsole.yaml). Order matters when
// several keys arrive in one patch, because VCSlider's setters cascade: setSliderMode() resets both
// range limits to 0..255 and the controlled attribute to Intensity, and setControlledFunction()
// switches the slider to Adjust mode (resetting again) - so the patch is applied as sliderMode ->
// controlledFunction -> controlledAttribute -> everything else -> range limits last, whatever
// order the JSON object's keys are in.

bool parseLevelChannels(Doc *doc, const QJsonValue &value, QList<QPair<quint32, quint32> > &out, QString *error)
{
    if (value.isArray() == false)
    {
        if (error) *error = QStringLiteral("levelChannels must be an array of {fixtureId, channel}");
        return false;
    }
    for (const QJsonValue &v : value.toArray())
    {
        QJsonObject entry = v.toObject();
        bool ok = false;
        quint32 fixtureId = entry.value(QStringLiteral("fixtureId")).toString().toUInt(&ok);
        Fixture *fixture = ok ? doc->fixture(fixtureId) : nullptr;
        if (fixture == nullptr)
        {
            if (error) *error = QStringLiteral("levelChannels: no such fixture '%1'").arg(entry.value(QStringLiteral("fixtureId")).toString());
            return false;
        }
        QJsonValue chValue = entry.value(QStringLiteral("channel"));
        if (chValue.isDouble() == false || chValue.toDouble() < 0 || chValue.toDouble() >= fixture->channels())
        {
            if (error) *error = QStringLiteral("levelChannels: fixture %1 has no channel %2").arg(fixtureId).arg(chValue.toDouble());
            return false;
        }
        out.append(qMakePair(fixtureId, quint32(chValue.toInt())));
    }
    return true;
}

void replaceLevelChannels(VCSlider *s, const QList<QPair<quint32, quint32> > &channels)
{
    s->clearLevelChannels();
    for (const auto &ch : channels)
        s->addLevelChannel(ch.first, ch.second);
}

bool applySliderConfig(VCSlider *s, Doc *doc, const QJsonObject &patch, QString *error)
{
    static const QSet<QString> knownKeys = {
        QStringLiteral("widgetStyle"), QStringLiteral("valueDisplayStyle"), QStringLiteral("invertedAppearance"),
        QStringLiteral("sliderMode"), QStringLiteral("catchValues"), QStringLiteral("rangeLowLimit"),
        QStringLiteral("rangeHighLimit"), QStringLiteral("monitorEnabled"), QStringLiteral("levelChannels"),
        QStringLiteral("controlledFunction"), QStringLiteral("controlledAttribute"), QStringLiteral("adjustFlashEnabled"),
        QStringLiteral("clickAndGoType"), QStringLiteral("cngPrimaryColor"), QStringLiteral("cngSecondaryColor"),
        QStringLiteral("grandMasterValueMode"), QStringLiteral("grandMasterChannelMode")
    };
    static const QHash<QString, VCSlider::ClickAndGoType> cngTypes = {
        { QStringLiteral("None"), VCSlider::CnGNone }, { QStringLiteral("Colors"), VCSlider::CnGColors },
        { QStringLiteral("Preset"), VCSlider::CnGPreset }
    };
    static const QHash<QString, GrandMaster::ValueMode> gmValueModes = {
        { QStringLiteral("Limit"), GrandMaster::Limit }, { QStringLiteral("Reduce"), GrandMaster::Reduce }
    };
    static const QHash<QString, GrandMaster::ChannelMode> gmChannelModes = {
        { QStringLiteral("Intensity"), GrandMaster::Intensity }, { QStringLiteral("AllChannels"), GrandMaster::AllChannels }
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
                *error = QStringLiteral("Unknown VcSliderConfig key '%1'").arg(it.key());
            return false;
        }
    }

    // ---- validate everything first (all-or-nothing) ----
    auto enumCheck = [&](const QString &key, const auto &table) -> bool
    {
        if (patch.contains(key) == false)
            return true;
        QString v = patch.value(key).toString();
        if (table.contains(v))
            return true;
        if (error) *error = QStringLiteral("Invalid %1 '%2'").arg(key, v);
        return false;
    };
    if (!enumCheck(QStringLiteral("widgetStyle"), widgetStyles) || !enumCheck(QStringLiteral("valueDisplayStyle"), displayStyles) ||
        !enumCheck(QStringLiteral("sliderMode"), sliderModes) || !enumCheck(QStringLiteral("clickAndGoType"), cngTypes) ||
        !enumCheck(QStringLiteral("grandMasterValueMode"), gmValueModes) || !enumCheck(QStringLiteral("grandMasterChannelMode"), gmChannelModes))
        return false;

    QList<QPair<quint32, quint32> > levelChannels;
    if (patch.contains(QStringLiteral("levelChannels")) &&
        parseLevelChannels(doc, patch.value(QStringLiteral("levelChannels")), levelChannels, error) == false)
        return false;

    // The controlled Function: a Function id string, or Function::invalidId() ("4294967295") to detach.
    bool hasControlledFunction = patch.contains(QStringLiteral("controlledFunction"));
    quint32 controlledFunction = Function::invalidId();
    if (hasControlledFunction)
    {
        bool ok = false;
        controlledFunction = patch.value(QStringLiteral("controlledFunction")).toString().toUInt(&ok);
        if (ok == false || (controlledFunction != Function::invalidId() && doc->function(controlledFunction) == nullptr))
        {
            if (error) *error = QStringLiteral("controlledFunction: no such function");
            return false;
        }
    }
    // The attribute is validated against the Function the slider will control AFTER this patch.
    Function *targetFunction = doc->function(hasControlledFunction ? controlledFunction : s->controlledFunction());
    if (patch.contains(QStringLiteral("controlledAttribute")))
    {
        QJsonValue v = patch.value(QStringLiteral("controlledAttribute"));
        int count = targetFunction != nullptr ? targetFunction->attributes().count() : 0;
        if (v.isDouble() == false || v.toDouble() < 0 || v.toDouble() >= count)
        {
            if (error) *error = targetFunction == nullptr ? QStringLiteral("controlledAttribute requires a controlled function")
                                                          : QStringLiteral("controlledAttribute must be 0..%1").arg(count - 1);
            return false;
        }
    }
    for (const QString &key : { QStringLiteral("cngPrimaryColor"), QStringLiteral("cngSecondaryColor") })
    {
        if (patch.contains(key) && QColor::isValidColorName(patch.value(key).toString()) == false)
        {
            if (error) *error = QStringLiteral("%1 must be a colour name or #rrggbb").arg(key);
            return false;
        }
    }
    // Range limits: Level mode's are DMX values (the editor's spin boxes confine them to 0..255);
    // Adjust mode's follow the controlled attribute's own min..max (VCSliderProperties.qml binds the
    // spin boxes to attributeMinValue/attributeMaxValue), which the engine only knows once the mode
    // and attribute are applied - so that part of the check runs after the mode switch below.
    for (const QString &key : { QStringLiteral("rangeLowLimit"), QStringLiteral("rangeHighLimit") })
    {
        if (patch.contains(key) && patch.value(key).isDouble() == false)
        {
            if (error) *error = QStringLiteral("%1 must be a number").arg(key);
            return false;
        }
    }
    QString requestedMode = patch.contains(QStringLiteral("sliderMode")) ? patch.value(QStringLiteral("sliderMode")).toString() : QString();
    bool willBeAdjust = requestedMode.isEmpty() ? s->sliderMode() == VCSlider::Adjust : requestedMode == QStringLiteral("Adjust");
    if (hasControlledFunction && controlledFunction != Function::invalidId())
        willBeAdjust = true; // setControlledFunction() switches to Adjust on its own
    if (willBeAdjust == false)
    {
        for (const QString &key : { QStringLiteral("rangeLowLimit"), QStringLiteral("rangeHighLimit") })
        {
            if (patch.contains(key) && (patch.value(key).toDouble() < 0.0 || patch.value(key).toDouble() > 255.0))
            {
                if (error) *error = QStringLiteral("%1 must be a number 0..255").arg(key);
                return false;
            }
        }
    }

    // ---- apply, in cascade order ----
    if (patch.contains(QStringLiteral("sliderMode")))
        s->setSliderMode(sliderModes.value(requestedMode));
    if (hasControlledFunction)
        s->setControlledFunction(controlledFunction);
    if (patch.contains(QStringLiteral("controlledAttribute")))
        s->setControlledAttribute(patch.value(QStringLiteral("controlledAttribute")).toInt());

    if (patch.contains(QStringLiteral("widgetStyle")))
        s->setWidgetStyle(widgetStyles.value(patch.value(QStringLiteral("widgetStyle")).toString()));
    if (patch.contains(QStringLiteral("valueDisplayStyle")))
        s->setValueDisplayStyle(displayStyles.value(patch.value(QStringLiteral("valueDisplayStyle")).toString()));
    if (patch.contains(QStringLiteral("invertedAppearance")))
        s->setInvertedAppearance(patch.value(QStringLiteral("invertedAppearance")).toBool());
    if (patch.contains(QStringLiteral("catchValues")))
        s->setCatchValues(patch.value(QStringLiteral("catchValues")).toBool());
    if (patch.contains(QStringLiteral("monitorEnabled")))
        s->setMonitorEnabled(patch.value(QStringLiteral("monitorEnabled")).toBool());
    if (patch.contains(QStringLiteral("levelChannels")))
        replaceLevelChannels(s, levelChannels);
    if (patch.contains(QStringLiteral("adjustFlashEnabled")))
        s->setAdjustFlashEnabled(patch.value(QStringLiteral("adjustFlashEnabled")).toBool());
    if (patch.contains(QStringLiteral("clickAndGoType")))
        s->setClickAndGoType(cngTypes.value(patch.value(QStringLiteral("clickAndGoType")).toString()));
    if (patch.contains(QStringLiteral("cngPrimaryColor")) || patch.contains(QStringLiteral("cngSecondaryColor")))
    {
        // VCSlider only has a combined setter (the on-screen colour tool sets both at once); it also
        // moves the fader to 128 and writes DMX, exactly as the Click & Go popup does.
        QColor primary = patch.contains(QStringLiteral("cngPrimaryColor")) ? QColor(patch.value(QStringLiteral("cngPrimaryColor")).toString()) : s->cngPrimaryColor();
        QColor secondary = patch.contains(QStringLiteral("cngSecondaryColor")) ? QColor(patch.value(QStringLiteral("cngSecondaryColor")).toString()) : s->cngSecondaryColor();
        s->setClickAndGoColors(primary, secondary);
    }
    if (patch.contains(QStringLiteral("grandMasterValueMode")))
        s->setGrandMasterValueMode(gmValueModes.value(patch.value(QStringLiteral("grandMasterValueMode")).toString()));
    if (patch.contains(QStringLiteral("grandMasterChannelMode")))
        s->setGrandMasterChannelMode(gmChannelModes.value(patch.value(QStringLiteral("grandMasterChannelMode")).toString()));

    // Range limits last (setSliderMode() above would have reset them). In Adjust mode the bounds are
    // the attribute's; anything outside is confined rather than rejected, since the mode switch this
    // very patch may have asked for is already applied at this point.
    if (s->sliderMode() == VCSlider::Adjust)
    {
        qreal lo = s->attributeMinValue(), hi = s->attributeMaxValue();
        if (patch.contains(QStringLiteral("rangeLowLimit")))
            s->setRangeLowLimit(qBound(lo, patch.value(QStringLiteral("rangeLowLimit")).toDouble(), hi));
        if (patch.contains(QStringLiteral("rangeHighLimit")))
            s->setRangeHighLimit(qBound(lo, patch.value(QStringLiteral("rangeHighLimit")).toDouble(), hi));
    }
    else
    {
        if (patch.contains(QStringLiteral("rangeLowLimit")))
            s->setRangeLowLimit(patch.value(QStringLiteral("rangeLowLimit")).toDouble());
        if (patch.contains(QStringLiteral("rangeHighLimit")))
            s->setRangeHighLimit(patch.value(QStringLiteral("rangeHighLimit")).toDouble());
    }

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

    // Level mode
    obj.insert(QStringLiteral("monitorEnabled"), s->monitorEnabled());
    QJsonArray channels;
    for (const SceneValue &scv : s->levelChannels())
    {
        QJsonObject ch;
        ch.insert(QStringLiteral("fixtureId"), QString::number(scv.fxi));
        ch.insert(QStringLiteral("channel"), int(scv.channel));
        channels.append(ch);
    }
    obj.insert(QStringLiteral("levelChannels"), channels);
    obj.insert(QStringLiteral("clickAndGoType"), VCSlider::clickAndGoTypeToString(s->clickAndGoType()));
    obj.insert(QStringLiteral("cngPrimaryColor"), s->cngPrimaryColor().name());
    obj.insert(QStringLiteral("cngSecondaryColor"), s->cngSecondaryColor().name());

    // Adjust mode
    obj.insert(QStringLiteral("controlledFunction"), QString::number(s->controlledFunction()));
    obj.insert(QStringLiteral("controlledAttribute"), s->controlledAttribute());
    obj.insert(QStringLiteral("adjustFlashEnabled"), s->adjustFlashEnabled());

    // Grand Master mode
    obj.insert(QStringLiteral("grandMasterValueMode"), s->grandMasterValueMode() == GrandMaster::Reduce ? QStringLiteral("Reduce") : QStringLiteral("Limit"));
    obj.insert(QStringLiteral("grandMasterChannelMode"), s->grandMasterChannelMode() == GrandMaster::AllChannels ? QStringLiteral("AllChannels") : QStringLiteral("Intensity"));
    return obj;
}

QJsonObject widgetTypeConfigToJson(VCWidget *w)
{
    // Button/Slider live in this file; every other type in app_apivcconfig_{layout,live}.cpp
    switch (w->type())
    {
        case VCWidget::ButtonWidget:        return buttonConfigToJson(qobject_cast<VCButton *>(w));
        case VCWidget::SliderWidget:        return sliderConfigToJson(qobject_cast<VCSlider *>(w));
        case VCWidget::FrameWidget:
        case VCWidget::SoloFrameWidget:     return ApiVcConfig::frameConfigToJson(w);
        case VCWidget::LabelWidget:         return ApiVcConfig::labelConfigToJson(w);
        case VCWidget::CueListWidget:       return ApiVcConfig::cueListConfigToJson(w);
        case VCWidget::SpeedWidget:         return ApiVcConfig::speedDialConfigToJson(w);
        case VCWidget::XYPadWidget:         return ApiVcConfig::xyPadConfigToJson(w);
        case VCWidget::ClockWidget:         return ApiVcConfig::clockConfigToJson(w);
        case VCWidget::AnimationWidget:     return ApiVcConfig::animationConfigToJson(w);
        case VCWidget::AudioTriggersWidget: return ApiVcConfig::audioTriggersConfigToJson(w);
        default:                            return QJsonObject();
    }
}

// --- Live state (vc.widget.get/list's additive per-type fields, and the vc.*Changed events) ---

QString buttonStateString(int state)
{
    switch (state)
    {
        case VCButton::Active: return QStringLiteral("active");
        case VCButton::Monitoring: return QStringLiteral("monitoring");
        default: return QStringLiteral("inactive");
    }
}

// VCXYPad::currentPosition() lives in a 0..(255 + 255/256) domain (vcxypad.cpp's kPosMax: DMX MSB with
// the LSB as a fraction, i.e. 65535/256 at full scale). The API exposes it normalized 0..1 so 1.0 is
// the full 16-bit span in both directions.
constexpr double kXyPadPositionScale = 65535.0 / 256.0;

double xyPadPosToNormalized(qreal pos)
{
    return qBound(0.0, double(pos) / kXyPadPositionScale, 1.0);
}

qreal normalizedToXyPadPos(double n)
{
    return qreal(qBound(0.0, n, 1.0) * kXyPadPositionScale);
}

void cueListPlaybackState(VCCueList *cl, int &playbackIndex, bool &running, bool &paused)
{
    // "running" = the Chaser is active at all (Playing or Paused) - a paused Chaser is still
    // Function::isRunning(); "paused" narrows that down. Both false = Stopped.
    VCCueList::PlaybackStatus status = cl->playbackStatus();
    playbackIndex = cl->playbackIndex();
    running = status != VCCueList::Stopped;
    paused = status == VCCueList::Paused;
}

// Speeds are uint milliseconds with Function::infiniteSpeed() (0xFFFFFFFF) meaning "infinite" - keep
// that sentinel intact as a positive number instead of letting an int cast turn it into -1.
QJsonValue speedToJson(uint ms)
{
    return QJsonValue(qint64(ms));
}

// Same value resolution VCCueListItem.qml's step rows show (ChaserEditor::stepDataMap(): the Chaser's
// Common/PerStep/Default speed modes decide whether a step's own value, the Chaser's, or the step
// Function's applies) - minus that helper's side effect of writing the resolved values back into the
// ChaserStep, which a read-only API call must not do.
QJsonArray cueListStepsToJson(Doc *doc, Chaser *chaser)
{
    QJsonArray steps;
    if (chaser == nullptr)
        return steps;

    for (int i = 0; i < chaser->stepsCount(); i++)
    {
        ChaserStep *step = chaser->stepAt(i);
        if (step == nullptr)
            continue;
        Function *func = doc->function(step->fid);

        uint fadeIn = 0, fadeOut = 0, hold = 0;
        switch (chaser->fadeInMode())
        {
            case Chaser::Common: fadeIn = chaser->fadeInSpeed(); break;
            case Chaser::PerStep: fadeIn = step->fadeIn; break;
            default: fadeIn = func != nullptr ? func->fadeInSpeed() : 0; break;
        }
        switch (chaser->fadeOutMode())
        {
            case Chaser::Common: fadeOut = chaser->fadeOutSpeed(); break;
            case Chaser::PerStep: fadeOut = step->fadeOut; break;
            default: fadeOut = func != nullptr ? func->fadeOutSpeed() : 0; break;
        }
        switch (chaser->durationMode())
        {
            case Chaser::Common: hold = Function::speedSubtract(chaser->duration(), step->fadeIn); break;
            case Chaser::PerStep: hold = step->hold; break;
            default:
                hold = func != nullptr ? Function::speedSubtract(func->totalDuration(), func->fadeInSpeed()) : 0;
            break;
        }

        QJsonObject obj;
        obj.insert(QStringLiteral("index"), i);
        obj.insert(QStringLiteral("name"), func != nullptr ? func->name() : QString());
        obj.insert(QStringLiteral("functionId"), QString::number(step->fid));
        obj.insert(QStringLiteral("fadeIn"), speedToJson(fadeIn));
        obj.insert(QStringLiteral("fadeOut"), speedToJson(fadeOut));
        obj.insert(QStringLiteral("hold"), speedToJson(hold));
        obj.insert(QStringLiteral("notes"), step->note);
        steps.append(obj);
    }
    return steps;
}

void appendLiveStateToJson(VCWidget *w, QJsonObject &obj)
{
    switch (w->type())
    {
        case VCWidget::ButtonWidget:
        {
            VCButton *b = qobject_cast<VCButton *>(w);
            obj.insert(QStringLiteral("state"), buttonStateString(b->state()));
        }
        break;
        case VCWidget::SliderWidget:
        {
            VCSlider *s = qobject_cast<VCSlider *>(w);
            obj.insert(QStringLiteral("value"), s->value());
            obj.insert(QStringLiteral("min"), s->rangeLowLimit());
            obj.insert(QStringLiteral("max"), s->rangeHighLimit());
        }
        break;
        case VCWidget::CueListWidget:
        {
            int playbackIndex = -1;
            bool running = false, paused = false;
            VCCueList *cl = qobject_cast<VCCueList *>(w);
            cueListPlaybackState(cl, playbackIndex, running, paused);
            obj.insert(QStringLiteral("playbackIndex"), playbackIndex);
            obj.insert(QStringLiteral("running"), running);
            obj.insert(QStringLiteral("paused"), paused);
            // Side fader seed (vc.cueList.sideFaderChanged): level plus the crossfade bookkeeping
            // VCCueListItem.qml's two step labels show.
            obj.insert(QStringLiteral("sideFaderLevel"), cl->sideFaderLevel());
            obj.insert(QStringLiteral("nextStepIndex"), cl->nextStepIndex());
            obj.insert(QStringLiteral("primaryTop"), cl->primaryTop());
        }
        break;
        case VCWidget::XYPadWidget:
        {
            QPointF pos = qobject_cast<VCXYPad *>(w)->currentPosition();
            obj.insert(QStringLiteral("x"), xyPadPosToNormalized(pos.x()));
            obj.insert(QStringLiteral("y"), xyPadPosToNormalized(pos.y()));
        }
        break;
        case VCWidget::SpeedWidget:
        {
            VCSpeedDial *sd = qobject_cast<VCSpeedDial *>(w);
            obj.insert(QStringLiteral("ms"), int(sd->currentTime()));
            // Seeds for vc.speedDial.factorChanged / tapChanged.
            obj.insert(QStringLiteral("factor"), ApiVcConfig::speedDialMultiplierName(int(sd->currentFactor())));
            obj.insert(QStringLiteral("tapTimeValue"), sd->tapTimeValue());
        }
        break;
        case VCWidget::FrameWidget:
        case VCWidget::SoloFrameWidget:
        {
            VCFrame *f = qobject_cast<VCFrame *>(w);
            obj.insert(QStringLiteral("currentPage"), f->currentPage());
            obj.insert(QStringLiteral("pages"), f->totalPagesNumber());
            obj.insert(QStringLiteral("multipage"), f->multiPageMode());
        }
        break;
        default:
        break;
    }
    // XYPad floor / active preset, Clock, Animation, AudioTriggers seeds (app_apivcconfig_live.cpp).
    ApiVcConfig::appendLiveSeed(w, obj);
}

} // namespace

VCWidget *App::vcFindWidget(quint32 id) const
{
    if (m_virtualConsole == nullptr)
        return nullptr;

    VCWidget *w = m_virtualConsole->widget(id);
    // VCPage is itself a VCWidget (it derives from VCFrame) and is registered in the same
    // VirtualConsole::m_widgetsMap as every other widget (see VirtualConsole::addPage()) - but a
    // page is not a "widget" in vc.widget.* API terms (it IS a "page" in vc.page.* terms), so hide
    // it here at the single lookup choke point every ApiVcHost widget method above goes through.
    if (w != nullptr && qobject_cast<VCPage *>(w) != nullptr)
        return nullptr;
    return w;
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
    {
        QVariantMap m = v.toMap();
        // VirtualConsole::widgetsList() returns a single {"label": "<None>"} placeholder (no "id"
        // key) when the VC has no widgets at all - skip it rather than parsing a phantom id 0.
        if (m.contains(QStringLiteral("id")) == false)
            continue;

        VCWidget *w = m.value(QStringLiteral("classRef")).value<VCWidget *>();
        if (w != nullptr && qobject_cast<VCPage *>(w) != nullptr)
            continue; // pages are containers, not "widgets" in API terms - see vcFindWidget()

        ids.append(m.value(QStringLiteral("id")).toUInt());
    }
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
    appendLiveStateToJson(w, obj);
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

    // Button/Slider live in this file; every other type in app_apivcconfig_{layout,live}.cpp
    switch (w->type())
    {
        case VCWidget::ButtonWidget:        return applyButtonConfig(qobject_cast<VCButton *>(w), configPatch, error);
        case VCWidget::SliderWidget:        return applySliderConfig(qobject_cast<VCSlider *>(w), m_doc, configPatch, error);
        case VCWidget::FrameWidget:
        case VCWidget::SoloFrameWidget:     return ApiVcConfig::applyFrameConfig(w, configPatch, error);
        case VCWidget::LabelWidget:         return ApiVcConfig::applyLabelConfig(w, configPatch, error);
        case VCWidget::CueListWidget:       return ApiVcConfig::applyCueListConfig(w, m_doc, configPatch, error);
        case VCWidget::SpeedWidget:         return ApiVcConfig::applySpeedDialConfig(w, m_doc, configPatch, error);
        case VCWidget::XYPadWidget:         return ApiVcConfig::applyXyPadConfig(w, configPatch, error);
        case VCWidget::ClockWidget:         return ApiVcConfig::applyClockConfig(w, configPatch, error);
        case VCWidget::AnimationWidget:     return ApiVcConfig::applyAnimationConfig(w, configPatch, error);
        case VCWidget::AudioTriggersWidget: return ApiVcConfig::applyAudioTriggersConfig(w, configPatch, error);
        default: break;
    }

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

/*****************************************************************************
 * Widgets - live interaction
 *****************************************************************************/

void App::vcSetLiveListener(ApiVcLiveListener *listener)
{
    m_vcLiveListener = listener;
}

void App::slotVcWidgetRegistered(VCWidget *widget)
{
    // Pages are VCFrames registered in the same map, but they are not widgets in API terms (see
    // vcFindWidget()) - their currentPageChanged would otherwise surface as a bogus vc.frame.pageChanged.
    if (widget == nullptr || qobject_cast<VCPage *>(widget) != nullptr)
        return;

    // A widget can enter the map more than once over its lifetime (VirtualConsole::moveWidget()
    // removes it and re-adds it, id clashes re-register it) and Qt::UniqueConnection is not available
    // for the functor connections below, so drop whatever relay this App already holds on it first.
    // App has no other connections from VC widgets - this is exactly the relay set below.
    disconnect(widget, nullptr, this, nullptr);

    // Pointer-to-member connects: these are qmlui classes living in this same binary (not the engine
    // DLL, where only the string-based form is reliable on this MinGW build - see CLAUDE.md), so the
    // compile-time checked form is safe here and preferable, since a silently mistyped SIGNAL() string
    // could only be caught by running the GUI.
    //
    // Each relay captures a QPointer to the widget instead of reading sender() in a member slot. Not
    // every one of these signals is emitted on the GUI thread: VCSlider::valueChanged fires from
    // writeDMXLevel()'s monitor feedback on the MasterTimer thread, which makes this connection a
    // queued one - and for a queued call Qt hands the slot a raw sender pointer that is already
    // dangling if the widget was deleted (widget/page delete, project close) between emission and
    // delivery. The QPointer is cleared by the widget's destruction, so such a late delivery is
    // simply dropped. `this` as the context object keeps every relay on the GUI thread regardless of
    // where the signal came from, which is what ApiVcLiveListener's contract promises.
    switch (widget->type())
    {
        case VCWidget::ButtonWidget:
        {
            QPointer<VCButton> b(qobject_cast<VCButton *>(widget));
            connect(b.data(), &VCButton::stateChanged, this, [this, b](int state)
            {
                if (b.isNull() || m_vcLiveListener == nullptr)
                    return;
                m_vcLiveListener->vcButtonStateChanged(b->id(), buttonStateString(state));
            });
        }
        break;
        case VCWidget::SliderWidget:
        {
            QPointer<VCSlider> s(qobject_cast<VCSlider *>(widget));
            connect(s.data(), &VCSlider::valueChanged, this, [this, s](int value)
            {
                if (s.isNull() || m_vcLiveListener == nullptr)
                    return;
                m_vcLiveListener->vcSliderValueChanged(s->id(), value);
            });
        }
        break;
        case VCWidget::CueListWidget:
        {
            QPointer<VCCueList> cl(qobject_cast<VCCueList *>(widget));
            auto relay = [this, cl]()
            {
                if (cl.isNull() || m_vcLiveListener == nullptr)
                    return;
                int playbackIndex = -1;
                bool running = false, paused = false;
                cueListPlaybackState(cl.data(), playbackIndex, running, paused);
                m_vcLiveListener->vcCueListPlaybackChanged(cl->id(), playbackIndex, running, paused);
            };
            // Both feed one event: the status signal covers play/pause/stop, the index signal covers
            // the Chaser advancing on its own (slotCurrentStepChanged -> setPlaybackIndex).
            connect(cl.data(), &VCCueList::playbackStatusChanged, this, relay);
            connect(cl.data(), &VCCueList::playbackIndexChanged, this, relay);

            // Side fader: the level itself plus the crossfade bookkeeping (which step the fader's
            // other end points at, and which end the current step sits on) all feed one event, since
            // the on-screen labels need all three together. nextStepIndex also changes on every
            // playback step, so a running crossfade list emits this alongside playbackChanged.
            auto sideFaderRelay = [this, cl]()
            {
                if (cl.isNull() || m_vcLiveListener == nullptr)
                    return;
                m_vcLiveListener->vcCueListSideFaderChanged(cl->id(), cl->sideFaderLevel(), cl->nextStepIndex(), cl->primaryTop());
            };
            connect(cl.data(), &VCCueList::sideFaderLevelChanged, this, sideFaderRelay);
            connect(cl.data(), &VCCueList::nextStepIndexChanged, this, sideFaderRelay);
            connect(cl.data(), &VCCueList::primaryTopChanged, this, sideFaderRelay);
        }
        break;
        case VCWidget::XYPadWidget:
        {
            QPointer<VCXYPad> pad(qobject_cast<VCXYPad *>(widget));
            connect(pad.data(), &VCXYPad::currentPositionChanged, this, [this, pad]()
            {
                if (pad.isNull() || m_vcLiveListener == nullptr)
                    return;
                QPointF pos = pad->currentPosition();
                m_vcLiveListener->vcXyPadPositionChanged(pad->id(), xyPadPosToNormalized(pos.x()),
                                                         xyPadPosToNormalized(pos.y()));
            });
        }
        break;
        case VCWidget::SpeedWidget:
        {
            QPointer<VCSpeedDial> sd(qobject_cast<VCSpeedDial *>(widget));
            connect(sd.data(), &VCSpeedDial::currentTimeChanged, this, [this, sd]()
            {
                if (sd.isNull() || m_vcLiveListener == nullptr)
                    return;
                m_vcLiveListener->vcSpeedDialValueChanged(sd->id(), int(sd->currentTime()));
            });
            // Multiplier factor (1/16..16 buttons, +/-, external input) and tap-tempo state.
            connect(sd.data(), &VCSpeedDial::currentFactorChanged, this, [this, sd]()
            {
                if (sd.isNull() || m_vcLiveListener == nullptr)
                    return;
                m_vcLiveListener->vcSpeedDialFactorChanged(sd->id(), ApiVcConfig::speedDialMultiplierName(int(sd->currentFactor())));
            });
            connect(sd.data(), &VCSpeedDial::tapTimeValueChanged, this, [this, sd]()
            {
                if (sd.isNull() || m_vcLiveListener == nullptr)
                    return;
                m_vcLiveListener->vcSpeedDialTapChanged(sd->id(), sd->tapTimeValue(), int(sd->currentTime()));
            });
        }
        break;
        case VCWidget::FrameWidget:
        case VCWidget::SoloFrameWidget:
        {
            QPointer<VCFrame> f(qobject_cast<VCFrame *>(widget));
            connect(f.data(), &VCFrame::currentPageChanged, this, [this, f](int page)
            {
                if (f.isNull() || m_vcLiveListener == nullptr)
                    return;
                m_vcLiveListener->vcFramePageChanged(f->id(), page);
            });
        }
        break;
        default:
        break;
    }
    // XYPad floor / active preset, Clock, Animation, AudioTriggers relays (app_apivchost_live.cpp).
    vcConnectLiveRelaysExt(widget);
}

bool App::vcButtonPress(quint32 id, bool pressed, QString *error)
{
    VCButton *b = qobject_cast<VCButton *>(vcFindWidget(id));
    if (b == nullptr)
    {
        if (error) *error = QStringLiteral("No such widget");
        return false;
    }

    // Same calls VCButtonItem.qml makes, with the contract's edge semantics: Toggle/Blackout/StopAll on
    // the down-edge only, Flash on both. requestStateChange()'s $pressed argument is NOT "toggle when
    // true" - for Toggle it is ignored (the engine flips based on its own state), for Blackout it is
    // the target state (setState(pressed ? Active : Inactive)) - so pass the button's new target state
    // exactly like the QML does, never the raw pointer state.
    switch (b->actionType())
    {
        case VCButton::Flash:
            if (m_doc->function(b->functionID()) == nullptr)
            {
                if (error) *error = QStringLiteral("No function attached to button");
                return false;
            }
            b->requestStateChange(pressed);
        break;
        case VCButton::Toggle:
            if (pressed == false)
                return true;
            if (m_doc->function(b->functionID()) == nullptr)
            {
                if (error) *error = QStringLiteral("No function attached to button");
                return false;
            }
            b->requestStateChange(b->state() == VCButton::Active ? false : true);
        break;
        case VCButton::Blackout:
            if (pressed == false)
                return true;
            b->requestStateChange(b->state() == VCButton::Active ? false : true);
        break;
        case VCButton::StopAll:
            if (pressed == false)
                return true;
            b->requestStateChange(true);
        break;
        default:
        break;
    }
    return true;
}

bool App::vcSliderSetValue(quint32 id, int value, QString *error)
{
    VCSlider *s = qobject_cast<VCSlider *>(vcFindWidget(id));
    if (s == nullptr)
    {
        if (error) *error = QStringLiteral("No such widget");
        return false;
    }

    // VCSlider::setValue() itself doesn't clamp; the on-screen fader/knob is physically confined to
    // [rangeLowLimit, rangeHighLimit] (VCSliderItem.qml's from/to), so confine the remote value too.
    int lo = qRound(s->rangeLowLimit());
    int hi = qRound(s->rangeHighLimit());
    value = qBound(qMin(lo, hi), value, qMax(lo, hi));

    s->setValue(value); // setDMX=true, updateFeedback=true - identical to `sliderObj.value = v` from QML
    return true;
}

bool App::vcCueListAction(quint32 id, CueListAction action, QString *error)
{
    VCCueList *cl = qobject_cast<VCCueList *>(vcFindWidget(id));
    if (cl == nullptr)
    {
        if (error) *error = QStringLiteral("No such widget");
        return false;
    }
    if (cl->chaser() == nullptr)
    {
        // Every *Clicked() below silently returns in this case - say so instead.
        if (error) *error = QStringLiteral("No Chaser attached to cue list");
        return false;
    }

    switch (action)
    {
        case CueListPlay: cl->playClicked(); break;
        case CueListStop: cl->stopClicked(); break;
        case CueListNext: cl->nextClicked(); break;
        case CueListPrevious: cl->previousClicked(); break;
    }
    return true;
}

bool App::vcCueListSetPlaybackIndex(quint32 id, int index, QString *error)
{
    VCCueList *cl = qobject_cast<VCCueList *>(vcFindWidget(id));
    if (cl == nullptr)
    {
        if (error) *error = QStringLiteral("No such widget");
        return false;
    }
    if (cl->chaser() == nullptr)
    {
        if (error) *error = QStringLiteral("No Chaser attached to cue list");
        return false;
    }

    // VCCueListItem.qml: selecting a row sets playbackIndex, Enter then calls playCurrentStep(). The
    // API folds both into one "jump to step" for index >= 0; -1 only clears the selection.
    cl->setPlaybackIndex(index);
    if (index >= 0)
        cl->playCurrentStep();
    return true;
}

QJsonObject App::vcCueListSnapshot(quint32 id) const
{
    VCCueList *cl = qobject_cast<VCCueList *>(vcFindWidget(id));
    if (cl == nullptr)
        return QJsonObject();

    int playbackIndex = -1;
    bool running = false, paused = false;
    cueListPlaybackState(cl, playbackIndex, running, paused);

    QJsonObject obj;
    obj.insert(QStringLiteral("steps"), cueListStepsToJson(m_doc, cl->chaser()));
    obj.insert(QStringLiteral("playbackIndex"), playbackIndex);
    obj.insert(QStringLiteral("running"), running);
    obj.insert(QStringLiteral("paused"), paused);
    return obj;
}

bool App::vcXyPadSetPosition(quint32 id, double x, double y, QString *error)
{
    VCXYPad *pad = qobject_cast<VCXYPad *>(vcFindWidget(id));
    if (pad == nullptr)
    {
        if (error) *error = QStringLiteral("No such widget");
        return false;
    }
    pad->setCurrentPosition(QPointF(normalizedToXyPadPos(x), normalizedToXyPadPos(y)));
    return true;
}

bool App::vcSpeedDialSetValue(quint32 id, int ms, QString *error)
{
    VCSpeedDial *sd = qobject_cast<VCSpeedDial *>(vcFindWidget(id));
    if (sd == nullptr)
    {
        if (error) *error = QStringLiteral("No such widget");
        return false;
    }
    sd->setCurrentTime(uint(qMax(0, ms)));
    return true;
}

bool App::vcSpeedDialTap(quint32 id, QString *error)
{
    VCSpeedDial *sd = qobject_cast<VCSpeedDial *>(vcFindWidget(id));
    if (sd == nullptr)
    {
        if (error) *error = QStringLiteral("No such widget");
        return false;
    }
    sd->tap();
    return true;
}

bool App::vcFrameGotoPage(quint32 id, int page, QString *error)
{
    VCFrame *f = qobject_cast<VCFrame *>(vcFindWidget(id));
    if (f == nullptr)
    {
        if (error) *error = QStringLiteral("No such widget");
        return false;
    }

    int pages = f->totalPagesNumber();
    if (page < 0 || page >= pages)
    {
        if (error) *error = QStringLiteral("page must be 0..%1").arg(pages - 1);
        return false;
    }
    // VCFrame::setCurrentPage() has no same-page early return: it would re-show every child, call
    // setDocModified() (bumping docRevision) and emit currentPageChanged() for a no-op request.
    if (page == f->currentPage())
        return true;

    f->gotoPage(page);
    return true;
}

QJsonObject App::vcFrameSnapshot(quint32 id) const
{
    VCFrame *f = qobject_cast<VCFrame *>(vcFindWidget(id));
    if (f == nullptr)
        return QJsonObject();

    QJsonObject obj;
    obj.insert(QStringLiteral("pages"), f->totalPagesNumber());
    obj.insert(QStringLiteral("currentPage"), f->currentPage());
    obj.insert(QStringLiteral("multipage"), f->multiPageMode());
    return obj;
}

/*****************************************************************************
 * Widgets - layout / configuration slice (ApiVcLayoutDomain)
 *****************************************************************************/

bool App::vcFrameSetPin(quint32 id, const QString &currentPin, const QString &newPin)
{
    VCFrame *f = qobject_cast<VCFrame *>(vcFindWidget(id));
    if (f == nullptr)
        return false;

    // Same check VirtualConsole::setPagePIN() applies to a page: an existing PIN must be
    // confirmed before it can be changed or cleared. Digits only (validated by the domain), so
    // toInt() is the engine's own int representation of the PIN.
    if (f->PIN() != 0)
    {
        bool ok = false;
        int current = currentPin.toInt(&ok);
        if (ok == false || current != f->PIN())
            return false;
    }

    f->setPIN(newPin.isEmpty() ? 0 : newPin.toInt());
    return true;
}

bool App::vcFrameValidatePin(quint32 id, const QString &pin) const
{
    VCFrame *f = qobject_cast<VCFrame *>(vcFindWidget(id));
    if (f == nullptr)
        return false;
    // Stateless, like vcValidatePagePin(): VCFrame::validatePIN() would unlock the frame for every
    // client at once, the spec promises a per-session unlock (kept client-side).
    return f->PIN() == 0 || f->PIN() == pin.toInt();
}

bool App::vcFrameCloneFirstPage(quint32 id, QJsonArray &createdIds, QString *error)
{
    VCFrame *f = qobject_cast<VCFrame *>(vcFindWidget(id));
    if (f == nullptr)
    {
        if (error) *error = QStringLiteral("No such widget");
        return false;
    }
    if (f->totalPagesNumber() < 2)
    {
        // VCFrame::cloneFirstPage() silently returns in this case - say so instead.
        if (error) *error = QStringLiteral("Frame has a single page - nothing to clone onto");
        return false;
    }

    QList<quint32> beforeList = vcWidgetIds();
    QSet<quint32> before(beforeList.begin(), beforeList.end());
    f->cloneFirstPage();
    QList<quint32> after = vcWidgetIds();
    std::sort(after.begin(), after.end());
    for (quint32 wid : after)
    {
        if (before.contains(wid) == false)
            createdIds.append(QString::number(wid));
    }
    return true;
}

bool App::vcSliderSetLevelChannels(quint32 id, const QList<QPair<quint32, quint32> > &channels, QString *error)
{
    VCSlider *s = qobject_cast<VCSlider *>(vcFindWidget(id));
    if (s == nullptr)
    {
        if (error) *error = QStringLiteral("No such widget");
        return false;
    }
    replaceLevelChannels(s, channels);
    return true;
}

bool App::vcSliderFlash(quint32 id, bool on, QString *error)
{
    VCSlider *s = qobject_cast<VCSlider *>(vcFindWidget(id));
    if (s == nullptr)
    {
        if (error) *error = QStringLiteral("No such widget");
        return false;
    }
    // VCSliderItem.qml only shows the flash button in Adjust mode with adjustFlashEnabled; the
    // engine's flashFunction() itself silently returns without a controlled Function.
    if (s->sliderMode() != VCSlider::Adjust || s->adjustFlashEnabled() == false)
    {
        if (error) *error = QStringLiteral("Slider is not in Adjust mode with the flash button enabled");
        return false;
    }
    if (m_doc->function(s->controlledFunction()) == nullptr)
    {
        if (error) *error = QStringLiteral("No function controlled by slider");
        return false;
    }
    s->flashFunction(on);
    return true;
}

// Shared tail of the two bulk creators: resolve the target container exactly like vcCreateWidget(),
// run $create, and report every widget id that appeared in the meantime (VCFrame::addFunctions()/
// addWidgetMatrix() return void, so the before/after difference is the only way to learn the ids).
template <typename Creator>
static QList<quint32> vcBulkCreate(App *app, VirtualConsole *vc, int page, quint32 parentId, VCWidget *parentWidget,
                                    QString *error, Creator create)
{
    VCFrame *targetFrame = nullptr;
    if (parentId == ApiVcHost::InvalidWidgetId)
        targetFrame = vc->page(page);
    else
        targetFrame = qobject_cast<VCFrame *>(parentWidget);

    if (targetFrame == nullptr)
    {
        if (error) *error = QStringLiteral("Unable to resolve target container");
        return QList<quint32>();
    }

    QList<quint32> beforeList = app->vcWidgetIds();
    QSet<quint32> before(beforeList.begin(), beforeList.end());

    create(targetFrame, targetFrame->renderItem());

    QList<quint32> after = app->vcWidgetIds();
    std::sort(after.begin(), after.end());
    QList<quint32> created;
    for (quint32 wid : after)
    {
        if (before.contains(wid) == false)
            created.append(wid);
    }
    return created;
}

QList<quint32> App::vcCreateWidgetsFromFunctions(int page, quint32 parentId, const QList<quint32> &functionIds,
                                                 QPointF position, const QString &widgetHint, QString *error)
{
    // VCFrame::addFunctions() picks the widget type from the drag's keyboard modifiers
    // (VCFrameItem.qml's drop handler): Shift = Adjust slider, Ctrl = cue list, none = button.
    int modifiers = Qt::NoModifier;
    if (widgetHint == QStringLiteral("adjustSlider"))
        modifiers = Qt::ShiftModifier;
    else if (widgetHint == QStringLiteral("cueList"))
        modifiers = Qt::ControlModifier;

    QVariantList ids;
    for (quint32 fid : functionIds)
        ids.append(QVariant(fid));

    QList<quint32> created = vcBulkCreate(this, m_virtualConsole, page, parentId, vcFindWidget(parentId), error,
        [&](VCFrame *frame, QQuickItem *parentItem)
        {
            frame->addFunctions(parentItem, ids, position.toPoint(), modifiers);
        });
    if (created.isEmpty() && error && error->isEmpty())
        *error = QStringLiteral("No widget was created");
    return created;
}

QList<quint32> App::vcCreateWidgetMatrix(int page, quint32 parentId, const QString &matrixType, QPointF position,
                                         int columns, int rows, int widgetWidth, int widgetHeight,
                                         bool soloFrame, QString *error)
{
    // VCFrame::addWidgetMatrix() only knows the literal "buttonmatrix" (VirtualConsole.qml's
    // PopupCreateMatrix passes it for buttons) - anything else means sliders.
    QString engineType = matrixType == QStringLiteral("Button") ? QStringLiteral("buttonmatrix") : QStringLiteral("slidermatrix");

    QList<quint32> created = vcBulkCreate(this, m_virtualConsole, page, parentId, vcFindWidget(parentId), error,
        [&](VCFrame *frame, QQuickItem *parentItem)
        {
            frame->addWidgetMatrix(parentItem, engineType, position.toPoint(), QSize(columns, rows),
                                   QSize(widgetWidth, widgetHeight), soloFrame);
        });
    if (created.isEmpty() && error && error->isEmpty())
        *error = QStringLiteral("No widget was created");
    return created;
}

QList<quint32> App::vcWidgetsUsingFunction(quint32 functionId) const
{
    QList<quint32> ids;
    for (const QVariant &v : m_virtualConsole->usageList(functionId))
    {
        QVariantMap m = v.toMap();
        // usageList() appends a single {"label": "<None>"} placeholder (no classRef) when nothing
        // references the Function - skip it.
        VCWidget *w = m.value(QStringLiteral("classRef")).value<VCWidget *>();
        if (w == nullptr || qobject_cast<VCPage *>(w) != nullptr)
            continue;
        ids.append(w->id());
    }
    std::sort(ids.begin(), ids.end());
    return ids;
}
