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
 * First slice of docs/api-spec/fragments/functions-core.yaml: direct
 * start/stop/pause of an already-authored Function by ID
 * (functions.start/functions.stop/functions.setPause,
 * functions-core.yaml:551-648), so a client can trigger an existing
 * preset/Scene without needing a Virtual Console widget. Structural
 * authoring of Functions (functions.create/update/delete, the ~250-message
 * rest of this domain) is a separate, much larger future slice -
 * deliberately not part of this class.
 *
 * §4b live/runtime action: no baseRevision, no broadcast event - matches a
 * real console (pressing a VC button doesn't itself notify other consoles
 * beyond whatever live state they already observe, e.g. DMX output).
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
