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
#include <QMap>
#include <QMutex>
#include <QObject>
#include <QPair>
#include <QSharedPointer>

#include "dmxsource.h"

class ApiServer;
class Doc;
class Universe;
class GenericFader;
class FadeChannel;
class MasterTimer;
class KeyPadParser;

/**
 * First real vertical slice of the control API (see docs/api-spec/fragments/io.yaml
 * and the feature's plan doc): universes (§4a structural), Grand Master and
 * Blackout (§4b live), and a subscribe-gated live DMX event (§4b, §5).
 * Registers its methods into the ApiServer's ApiDispatcher and connects to
 * InputOutputMap/Universe signals to broadcast events - the same
 * constructor-injected-Doc*, connect-to-existing-signals pattern every
 * qmlui manager class already uses (e.g. qmlui/fixturemanager.cpp,
 * qmlui/simpledesk.cpp).
 *
 * Also implements the io.simpleDesk.* live-compose slice (get/setChannel/
 * setChannels/resetChannel/resetUniverse/setUniverseFilter - io.yaml ~1463-1800): a
 * from-scratch, engine/src-only reimplementation of qmlui::SimpleDesk's
 * DMXSource/GenericFader pattern (SimpleDesk itself is qmlui-only and
 * unreachable from this deliberately qmlui-free module - see
 * docs/agent-reports/2026-08-31-lighting-redesign-and-websocket-plan.md).
 */
class ApiIoDomain : public QObject, public DMXSource
{
    Q_OBJECT

public:
    ApiIoDomain(Doc *doc, ApiServer *server, QObject *parent = nullptr);
    ~ApiIoDomain() override;

    /** Programmatic io.simpleDesk.setChannels: holds every (absolute
     *  address, value) pair as a live Simple Desk override and broadcasts
     *  io.simpleDesk.channelChanged for each, attributed to originClientId.
     *  Used by other domains that write live DMX through the same override
     *  path a client would (ApiMonitorDomain's fixtures.monitor.aimAt). */
    void overrideChannels(const QList<QPair<quint32, uchar>> &entries, const QString &originClientId);

    /** @reimp DMXSource - pushes every live-held Simple Desk value through
     *  this class's own per-universe GenericFader(s), mirroring
     *  qmlui/simpledesk.cpp's writeDMX() exactly (see its own comments for
     *  why the actual Universe/GenericFader manipulation has to happen here,
     *  on MasterTimer's thread, rather than directly in a request handler). */
    void writeDMX(MasterTimer *timer, QList<Universe *> universes) override;

    /** Broadcast io.universe.updated {universe: IoUniverseDetail, docRevision}
     *  - the one structural event for io.universe.update AND every
     *  io.patch.* mutation (io.yaml's own IoUniverseUpdatedEvent doc), so
     *  clients never re-fetch after a patch change. Public so
     *  ApiIoConfigDomain (io.patch.setParameters) reuses the same JSON. */
    void broadcastUniverseUpdated(Universe *universe, const QString &originClientId);

    /** §4c library counter for io.inputProfile.* (00-conventions.md): read by
     *  io.inputProfile.list here, bumped by ApiIoConfigDomain's save/delete. */
    quint32 profilesRevision() const { return m_profilesRevision; }
    void bumpProfilesRevision() { m_profilesRevision++; }

private:
    void registerMethods();
    void watchUniverse(Universe *universe);

    /** Absolute-address (universeId<<9 + channel) held-value -> IoSimpleDeskChannel
     *  JSON, per io.yaml's schema. Caller must hold m_simpleDeskMutex. */
    QJsonObject simpleDeskChannelToJson(quint32 address) const;

    /** Get-or-create this domain's own GenericFader for universeId (requested
     *  at Universe::SimpleDesk priority - see this feature's plan doc for why
     *  a second, unrelated Universe::SimpleDesk-priority fader coexisting
     *  with the real SimpleDesk's is safe) and the FadeChannel within it for
     *  (fixtureId, channel). universeId must already be < universes.count(). */
    FadeChannel *simpleDeskFader(const QList<Universe *> &universes, quint32 universeId,
                                  quint32 fixtureId, quint32 channel);

    /** Hold (universeId<<9)+channel -> value overrides and broadcast one
     *  io.simpleDesk.channelChanged per entry - the shared core of
     *  setChannel/setChannels/sendKeypadCommand. */
    void applySimpleDeskValues(const QList<QPair<quint32, uchar>> &entries, const QString &originClientId);

private slots:
    void slotUniverseAdded(quint32 id);
    /** InputOutputMap::universeRemoved relay (fired synchronously from
     *  within removeUniverse(), after Doc::setModified() bumped the
     *  revision): drops this domain's per-universe state and broadcasts
     *  io.universe.deleted {universeId, docRevision}. */
    void slotUniverseRemoved(quint32 id);
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

    /** Guards every m_simpleDesk* member below: writeDMX() runs on
     *  MasterTimer's own thread while request handlers run on whatever
     *  thread ApiServer/this domain was constructed on (see apiserver.h's
     *  own doc comment) - exactly the SimpleDesk::m_mutex situation
     *  qmlui/simpledesk.h/.cpp already has to deal with. */
    mutable QMutex m_simpleDeskMutex;

    /** Currently-held live override values, keyed by absolute address
     *  ((universeId<<9)+channel, matching IoSimpleDeskChannel's wire
     *  encoding) - mirrors SimpleDesk::m_values. */
    QHash<quint32, uchar> m_simpleDeskValues;

    /** One GenericFader per universe this domain has ever written to,
     *  mirroring SimpleDesk::m_fadersMap. */
    QMap<quint32, QSharedPointer<GenericFader>> m_simpleDeskFaders;

    enum SimpleDeskCommand { SimpleDeskResetChannel, SimpleDeskResetUniverse };

    /** (command, address-or-universeId) pairs queued by resetChannel/
     *  resetUniverse request handlers and drained by writeDMX() - the actual
     *  Universe/GenericFader manipulation those commands need can only
     *  safely happen on MasterTimer's thread, mirroring
     *  SimpleDesk::m_commandQueue. */
    QList<QPair<int, quint32>> m_simpleDeskCommandQueue;

    /** io.simpleDesk.setUniverseFilter/get's "which universe is Simple
     *  Desk's channel view currently targeting" - global engine state
     *  shared by every connected client (io.yaml's own description of this
     *  field), not persisted, not per-session. */
    quint32 m_simpleDeskUniverseFilter = 0;

    /** io.simpleDesk.sendKeypadCommand: the engine's own keypad grammar
     *  (engine/src/keypadparser.h). One parser instance for the domain, as
     *  qmlui's SimpleDesk has - it remembers the last channel selection so
     *  "1 THRU 4" followed by "AT 50" works across two commands. Only ever
     *  used from request handlers (server thread), so no mutex. */
    KeyPadParser *m_keyPadParser;

    /** Most recent first, capped at MAX_KEYPAD_HISTORY like qmlui's
     *  SimpleDesk::m_keypadCommandHistory. Global (shared by every client),
     *  broadcast via io.simpleDesk.commandHistoryChanged. */
    QStringList m_keypadCommandHistory;

    quint32 m_profilesRevision = 0;
};

#endif
