/*
  Q Light Controller Plus
  app_apivcconfig_cue.cpp

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
 * vc.widget.setConfig / typeConfig for CueList and SpeedDial, plus the SpeedDial preset list - see
 * app_apivcconfig.h for the contract and docs/api-spec/fragments/virtualconsole.yaml for the wire
 * shapes (VcCueListConfig, VcSpeedDialConfig, VcSpeedDialPreset / VcSpeedDialPresetData).
 *
 * Mirrors what VCCueListProperties.qml, VCSpeedDialProperties.qml and VCSpeedDialPresets.qml do on
 * the same widget objects, minus the QML-only bits (cue list column widths, the s/ms display toggle).
 */

#include <QHash>
#include <QJsonArray>
#include <QJsonValue>
#include <QSet>
#include <QVariantList>
#include <QVariantMap>

#include "app_apivcconfig.h"
#include "doc.h"
#include "function.h"
#include "virtualconsole/vccuelist.h"
#include "virtualconsole/vcspeeddial.h"

namespace
{

/** Strict integer check (same rule as ApiVcDomain's live methods): a whole JSON number only. */
bool jsonIsInteger(const QJsonValue &v)
{
    if (v.isDouble() == false)
        return false;
    double d = v.toDouble();
    if (d < -2147483648.0 || d > 2147483647.0)
        return false;
    return d == double(int(d));
}

bool rejectUnknownKeys(const QJsonObject &patch, const QSet<QString> &knownKeys, const QString &schema, QString *error)
{
    for (auto it = patch.constBegin(); it != patch.constEnd(); ++it)
    {
        if (knownKeys.contains(it.key()) == false)
        {
            if (error)
                *error = QStringLiteral("Unknown %1 key '%2'").arg(schema, it.key());
            return false;
        }
    }
    return true;
}

// --- VcCueListConfig enums ---

const QHash<QString, VCCueList::NextPrevBehavior> &nextPrevBehaviors()
{
    static const QHash<QString, VCCueList::NextPrevBehavior> map = {
        { QStringLiteral("DefaultRunFirst"), VCCueList::DefaultRunFirst }, { QStringLiteral("RunNext"), VCCueList::RunNext },
        { QStringLiteral("Select"), VCCueList::Select }, { QStringLiteral("Nothing"), VCCueList::Nothing }
    };
    return map;
}

const QHash<QString, VCCueList::PlaybackLayout> &playbackLayouts()
{
    static const QHash<QString, VCCueList::PlaybackLayout> map = {
        { QStringLiteral("PlayPauseStop"), VCCueList::PlayPauseStop }, { QStringLiteral("PlayStopPause"), VCCueList::PlayStopPause }
    };
    return map;
}

const QHash<QString, VCCueList::FaderMode> &faderModes()
{
    static const QHash<QString, VCCueList::FaderMode> map = {
        { QStringLiteral("None"), VCCueList::None }, { QStringLiteral("Crossfade"), VCCueList::Crossfade },
        { QStringLiteral("Steps"), VCCueList::Steps }
    };
    return map;
}

template <typename Enum>
QString enumKey(const QHash<QString, Enum> &map, Enum value, const QString &fallback)
{
    for (auto it = map.constBegin(); it != map.constEnd(); ++it)
    {
        if (it.value() == value)
            return it.key();
    }
    return fallback;
}

// --- VcSpeedDialMultiplier / visibility flags, in VCSpeedDial enum order ---

const QStringList &multiplierNames()
{
    static const QStringList names = {
        QStringLiteral("None"), QStringLiteral("Zero"), QStringLiteral("OneSixteenth"), QStringLiteral("OneEighth"),
        QStringLiteral("OneFourth"), QStringLiteral("Half"), QStringLiteral("One"), QStringLiteral("Two"),
        QStringLiteral("Four"), QStringLiteral("Eight"), QStringLiteral("Sixteen")
    };
    return names;
}

struct VisibilityFlag { const char *name; quint32 bit; };
const VisibilityFlag kVisibilityFlags[] = {
    { "PlusMinus", VCSpeedDial::PlusMinus }, { "Dial", VCSpeedDial::Dial }, { "Tap", VCSpeedDial::Tap },
    { "Hours", VCSpeedDial::Hours }, { "Minutes", VCSpeedDial::Minutes }, { "Seconds", VCSpeedDial::Seconds },
    { "Milliseconds", VCSpeedDial::Milliseconds }, { "Multipliers", VCSpeedDial::Multipliers },
    { "Apply", VCSpeedDial::Apply }, { "Beats", VCSpeedDial::Beats }, { "XPad", VCSpeedDial::XPad }
};

QJsonArray visibilityMaskToJson(quint32 mask)
{
    QJsonArray arr;
    for (const VisibilityFlag &f : kVisibilityFlags)
    {
        if (mask & f.bit)
            arr.append(QString::fromLatin1(f.name));
    }
    return arr;
}

bool visibilityMaskFromJson(const QJsonValue &v, quint32 &mask, QString *error)
{
    if (v.isArray() == false)
    {
        if (error) *error = QStringLiteral("visibilityMask must be an array of flag names");
        return false;
    }
    mask = 0;
    for (const QJsonValue &item : v.toArray())
    {
        bool found = false;
        for (const VisibilityFlag &f : kVisibilityFlags)
        {
            if (item.toString() == QLatin1String(f.name))
            {
                mask |= f.bit;
                found = true;
                break;
            }
        }
        if (found == false)
        {
            if (error) *error = QStringLiteral("Unknown visibilityMask flag '%1'").arg(item.toString());
            return false;
        }
    }
    return true;
}

/** A per-function factor field: absent = keep $fallback; otherwise must be a VcSpeedDialMultiplier. */
bool multiplierFromJson(const QJsonObject &obj, const QString &key, VCSpeedDial::SpeedMultiplier fallback,
                        VCSpeedDial::SpeedMultiplier &out, QString *error)
{
    if (obj.contains(key) == false)
    {
        out = fallback;
        return true;
    }
    int index = multiplierNames().indexOf(obj.value(key).toString());
    if (index < 0)
    {
        if (error) *error = QStringLiteral("Invalid %1 '%2'").arg(key, obj.value(key).toString());
        return false;
    }
    out = VCSpeedDial::SpeedMultiplier(index);
    return true;
}

// --- VcSpeedDialPreset ---

/** Finds a preset in VCSpeedDial::presetsList() (the QML-facing {id, name, value} maps). */
bool findSpeedDialPreset(VCSpeedDial *sd, int presetId, QVariantMap *out)
{
    for (const QVariant &v : sd->presetsList())
    {
        QVariantMap m = v.toMap();
        if (m.value(QStringLiteral("id")).toInt() == presetId)
        {
            if (out) *out = m;
            return true;
        }
    }
    return false;
}

} // namespace

namespace ApiVcConfig
{

QString speedDialMultiplierName(int factor)
{
    return multiplierNames().value(factor, QStringLiteral("One"));
}

int speedDialMultiplierFromName(const QString &name)
{
    return multiplierNames().indexOf(name);
}

/*****************************************************************************
 * VcCueListConfig
 *****************************************************************************/

QJsonObject cueListConfigToJson(VCWidget *w)
{
    VCCueList *cl = qobject_cast<VCCueList *>(w);
    QJsonObject obj;
    obj.insert(QStringLiteral("chaserID"), QString::number(cl->chaserID()));
    obj.insert(QStringLiteral("nextPrevBehavior"), enumKey(nextPrevBehaviors(), cl->nextPrevBehavior(), QStringLiteral("DefaultRunFirst")));
    obj.insert(QStringLiteral("playbackLayout"), enumKey(playbackLayouts(), cl->playbackLayout(), QStringLiteral("PlayPauseStop")));
    obj.insert(QStringLiteral("sideFaderMode"), enumKey(faderModes(), cl->sideFaderMode(), QStringLiteral("None")));
    return obj;
}

bool applyCueListConfig(VCWidget *w, Doc *doc, const QJsonObject &patch, QString *error)
{
    static const QSet<QString> knownKeys = {
        QStringLiteral("chaserID"), QStringLiteral("nextPrevBehavior"), QStringLiteral("playbackLayout"),
        QStringLiteral("sideFaderMode")
    };
    VCCueList *cl = qobject_cast<VCCueList *>(w);

    if (rejectUnknownKeys(patch, knownKeys, QStringLiteral("VcCueListConfig"), error) == false)
        return false;

    // Validate everything first so a bad key leaves the widget untouched.
    quint32 chaserId = cl->chaserID();
    if (patch.contains(QStringLiteral("chaserID")))
    {
        bool ok = false;
        chaserId = patch.value(QStringLiteral("chaserID")).toString().toUInt(&ok);
        if (ok == false)
        {
            if (error) *error = QStringLiteral("Invalid chaserID");
            return false;
        }
        if (chaserId != Function::invalidId())
        {
            Function *f = doc->function(chaserId);
            if (f == nullptr)
            {
                if (error) *error = QStringLiteral("No such Function '%1'").arg(chaserId);
                return false;
            }
            // Sequence is a Chaser subclass, so both attach fine - anything else has no steps to show.
            if (f->type() != Function::ChaserType && f->type() != Function::SequenceType)
            {
                if (error) *error = QStringLiteral("Function '%1' is a %2, not a Chaser").arg(f->name(), f->typeString());
                return false;
            }
        }
    }
    VCCueList::NextPrevBehavior nextPrev = cl->nextPrevBehavior();
    if (patch.contains(QStringLiteral("nextPrevBehavior")))
    {
        QString v = patch.value(QStringLiteral("nextPrevBehavior")).toString();
        if (nextPrevBehaviors().contains(v) == false)
        {
            if (error) *error = QStringLiteral("Invalid nextPrevBehavior '%1'").arg(v);
            return false;
        }
        nextPrev = nextPrevBehaviors().value(v);
    }
    VCCueList::PlaybackLayout layout = cl->playbackLayout();
    if (patch.contains(QStringLiteral("playbackLayout")))
    {
        QString v = patch.value(QStringLiteral("playbackLayout")).toString();
        if (playbackLayouts().contains(v) == false)
        {
            if (error) *error = QStringLiteral("Invalid playbackLayout '%1'").arg(v);
            return false;
        }
        layout = playbackLayouts().value(v);
    }
    VCCueList::FaderMode faderMode = cl->sideFaderMode();
    if (patch.contains(QStringLiteral("sideFaderMode")))
    {
        QString v = patch.value(QStringLiteral("sideFaderMode")).toString();
        if (faderModes().contains(v) == false)
        {
            if (error) *error = QStringLiteral("Invalid sideFaderMode '%1'").arg(v);
            return false;
        }
        faderMode = faderModes().value(v);
    }

    if (patch.contains(QStringLiteral("chaserID")))
        cl->setChaserID(chaserId); // same call as VCCueListProperties.qml's detach / the drop handler
    cl->setNextPrevBehavior(nextPrev);
    cl->setPlaybackLayout(layout);
    // NOTE: VCCueList::setSideFaderMode() resets the fader level (Steps -> 255, Crossfade -> 100),
    // which surfaces as a vc.cueList.sideFaderChanged event right before vc.widget.configChanged.
    cl->setSideFaderMode(faderMode);
    return true;
}

/*****************************************************************************
 * VcSpeedDialConfig
 *****************************************************************************/

QJsonObject speedDialConfigToJson(VCWidget *w)
{
    VCSpeedDial *sd = qobject_cast<VCSpeedDial *>(w);
    QJsonObject obj;
    obj.insert(QStringLiteral("visibilityMask"), visibilityMaskToJson(sd->visibilityMask()));
    obj.insert(QStringLiteral("timeMinimumValue"), int(sd->timeMinimumValue()));
    obj.insert(QStringLiteral("timeMaximumValue"), int(sd->timeMaximumValue()));
    obj.insert(QStringLiteral("resetOnDialChange"), sd->resetOnDialChange());
    obj.insert(QStringLiteral("controlBPM"), sd->controlBPM());

    QJsonArray functions;
    const QMap<quint32, VCSpeedDial::VCSpeedDialFunction> map = sd->functions();
    for (auto it = map.constBegin(); it != map.constEnd(); ++it)
    {
        QJsonObject f;
        f.insert(QStringLiteral("functionID"), QString::number(it.key()));
        f.insert(QStringLiteral("fadeInFactor"), multiplierNames().value(int(it.value().m_fadeInFactor), QStringLiteral("None")));
        f.insert(QStringLiteral("fadeOutFactor"), multiplierNames().value(int(it.value().m_fadeOutFactor), QStringLiteral("None")));
        f.insert(QStringLiteral("durationFactor"), multiplierNames().value(int(it.value().m_durationFactor), QStringLiteral("None")));
        functions.append(f);
    }
    obj.insert(QStringLiteral("functions"), functions);

    // Read-only convenience so a client sees the preset list in one vc.widget.get; mutations go
    // through vc.widget.preset.* / vc.speedDial.preset.update (applySpeedDialConfig() refuses the key).
    obj.insert(QStringLiteral("presets"), speedDialPresetsToJson(w));
    return obj;
}

bool applySpeedDialConfig(VCWidget *w, Doc *doc, const QJsonObject &patch, QString *error)
{
    static const QSet<QString> knownKeys = {
        QStringLiteral("visibilityMask"), QStringLiteral("timeMinimumValue"), QStringLiteral("timeMaximumValue"),
        QStringLiteral("resetOnDialChange"), QStringLiteral("controlBPM"), QStringLiteral("functions")
    };
    VCSpeedDial *sd = qobject_cast<VCSpeedDial *>(w);

    if (patch.contains(QStringLiteral("presets")))
    {
        if (error) *error = QStringLiteral("presets is read-only here - use vc.widget.preset.add/remove and vc.speedDial.preset.update");
        return false;
    }
    if (rejectUnknownKeys(patch, knownKeys, QStringLiteral("VcSpeedDialConfig"), error) == false)
        return false;

    // Validate everything first so a bad key leaves the widget untouched.
    quint32 mask = sd->visibilityMask();
    if (patch.contains(QStringLiteral("visibilityMask")) &&
        visibilityMaskFromJson(patch.value(QStringLiteral("visibilityMask")), mask, error) == false)
        return false;

    uint timeMin = sd->timeMinimumValue();
    uint timeMax = sd->timeMaximumValue();
    for (const QString &key : { QStringLiteral("timeMinimumValue"), QStringLiteral("timeMaximumValue") })
    {
        if (patch.contains(key) == false)
            continue;
        QJsonValue v = patch.value(key);
        if (jsonIsInteger(v) == false || v.toInt() < 0)
        {
            if (error) *error = QStringLiteral("%1 must be a non-negative integer (milliseconds)").arg(key);
            return false;
        }
        (key == QStringLiteral("timeMinimumValue") ? timeMin : timeMax) = uint(v.toInt());
    }

    for (const QString &key : { QStringLiteral("resetOnDialChange"), QStringLiteral("controlBPM") })
    {
        if (patch.contains(key) && patch.value(key).isBool() == false)
        {
            if (error) *error = QStringLiteral("%1 must be a boolean").arg(key);
            return false;
        }
    }

    // `functions` replaces the whole controlled-Function list (a partial patch of the list itself
    // would be ambiguous). Each entry: functionID (must exist) + optional per-speed factors; the
    // defaults match VCSpeedDial::addFunction(quint32): fadeIn/fadeOut not sent, duration 1x.
    QMap<quint32, VCSpeedDial::VCSpeedDialFunction> newFunctions;
    if (patch.contains(QStringLiteral("functions")))
    {
        QJsonValue fv = patch.value(QStringLiteral("functions"));
        if (fv.isArray() == false)
        {
            if (error) *error = QStringLiteral("functions must be an array");
            return false;
        }
        const QMap<quint32, VCSpeedDial::VCSpeedDialFunction> current = sd->functions();
        for (const QJsonValue &item : fv.toArray())
        {
            QJsonObject obj = item.toObject();
            bool ok = false;
            quint32 fid = obj.value(QStringLiteral("functionID")).toString().toUInt(&ok);
            if (ok == false || doc->function(fid) == nullptr)
            {
                if (error) *error = QStringLiteral("functions: no such Function '%1'").arg(obj.value(QStringLiteral("functionID")).toString());
                return false;
            }
            VCSpeedDial::VCSpeedDialFunction func;
            func.m_fId = fid;
            // Factors default to the function's current ones when it is already attached, so a
            // client that only appends a new entry keeps the others intact.
            VCSpeedDial::VCSpeedDialFunction base;
            base.m_fId = fid;
            base.m_fadeInFactor = VCSpeedDial::None;
            base.m_fadeOutFactor = VCSpeedDial::None;
            base.m_durationFactor = VCSpeedDial::One;
            if (current.contains(fid))
                base = current.value(fid);
            if (multiplierFromJson(obj, QStringLiteral("fadeInFactor"), base.m_fadeInFactor, func.m_fadeInFactor, error) == false ||
                multiplierFromJson(obj, QStringLiteral("fadeOutFactor"), base.m_fadeOutFactor, func.m_fadeOutFactor, error) == false ||
                multiplierFromJson(obj, QStringLiteral("durationFactor"), base.m_durationFactor, func.m_durationFactor, error) == false)
                return false;
            newFunctions.insert(fid, func);
        }
    }

    sd->setVisibilityMask(mask);
    sd->setTimeMinimumValue(timeMin);
    sd->setTimeMaximumValue(timeMax);
    if (patch.contains(QStringLiteral("resetOnDialChange")))
        sd->setResetOnDialChange(patch.value(QStringLiteral("resetOnDialChange")).toBool());
    if (patch.contains(QStringLiteral("controlBPM")))
        sd->setControlBPM(patch.value(QStringLiteral("controlBPM")).toBool());
    if (patch.contains(QStringLiteral("functions")))
    {
        // Through the public add/remove pair (both emit functionsListChanged, unlike setFunctions()),
        // so an open VCSpeedDialProperties panel follows the API change.
        for (quint32 fid : sd->functions().keys())
        {
            if (newFunctions.contains(fid) == false)
                sd->removeFunction(fid);
        }
        for (auto it = newFunctions.constBegin(); it != newFunctions.constEnd(); ++it)
            sd->addFunction(it.value());
    }
    return true;
}

/*****************************************************************************
 * VcSpeedDialPreset - vc.widget.preset.add/apply/remove, vc.speedDial.preset.update
 *****************************************************************************/

QJsonArray speedDialPresetsToJson(VCWidget *w)
{
    VCSpeedDial *sd = qobject_cast<VCSpeedDial *>(w);
    QJsonArray arr;
    for (const QVariant &v : sd->presetsList())
    {
        QVariantMap m = v.toMap();
        QJsonObject p;
        p.insert(QStringLiteral("presetId"), m.value(QStringLiteral("id")).toInt());
        p.insert(QStringLiteral("name"), m.value(QStringLiteral("name")).toString());
        p.insert(QStringLiteral("valueMs"), m.value(QStringLiteral("value")).toInt());
        arr.append(p);
    }
    return arr;
}

int addSpeedDialPreset(VCWidget *w, const QJsonObject &data, QString *error)
{
    VCSpeedDial *sd = qobject_cast<VCSpeedDial *>(w);
    QJsonValue name = data.value(QStringLiteral("name"));
    QJsonValue valueMs = data.value(QStringLiteral("valueMs"));
    if (name.isString() == false || name.toString().isEmpty())
    {
        if (error) *error = QStringLiteral("preset.name must be a non-empty string");
        return -1;
    }
    if (jsonIsInteger(valueMs) == false || valueMs.toInt() < 0)
    {
        if (error) *error = QStringLiteral("preset.valueMs must be a non-negative integer");
        return -1;
    }
    for (auto it = data.constBegin(); it != data.constEnd(); ++it)
    {
        if (it.key() != QStringLiteral("name") && it.key() != QStringLiteral("valueMs"))
        {
            if (error) *error = QStringLiteral("Unknown VcSpeedDialPresetData key '%1'").arg(it.key());
            return -1;
        }
    }
    // Preset ids are a quint8; VCSpeedDial hands them out from 16 upwards and never reuses one.
    if (sd->presetsList().size() >= 200)
    {
        if (error) *error = QStringLiteral("Too many presets on this speed dial");
        return -1;
    }
    return sd->addPreset(name.toString(), valueMs.toInt());
}

bool removeSpeedDialPreset(VCWidget *w, int presetId, QString *error)
{
    VCSpeedDial *sd = qobject_cast<VCSpeedDial *>(w);
    if (findSpeedDialPreset(sd, presetId, nullptr) == false)
    {
        if (error) *error = QStringLiteral("No such preset");
        return false;
    }
    sd->removePreset(quint8(presetId));
    return true;
}

bool applySpeedDialPreset(VCWidget *w, int presetId, QString *error)
{
    VCSpeedDial *sd = qobject_cast<VCSpeedDial *>(w);
    QVariantMap preset;
    if (findSpeedDialPreset(sd, presetId, &preset) == false)
    {
        if (error) *error = QStringLiteral("No such preset");
        return false;
    }
    // VCSpeedDialItem.qml's preset button: `speedObj.currentTime = modelData.value` - the new value
    // reaches clients as vc.speedDial.valueChanged (and is applied to the attached Functions).
    sd->setCurrentTime(uint(qMax(0, preset.value(QStringLiteral("value")).toInt())));
    return true;
}

bool updateSpeedDialPreset(VCWidget *w, int presetId, const QJsonObject &patch, QString *error)
{
    VCSpeedDial *sd = qobject_cast<VCSpeedDial *>(w);
    if (findSpeedDialPreset(sd, presetId, nullptr) == false)
    {
        if (error) *error = QStringLiteral("No such preset");
        return false;
    }
    if (patch.contains(QStringLiteral("name")))
        sd->setPresetName(quint8(presetId), patch.value(QStringLiteral("name")).toString());
    if (patch.contains(QStringLiteral("valueMs")))
        sd->setPresetValue(quint8(presetId), patch.value(QStringLiteral("valueMs")).toInt());
    return true;
}

}
