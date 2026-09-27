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

#include <QJsonObject>
#include <QString>

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
 *   app_apivcconfig_layout.cpp - Frame/SoloFrame, Label, CueList, SpeedDial
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

    QJsonObject cueListConfigToJson(VCWidget *w);
    bool applyCueListConfig(VCWidget *w, const QJsonObject &patch, QString *error);

    QJsonObject speedDialConfigToJson(VCWidget *w);
    bool applySpeedDialConfig(VCWidget *w, const QJsonObject &patch, QString *error);

    // ---- app_apivcconfig_live.cpp ----
    QJsonObject xyPadConfigToJson(VCWidget *w);
    bool applyXyPadConfig(VCWidget *w, const QJsonObject &patch, QString *error);

    QJsonObject clockConfigToJson(VCWidget *w);
    bool applyClockConfig(VCWidget *w, const QJsonObject &patch, QString *error);

    QJsonObject animationConfigToJson(VCWidget *w);
    bool applyAnimationConfig(VCWidget *w, const QJsonObject &patch, QString *error);

    QJsonObject audioTriggersConfigToJson(VCWidget *w);
    bool applyAudioTriggersConfig(VCWidget *w, const QJsonObject &patch, QString *error);
}

#endif
