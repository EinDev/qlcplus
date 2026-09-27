/*
  Q Light Controller Plus
  app_apivcconfig_layout.cpp

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
 * vc.widget.setConfig / typeConfig for Frame, SoloFrame, Label, CueList and
 * SpeedDial - see app_apivcconfig.h for the contract. Not implemented yet:
 * every function below reports "not yet supported" so vc.widget.setConfig
 * keeps failing loudly for these types until their slice lands.
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

QJsonObject frameConfigToJson(VCWidget *) { return QJsonObject(); }
bool applyFrameConfig(VCWidget *w, const QJsonObject &, QString *error) { return notSupported(w, error); }

QJsonObject labelConfigToJson(VCWidget *) { return QJsonObject(); }
bool applyLabelConfig(VCWidget *w, const QJsonObject &, QString *error) { return notSupported(w, error); }

QJsonObject cueListConfigToJson(VCWidget *) { return QJsonObject(); }
bool applyCueListConfig(VCWidget *w, const QJsonObject &, QString *error) { return notSupported(w, error); }

QJsonObject speedDialConfigToJson(VCWidget *) { return QJsonObject(); }
bool applySpeedDialConfig(VCWidget *w, const QJsonObject &, QString *error) { return notSupported(w, error); }

}
