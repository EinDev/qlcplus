/*
  Q Light Controller Plus - Control API
  apifixtureremapdomain.h

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

#ifndef APIFIXTUREREMAPDOMAIN_H
#define APIFIXTUREREMAPDOMAIN_H

#include <QObject>

class ApiServer;
class Doc;

/**
 * Implementation of docs/api-spec/fragments/fixtures.yaml section 4,
 * "Fixture remapping": fixtures.remap.suggestChannelMap (read-only channel
 * matching against a not-yet-created target definition) and
 * fixtures.remap.apply (the one-shot batch commit).
 *
 * apply is FixtureRemapper::applyRemap() (engine/src/fixtureremapper.cpp)
 * driven headlessly. Two things the QML FixtureRemapManager gets from its
 * staging Doc have to be rebuilt here explicitly:
 *
 * - Doc::replaceFixtures() deletes EVERY fixture and recreates only the
 *   list it is handed, and FixtureRemapper::remapSceneValues() drops every
 *   value whose channel has no mapping. So every source fixture the request
 *   leaves untouched is cloned into the target list under its own id with an
 *   identity channel map, or Scenes / groups / channel groups would silently
 *   lose it (the QML tool's "clone" button does this per fixture by hand).
 * - Fixture ids: replaceFixtures() takes the id from each passed object, so
 *   untouched clones keep theirs and new targets get fresh ids above the
 *   current maximum.
 *
 * Virtual Console widgets are remapped through ApiVcHost::vcRemapChannels()
 * (qmlui's App), since VC classes are UI-layer objects the engine's remapper
 * deliberately does not touch. Structural (§4a): baseRevision in, one
 * Doc::setModified() after the whole commit, fixtures.remap.applied +
 * fixtures.monitor.changed out.
 */
class ApiFixtureRemapDomain : public QObject
{
    Q_OBJECT

public:
    ApiFixtureRemapDomain(Doc *doc, ApiServer *server, QObject *parent = nullptr);

private:
    void registerMethods();

private:
    Doc *m_doc;
    ApiServer *m_server;
};

#endif
