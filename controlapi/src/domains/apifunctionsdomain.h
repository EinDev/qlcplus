/*
  Q Light Controller Plus - Control API
  apifunctionsdomain.h

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

#ifndef APIFUNCTIONSDOMAIN_H
#define APIFUNCTIONSDOMAIN_H

#include <QObject>

class ApiServer;
class Doc;

/**
 * docs/api-spec/fragments/functions-core.yaml: functions.start/stop/setPause
 * (§4b live/runtime - no baseRevision, no broadcast event, matching a real
 * console: pressing a VC button doesn't itself notify other consoles beyond
 * whatever live state they already observe, e.g. DMX output), plus the
 * generic structural (§4a) CRUD shared by all 10 Function types
 * (functions.list/get/create/delete/rename/move/update), Scene-specific
 * value/membership editing (functions.scene.setValues/setValue/unsetValue/
 * setMembers), and the Chaser/Sequence step CRUD they share
 * (functions.steps.addStep/replaceStep/removeStep/moveStep).
 *
 * functions.get's typeDetail is fully implemented for Scene/Chaser/Sequence
 * only; the other 7 types (EFX/Collection/Script/RGBMatrix/Show/Audio/Video)
 * get a minimal {functionId} placeholder for now - their full detail shapes
 * (FunctionsEfxDetail etc.) are functions-advanced.yaml territory, a
 * deliberately separate future slice, not an oversight.
 */
class ApiFunctionsDomain : public QObject
{
    Q_OBJECT

public:
    ApiFunctionsDomain(Doc *doc, ApiServer *server, QObject *parent = nullptr);

private:
    void registerMethods();

private:
    Doc *m_doc;
    ApiServer *m_server;
};

#endif
