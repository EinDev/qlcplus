/*
  Q Light Controller Plus
  app_apivcconfig_live.cpp

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

/*
 * vc.widget.setConfig / typeConfig for XYPad, Clock, Animation and AudioTriggers, the XYPad /
 * Animation preset shaping behind vc.widget.preset.* and the live-state seeds of those four widget
 * types - see app_apivcconfig.h for the contract and docs/api-spec/fragments/virtualconsole.yaml
 * for the VcXyPadConfig / VcClockConfig / VcAnimationConfig / VcAudioTriggersConfig schemas.
 *
 * Every typeConfig mirrors the matching QML property panel (VCXYPadProperties.qml, VCClockProperties.
 * qml, VCAnimationProperties.qml, VCAudioTriggersProperties.qml) field by field, and additionally
 * carries the sub-resource lists a client has to render (`fixtures` + `presets` of a pad, `schedules`
 * of a clock, `presets` of an animation, `bars` of the audio triggers) read-only: they are edited
 * through their own methods (vc.xyPad.fixture.*, vc.widget.preset.*, vc.clock.schedule.*,
 * vc.audioTriggers.setBarConfig - ApiVcLiveDomain / ApiVcDomain) and a setConfig patch naming them is
 * refused.
 */

#include <QColor>
#include <QJsonArray>
#include <QJsonValue>
#include <QModelIndex>
#include <QSet>
#include <QVariantList>
#include <QVariantMap>
#include <QVector3D>

#include "app_apivcconfig.h"
#include "doc.h"
#include "fixture.h"
#include "fixturegroup.h"
#include "function.h"
#include "listmodel.h"
#include "rgbmatrix.h"
#include "scenevalue.h"
#include "virtualconsole/vcwidget.h"
#include "virtualconsole/vcxypad.h"
#include "virtualconsole/vcxypadpreset.h"
#include "virtualconsole/vcclock.h"
#include "virtualconsole/vcanimation.h"
#include "virtualconsole/vcanimationpreset.h"
#include "virtualconsole/vcaudiotriggers.h"

namespace
{
const QString kInvalidId = QStringLiteral("4294967295");

bool boolField(const QJsonObject &patch, const QString &key, bool &out, QString *error)
{
    if (patch.contains(key) == false)
        return true;
    QJsonValue v = patch.value(key);
    if (v.isBool() == false)
    {
        if (error) *error = QStringLiteral("%1 must be a boolean").arg(key);
        return false;
    }
    out = v.toBool();
    return true;
}

/** Whole number within [lo, hi]. */
bool intField(const QJsonObject &patch, const QString &key, int lo, int hi, int &out, QString *error)
{
    if (patch.contains(key) == false)
        return true;
    QJsonValue v = patch.value(key);
    if (v.isDouble() == false || v.toDouble() != double(int(v.toDouble())) || v.toDouble() < lo || v.toDouble() > hi)
    {
        if (error) *error = QStringLiteral("%1 must be an integer %2..%3").arg(key).arg(lo).arg(hi);
        return false;
    }
    out = v.toInt();
    return true;
}

/** Rejects any key of $patch outside $allowed / $readOnly (the latter with a pointer to the method
 *  that edits it). */
bool checkKeys(const QJsonObject &patch, const QSet<QString> &allowed, const QHash<QString, QString> &readOnly,
               const QString &schema, QString *error)
{
    for (auto it = patch.constBegin(); it != patch.constEnd(); ++it)
    {
        if (allowed.contains(it.key()))
            continue;
        if (readOnly.contains(it.key()))
        {
            if (error) *error = QStringLiteral("%1 is read-only - use %2").arg(it.key(), readOnly.value(it.key()));
            return false;
        }
        if (error) *error = QStringLiteral("Unknown %1 key '%2'").arg(schema, it.key());
        return false;
    }
    return true;
}

QString idString(quint32 id)
{
    return QString::number(id);
}

QJsonObject rangeToJson(const QPointF &range)
{
    QJsonObject o;
    o.insert(QStringLiteral("min"), range.x());
    o.insert(QStringLiteral("max"), range.y());
    return o;
}

QJsonObject vectorToJson(const QVector3D &v)
{
    QJsonObject o;
    o.insert(QStringLiteral("x"), double(v.x()));
    o.insert(QStringLiteral("y"), double(v.y()));
    o.insert(QStringLiteral("z"), double(v.z()));
    return o;
}

/*****************************************************************************
 * XY Pad
 *****************************************************************************/

QString xyPadDisplayModeName(VCXYPad::DisplayMode mode)
{
    switch (mode)
    {
        case VCXYPad::Degrees: return QStringLiteral("Degrees");
        case VCXYPad::DMX:     return QStringLiteral("DMX");
        case VCXYPad::Percentage:
        default:               return QStringLiteral("Percentage");
    }
}

int xyPadDisplayModeFromName(const QString &name)
{
    if (name == QStringLiteral("Percentage")) return VCXYPad::Percentage;
    if (name == QStringLiteral("Degrees"))    return VCXYPad::Degrees;
    if (name == QStringLiteral("DMX"))        return VCXYPad::DMX;
    return -1;
}

/** VCXYPad::m_fixtures is private; the pad's ListModel (fixtureList()) is the public view of it, one
 *  row per entry with roles name/fxID/head/groupID/isGroup/xRange/yRange - and headsRangeInfo() gives
 *  the numeric Pan/Tilt range of a row in the current display units. */
QJsonArray xyPadFixturesToJson(VCXYPad *pad)
{
    QJsonArray arr;
    ListModel *list = pad->fixtureList().value<ListModel *>();
    if (list == nullptr)
        return arr;

    for (int row = 0; row < list->rowCount(); row++)
    {
        QModelIndex idx = list->index(row, 0);
        QJsonObject entry;
        bool isGroup = list->data(idx, QStringLiteral("isGroup")).toBool();
        if (isGroup)
            entry.insert(QStringLiteral("fixtureGroupId"), idString(list->data(idx, QStringLiteral("groupID")).toUInt()));
        else
        {
            entry.insert(QStringLiteral("fixtureId"), idString(list->data(idx, QStringLiteral("fxID")).toUInt()));
            entry.insert(QStringLiteral("headIndex"), list->data(idx, QStringLiteral("head")).toInt());
        }
        entry.insert(QStringLiteral("name"), list->data(idx, QStringLiteral("name")).toString());
        // VCXYPad::XYPadFixture::m_enabled is never cleared by the qmlui widget (loaded as true,
        // no UI toggles it) - reported for the schema's sake.
        entry.insert(QStringLiteral("enabled"), true);

        QVariantMap info = pad->headsRangeInfo(QVariantList() << row);
        entry.insert(QStringLiteral("units"), info.value(QStringLiteral("units")).toString());
        QJsonObject xRange, yRange;
        xRange.insert(QStringLiteral("min"), info.value(QStringLiteral("xMin")).toInt());
        xRange.insert(QStringLiteral("max"), info.value(QStringLiteral("xMax")).toInt());
        xRange.insert(QStringLiteral("reverse"), info.value(QStringLiteral("xReverse")).toBool());
        xRange.insert(QStringLiteral("maxValue"), info.value(QStringLiteral("xMaxValue")).toInt());
        yRange.insert(QStringLiteral("min"), info.value(QStringLiteral("yMin")).toInt());
        yRange.insert(QStringLiteral("max"), info.value(QStringLiteral("yMax")).toInt());
        yRange.insert(QStringLiteral("reverse"), info.value(QStringLiteral("yReverse")).toBool());
        yRange.insert(QStringLiteral("maxValue"), info.value(QStringLiteral("yMaxValue")).toInt());
        entry.insert(QStringLiteral("xRange"), xRange);
        entry.insert(QStringLiteral("yRange"), yRange);
        // The strings the QML list shows ("0% - 100%", "540° - 0° (R)").
        entry.insert(QStringLiteral("xRangeLabel"), list->data(idx, QStringLiteral("xRange")).toString());
        entry.insert(QStringLiteral("yRangeLabel"), list->data(idx, QStringLiteral("yRange")).toString());
        arr.append(entry);
    }
    return arr;
}

/** Does the pad currently drive $head (directly or through a group)? Used to give the head
 *  presets' "not in the pad" refusal a precise message. */
int xyPadHeadsCount(VCXYPad *pad, const VCXYPadPreset *preset)
{
    Q_UNUSED(pad)
    if (preset->m_fxGroupID != FixtureGroup::invalidId())
    {
        FixtureGroup *group = pad->doc() != nullptr ? pad->doc()->fixtureGroup(preset->m_fxGroupID) : nullptr;
        return group != nullptr ? group->headList().count() : 0;
    }
    return preset->m_fxGroup.count();
}

QJsonObject xyPadPresetToJson(VCXYPad *pad, const VCXYPadPreset *preset)
{
    QJsonObject obj;
    obj.insert(QStringLiteral("presetId"), int(preset->m_id));
    obj.insert(QStringLiteral("name"), preset->m_name);
    obj.insert(QStringLiteral("active"), int(preset->m_id) == pad->activePresetId());
    obj.insert(QStringLiteral("color"), preset->color());
    switch (preset->m_type)
    {
        case VCXYPadPreset::Position:
        {
            obj.insert(QStringLiteral("presetType"), QStringLiteral("position"));
            QJsonObject pos;
            pos.insert(QStringLiteral("x"), preset->m_dmxPos.x());
            pos.insert(QStringLiteral("y"), preset->m_dmxPos.y());
            obj.insert(QStringLiteral("position"), pos);
        }
        break;
        case VCXYPadPreset::EFX:
        case VCXYPadPreset::Scene:
            obj.insert(QStringLiteral("presetType"), QStringLiteral("function"));
            obj.insert(QStringLiteral("functionID"), idString(preset->m_funcID));
            // Additive: which of the two Function kinds this is (the QML icon differs).
            obj.insert(QStringLiteral("functionType"), VCXYPadPreset::typeToString(preset->m_type));
        break;
        case VCXYPadPreset::FixtureGroup:
        default:
            if (preset->m_fxGroupID != FixtureGroup::invalidId())
            {
                obj.insert(QStringLiteral("presetType"), QStringLiteral("fixtureGroup"));
                obj.insert(QStringLiteral("fixtureGroupId"), idString(preset->m_fxGroupID));
            }
            else
            {
                obj.insert(QStringLiteral("presetType"), QStringLiteral("fixtureGroupHead"));
                QJsonArray heads;
                for (const GroupHead &head : preset->m_fxGroup)
                {
                    QJsonObject h;
                    h.insert(QStringLiteral("fixtureId"), idString(head.fxi));
                    h.insert(QStringLiteral("headIndex"), head.head);
                    heads.append(h);
                }
                obj.insert(QStringLiteral("heads"), heads);
                if (preset->m_fxGroup.isEmpty() == false)
                {
                    obj.insert(QStringLiteral("fixtureId"), idString(preset->m_fxGroup.first().fxi));
                    obj.insert(QStringLiteral("headIndex"), preset->m_fxGroup.first().head);
                }
            }
            obj.insert(QStringLiteral("headsCount"), xyPadHeadsCount(pad, preset));
        break;
    }
    return obj;
}

const VCXYPadPreset *xyPadFindPreset(VCXYPad *pad, int presetId)
{
    for (const VCXYPadPreset *preset : pad->presetObjects())
    {
        if (int(preset->m_id) == presetId)
            return preset;
    }
    return nullptr;
}

/*****************************************************************************
 * Clock
 *****************************************************************************/

QString clockTypeName(VCClock::ClockType type)
{
    switch (type)
    {
        case VCClock::Stopwatch: return QStringLiteral("Stopwatch");
        case VCClock::Countdown: return QStringLiteral("Countdown");
        case VCClock::Clock:
        default:                 return QStringLiteral("Clock");
    }
}

int clockTypeFromName(const QString &name)
{
    if (name == QStringLiteral("Clock"))     return VCClock::Clock;
    if (name == QStringLiteral("Stopwatch")) return VCClock::Stopwatch;
    if (name == QStringLiteral("Countdown")) return VCClock::Countdown;
    return -1;
}

QJsonArray clockSchedulesToJson(VCClock *clock)
{
    QJsonArray arr;
    int index = 0;
    for (VCClockSchedule *sch : clock->schedules())
    {
        QJsonObject obj;
        obj.insert(QStringLiteral("index"), index++);
        obj.insert(QStringLiteral("functionID"), idString(sch->functionID()));
        Function *f = clock->doc() != nullptr ? clock->doc()->function(sch->functionID()) : nullptr;
        obj.insert(QStringLiteral("functionName"), f != nullptr ? f->name() : QString());
        obj.insert(QStringLiteral("startTime"), sch->startTime());
        obj.insert(QStringLiteral("stopTime"), sch->stopTime());
        obj.insert(QStringLiteral("weekFlags"), sch->weekFlags());
        arr.append(obj);
    }
    return arr;
}

/*****************************************************************************
 * Animation
 *****************************************************************************/

// VcAnimationConfig.visibilityMask entries <-> VCAnimation::Visibility bits, in bit order.
const struct { const char *name; quint32 bit; } kAnimationVisibility[] = {
    { "Fader", VCAnimation::Fader }, { "Label", VCAnimation::Label }, { "PresetCombo", VCAnimation::PresetCombo },
    { "Color1", VCAnimation::Color1 }, { "Color2", VCAnimation::Color2 }, { "Color3", VCAnimation::Color3 },
    { "Color4", VCAnimation::Color4 }, { "Color5", VCAnimation::Color5 }
};

QJsonArray animationVisibilityToJson(quint32 mask)
{
    QJsonArray arr;
    for (const auto &v : kAnimationVisibility)
    {
        if (mask & v.bit)
            arr.append(QString::fromLatin1(v.name));
    }
    return arr;
}

bool animationVisibilityFromJson(const QJsonValue &value, quint32 &mask, QString *error)
{
    if (value.isArray() == false)
    {
        if (error) *error = QStringLiteral("visibilityMask must be an array of Fader/Label/PresetCombo/Color1..Color5");
        return false;
    }
    mask = 0;
    for (const QJsonValue &entry : value.toArray())
    {
        bool known = false;
        for (const auto &v : kAnimationVisibility)
        {
            if (entry.toString() == QString::fromLatin1(v.name))
            {
                mask |= v.bit;
                known = true;
                break;
            }
        }
        if (known == false)
        {
            if (error) *error = QStringLiteral("Unknown visibilityMask entry '%1'").arg(entry.toString());
            return false;
        }
    }
    return true;
}

QString colorString(const QColor &c)
{
    return c.isValid() ? c.name(QColor::HexRgb) : QString();
}

QString animationPresetTypeName(const VCAnimationPreset *preset)
{
    switch (preset->m_type)
    {
        case VCAnimationPreset::Color1: case VCAnimationPreset::Color2: case VCAnimationPreset::Color3:
        case VCAnimationPreset::Color4: case VCAnimationPreset::Color5:
            return QStringLiteral("color");
        case VCAnimationPreset::Color1Knob: case VCAnimationPreset::Color2Knob: case VCAnimationPreset::Color3Knob:
        case VCAnimationPreset::Color4Knob: case VCAnimationPreset::Color5Knob:
            return QStringLiteral("colorKnobs");
        case VCAnimationPreset::Color1Reset: case VCAnimationPreset::Color2Reset: case VCAnimationPreset::Color3Reset:
        case VCAnimationPreset::Color4Reset: case VCAnimationPreset::Color5Reset:
            return QStringLiteral("colorReset");
        case VCAnimationPreset::Text:
            return QStringLiteral("text");
        case VCAnimationPreset::Animation:
        default:
            return QStringLiteral("algorithm");
    }
}

QJsonObject animationPresetToJson(VCAnimation *anim, const VCAnimationPreset *preset)
{
    QJsonObject obj;
    obj.insert(QStringLiteral("presetId"), int(preset->m_id));
    QString type = animationPresetTypeName(preset);
    obj.insert(QStringLiteral("presetType"), type);
    obj.insert(QStringLiteral("active"), int(preset->m_id) == anim->activePresetId());
    bool isKnob = preset->widgetType() == VCAnimationPreset::Knob;
    obj.insert(QStringLiteral("isKnob"), isKnob);
    if (preset->colorIndex() >= 0)
        obj.insert(QStringLiteral("colorIndex"), preset->colorIndex());
    if (type == QStringLiteral("color"))
        obj.insert(QStringLiteral("color"), colorString(preset->m_color));
    if (isKnob)
    {
        // Additive: which RGB component this knob drives (the knob's tint on screen) and its live value.
        QString channel = preset->m_color == QColor(Qt::red) ? QStringLiteral("red")
                        : preset->m_color == QColor(Qt::green) ? QStringLiteral("green")
                        : preset->m_color == QColor(Qt::blue) ? QStringLiteral("blue") : QString();
        obj.insert(QStringLiteral("knobChannel"), channel);
        obj.insert(QStringLiteral("knobColor"), colorString(preset->m_color));
        obj.insert(QStringLiteral("knobValue"), anim->presetKnobValue(preset->m_id));
    }
    if (type == QStringLiteral("text"))
        obj.insert(QStringLiteral("text"), preset->m_resource);
    if (type == QStringLiteral("algorithm"))
    {
        obj.insert(QStringLiteral("algorithmName"), preset->m_resource);
        QJsonObject props;
        for (auto it = preset->m_properties.constBegin(); it != preset->m_properties.constEnd(); ++it)
            props.insert(it.key(), it.value());
        obj.insert(QStringLiteral("algorithmProperties"), props);
    }
    return obj;
}

const VCAnimationPreset *animationFindPreset(VCAnimation *anim, int presetId)
{
    for (const VCAnimationPreset *preset : anim->presetObjects())
    {
        if (int(preset->m_id) == presetId)
            return preset;
    }
    return nullptr;
}

/*****************************************************************************
 * Audio triggers
 *****************************************************************************/

QString barTypeName(int type)
{
    switch (type)
    {
        case VCAudioTriggers::DMXBar:      return QStringLiteral("DMXBar");
        case VCAudioTriggers::FunctionBar: return QStringLiteral("FunctionBar");
        case VCAudioTriggers::VCWidgetBar: return QStringLiteral("VCWidgetBar");
        case VCAudioTriggers::None:
        default:                           return QStringLiteral("None");
    }
}

QJsonArray audioBarsToJson(VCAudioTriggers *at)
{
    QJsonArray arr;
    QVariantList info = at->barsInfo();
    for (int i = 0; i < at->barsNumber(); i++)
    {
        const VCAudioTriggers::AudioBar *bar = at->barAt(i);
        if (bar == nullptr)
            break;
        QJsonObject obj;
        obj.insert(QStringLiteral("index"), i);
        obj.insert(QStringLiteral("type"), barTypeName(bar->m_type));
        obj.insert(QStringLiteral("minThreshold"), int(bar->m_minThreshold));
        obj.insert(QStringLiteral("maxThreshold"), int(bar->m_maxThreshold));
        // Additive: the frequency-band label VCAudioTriggersProperties.qml shows ("#3 (250Hz - 499Hz)").
        if (i < info.size())
            obj.insert(QStringLiteral("label"), info.at(i).toMap().value(QStringLiteral("bLabel")).toString());
        if (bar->m_type == VCAudioTriggers::FunctionBar)
        {
            obj.insert(QStringLiteral("functionId"), idString(bar->m_functionId));
            Function *f = at->doc() != nullptr ? at->doc()->function(bar->m_functionId) : nullptr;
            obj.insert(QStringLiteral("functionName"), f != nullptr ? f->name() : QString());
        }
        if (bar->m_type == VCAudioTriggers::VCWidgetBar)
        {
            obj.insert(QStringLiteral("triggeredWidgetId"), idString(bar->m_widgetId));
            if (i < info.size())
                obj.insert(QStringLiteral("triggeredWidgetCaption"), info.at(i).toMap().value(QStringLiteral("strVal")).toString());
        }
        if (bar->m_type == VCAudioTriggers::DMXBar)
        {
            QJsonArray channels;
            for (const SceneValue &scv : bar->m_dmxChannels)
            {
                QJsonObject c;
                c.insert(QStringLiteral("fixtureId"), idString(scv.fxi));
                c.insert(QStringLiteral("channel"), int(scv.channel));
                channels.append(c);
            }
            obj.insert(QStringLiteral("dmxChannels"), channels);
        }
        arr.append(obj);
    }
    return arr;
}

QJsonArray levelsToJson(const QVariantList &levels)
{
    QJsonArray arr;
    for (const QVariant &v : levels)
        arr.append(v.toInt());
    return arr;
}

} // namespace

namespace ApiVcConfig
{

/*****************************************************************************
 * XY Pad config
 *****************************************************************************/

QJsonObject xyPadConfigToJson(VCWidget *w)
{
    VCXYPad *pad = qobject_cast<VCXYPad *>(w);
    QJsonObject obj;
    if (pad == nullptr)
        return obj;

    obj.insert(QStringLiteral("invertedAppearance"), pad->invertedAppearance());
    obj.insert(QStringLiteral("displayMode"), xyPadDisplayModeName(pad->displayMode()));
    obj.insert(QStringLiteral("horizontalRange"), rangeToJson(pad->horizontalRange()));
    obj.insert(QStringLiteral("verticalRange"), rangeToJson(pad->verticalRange()));
    obj.insert(QStringLiteral("floorControl"), pad->floorControl());
    // Read-only additions: what the pad drives and offers, plus the floor geometry a client needs to
    // draw the stage grid (VCXYPadItem.qml) - see the schema notes.
    obj.insert(QStringLiteral("floorPosition"), vectorToJson(pad->floorPosition()));
    obj.insert(QStringLiteral("floorSize"), vectorToJson(pad->floorSize()));
    obj.insert(QStringLiteral("floorHeightMax"), double(pad->floorHeightMax()));
    obj.insert(QStringLiteral("floorHeightStep"), double(pad->floorHeightStep()));
    obj.insert(QStringLiteral("fixtures"), xyPadFixturesToJson(pad));
    obj.insert(QStringLiteral("presets"), xyPadPresetsToJson(w));
    obj.insert(QStringLiteral("activePresetId"), pad->activePresetId());
    return obj;
}

bool applyXyPadConfig(VCWidget *w, const QJsonObject &patch, QString *error)
{
    static const QSet<QString> keys = {
        QStringLiteral("invertedAppearance"), QStringLiteral("displayMode"), QStringLiteral("horizontalRange"),
        QStringLiteral("verticalRange"), QStringLiteral("floorControl")
    };
    static const QHash<QString, QString> readOnly = {
        { QStringLiteral("fixtures"), QStringLiteral("vc.xyPad.fixture.add/remove and vc.xyPad.setHeadsRange") },
        { QStringLiteral("presets"), QStringLiteral("vc.widget.preset.add/remove, vc.xyPad.preset.move/rename") },
        { QStringLiteral("activePresetId"), QStringLiteral("vc.widget.preset.apply") },
        { QStringLiteral("floorPosition"), QStringLiteral("vc.xyPad.setFloorPosition") },
        { QStringLiteral("floorSize"), QStringLiteral("the 3D environment settings") },
        { QStringLiteral("floorHeightMax"), QStringLiteral("(constant)") },
        { QStringLiteral("floorHeightStep"), QStringLiteral("(constant)") }
    };

    VCXYPad *pad = qobject_cast<VCXYPad *>(w);
    if (pad == nullptr)
    {
        if (error) *error = QStringLiteral("Widget is not an XYPad");
        return false;
    }
    if (checkKeys(patch, keys, readOnly, QStringLiteral("VcXyPadConfig"), error) == false)
        return false;

    bool inverted = pad->invertedAppearance(), floor = pad->floorControl();
    if (!boolField(patch, QStringLiteral("invertedAppearance"), inverted, error) ||
        !boolField(patch, QStringLiteral("floorControl"), floor, error))
        return false;

    int displayMode = int(pad->displayMode());
    if (patch.contains(QStringLiteral("displayMode")))
    {
        displayMode = xyPadDisplayModeFromName(patch.value(QStringLiteral("displayMode")).toString());
        if (displayMode < 0)
        {
            if (error) *error = QStringLiteral("displayMode must be Percentage, Degrees or DMX");
            return false;
        }
    }

    // Ranges: {min, max} in the pad's native 0..255.996 units, min < max (CustomRangeSlider's own rule).
    QPointF ranges[2] = { pad->horizontalRange(), pad->verticalRange() };
    const QString rangeKeys[2] = { QStringLiteral("horizontalRange"), QStringLiteral("verticalRange") };
    for (int i = 0; i < 2; i++)
    {
        if (patch.contains(rangeKeys[i]) == false)
            continue;
        QJsonObject r = patch.value(rangeKeys[i]).toObject();
        QJsonValue lo = r.value(QStringLiteral("min")), hi = r.value(QStringLiteral("max"));
        if (patch.value(rangeKeys[i]).isObject() == false || lo.isDouble() == false || hi.isDouble() == false ||
            lo.toDouble() < 0.0 || hi.toDouble() > 256.0 || lo.toDouble() >= hi.toDouble())
        {
            if (error) *error = QStringLiteral("%1 must be {min, max} with 0 <= min < max <= 256").arg(rangeKeys[i]);
            return false;
        }
        ranges[i] = QPointF(lo.toDouble(), hi.toDouble());
    }

    if (patch.contains(QStringLiteral("invertedAppearance")))
        pad->setInvertedAppearance(inverted);
    if (patch.contains(QStringLiteral("displayMode")))
        pad->setDisplayMode(VCXYPad::DisplayMode(displayMode));
    if (patch.contains(QStringLiteral("horizontalRange")))
        pad->setHorizontalRange(ranges[0]);
    if (patch.contains(QStringLiteral("verticalRange")))
        pad->setVerticalRange(ranges[1]);
    if (patch.contains(QStringLiteral("floorControl")))
        pad->setFloorControl(floor);
    return true;
}

/*****************************************************************************
 * Clock config
 *****************************************************************************/

QJsonObject clockConfigToJson(VCWidget *w)
{
    VCClock *clock = qobject_cast<VCClock *>(w);
    QJsonObject obj;
    if (clock == nullptr)
        return obj;

    obj.insert(QStringLiteral("clockType"), clockTypeName(clock->clockType()));
    // VCClock::targetTime() reports 0 unless the type is Countdown - same as the QML DayTimeTool sees.
    obj.insert(QStringLiteral("targetTime"), clock->targetTime());
    obj.insert(QStringLiteral("enableSchedule"), clock->enableSchedule());
    obj.insert(QStringLiteral("schedules"), clockSchedulesToJson(clock)); // read-only
    return obj;
}

bool applyClockConfig(VCWidget *w, const QJsonObject &patch, QString *error)
{
    static const QSet<QString> keys = {
        QStringLiteral("clockType"), QStringLiteral("targetTime"), QStringLiteral("enableSchedule")
    };
    static const QHash<QString, QString> readOnly = {
        { QStringLiteral("schedules"), QStringLiteral("vc.clock.schedule.add/update/remove") }
    };

    VCClock *clock = qobject_cast<VCClock *>(w);
    if (clock == nullptr)
    {
        if (error) *error = QStringLiteral("Widget is not a Clock");
        return false;
    }
    if (checkKeys(patch, keys, readOnly, QStringLiteral("VcClockConfig"), error) == false)
        return false;

    bool enable = clock->enableSchedule();
    if (!boolField(patch, QStringLiteral("enableSchedule"), enable, error))
        return false;
    int type = int(clock->clockType());
    if (patch.contains(QStringLiteral("clockType")))
    {
        type = clockTypeFromName(patch.value(QStringLiteral("clockType")).toString());
        if (type < 0)
        {
            if (error) *error = QStringLiteral("clockType must be Clock, Stopwatch or Countdown");
            return false;
        }
    }
    int target = clock->targetTime();
    // DayTimeTool.qml spans 00:00:00 .. 23:59:59 (in ms here).
    if (!intField(patch, QStringLiteral("targetTime"), 0, 86399999, target, error))
        return false;

    // Type first: setClockType() resets the runtime and, for Countdown, seeds it from the target.
    if (patch.contains(QStringLiteral("clockType")))
        clock->setClockType(VCClock::ClockType(type));
    if (patch.contains(QStringLiteral("targetTime")))
        clock->setTargetTime(target);
    if (patch.contains(QStringLiteral("enableSchedule")))
        clock->setEnableSchedule(enable);
    return true;
}

/*****************************************************************************
 * Animation config
 *****************************************************************************/

QStringList animationColorStrings(VCWidget *w)
{
    QStringList list;
    VCAnimation *anim = qobject_cast<VCAnimation *>(w);
    if (anim == nullptr)
        return list;
    for (int i = 0; i < RGBAlgorithmColorDisplayCount; i++)
        list.append(colorString(anim->colorAt(i)));
    return list;
}

QJsonObject animationConfigToJson(VCWidget *w)
{
    VCAnimation *anim = qobject_cast<VCAnimation *>(w);
    QJsonObject obj;
    if (anim == nullptr)
        return obj;

    obj.insert(QStringLiteral("visibilityMask"), animationVisibilityToJson(anim->visibilityMask()));
    obj.insert(QStringLiteral("functionID"), idString(anim->functionID()));
    obj.insert(QStringLiteral("instantChanges"), anim->instantChanges());
    obj.insert(QStringLiteral("algorithmIndex"), anim->algorithmIndex());
    obj.insert(QStringLiteral("colors"), QJsonArray::fromStringList(animationColorStrings(w)));
    // Read-only additions: how many colour slots the current algorithm uses (VCAnimationItem.qml's
    // swatch repeater), the algorithm names algorithmIndex indexes (RGBAlgorithm::algorithms(doc), the
    // same list as functions.rgbmatrix.listAlgorithms) and the presets.
    obj.insert(QStringLiteral("colorCount"), anim->colorCount());
    obj.insert(QStringLiteral("algorithms"), QJsonArray::fromStringList(anim->algorithms()));
    obj.insert(QStringLiteral("presets"), animationPresetsToJson(w));
    obj.insert(QStringLiteral("activePresetId"), anim->activePresetId());
    return obj;
}

bool applyAnimationConfig(VCWidget *w, const QJsonObject &patch, QString *error)
{
    static const QSet<QString> keys = {
        QStringLiteral("visibilityMask"), QStringLiteral("functionID"), QStringLiteral("instantChanges"),
        QStringLiteral("algorithmIndex"), QStringLiteral("colors")
    };
    static const QHash<QString, QString> readOnly = {
        { QStringLiteral("presets"), QStringLiteral("vc.widget.preset.add/remove, vc.animation.preset.move") },
        { QStringLiteral("activePresetId"), QStringLiteral("vc.widget.preset.apply") },
        { QStringLiteral("colorCount"), QStringLiteral("algorithmIndex (it follows the algorithm)") },
        { QStringLiteral("algorithms"), QStringLiteral("functions.rgbmatrix.listAlgorithms to read the list") }
    };

    VCAnimation *anim = qobject_cast<VCAnimation *>(w);
    if (anim == nullptr)
    {
        if (error) *error = QStringLiteral("Widget is not an Animation");
        return false;
    }
    if (checkKeys(patch, keys, readOnly, QStringLiteral("VcAnimationConfig"), error) == false)
        return false;

    bool instant = anim->instantChanges();
    if (!boolField(patch, QStringLiteral("instantChanges"), instant, error))
        return false;

    quint32 mask = anim->visibilityMask();
    if (patch.contains(QStringLiteral("visibilityMask")) &&
        animationVisibilityFromJson(patch.value(QStringLiteral("visibilityMask")), mask, error) == false)
        return false;

    // The controlled Function must be an RGB Matrix (or the "no function" sentinel): setFunctionID()
    // silently detaches on anything else.
    quint32 functionId = anim->functionID();
    if (patch.contains(QStringLiteral("functionID")))
    {
        QJsonValue v = patch.value(QStringLiteral("functionID"));
        bool ok = false;
        functionId = v.toString().toUInt(&ok);
        if (v.isString() == false || ok == false)
        {
            if (error) *error = QStringLiteral("functionID must be a function id string (\"%1\" to detach)").arg(kInvalidId);
            return false;
        }
        if (functionId != Function::invalidId())
        {
            Function *f = anim->doc() != nullptr ? anim->doc()->function(functionId) : nullptr;
            if (f == nullptr)
            {
                if (error) *error = QStringLiteral("No such function '%1'").arg(v.toString());
                return false;
            }
            if (f->type() != Function::RGBMatrixType)
            {
                if (error) *error = QStringLiteral("Function '%1' is a %2, an Animation widget controls an RGB Matrix")
                                        .arg(f->name(), Function::typeToString(f->type()));
                return false;
            }
        }
    }

    int algorithmIndex = anim->algorithmIndex();
    if (!intField(patch, QStringLiteral("algorithmIndex"), 0, qMax(0, anim->algorithms().count() - 1), algorithmIndex, error))
        return false;

    // colors: up to 5 entries, each a colour string or null / "" to clear the slot; a shorter array
    // leaves the remaining slots alone.
    QList<QPair<int, QColor> > colors;
    if (patch.contains(QStringLiteral("colors")))
    {
        QJsonValue v = patch.value(QStringLiteral("colors"));
        if (v.isArray() == false || v.toArray().size() > RGBAlgorithmColorDisplayCount)
        {
            if (error) *error = QStringLiteral("colors must be an array of up to %1 colour strings (null clears a slot)").arg(RGBAlgorithmColorDisplayCount);
            return false;
        }
        int slot = 0;
        for (const QJsonValue &entry : v.toArray())
        {
            QColor color;
            if (entry.isNull() == false && entry.toString().isEmpty() == false)
            {
                color = QColor(entry.toString());
                if (color.isValid() == false)
                {
                    if (error) *error = QStringLiteral("colors[%1] '%2' is not a colour").arg(slot).arg(entry.toString());
                    return false;
                }
            }
            colors.append(qMakePair(slot++, color));
        }
    }

    // Function first: setFunctionID() reloads colours and the algorithm index from the new matrix, so
    // colours / algorithm given in the same patch must land after it.
    if (patch.contains(QStringLiteral("functionID")))
        anim->setFunctionID(functionId);
    if (patch.contains(QStringLiteral("algorithmIndex")))
        anim->setAlgorithmIndex(algorithmIndex);
    for (const auto &c : colors)
        anim->setColorAt(c.first, c.second);
    if (patch.contains(QStringLiteral("visibilityMask")))
        anim->setVisibilityMask(mask);
    if (patch.contains(QStringLiteral("instantChanges")))
        anim->setInstantChanges(instant);
    return true;
}

/*****************************************************************************
 * Audio triggers config
 *****************************************************************************/

QJsonObject audioTriggersConfigToJson(VCWidget *w)
{
    VCAudioTriggers *at = qobject_cast<VCAudioTriggers *>(w);
    QJsonObject obj;
    if (at == nullptr)
        return obj;

    obj.insert(QStringLiteral("barsNumber"), at->barsNumber());
    obj.insert(QStringLiteral("volumeLevel"), int(at->volumeLevel()));
    obj.insert(QStringLiteral("bars"), audioBarsToJson(at)); // read-only
    return obj;
}

bool applyAudioTriggersConfig(VCWidget *w, const QJsonObject &patch, QString *error)
{
    static const QSet<QString> keys = { QStringLiteral("barsNumber"), QStringLiteral("volumeLevel") };
    static const QHash<QString, QString> readOnly = {
        { QStringLiteral("bars"), QStringLiteral("vc.audioTriggers.setBarConfig") }
    };

    VCAudioTriggers *at = qobject_cast<VCAudioTriggers *>(w);
    if (at == nullptr)
    {
        if (error) *error = QStringLiteral("Widget is not an AudioTriggers widget");
        return false;
    }
    if (checkKeys(patch, keys, readOnly, QStringLiteral("VcAudioTriggersConfig"), error) == false)
        return false;

    // barsNumber counts the volume bar (bar 0) like VCAudioTriggers::barsNumber(); the QML spin box
    // shows barsNumber - 1 spectrum bars. AudioCapture's FFT bands cap the useful count at 32 + 1.
    int bars = at->barsNumber();
    int volume = at->volumeLevel();
    if (!intField(patch, QStringLiteral("barsNumber"), 1, 33, bars, error) ||
        !intField(patch, QStringLiteral("volumeLevel"), 0, 100, volume, error))
        return false;

    if (patch.contains(QStringLiteral("barsNumber")))
    {
        // Resizing while capturing would desynchronise the registered band count: stop, resize, restart.
        bool wasCapturing = at->captureEnabled();
        if (wasCapturing)
            at->setCaptureEnabled(false);
        at->setBarsNumber(bars);
        if (wasCapturing)
            at->setCaptureEnabled(true);
    }
    if (patch.contains(QStringLiteral("volumeLevel")))
        at->setVolumeLevel(uchar(volume));
    return true;
}

/*****************************************************************************
 * XY Pad presets (vc.widget.preset.*)
 *****************************************************************************/

QJsonArray xyPadPresetsToJson(VCWidget *w)
{
    QJsonArray arr;
    VCXYPad *pad = qobject_cast<VCXYPad *>(w);
    if (pad == nullptr)
        return arr;
    for (const VCXYPadPreset *preset : pad->presetObjects())
        arr.append(xyPadPresetToJson(pad, preset));
    return arr;
}

int addXyPadPreset(VCWidget *w, const QJsonObject &data, QString *error)
{
    VCXYPad *pad = qobject_cast<VCXYPad *>(w);
    Doc *doc = w != nullptr ? w->doc() : nullptr;
    if (pad == nullptr || doc == nullptr)
    {
        if (error) *error = QStringLiteral("Widget is not an XYPad");
        return -1;
    }

    QString type = data.value(QStringLiteral("presetType")).toString();
    int presetId = -1;
    if (type == QStringLiteral("position"))
    {
        // The current cursor position - what the "Create a position preset" button does.
        presetId = pad->addPositionPreset();
    }
    else if (type == QStringLiteral("function"))
    {
        bool ok = false;
        quint32 fid = data.value(QStringLiteral("functionID")).toString().toUInt(&ok);
        Function *f = ok ? doc->function(fid) : nullptr;
        if (f == nullptr)
        {
            if (error) *error = QStringLiteral("preset.functionID must name an existing function");
            return -1;
        }
        presetId = pad->addFunctionPreset(fid);
        if (presetId < 0)
        {
            if (error) *error = QStringLiteral("Function '%1' must be an EFX or a Scene with Pan/Tilt channels").arg(f->name());
            return -1;
        }
    }
    else if (type == QStringLiteral("fixtureGroup"))
    {
        // A FixtureGroup reference (resolved live), or - additive - every head of one fixture that is
        // on the pad (what dropping a fixture from the fixture tree does on screen).
        if (data.contains(QStringLiteral("fixtureGroupId")))
        {
            bool ok = false;
            quint32 gid = data.value(QStringLiteral("fixtureGroupId")).toString().toUInt(&ok);
            FixtureGroup *group = ok ? doc->fixtureGroup(gid) : nullptr;
            if (group == nullptr)
            {
                if (error) *error = QStringLiteral("preset.fixtureGroupId must name an existing fixture group");
                return -1;
            }
            presetId = pad->addFixtureGroupPreset(QVariant::fromValue(group));
            if (presetId < 0)
            {
                if (error) *error = QStringLiteral("No head of group '%1' is on this pad").arg(group->name());
                return -1;
            }
        }
        else
        {
            bool ok = false;
            quint32 fxid = data.value(QStringLiteral("fixtureId")).toString().toUInt(&ok);
            Fixture *fixture = ok ? doc->fixture(fxid) : nullptr;
            if (fixture == nullptr)
            {
                if (error) *error = QStringLiteral("presetType fixtureGroup needs preset.fixtureGroupId (or preset.fixtureId for every head of one fixture)");
                return -1;
            }
            presetId = pad->addFixtureGroupPreset(QVariant::fromValue(fixture));
            if (presetId < 0)
            {
                if (error) *error = QStringLiteral("No head of fixture '%1' is on this pad").arg(fixture->name());
                return -1;
            }
        }
    }
    else if (type == QStringLiteral("fixtureGroupHead"))
    {
        bool ok = false;
        quint32 fxid = data.value(QStringLiteral("fixtureId")).toString().toUInt(&ok);
        Fixture *fixture = ok ? doc->fixture(fxid) : nullptr;
        QJsonValue head = data.value(QStringLiteral("headIndex"));
        if (fixture == nullptr || head.isDouble() == false || head.toInt() < 0 || head.toInt() >= fixture->heads())
        {
            if (error) *error = QStringLiteral("presetType fixtureGroupHead needs preset.fixtureId (existing) and preset.headIndex (within the fixture)");
            return -1;
        }
        presetId = pad->addFixtureGroupHeadPreset(int(fxid), head.toInt());
        if (presetId < 0)
        {
            if (error) *error = QStringLiteral("Head %1 of fixture '%2' is not on this pad").arg(head.toInt()).arg(fixture->name());
            return -1;
        }
    }
    else
    {
        if (error) *error = QStringLiteral("preset.presetType must be position, function, fixtureGroup or fixtureGroupHead");
        return -1;
    }

    // Additive: an explicit name (the on-screen flow names it afterwards through the Preset name field).
    if (presetId >= 0 && data.value(QStringLiteral("name")).isString() && data.value(QStringLiteral("name")).toString().isEmpty() == false)
        pad->setPresetName(quint8(presetId), data.value(QStringLiteral("name")).toString());
    return presetId;
}

bool removeXyPadPreset(VCWidget *w, int presetId, QString *error)
{
    VCXYPad *pad = qobject_cast<VCXYPad *>(w);
    if (pad == nullptr || xyPadFindPreset(pad, presetId) == nullptr)
    {
        if (error) *error = QStringLiteral("No such preset");
        return false;
    }
    pad->removePreset(quint8(presetId));
    return true;
}

bool applyXyPadPreset(VCWidget *w, int presetId, QString *error)
{
    VCXYPad *pad = qobject_cast<VCXYPad *>(w);
    if (pad == nullptr || xyPadFindPreset(pad, presetId) == nullptr)
    {
        if (error) *error = QStringLiteral("No such preset");
        return false;
    }
    // Same as VCXYPadItem.qml's preset button. An already-active Function / group preset is
    // deactivated (toggle) - the resulting activePresetId travels on vc.xyPad.activePresetChanged.
    pad->applyPreset(quint8(presetId));
    return true;
}

/*****************************************************************************
 * Animation presets (vc.widget.preset.*)
 *****************************************************************************/

QJsonArray animationPresetsToJson(VCWidget *w)
{
    QJsonArray arr;
    VCAnimation *anim = qobject_cast<VCAnimation *>(w);
    if (anim == nullptr)
        return arr;
    for (const VCAnimationPreset *preset : anim->presetObjects())
        arr.append(animationPresetToJson(anim, preset));
    return arr;
}

int addAnimationPreset(VCWidget *w, const QJsonObject &data, QString *error)
{
    VCAnimation *anim = qobject_cast<VCAnimation *>(w);
    if (anim == nullptr)
    {
        if (error) *error = QStringLiteral("Widget is not an Animation");
        return -1;
    }

    QString type = data.value(QStringLiteral("presetType")).toString();
    QJsonValue slot = data.value(QStringLiteral("colorIndex"));
    bool slotOk = slot.isDouble() && slot.toInt() >= 0 && slot.toInt() < RGBAlgorithmColorDisplayCount;

    if (type == QStringLiteral("color"))
    {
        QColor color(data.value(QStringLiteral("color")).toString());
        if (slotOk == false || color.isValid() == false)
        {
            if (error) *error = QStringLiteral("presetType color needs preset.colorIndex (0..%1) and preset.color (a colour string)").arg(RGBAlgorithmColorDisplayCount - 1);
            return -1;
        }
        return anim->addColorPreset(slot.toInt(), color);
    }
    if (type == QStringLiteral("colorKnobs"))
    {
        if (slotOk == false)
        {
            if (error) *error = QStringLiteral("presetType colorKnobs needs preset.colorIndex (0..%1)").arg(RGBAlgorithmColorDisplayCount - 1);
            return -1;
        }
        // Adds the R/G/B knob trio; the engine returns the last of the three ids.
        return anim->addColorKnobsPreset(slot.toInt());
    }
    if (type == QStringLiteral("text"))
    {
        QString text = data.value(QStringLiteral("text")).toString();
        if (text.isEmpty())
        {
            if (error) *error = QStringLiteral("presetType text needs a non-empty preset.text");
            return -1;
        }
        return anim->addTextPreset(text);
    }
    if (type == QStringLiteral("algorithm"))
    {
        QString name = data.value(QStringLiteral("algorithmName")).toString();
        if (name.isEmpty() || anim->scriptAlgorithms().contains(name) == false)
        {
            if (error) *error = QStringLiteral("preset.algorithmName must be one of the installed RGB script algorithms");
            return -1;
        }
        QJsonValue props = data.value(QStringLiteral("algorithmProperties"));
        if (data.contains(QStringLiteral("algorithmProperties")) && props.isObject() == false)
        {
            if (error) *error = QStringLiteral("preset.algorithmProperties must be an object of name -> value strings");
            return -1;
        }
        // Only the script's own properties are stored - unknown names would never be applied.
        QSet<QString> known;
        for (const QVariant &p : anim->algorithmProperties(name))
            known.insert(p.toMap().value(QStringLiteral("name")).toString());
        QVariantMap properties;
        QJsonObject propsObj = props.toObject();
        for (auto it = propsObj.constBegin(); it != propsObj.constEnd(); ++it)
        {
            if (known.contains(it.key()) == false)
            {
                if (error) *error = QStringLiteral("'%1' is not a property of algorithm '%2'").arg(it.key(), name);
                return -1;
            }
            properties.insert(it.key(), it.value().isString() ? it.value().toString() : it.value().toVariant().toString());
        }
        return anim->addAlgorithmPreset(name, properties);
    }

    if (error) *error = QStringLiteral("preset.presetType must be color, colorKnobs, text or algorithm");
    return -1;
}

bool removeAnimationPreset(VCWidget *w, int presetId, QString *error)
{
    VCAnimation *anim = qobject_cast<VCAnimation *>(w);
    if (anim == nullptr || animationFindPreset(anim, presetId) == nullptr)
    {
        if (error) *error = QStringLiteral("No such preset");
        return false;
    }
    anim->removePreset(quint8(presetId));
    return true;
}

bool applyAnimationPreset(VCWidget *w, int presetId, QString *error)
{
    VCAnimation *anim = qobject_cast<VCAnimation *>(w);
    const VCAnimationPreset *preset = anim != nullptr ? animationFindPreset(anim, presetId) : nullptr;
    if (preset == nullptr)
    {
        if (error) *error = QStringLiteral("No such preset");
        return false;
    }
    if (preset->widgetType() == VCAnimationPreset::Knob)
    {
        // VCAnimation::applyPreset() is a no-op for knobs - say so instead of acking nothing.
        if (error) *error = QStringLiteral("A colour knob preset has no single apply moment - use vc.animation.setPresetKnobValue");
        return false;
    }
    anim->applyPreset(quint8(presetId));
    return true;
}

/*****************************************************************************
 * Live seeds
 *****************************************************************************/

void appendLiveSeed(VCWidget *w, QJsonObject &obj)
{
    if (w == nullptr)
        return;
    switch (w->type())
    {
        case VCWidget::XYPadWidget:
        {
            VCXYPad *pad = qobject_cast<VCXYPad *>(w);
            obj.insert(QStringLiteral("floorPosition"), vectorToJson(pad->floorPosition()));
            obj.insert(QStringLiteral("activePresetId"), pad->activePresetId());
        }
        break;
        case VCWidget::ClockWidget:
        {
            VCClock *clock = qobject_cast<VCClock *>(w);
            obj.insert(QStringLiteral("currentTime"), clock->currentTime());
            obj.insert(QStringLiteral("running"), clock->timerRunning());
        }
        break;
        case VCWidget::AnimationWidget:
        {
            VCAnimation *anim = qobject_cast<VCAnimation *>(w);
            obj.insert(QStringLiteral("faderLevel"), anim->faderLevel());
            obj.insert(QStringLiteral("activePresetId"), anim->activePresetId());
        }
        break;
        case VCWidget::AudioTriggersWidget:
        {
            VCAudioTriggers *at = qobject_cast<VCAudioTriggers *>(w);
            obj.insert(QStringLiteral("captureEnabled"), at->captureEnabled());
            obj.insert(QStringLiteral("levels"), levelsToJson(at->audioLevels()));
        }
        break;
        default:
        break;
    }
}

}
