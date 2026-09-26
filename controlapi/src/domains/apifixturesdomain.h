/*
  Q Light Controller Plus - Control API
  apifixturesdomain.h

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

#ifndef APIFIXTURESDOMAIN_H
#define APIFIXTURESDOMAIN_H

#include <QObject>

class ApiServer;
class Doc;

/**
 * Fixture-patching slice of docs/api-spec/fragments/fixtures.yaml - section
 * "2. Patching": fixtures.list/get/patch/update/unpatch, plus the advisory
 * fixtures.findAvailableAddress helper - and the read-only section "1.
 * Fixture definition library browsing" (fixtures.defs.listManufacturers/
 * listModels/getModel/getMode over the Doc's QLCFixtureDefCache, which is
 * what the Add Fixture dialog browses). Fixture groups (fixtures.group.*,
 * section 3) live in ApiFixtureGroupDomain; fixture remapping
 * (fixtures.remap.*, section 4) is a separate, larger slice and is
 * deliberately NOT implemented here.
 *
 * All patching methods here are §4a structural mutations (baseRevision in,
 * docRevision out + broadcast to every client, see 00-conventions.md).
 * Unlike ApiIoDomain (which has to react to InputOutputMap/Universe signals
 * fired asynchronously relative to the request that caused them), every
 * mutation this class makes is fully synchronous and directly driven by its
 * own Doc::addFixture()/deleteFixture()/Fixture setter calls - so building
 * the broadcast event's data right after the mutating call succeeds, using
 * the requesting session's own clientId as originClientId directly, is
 * sufficient. This is the same direct-broadcast pattern
 * ApiIoDomain::registerMethods() already uses for io.simpleDesk.dump's
 * functions.created/functions.updated events (apiiodomain.cpp) - no
 * equivalent of ApiIoDomain's m_pendingOriginClientId indirection is needed
 * here.
 */
class ApiFixturesDomain : public QObject
{
    Q_OBJECT

public:
    ApiFixturesDomain(Doc *doc, ApiServer *server, QObject *parent = nullptr);

private:
    void registerMethods();

private:
    Doc *m_doc;
    ApiServer *m_server;
};

#endif
