/*
  Q Light Controller Plus - Control API
  apiiodomain.h

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

#ifndef APIIODOMAIN_H
#define APIIODOMAIN_H

#include <QHash>
#include <QObject>

class ApiServer;
class Doc;
class Universe;

/**
 * First real vertical slice of the control API (see docs/api-spec/fragments/io.yaml
 * and the feature's plan doc): universes (§4a structural), Grand Master and
 * Blackout (§4b live), and a subscribe-gated live DMX event (§4b, §5).
 * Registers its methods into the ApiServer's ApiDispatcher and connects to
 * InputOutputMap/Universe signals to broadcast events - the same
 * constructor-injected-Doc*, connect-to-existing-signals pattern every
 * qmlui manager class already uses (e.g. qmlui/fixturemanager.cpp,
 * qmlui/simpledesk.cpp).
 */
class ApiIoDomain : public QObject
{
    Q_OBJECT

public:
    ApiIoDomain(Doc *doc, ApiServer *server, QObject *parent = nullptr);

private:
    void registerMethods();
    void watchUniverse(Universe *universe);

private slots:
    void slotUniverseAdded(quint32 id);
    void slotUniverseWritten(quint32 id, const QByteArray &postGMValues);
    void slotGrandMasterValueChanged(uchar value);
    void slotBlackoutChanged(bool blackout);

private:
    Doc *m_doc;
    ApiServer *m_server;

    /** Last-broadcast DMX snapshot per universe, so live events can be
     *  delta-only (docs/api-spec/fragments/io.yaml's io.dmx.universe.*.changed) */
    QHash<quint32, QByteArray> m_lastUniverseSnapshot;

    /** io.universe.create's optional "name" param, stashed here just before
     *  calling InputOutputMap::addUniverse() so slotUniverseAdded() - invoked
     *  synchronously from within that call, see its own comment - can apply
     *  it to the new Universe before broadcasting io.universe.created, so
     *  the event (and every response built after addUniverse() returns)
     *  reflects the requested name instead of the engine's default one. */
    bool m_hasPendingUniverseName = false;
    QString m_pendingUniverseName;

    /** Requesting client's id, stashed by a request handler just before
     *  calling an InputOutputMap/GrandMaster mutator whose resulting signal
     *  (relayed to a broadcast in one of the slots above) fires
     *  synchronously on this same thread - see 00-conventions.md §3/§9 on
     *  originClientId. Every setter that writes here is unconditionally
     *  cleared again right after the mutator call returns, since several of
     *  them (setGrandMasterValue, setBlackout, Doc::setMode) are no-ops that
     *  never emit when the requested value already matches the current one
     *  - otherwise a stale id could get attributed to a later, unrelated,
     *  non-request-driven change. Empty (the default) means "not currently
     *  inside a request that should be attributed" - broadcasts read it as
     *  their originClientId either way, so an unrelated/engine-driven change
     *  correctly gets a null origin. */
    QString m_pendingOriginClientId;
};

#endif
