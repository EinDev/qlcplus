/*
  Q Light Controller Plus - Control API
  apicoredomain.h

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

#ifndef APICOREDOMAIN_H
#define APICOREDOMAIN_H

#include <QElapsedTimer>
#include <QList>
#include <QObject>
#include "doc.h"
#include "apiprojecthost.h"

class ApiServer;
class ApiSession;
class QTimer;

/**
 * Implementation of the "core.*" domain: project lifecycle, mode, settings,
 * the beat generator (core.bpm.get/set/tap, core.bpm.changed, core.beat -
 * all §4b live state on InputOutputMap's beat generator, no baseRevision)
 * and undo/redo (core.undo/redo/history.get, core.history.changed) through
 * the ApiProjectHost undo hooks qmlui's App implements on top of Tardis.
 *
 * Undo/redo caveats (documented in core-notes.md too): Tardis only records
 * edits made through the qmlui UI, so changes made via this API's own
 * structural methods are not undoable; and an undo re-invokes engine
 * setters directly, so the affected domain's normal change event does NOT
 * fire - clients must treat core.history.changed's docRevision bump as
 * "something changed, refetch".
 */
class ApiCoreDomain : public QObject
{
    Q_OBJECT

public:
    ApiCoreDomain(Doc *doc, ApiServer *server, QObject *parent = nullptr);

private:
    void registerMethods();
    ApiProjectHost *projectHost() const;

    /** Broadcasts core.project.loaded ({reason, project: CoreProjectMetadata}),
     *  the one event a spec-built client should refresh all its domain state
     *  from - see core.project.new/open/close handlers, the only callers. */
    void broadcastProjectLoaded(const QString &reason, const QString &originClientId);

    /** Whether host's undo/redo hooks may be used at all: a host is
     *  present (see projectHost()). Sends UNSUPPORTED and returns false
     *  otherwise - shared by core.undo/redo/history.get. */
    bool requireUndoHost(ApiSession *session, const QString &id, ApiProjectHost **host) const;

    /** {canUndo, canRedo, undoText?, redoText?, docRevision} - the shared
     *  part of core.history.get's result, core.undo/redo's result and
     *  core.history.changed's data. */
    QJsonObject historyStateToJson() const;

    /** Switch the beat generator to Internal if it is currently Disabled
     *  (InputOutputMap::setBpmNumber() is a silent no-op while Disabled) -
     *  shared by core.bpm.set/tap. Returns false (after sending an
     *  INVALID_STATE error) when a Plugin/Audio source owns the tempo. */
    bool ensureInternalBeatGenerator(ApiSession *session, const QString &id);

private slots:
    void slotModeChanged(Doc::Mode mode);
    void slotDocRevisionChanged(quint32 revision);
    void slotRecentFilesChanged();
    void slotWorkingPathChanged(QString path);

    /** InputOutputMap::bpmNumberChanged / beatGeneratorTypeChanged relays -
     *  both broadcast the full core.bpm.changed {bpm, generator} state */
    void slotBpmNumberChanged(int bpm);
    void slotBeatGeneratorTypeChanged();

    /** InputOutputMap::beat relay - broadcasts core.beat. Only ever fires
     *  while a generator is active (Internal ticks, or a Plugin/Audio
     *  source's processed beats), which is the "rate-limited to actual
     *  beats" the web UI contract asks for. */
    void slotBeat();

    /** Host historyChanged() relay: arms m_historyTimer so a burst of
     *  recorded actions (a drag records dozens within Tardis's 150ms
     *  batching window) collapses into one core.history.changed. */
    void slotHistoryChanged();
    void slotBroadcastHistoryChanged();

private:
    Doc *m_doc;
    ApiServer *m_server;

    /** Requesting client's id, stashed by core.mode.set just before calling
     *  Doc::setMode() so slotModeChanged() - fired synchronously from
     *  within that call if the mode actually changed - can attribute
     *  core.mode.changed to it instead of leaving originClientId null (see
     *  00-conventions.md §3/§9). Doc::setMode() is a no-op (and never emits)
     *  when the requested mode already matches the current one, so this is
     *  unconditionally cleared again right after the call returns - the
     *  same "set before, clear after" pattern as ApiIoDomain's
     *  m_pendingOriginClientId, see its longer comment there. */
    QString m_pendingOriginClientId;

    /** core.bpm.tap state: process-uptime clock plus the timestamps of the
     *  recent taps in the current run. A gap longer than
     *  TAP_RESET_INTERVAL_MS starts a fresh run (first tap of a run sets no
     *  tempo, like any tap-tempo button). Global engine state, not
     *  per-session - two operators tapping alternately do fight, exactly
     *  like on a physical console. */
    QElapsedTimer m_tapClock;
    QList<qint64> m_tapTimesMs;

    /** Coalescing single-shot for core.history.changed, see slotHistoryChanged() */
    QTimer *m_historyTimer;
};

#endif
