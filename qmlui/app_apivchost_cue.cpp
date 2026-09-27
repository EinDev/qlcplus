/*
  Q Light Controller Plus
  app_apivchost_cue.cpp

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
 * App's ApiVcHost implementation, part two: the Cue List side fader, the Speed Dial extras
 * (factor / apply / reset tap) and the generic widget-preset methods (vc.widget.preset.add/apply/
 * remove, vc.speedDial.preset.update). Split out of app_apivchost.cpp so this slice can evolve
 * without touching the Button/Slider/page/geometry code there; the per-widget-type preset shaping
 * it dispatches into lives in app_apivcconfig_{cue,live}.cpp (see app_apivcconfig.h).
 *
 * The matching change signals (VCCueList::sideFaderLevelChanged, VCSpeedDial::currentFactorChanged /
 * tapTimeValueChanged) are relayed to the ApiVcLiveListener from App::slotVcWidgetRegistered() in
 * app_apivchost.cpp, next to the other widgets' relays.
 */

#include <QJsonArray>
#include <QJsonObject>

#include "app.h"
#include "app_apivcconfig.h"
#include "virtualconsole/vcwidget.h"
#include "virtualconsole/vccuelist.h"
#include "virtualconsole/vcspeeddial.h"

/*****************************************************************************
 * Cue list side fader / speed dial extras
 *****************************************************************************/

bool App::vcCueListSetSideFaderLevel(quint32 id, int level, QString *error)
{
    VCCueList *cl = qobject_cast<VCCueList *>(vcFindWidget(id));
    if (cl == nullptr)
    {
        if (error) *error = QStringLiteral("No such widget");
        return false;
    }
    if (cl->sideFaderMode() == VCCueList::None)
    {
        // VCCueListItem.qml hides the whole side fader column in this mode.
        if (error) *error = QStringLiteral("Side fader mode is None");
        return false;
    }

    // Same confinement as the on-screen QLCPlusFader (`to: Crossfade ? 100 : 255`).
    int hi = cl->sideFaderMode() == VCCueList::Crossfade ? 100 : 255;
    cl->setSideFaderLevel(qBound(0, level, hi));
    return true;
}

bool App::vcSpeedDialSetFactor(quint32 id, const QString &factor, QString *error)
{
    VCSpeedDial *sd = qobject_cast<VCSpeedDial *>(vcFindWidget(id));
    if (sd == nullptr)
    {
        if (error) *error = QStringLiteral("No such widget");
        return false;
    }
    int value = ApiVcConfig::speedDialMultiplierFromName(factor);
    if (value < int(VCSpeedDial::OneSixteenth) || value > int(VCSpeedDial::Sixteen))
    {
        if (error) *error = QStringLiteral("Invalid factor '%1'").arg(factor);
        return false;
    }
    sd->setCurrentFactor(VCSpeedDial::SpeedMultiplier(value)); // same as the 1/16..16 buttons in VCSpeedDialItem.qml
    return true;
}

bool App::vcSpeedDialApply(quint32 id, QString *error)
{
    VCSpeedDial *sd = qobject_cast<VCSpeedDial *>(vcFindWidget(id));
    if (sd == nullptr)
    {
        if (error) *error = QStringLiteral("No such widget");
        return false;
    }
    sd->applyFunctionsTime(true); // the "Apply" button: enqueue=true so undo/peers see it
    return true;
}

bool App::vcSpeedDialResetTap(quint32 id, QString *error)
{
    VCSpeedDial *sd = qobject_cast<VCSpeedDial *>(vcFindWidget(id));
    if (sd == nullptr)
    {
        if (error) *error = QStringLiteral("No such widget");
        return false;
    }
    sd->resetTap(); // right-click on the TAP button
    return true;
}

/*****************************************************************************
 * Widget presets
 *****************************************************************************/

bool App::vcWidgetSupportsPresets(quint32 id) const
{
    VCWidget *w = vcFindWidget(id);
    return w != nullptr && w->supportsPresets();
}

QJsonArray App::vcWidgetPresets(quint32 id) const
{
    VCWidget *w = vcFindWidget(id);
    if (w == nullptr)
        return QJsonArray();
    switch (w->type())
    {
        case VCWidget::SpeedWidget:     return ApiVcConfig::speedDialPresetsToJson(w);
        case VCWidget::XYPadWidget:     return ApiVcConfig::xyPadPresetsToJson(w);
        case VCWidget::AnimationWidget: return ApiVcConfig::animationPresetsToJson(w);
        default:                        return QJsonArray();
    }
}

int App::vcWidgetPresetAdd(quint32 id, const QJsonObject &preset, QString *error)
{
    VCWidget *w = vcFindWidget(id);
    if (w == nullptr)
    {
        if (error) *error = QStringLiteral("No such widget");
        return -1;
    }
    switch (w->type())
    {
        case VCWidget::SpeedWidget:     return ApiVcConfig::addSpeedDialPreset(w, preset, error);
        case VCWidget::XYPadWidget:     return ApiVcConfig::addXyPadPreset(w, preset, error);
        case VCWidget::AnimationWidget: return ApiVcConfig::addAnimationPreset(w, preset, error);
        default: break;
    }
    if (error) *error = QStringLiteral("Widget type has no presets");
    return -1;
}

bool App::vcWidgetPresetRemove(quint32 id, int presetId, QString *error)
{
    VCWidget *w = vcFindWidget(id);
    if (w == nullptr)
    {
        if (error) *error = QStringLiteral("No such widget");
        return false;
    }
    switch (w->type())
    {
        case VCWidget::SpeedWidget:     return ApiVcConfig::removeSpeedDialPreset(w, presetId, error);
        case VCWidget::XYPadWidget:     return ApiVcConfig::removeXyPadPreset(w, presetId, error);
        case VCWidget::AnimationWidget: return ApiVcConfig::removeAnimationPreset(w, presetId, error);
        default: break;
    }
    if (error) *error = QStringLiteral("Widget type has no presets");
    return false;
}

bool App::vcWidgetPresetApply(quint32 id, int presetId, QString *error)
{
    VCWidget *w = vcFindWidget(id);
    if (w == nullptr)
    {
        if (error) *error = QStringLiteral("No such widget");
        return false;
    }
    switch (w->type())
    {
        case VCWidget::SpeedWidget:     return ApiVcConfig::applySpeedDialPreset(w, presetId, error);
        case VCWidget::XYPadWidget:     return ApiVcConfig::applyXyPadPreset(w, presetId, error);
        case VCWidget::AnimationWidget: return ApiVcConfig::applyAnimationPreset(w, presetId, error);
        default: break;
    }
    if (error) *error = QStringLiteral("Widget type has no presets");
    return false;
}

bool App::vcSpeedDialPresetUpdate(quint32 id, int presetId, const QJsonObject &patch, QString *error)
{
    VCWidget *w = vcFindWidget(id);
    if (w == nullptr || w->type() != VCWidget::SpeedWidget)
    {
        if (error) *error = QStringLiteral("No such Speed widget");
        return false;
    }
    return ApiVcConfig::updateSpeedDialPreset(w, presetId, patch, error);
}
