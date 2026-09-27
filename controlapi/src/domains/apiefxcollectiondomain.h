/*
  Q Light Controller Plus - Control API
  apiefxcollectiondomain.h

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

#ifndef APIEFXCOLLECTIONDOMAIN_H
#define APIEFXCOLLECTIONDOMAIN_H

#include <QObject>
#include <QString>

class ApiServer;
class ApiSession;
class Collection;
class Doc;
class EFX;

/**
 * functions.efx.* and functions.collection.* (docs/api-spec/fragments/
 * functions-core.yaml): the type-specific editing surface of the two Function
 * types the generic ApiFunctionsDomain leaves alone, plus their functions.get
 * typeDetail builders (registered through
 * ApiFunctionsDomain::setTypeDetailProvider() from the constructor, so
 * apifunctionsdomain.cpp never needs to know about this file).
 *
 * All mutations are §4a document edits: baseRevision checked first, then the
 * engine call, then - because the engine is uneven about it (Collection::
 * add/removeFunction and most EFX setters emit changed() -> Doc::setModified()
 * -> docRevision bump on their own, EFXFixture's setters and
 * EFX::removeFixture(id, head) do not) - the revision is compared with the
 * value before the call and Doc::setModified() is called by hand if nothing
 * bumped it. A mutation therefore always returns a fresh docRevision, never
 * the one the client sent in.
 *
 * Events: functions.efx.changed (full parameter set), functions.efx.
 * fixturesChanged (full participant list) and functions.collection.
 * membersChanged (full member list) - all ungated, all small.
 *
 * functions.efx.getPreview is read-only and mirrors what qmlui's
 * EFXEditor::updateAlgorithmData() feeds EFXPreview.qml: the 512-point
 * pattern polygon (EFX::preview) plus, per participant, the index of the
 * pattern point it starts from and the direction it walks in.
 */
class ApiEfxCollectionDomain : public QObject
{
    Q_OBJECT

public:
    ApiEfxCollectionDomain(Doc *doc, ApiServer *server, QObject *parent = nullptr);

private:
    void registerMethods();
    void registerCollectionMethods();
    void registerEfxMethods();

    /** The three change broadcasts, each carrying exactly the shape the
     *  matching functions.get typeDetail slice has. */
    void broadcastEfxChanged(EFX *efx, const QString &originClientId);
    void broadcastEfxFixturesChanged(EFX *efx, const QString &originClientId);
    void broadcastCollectionMembersChanged(Collection *collection, const QString &originClientId);

private:
    Doc *m_doc;
    ApiServer *m_server;
};

#endif
