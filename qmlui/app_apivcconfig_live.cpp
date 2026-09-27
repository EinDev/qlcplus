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
 * vc.widget.setConfig / typeConfig for XYPad, Clock, Animation and
 * AudioTriggers - see app_apivcconfig.h for the contract. Not implemented
 * yet: every function below reports "not yet supported" so
 * vc.widget.setConfig keeps failing loudly for these types until their slice
 * lands.
 */

#include "app_apivcconfig.h"
#include "virtualconsole/vcwidget.h"

namespace
{
bool notSupported(VCWidget *w, QString *error)
{
    if (error)
        *error = QStringLiteral("vc.widget.setConfig is not yet supported for widget type '%1'")
                     .arg(w ? w->typeToString(w->type()) : QStringLiteral("?"));
    return false;
}
}

namespace ApiVcConfig
{

QJsonObject xyPadConfigToJson(VCWidget *) { return QJsonObject(); }
bool applyXyPadConfig(VCWidget *w, const QJsonObject &, QString *error) { return notSupported(w, error); }

QJsonObject clockConfigToJson(VCWidget *) { return QJsonObject(); }
bool applyClockConfig(VCWidget *w, const QJsonObject &, QString *error) { return notSupported(w, error); }

QJsonObject animationConfigToJson(VCWidget *) { return QJsonObject(); }
bool applyAnimationConfig(VCWidget *w, const QJsonObject &, QString *error) { return notSupported(w, error); }

QJsonObject audioTriggersConfigToJson(VCWidget *) { return QJsonObject(); }
bool applyAudioTriggersConfig(VCWidget *w, const QJsonObject &, QString *error) { return notSupported(w, error); }

// --- Presets (see app_apivcconfig.h) - the generic vc.widget.preset.* plumbing in App/ApiVcDomain is
// in place and dispatches here; XYPad (VCXYPad::addPositionPreset/addFunctionPreset/
// addFixtureGroupPreset/addFixtureGroupHeadPreset/removePreset/applyPreset/presetsList) and Animation
// (VCAnimation::addColorPreset/addColorKnobsPreset/addTextPreset/addAlgorithmPreset/removePreset/
// applyPreset/presetsList) still have to be shaped onto the spec's VcXyPadPreset(Data) /
// VcAnimationPreset(Data) here. Until then: no presets listed, every mutation refused with a clear error.

namespace
{
bool presetsNotSupported(VCWidget *w, QString *error)
{
    if (error)
        *error = QStringLiteral("vc.widget.preset.* is not yet supported for widget type '%1'")
                     .arg(w ? w->typeToString(w->type()) : QStringLiteral("?"));
    return false;
}
}

QJsonArray xyPadPresetsToJson(VCWidget *) { return QJsonArray(); }
int addXyPadPreset(VCWidget *w, const QJsonObject &, QString *error) { presetsNotSupported(w, error); return -1; }
bool removeXyPadPreset(VCWidget *w, int, QString *error) { return presetsNotSupported(w, error); }
bool applyXyPadPreset(VCWidget *w, int, QString *error) { return presetsNotSupported(w, error); }

QJsonArray animationPresetsToJson(VCWidget *) { return QJsonArray(); }
int addAnimationPreset(VCWidget *w, const QJsonObject &, QString *error) { presetsNotSupported(w, error); return -1; }
bool removeAnimationPreset(VCWidget *w, int, QString *error) { return presetsNotSupported(w, error); }
bool applyAnimationPreset(VCWidget *w, int, QString *error) { return presetsNotSupported(w, error); }

}
