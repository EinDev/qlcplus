/*
  Q Light Controller Plus
  app_apivcconfig.h

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

#ifndef APP_APIVCCONFIG_H
#define APP_APIVCCONFIG_H

#include <QJsonArray>
#include <QJsonObject>
#include <QString>
#include <QStringList>

class VCWidget;
class Doc;

/**
 * Per-widget-type halves of App's ApiVcHost implementation (app_apivchost.cpp):
 * vc.widget.get/list's `typeConfig` snapshot and vc.widget.setConfig's patch
 * application, for every widget type other than VCButton/VCSlider (those two
 * stay in app_apivchost.cpp itself).
 *
 * Split into two translation units by ownership so the widget types can be
 * implemented independently without touching one another's file:
 *   app_apivcconfig_layout.cpp - Frame/SoloFrame, Label
 *   app_apivcconfig_cue.cpp    - CueList, SpeedDial
 *   app_apivcconfig_live.cpp   - XYPad, Clock, Animation, AudioTriggers
 *
 * Contract (same as applyButtonConfig/applySliderConfig in app_apivchost.cpp):
 *  - `<type>ConfigToJson(w)` returns the full type-specific config object, the
 *    shape docs/api-spec/fragments/virtualconsole.yaml gives for that widget's
 *    Vc<Type>Config schema. An empty object means "no config exposed yet".
 *  - `apply<Type>Config(w, patch, error)` applies a *partial* patch (only the
 *    keys present), validates every value, rejects unknown keys with an
 *    explanatory `error`, and returns false without applying anything when
 *    any key is invalid. Returns true after applying. Callers broadcast
 *    vc.widget.configChanged themselves.
 *  - Every function is handed the VCWidget* already checked to be of the
 *    matching type; casting with qobject_cast<VC...*>() is safe.
 */
namespace ApiVcConfig
{
    // ---- app_apivcconfig_layout.cpp ----
    QJsonObject frameConfigToJson(VCWidget *w);      // Frame and SoloFrame
    bool applyFrameConfig(VCWidget *w, const QJsonObject &patch, QString *error);

    QJsonObject labelConfigToJson(VCWidget *w);
    bool applyLabelConfig(VCWidget *w, const QJsonObject &patch, QString *error);

    // ---- app_apivcconfig_cue.cpp ----
    // These two take the Doc as well: attaching a Chaser / listing controlled Functions has to check
    // the Function ids exist (and are Chasers) before touching the widget, since VCCueList::
    // setChaserID() silently detaches on an unknown id.
    QJsonObject cueListConfigToJson(VCWidget *w);
    bool applyCueListConfig(VCWidget *w, Doc *doc, const QJsonObject &patch, QString *error);

    QJsonObject speedDialConfigToJson(VCWidget *w);
    bool applySpeedDialConfig(VCWidget *w, Doc *doc, const QJsonObject &patch, QString *error);

    // ---- app_apivcconfig_live.cpp ----
    QJsonObject xyPadConfigToJson(VCWidget *w);
    bool applyXyPadConfig(VCWidget *w, const QJsonObject &patch, QString *error);

    QJsonObject clockConfigToJson(VCWidget *w);
    bool applyClockConfig(VCWidget *w, const QJsonObject &patch, QString *error);

    QJsonObject animationConfigToJson(VCWidget *w);
    bool applyAnimationConfig(VCWidget *w, const QJsonObject &patch, QString *error);

    QJsonObject audioTriggersConfigToJson(VCWidget *w);
    bool applyAudioTriggersConfig(VCWidget *w, const QJsonObject &patch, QString *error);

    /**
     * Widget presets (vc.widget.preset.add/apply/remove, vc.speedDial.preset.update) for the three
     * preset-capable widget types (VCWidget::supportsPresets(): Speed, XYPad, Animation). App's
     * vcWidgetPreset*() (app_apivchost_cue.cpp) dispatches on the widget type into these:
     *  - `<type>PresetsToJson(w)` - the widget's full preset list in display order, one object per
     *    preset in the spec's Vc<Type>Preset shape (always carrying an integer `presetId`).
     *  - `add<Type>Preset(w, data, error)` - validates `data` against Vc<Type>PresetData, adds the
     *    preset and returns its new id, or -1 with `error` set (nothing added).
     *  - `remove<Type>Preset(w, presetId, error)` / `apply<Type>Preset(w, presetId, error)` - the
     *    preset is known to exist; apply is the live activation (Speed: currentTime = preset value,
     *    XYPad/Animation: their applyPreset()).
     * Same file ownership as the config functions above: Speed in app_apivcconfig_cue.cpp, XYPad and
     * Animation in app_apivcconfig_live.cpp.
     */
    // ---- app_apivcconfig_cue.cpp ----
    QJsonArray speedDialPresetsToJson(VCWidget *w);
    int addSpeedDialPreset(VCWidget *w, const QJsonObject &data, QString *error);
    bool removeSpeedDialPreset(VCWidget *w, int presetId, QString *error);
    bool applySpeedDialPreset(VCWidget *w, int presetId, QString *error);
    /** vc.speedDial.preset.update: applies whichever of "name" / "valueMs" are in $patch. */
    bool updateSpeedDialPreset(VCWidget *w, int presetId, const QJsonObject &patch, QString *error);

    /** VcSpeedDialMultiplier wire spelling <-> VCSpeedDial::SpeedMultiplier (as int); the name lookup
     *  returns -1 for an unknown spelling. Shared by the live factor relay/setter in app_apivchost*.cpp. */
    QString speedDialMultiplierName(int factor);
    int speedDialMultiplierFromName(const QString &name);

    // ---- app_apivcconfig_live.cpp ----
    QJsonArray xyPadPresetsToJson(VCWidget *w);
    int addXyPadPreset(VCWidget *w, const QJsonObject &data, QString *error);
    bool removeXyPadPreset(VCWidget *w, int presetId, QString *error);
    bool applyXyPadPreset(VCWidget *w, int presetId, QString *error);

    QJsonArray animationPresetsToJson(VCWidget *w);
    int addAnimationPreset(VCWidget *w, const QJsonObject &data, QString *error);
    bool removeAnimationPreset(VCWidget *w, int presetId, QString *error);
    bool applyAnimationPreset(VCWidget *w, int presetId, QString *error);

    /**
     * Live-state seeds of the XY Pad / Clock / Animation / Audio Triggers slice, added to
     * vc.widget.get/list's VcWidgetSummary next to the fields appendLiveStateToJson() in
     * app_apivchost.cpp already writes: XYPad floorPosition + activePresetId, Clock currentTime +
     * running, Animation faderLevel + activePresetId, AudioTriggers captureEnabled + levels. No-op
     * for every other widget type. (app_apivcconfig_live.cpp)
     */
    void appendLiveSeed(VCWidget *w, QJsonObject &obj);

    /** The animation's colour slots as "#rrggbb" strings ("" for an unset slot), in slot order -
     *  shared by animationConfigToJson() and the vc.animation.styleChanged relay. */
    QStringList animationColorStrings(VCWidget *w);
}

#endif
