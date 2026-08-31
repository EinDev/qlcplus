/*
  Q Light Controller Plus - Control API
  apifixturegroupdomain.h

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

#ifndef APIFIXTUREGROUPDOMAIN_H
#define APIFIXTUREGROUPDOMAIN_H

#include <QObject>
#include <QString>

class ApiServer;
class Doc;

/**
 * Implementation of the "fixtures.group.*" methods (see
 * docs/api-spec/fragments/fixtures.yaml) - CRUD plus grid-layout mutators
 * for FixtureGroup (engine/src/fixturegroup.h), a named 2D layout grid of
 * fixture heads. Same constructor-injected-Doc*, register-into-dispatcher
 * pattern as ApiIoDomain/ApiCoreDomain.
 *
 * Structural (00-conventions.md §4a): every mutation requires baseRevision
 * and results in Doc::docRevision() being bumped - via
 * FixtureGroup::changed() -> Doc::slotFixtureGroupChanged() -> Doc::
 * setModified() for rename/setSize/assignFixture/assignHead/unassignHead/
 * unassignFixture/swapHeads/reset, or directly via Doc::addFixtureGroup()/
 * deleteFixtureGroup() for create/delete.
 *
 * Event broadcasting is deliberately split two ways:
 *
 * - create/delete build and broadcast their own event explicitly, inline in
 *   the request handler. Doc::addFixtureGroup()/deleteFixtureGroup() (see
 *   doc.cpp) emit their fixtureGroupAdded/fixtureGroupRemoved signal BEFORE
 *   calling setModified()/bumpRevision() - a signal-driven broadcast from
 *   those would read a stale docRevision, so the handler builds the event
 *   itself right after the call returns instead.
 *
 * - rename/setSize/assignFixture/assignHead/unassignHead/unassignFixture/
 *   swapHeads/reset instead broadcast from a connection to Doc::
 *   fixtureGroupChanged (whose emit order in Doc::slotFixtureGroupChanged
 *   IS setModified() then emit, so docRevision() is already current there).
 *   This is signal-driven rather than built inline per-handler so that a
 *   fixture group changed some other way while this server is running -
 *   qmlui's own Fixture Group Editor, or the fixtures.unpatch cascade that
 *   deletes/empties a group as a side effect of removing its last fixture -
 *   still reaches every connected client, exactly why ApiIoDomain listens
 *   to InputOutputMap's real signals instead of only broadcasting from its
 *   own request handlers. rename's handler also triggers this same
 *   Doc::fixtureGroupChanged (FixtureGroup::setName() emits the same
 *   generic changed() signal every other mutator does) - a pending-kind
 *   stash (m_pendingChangeKind) tells the slot which of the two possible
 *   wire topics ("fixtures.group.renamed" vs the catch-all
 *   "fixtures.group.updated") to use for the change it's about to be
 *   synchronously notified of, mirroring ApiIoDomain's own
 *   m_pendingOriginClientId/m_hasPendingUniverseName "stash before the call,
 *   read inside the synchronous slot, clear right after" idiom.
 */
class ApiFixtureGroupDomain : public QObject
{
    Q_OBJECT

public:
    ApiFixtureGroupDomain(Doc *doc, ApiServer *server, QObject *parent = nullptr);

private:
    void registerMethods();

private slots:
    void slotFixtureGroupChanged(quint32 id);

private:
    Doc *m_doc;
    ApiServer *m_server;

    enum ChangeKind { ChangeUpdated, ChangeRenamed };

    /** Which wire event slotFixtureGroupChanged() should broadcast for the
     *  FixtureGroup::changed() it's about to be synchronously notified of -
     *  see this class's own doc comment above. Defaults to ChangeUpdated so
     *  a change from any mutator that doesn't touch this (or a change from
     *  outside this domain entirely, e.g. qmlui's Fixture Group Editor)
     *  gets the catch-all topic without every handler having to set it. */
    ChangeKind m_pendingChangeKind = ChangeUpdated;

    /** fixtures.group.renamed's payload needs the new name alongside
     *  groupId/docRevision - simplest source is the rename handler that
     *  already has it, rather than re-deriving "what changed" from the
     *  group's post-change state inside the slot. */
    QString m_pendingRenamedName;

    /** Requesting client's id, stashed the same way as ApiIoDomain's
     *  m_pendingOriginClientId - every mutator handler below sets this
     *  immediately before its FixtureGroup/Doc call and unconditionally
     *  clears it right after, since a couple of these calls are no-ops that
     *  never signal (e.g. FixtureGroup::setName() to the same name,
     *  FixtureGroup::resignHead() on an already-empty cell) - a stale id
     *  left behind could otherwise get attributed to a later, unrelated
     *  change. */
    QString m_pendingOriginClientId;
};

#endif
