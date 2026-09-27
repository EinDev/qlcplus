/*
  Q Light Controller Plus - Control API
  apishowpreviewdomain.h

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

#ifndef APISHOWPREVIEWDOMAIN_H
#define APISHOWPREVIEWDOMAIN_H

#include <QJsonObject>
#include <QObject>
#include <QSet>

class ApiDispatcher;
class ApiShowHost;
class ApiServer;
class Track;
class Show;
class Doc;

/**
 * functions.show.* leftovers of docs/api-spec/fragments/functions-advanced.yaml (added
 * 2026-09-27), next to ApiShowDomain:
 *
 * - Scrub preview (§4b, runtime): functions.show.preview {showId, time} shows the Show's state at
 *   $time while it is stopped or paused - the desktop Show Manager's preview-at-cursor
 *   (ShowManager::previewAt, docs/agent-reports/2026-09-15-show-scrub-preview-design.md). The
 *   engine does the work: Show::setScrubMode() freezes the ShowRunner, Show::requestSeek() moves
 *   it, both atomics the runner reads on the MasterTimer thread, so nothing here touches the
 *   runner. A stopped Show is started at $time in scrub mode (FunctionParent ControlApi), a
 *   previewing one seeks, a paused one is handed over to the frozen runner; a playing one is
 *   refused (INVALID_STATE). functions.show.endPreview {showId, play} either stops the preview or
 *   leaves scrub mode so the Show plays on from the cursor. functions.show.previewChanged
 *   {functionId, previewing, time} announces start / end (not every seek); a preview stopped by
 *   anyone else (functions.stop, the desktop, the VC) ends it too. Audio stays silent while
 *   frozen (ShowRunner skips it), fixtures and video follow the cursor, like the desktop.
 *
 * - Track Spout output size (§4a): functions.show.track.setSpoutSize {showId, trackId, width,
 *   height, baseRevision} stores Track::spoutSize (0x0 unsets it) - document state saved as the
 *   track's SpoutSize attribute - and lets the host resize the live sender (ApiShowHost).
 *   Broadcast: functions.show.track.spoutSizeChanged. trackSpoutJson() is the "spout" block every
 *   FunctionsShowTrack carries when the track holds Spout-mode Videos or a fixed size (the desktop
 *   track header's label / PopupSpoutSizeMismatch inputs).
 */
class ApiShowPreviewDomain : public QObject
{
    Q_OBJECT

public:
    ApiShowPreviewDomain(Doc *doc, ApiServer *server, QObject *parent = nullptr);
    ~ApiShowPreviewDomain();

    /** Largest Spout size, PopupTrackSpoutSize.qml's spin box range (1..16384). */
    static const int MaxSpoutSize = 16384;

    /** {fixedSize, outputSize, clips[], mismatch} for $track, or an empty object when the track
     *  has no Spout-mode Video and no fixed size. Uses the live host (if one is attached to the
     *  current server) for the sender's size. */
    static QJsonObject trackSpoutJson(Doc *doc, Track *track);

private slots:
    void slotShowStopped(quint32 id);

private:
    void registerMethods(ApiDispatcher *d);
    void broadcastPreview(Show *show, bool previewing, quint32 time, const QString &originClientId);

private:
    Doc *m_doc;
    ApiServer *m_server;
    /** Shows this domain put into scrub mode and has not seen end yet */
    QSet<quint32> m_previewing;

    /** The host of the most recently constructed domain (cleared by its destructor), read by
     *  the static trackSpoutJson(), which ApiShowDomain's static serialisers call. */
    static ApiShowHost *s_host;
    static ApiShowPreviewDomain *s_instance;
};

#endif
