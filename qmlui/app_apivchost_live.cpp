/*
  Q Light Controller Plus
  app_apivchost_live.cpp

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
 * App's ApiVcHost implementation, part three: the XY Pad fixtures / presets / floor control, Clock,
 * Animation and Audio Triggers methods of ApiVcLiveDomain (controlapi/src/domains/apivclivedomain.cpp)
 * and the relays feeding its ApiVcLiveListenerExt. Split out of app_apivchost.cpp like the cue-list
 * half (app_apivchost_cue.cpp), so the per-widget slices do not collide; the typeConfig / preset
 * shaping this file's methods rely on lives in app_apivcconfig_live.cpp.
 *
 * Every method drives the widget exactly like its QML item / property panel does (VCXYPadProperties.
 * qml's addFixture/addHead/addGroup/removeHeads/setHeadsRange, VCXYPadItem.qml's floorPosition,
 * VCClockItem.qml's playPauseTimer()/resetTimer(), VCClockProperties.qml's schedule editing,
 * VCAnimationItem.qml's faderLevel / setPresetKnobValue, VCAudioTriggersItem.qml's captureEnabled and
 * VCAudioTriggersProperties.qml's per-bar setters).
 */

#include <QColor>
#include <QJsonArray>
#include <QJsonObject>
#include <QJsonValue>
#include <QModelIndex>
#include <QPointer>
#include <QVariantList>
#include <QVector3D>

#include "app.h"
#include "app_apivcconfig.h"
#include "doc.h"
#include "fixture.h"
#include "fixturegroup.h"
#include "listmodel.h"
#include "scenevalue.h"
#include "virtualconsole/vcwidget.h"
#include "virtualconsole/vcxypad.h"
#include "virtualconsole/vcxypadpreset.h"
#include "virtualconsole/vcanimationpreset.h"
#include "virtualconsole/vcclock.h"
#include "virtualconsole/vcanimation.h"
#include "virtualconsole/vcaudiotriggers.h"
#include "virtualconsole/vcslider.h"

namespace {

/** The rows of the pad's fixture ListModel (VCXYPad::fixtureList()) named by $heads - each entry
 *  {fixtureId, headIndex} or {fixtureGroupId} - in the QVariantList shape VCXYPad::removeHeads() /
 *  setHeadsRange() / headsRangeInfo() take. Unknown entries are skipped. */
QVariantList xyPadRowsFor(VCXYPad *pad, const QJsonArray &heads)
{
    QVariantList rows;
    ListModel *list = pad->fixtureList().value<ListModel *>();
    if (list == nullptr)
        return rows;

    for (const QJsonValue &v : heads)
    {
        QJsonObject h = v.toObject();
        bool isGroup = h.contains(QStringLiteral("fixtureGroupId"));
        quint32 wanted = isGroup ? h.value(QStringLiteral("fixtureGroupId")).toString().toUInt()
                                 : h.value(QStringLiteral("fixtureId")).toString().toUInt();
        int wantedHead = h.value(QStringLiteral("headIndex")).toInt(0);

        for (int row = 0; row < list->rowCount(); row++)
        {
            QModelIndex idx = list->index(row, 0);
            bool rowIsGroup = list->data(idx, QStringLiteral("isGroup")).toBool();
            if (rowIsGroup != isGroup)
                continue;
            bool match = isGroup ? list->data(idx, QStringLiteral("groupID")).toUInt() == wanted
                                 : (list->data(idx, QStringLiteral("fxID")).toUInt() == wanted &&
                                    list->data(idx, QStringLiteral("head")).toInt() == wantedHead);
            if (match && rows.contains(row) == false)
            {
                rows.append(row);
                break;
            }
        }
    }
    return rows;
}

int xyPadRowCount(VCXYPad *pad)
{
    ListModel *list = pad->fixtureList().value<ListModel *>();
    return list != nullptr ? list->rowCount() : 0;
}

QList<int> levelsList(const QVariantList &levels)
{
    QList<int> out;
    for (const QVariant &v : levels)
        out.append(v.toInt());
    return out;
}

VCAudioTriggers::BarType barTypeFromName(const QString &name)
{
    if (name == QStringLiteral("DMXBar"))      return VCAudioTriggers::DMXBar;
    if (name == QStringLiteral("FunctionBar")) return VCAudioTriggers::FunctionBar;
    if (name == QStringLiteral("VCWidgetBar")) return VCAudioTriggers::VCWidgetBar;
    return VCAudioTriggers::None;
}

} // namespace

/*****************************************************************************
 * Listener + relays
 *****************************************************************************/

void App::vcSetLiveListenerExt(ApiVcLiveListenerExt *listener)
{
    m_vcLiveListenerExt = listener;
}

void App::vcConnectLiveRelaysExt(VCWidget *widget)
{
    // Same rules as slotVcWidgetRegistered()'s relays: QPointer-guarded functors with `this` as the
    // context object (GUI thread, survive a queued delivery after the widget died), and the caller's
    // disconnect(widget, nullptr, this, nullptr) drops them on re-registration.
    switch (widget->type())
    {
        case VCWidget::XYPadWidget:
        {
            QPointer<VCXYPad> pad(qobject_cast<VCXYPad *>(widget));
            connect(pad.data(), &VCXYPad::floorPositionChanged, this, [this, pad]()
            {
                if (pad.isNull() || m_vcLiveListenerExt == nullptr)
                    return;
                QVector3D p = pad->floorPosition();
                m_vcLiveListenerExt->vcXyPadFloorPositionChanged(pad->id(), p.x(), p.y(), p.z());
            });
            connect(pad.data(), &VCXYPad::activePresetIdChanged, this, [this, pad]()
            {
                if (pad.isNull() || m_vcLiveListenerExt == nullptr)
                    return;
                m_vcLiveListenerExt->vcXyPadActivePresetChanged(pad->id(), pad->activePresetId());
            });
            // The yellow "where the heads point" dots (fixturePositions, read back from the output).
            connect(pad.data(), &VCXYPad::fixturePositionsChanged, this, [this, pad]()
            {
                if (pad.isNull() || m_vcLiveListenerExt == nullptr)
                    return;
                m_vcLiveListenerExt->vcXyPadFixturePositionsChanged(pad->id(), ApiVcConfig::xyPadFixturePositions(pad.data()));
            });
        }
        break;
        case VCWidget::SliderWidget:
        {
            // Level-mode monitor readback. monitorValueChanged is emitted from writeDMXLevel() on the
            // MasterTimer thread: `this` as context makes it a queued call onto the GUI thread.
            QPointer<VCSlider> slider(qobject_cast<VCSlider *>(widget));
            auto relay = [this, slider]()
            {
                if (slider.isNull() || m_vcLiveListenerExt == nullptr)
                    return;
                m_vcLiveListenerExt->vcSliderMonitorChanged(slider->id(), slider->monitorValue(), slider->isOverriding());
            };
            connect(slider.data(), &VCSlider::monitorValueChanged, this, relay);
            connect(slider.data(), &VCSlider::isOverridingChanged, this, relay);
        }
        break;
        case VCWidget::ClockWidget:
        {
            QPointer<VCClock> clock(qobject_cast<VCClock *>(widget));
            auto relay = [this, clock]()
            {
                if (clock.isNull() || m_vcLiveListenerExt == nullptr)
                    return;
                m_vcLiveListenerExt->vcClockTimeChanged(clock->id(), clock->currentTime(), clock->timerRunning());
            };
            // currentTimeChanged is the 1 Hz (Clock) / 10 Hz (running Stopwatch/Countdown) tick and is
            // also emitted immediately by playPauseTimer()/resetTimer(); timerRunningChanged covers a
            // Countdown stopping itself at 0.
            connect(clock.data(), &VCClock::currentTimeChanged, this, relay);
            connect(clock.data(), &VCClock::timerRunningChanged, this, relay);
        }
        break;
        case VCWidget::AnimationWidget:
        {
            QPointer<VCAnimation> anim(qobject_cast<VCAnimation *>(widget));
            connect(anim.data(), &VCAnimation::faderLevelChanged, this, [this, anim]()
            {
                if (anim.isNull() || m_vcLiveListenerExt == nullptr)
                    return;
                m_vcLiveListenerExt->vcAnimationFaderLevelChanged(anim->id(), anim->faderLevel());
            });
            connect(anim.data(), &VCAnimation::activePresetIdChanged, this, [this, anim]()
            {
                if (anim.isNull() || m_vcLiveListenerExt == nullptr)
                    return;
                m_vcLiveListenerExt->vcAnimationActivePresetChanged(anim->id(), anim->activePresetId(), -1, -1);
            });
            // Colours and the algorithm change live (colour tool drags, presets, knobs, the algorithm
            // combo) without a docRevision bump; colorsChanged covers every setColorN() path.
            auto styleRelay = [this, anim]()
            {
                if (anim.isNull() || m_vcLiveListenerExt == nullptr)
                    return;
                m_vcLiveListenerExt->vcAnimationStyleChanged(anim->id(), anim->algorithmIndex(),
                                                             ApiVcConfig::animationColorStrings(anim.data()));
            };
            connect(anim.data(), &VCAnimation::colorsChanged, this, styleRelay);
            connect(anim.data(), &VCAnimation::algorithmIndexChanged, this, styleRelay);
        }
        break;
        case VCWidget::AudioTriggersWidget:
        {
            QPointer<VCAudioTriggers> at(qobject_cast<VCAudioTriggers *>(widget));
            connect(at.data(), &VCAudioTriggers::captureEnabledChanged, this, [this, at]()
            {
                if (at.isNull() || m_vcLiveListenerExt == nullptr)
                    return;
                m_vcLiveListenerExt->vcAudioTriggersCaptureEnabledChanged(at->id(), at->captureEnabled());
            });
            connect(at.data(), &VCAudioTriggers::audioLevelsChanged, this, [this, at]()
            {
                if (at.isNull() || m_vcLiveListenerExt == nullptr)
                    return;
                m_vcLiveListenerExt->vcAudioTriggersLevelsChanged(at->id(), levelsList(at->audioLevels()));
            });
        }
        break;
        default:
        break;
    }
}

/*****************************************************************************
 * XY Pad
 *****************************************************************************/

bool App::vcXyPadSetFloorPosition(quint32 id, double x, double y, double z, QString *error)
{
    VCXYPad *pad = qobject_cast<VCXYPad *>(vcFindWidget(id));
    if (pad == nullptr)
    {
        if (error) *error = QStringLiteral("No such widget");
        return false;
    }
    if (pad->floorControl() == false)
    {
        if (error) *error = QStringLiteral("Floor control is off on this pad (VcXyPadConfig.floorControl)");
        return false;
    }
    // setFloorPosition() confines the point to the reachable floor area / height range itself.
    pad->setFloorPosition(QVector3D(float(x), float(y), float(z)));
    return true;
}

bool App::vcXyPadAddFixtures(quint32 id, XyPadAddKind kind, quint32 refId, int headIndex, int *addedPresetId, QString *error)
{
    VCXYPad *pad = qobject_cast<VCXYPad *>(vcFindWidget(id));
    if (pad == nullptr)
    {
        if (error) *error = QStringLiteral("No such widget");
        return false;
    }
    if (addedPresetId != nullptr)
        *addedPresetId = -1;

    int before = xyPadRowCount(pad);
    switch (kind)
    {
        case XyPadAddFixture:
        {
            Fixture *fixture = m_doc->fixture(refId);
            if (fixture == nullptr)
            {
                if (error) *error = QStringLiteral("No such fixture");
                return false;
            }
            pad->addFixture(QVariant::fromValue(fixture));
            if (xyPadRowCount(pad) == before)
            {
                if (error) *error = QStringLiteral("Fixture '%1' has no Pan/Tilt head that is not already on this pad").arg(fixture->name());
                return false;
            }
        }
        break;
        case XyPadAddHead:
        {
            Fixture *fixture = m_doc->fixture(refId);
            if (fixture == nullptr)
            {
                if (error) *error = QStringLiteral("No such fixture");
                return false;
            }
            pad->addHead(int(refId), headIndex);
            if (xyPadRowCount(pad) == before)
            {
                if (error) *error = QStringLiteral("Head %1 of fixture '%2' is already on this pad").arg(headIndex).arg(fixture->name());
                return false;
            }
        }
        break;
        case XyPadAddGroup:
        {
            FixtureGroup *group = m_doc->fixtureGroup(refId);
            if (group == nullptr)
            {
                if (error) *error = QStringLiteral("No such fixture group");
                return false;
            }
            int presetsBefore = pad->presetObjects().count();
            pad->addGroup(QVariant::fromValue(group));
            if (xyPadRowCount(pad) == before)
            {
                if (error) *error = QStringLiteral("Group '%1' is already on this pad").arg(group->name());
                return false;
            }
            // addGroup() also creates a fixture-group preset for the dropped group.
            if (addedPresetId != nullptr && pad->presetObjects().count() > presetsBefore)
                *addedPresetId = int(pad->presetObjects().last()->m_id);
        }
        break;
        case XyPadAddUniverse:
        {
            // VCXYPad::addGroup(Universe*) expands a universe to its fixtures: do the same walk here
            // (the pad only needs the Fixture references).
            for (Fixture *fixture : m_doc->fixtures())
            {
                if (fixture->universe() == refId)
                    pad->addFixture(QVariant::fromValue(fixture));
            }
            if (xyPadRowCount(pad) == before)
            {
                if (error) *error = QStringLiteral("Universe %1 has no Pan/Tilt fixture that is not already on this pad").arg(refId + 1);
                return false;
            }
        }
        break;
    }
    return true;
}

bool App::vcXyPadRemoveHeads(quint32 id, const QJsonArray &heads, QString *error)
{
    VCXYPad *pad = qobject_cast<VCXYPad *>(vcFindWidget(id));
    if (pad == nullptr)
    {
        if (error) *error = QStringLiteral("No such widget");
        return false;
    }
    QVariantList rows = xyPadRowsFor(pad, heads);
    if (rows.isEmpty())
    {
        if (error) *error = QStringLiteral("None of the given heads is on this pad");
        return false;
    }
    pad->removeHeads(rows);
    return true;
}

bool App::vcXyPadSetHeadsRange(quint32 id, const QJsonArray &heads, int xMin, int xMax, bool xReverse,
                               int yMin, int yMax, bool yReverse, QString *error)
{
    VCXYPad *pad = qobject_cast<VCXYPad *>(vcFindWidget(id));
    if (pad == nullptr)
    {
        if (error) *error = QStringLiteral("No such widget");
        return false;
    }
    QVariantList rows = xyPadRowsFor(pad, heads);
    if (rows.isEmpty())
    {
        if (error) *error = QStringLiteral("None of the given heads is on this pad");
        return false;
    }
    // The values are in the current display units; the dialog caps them at headsRangeInfo()'s
    // xMaxValue / yMaxValue (100 %, 255 DMX or the smallest Pan/Tilt travel in degrees).
    QVariantMap info = pad->headsRangeInfo(rows);
    int xMaxValue = info.value(QStringLiteral("xMaxValue")).toInt();
    int yMaxValue = info.value(QStringLiteral("yMaxValue")).toInt();
    if (xMaxValue <= 0 || yMaxValue <= 0)
    {
        if (error) *error = QStringLiteral("The selected heads have no Pan/Tilt range in the current display mode");
        return false;
    }
    if (xMin < 0 || xMax > xMaxValue || xMin >= xMax || yMin < 0 || yMax > yMaxValue || yMin >= yMax)
    {
        if (error) *error = QStringLiteral("Ranges must satisfy 0 <= min < max <= %1 (X) / %2 (Y) in %3")
                                .arg(xMaxValue).arg(yMaxValue)
                                .arg(info.value(QStringLiteral("units")).toString().isEmpty() ? QStringLiteral("DMX") : info.value(QStringLiteral("units")).toString());
        return false;
    }
    pad->setHeadsRange(rows, xMin, xMax, xReverse, yMin, yMax, yReverse);
    return true;
}

int App::vcWidgetPresetMove(quint32 id, int presetId, bool up, QString *error)
{
    VCWidget *w = vcFindWidget(id);
    if (VCXYPad *pad = qobject_cast<VCXYPad *>(w))
        return up ? pad->movePresetUp(quint8(presetId)) : pad->movePresetDown(quint8(presetId));
    if (VCAnimation *anim = qobject_cast<VCAnimation *>(w))
        return up ? anim->movePresetUp(quint8(presetId)) : anim->movePresetDown(quint8(presetId));
    if (error) *error = QStringLiteral("Widget has no reorderable presets");
    return -1;
}

bool App::vcXyPadRenamePreset(quint32 id, int presetId, const QString &name, QString *error)
{
    VCXYPad *pad = qobject_cast<VCXYPad *>(vcFindWidget(id));
    if (pad == nullptr)
    {
        if (error) *error = QStringLiteral("No such widget");
        return false;
    }
    pad->setPresetName(quint8(presetId), name);
    return true;
}

/*****************************************************************************
 * Clock
 *****************************************************************************/

bool App::vcClockPlayPause(quint32 id, QString *error)
{
    VCClock *clock = qobject_cast<VCClock *>(vcFindWidget(id));
    if (clock == nullptr)
    {
        if (error) *error = QStringLiteral("No such widget");
        return false;
    }
    if (clock->clockType() == VCClock::Clock)
    {
        if (error) *error = QStringLiteral("A Clock-type widget has no timer to start (set clockType to Stopwatch or Countdown)");
        return false;
    }
    if (clock->clockType() == VCClock::Countdown && clock->currentTime() <= 0)
    {
        if (error) *error = QStringLiteral("The countdown has reached 0 - reset it (or set a target time) first");
        return false;
    }
    clock->playPauseTimer();
    return true;
}

bool App::vcClockReset(quint32 id, QString *error)
{
    VCClock *clock = qobject_cast<VCClock *>(vcFindWidget(id));
    if (clock == nullptr)
    {
        if (error) *error = QStringLiteral("No such widget");
        return false;
    }
    if (clock->clockType() == VCClock::Clock)
    {
        if (error) *error = QStringLiteral("A Clock-type widget has no timer to reset");
        return false;
    }
    clock->resetTimer();
    return true;
}

bool App::vcClockAddSchedules(quint32 id, const QList<quint32> &functionIds, QString *error)
{
    VCClock *clock = qobject_cast<VCClock *>(vcFindWidget(id));
    if (clock == nullptr)
    {
        if (error) *error = QStringLiteral("No such widget");
        return false;
    }
    QVariantList ids;
    for (quint32 fid : functionIds)
        ids.append(fid);
    int before = clock->schedules().count();
    clock->addSchedules(ids);
    if (clock->schedules().count() == before)
    {
        if (error) *error = QStringLiteral("No schedule was added");
        return false;
    }
    return true;
}

bool App::vcClockUpdateSchedule(quint32 id, int index, const QJsonObject &patch, QString *error)
{
    VCClock *clock = qobject_cast<VCClock *>(vcFindWidget(id));
    if (clock == nullptr)
    {
        if (error) *error = QStringLiteral("No such widget");
        return false;
    }
    QList<VCClockSchedule *> schedules = clock->schedules();
    if (index < 0 || index >= schedules.count())
    {
        if (error) *error = QStringLiteral("No such schedule");
        return false;
    }
    VCClockSchedule *sch = schedules.at(index);
    if (patch.contains(QStringLiteral("startTime")))
        sch->setStartTime(patch.value(QStringLiteral("startTime")).toInt());
    if (patch.contains(QStringLiteral("stopTime")))
        sch->setStopTime(patch.value(QStringLiteral("stopTime")).toInt());
    if (patch.contains(QStringLiteral("weekFlags")))
        sch->setWeekFlags(patch.value(QStringLiteral("weekFlags")).toInt());
    // A re-armed schedule may fire again (VCClock::slotTimerTimeout() clears m_canPlay after a
    // one-shot run) - editing it is the operator's way of asking for exactly that.
    sch->m_canPlay = true;
    return true;
}

bool App::vcClockRemoveSchedule(quint32 id, int index, QString *error)
{
    VCClock *clock = qobject_cast<VCClock *>(vcFindWidget(id));
    if (clock == nullptr)
    {
        if (error) *error = QStringLiteral("No such widget");
        return false;
    }
    // VCClock::removeSchedule() only rejects index > count (off by one) - check the real bound here.
    if (index < 0 || index >= clock->schedules().count())
    {
        if (error) *error = QStringLiteral("No such schedule");
        return false;
    }
    clock->removeSchedule(index);
    return true;
}

/*****************************************************************************
 * Animation
 *****************************************************************************/

bool App::vcAnimationSetFaderLevel(quint32 id, int level, QString *error)
{
    VCAnimation *anim = qobject_cast<VCAnimation *>(vcFindWidget(id));
    if (anim == nullptr)
    {
        if (error) *error = QStringLiteral("No such widget");
        return false;
    }
    if (m_doc->function(anim->functionID()) == nullptr)
    {
        // VCAnimation::setFaderLevel() returns without storing anything when no matrix is attached.
        if (error) *error = QStringLiteral("No RGB Matrix is attached to this Animation widget (VcAnimationConfig.functionID)");
        return false;
    }
    anim->setFaderLevel(qBound(0, level, 255));
    return true;
}

bool App::vcSliderResetOverride(quint32 id, QString *error)
{
    VCSlider *slider = qobject_cast<VCSlider *>(vcFindWidget(id));
    if (slider == nullptr)
    {
        if (error) *error = QStringLiteral("No such widget");
        return false;
    }
    if (slider->sliderMode() != VCSlider::Level || slider->monitorEnabled() == false)
    {
        if (error) *error = QStringLiteral("Only a Level slider with channel monitoring has an override to reset");
        return false;
    }
    // VCSliderItem.qml's red X: back to following the monitored channels
    slider->setIsOverriding(false);
    return true;
}

bool App::vcAnimationSetPresetKnobValue(quint32 id, int presetId, int value, QString *error)
{
    VCAnimation *anim = qobject_cast<VCAnimation *>(vcFindWidget(id));
    if (anim == nullptr)
    {
        if (error) *error = QStringLiteral("No such widget");
        return false;
    }
    bool isKnob = false;
    for (const VCAnimationPreset *preset : anim->presetObjects())
    {
        if (int(preset->m_id) == presetId)
            isKnob = preset->widgetType() == VCAnimationPreset::Knob;
    }
    if (isKnob == false)
    {
        if (error) *error = QStringLiteral("Preset %1 is not a colour knob").arg(presetId);
        return false;
    }
    anim->setPresetKnobValue(quint8(presetId), qBound(0, value, 255));
    // The knob has no signal of its own (the colour it changes travels on vc.animation.styleChanged);
    // report the turn as the spec's activePresetChanged with the knob's id and value.
    if (m_vcLiveListenerExt != nullptr)
        m_vcLiveListenerExt->vcAnimationActivePresetChanged(anim->id(), anim->activePresetId(), presetId, qBound(0, value, 255));
    return true;
}

/*****************************************************************************
 * Audio triggers
 *****************************************************************************/

bool App::vcAudioTriggersSetCaptureEnabled(quint32 id, bool enabled, QString *error)
{
    VCAudioTriggers *at = qobject_cast<VCAudioTriggers *>(vcFindWidget(id));
    if (at == nullptr)
    {
        if (error) *error = QStringLiteral("No such widget");
        return false;
    }
    at->setCaptureEnabled(enabled);
    return true;
}

bool App::vcAudioTriggersSetBarConfig(quint32 id, int index, const QJsonObject &patch, QString *error)
{
    VCAudioTriggers *at = qobject_cast<VCAudioTriggers *>(vcFindWidget(id));
    if (at == nullptr)
    {
        if (error) *error = QStringLiteral("No such widget");
        return false;
    }
    const VCAudioTriggers::AudioBar *bar = at->barAt(index);
    if (bar == nullptr)
    {
        if (error) *error = QStringLiteral("No such bar");
        return false;
    }

    // VCAudioTriggers' per-bar setters work on its "selected bar" (also the property panel's editing
    // selection): point it at $index for the duration and restore it afterwards.
    int previousSelection = at->selectedBar();
    at->setSelectedBar(index);

    if (patch.contains(QStringLiteral("type")))
    {
        VCAudioTriggers::BarType type = barTypeFromName(patch.value(QStringLiteral("type")).toString());
        // setBarType() resets thresholds / function / widget / channels - only when the type changes.
        if (type != bar->m_type)
            at->setBarType(type);
    }
    // Thresholds: the wire is 0..255 (the stored value), the engine setter takes whole percents -
    // convert, keeping the other one when only one is given.
    if (patch.contains(QStringLiteral("minThreshold")) || patch.contains(QStringLiteral("maxThreshold")))
    {
        int lo = patch.contains(QStringLiteral("minThreshold")) ? patch.value(QStringLiteral("minThreshold")).toInt() : int(bar->m_minThreshold);
        int hi = patch.contains(QStringLiteral("maxThreshold")) ? patch.value(QStringLiteral("maxThreshold")).toInt() : int(bar->m_maxThreshold);
        at->setBarThresholds(uchar(qRound(lo * 100.0 / 255.0)), uchar(qRound(hi * 100.0 / 255.0)));
    }
    if (patch.contains(QStringLiteral("functionId")))
        at->setBarFunction(patch.value(QStringLiteral("functionId")).toString().toUInt());
    if (patch.contains(QStringLiteral("triggeredWidgetId")))
        at->setBarWidget(patch.value(QStringLiteral("triggeredWidgetId")).toString().toUInt());
    if (patch.contains(QStringLiteral("dmxChannels")))
    {
        QList<SceneValue> channels;
        for (const QJsonValue &v : patch.value(QStringLiteral("dmxChannels")).toArray())
        {
            QJsonObject c = v.toObject();
            channels.append(SceneValue(c.value(QStringLiteral("fixtureId")).toString().toUInt(),
                                       quint32(c.value(QStringLiteral("channel")).toInt()), 0));
        }
        at->setBarDmxChannels(channels);
    }

    at->setSelectedBar(previousSelection);
    return true;
}
