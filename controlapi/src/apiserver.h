/*
  Q Light Controller Plus - Control API
  apiserver.h

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

#ifndef APISERVER_H
#define APISERVER_H

#include <QHash>
#include <QObject>

#include "apidispatcher.h"

class QWebSocketServer;
class ApiSession;
class ApiIoDomain;
class ApiCoreDomain;
class Doc;

/** Default port for the control API's WebSocket server. Distinct from
 *  webaccess's legacy remote-control server (9999, webaccessbase.cpp) and
 *  Tardis's native peer-sync server (9998, networkmanager.cpp). */
#define API_SERVER_DEFAULT_PORT 9010

/**
 * Entry point of the WebSocket control API (see docs/api-spec/). Owns the
 * QWebSocketServer, wraps every accepted connection in an ApiSession, feeds
 * incoming frames to an ApiDispatcher, and is the single broadcast
 * chokepoint every domain listener (e.g. ApiIoDomain) calls to push event
 * frames out to connected clients.
 *
 * Constructed the same way every other qmlui manager class is constructed
 * (see qmlui/app.cpp's App::startup()): given a Doc* explicitly, no global/
 * singleton Doc accessor exists or is added here. Deliberately has NO
 * dependency on qmlui, Tardis, or QQuickView - this module must build and
 * run standalone.
 *
 * Runs entirely on the thread it's constructed on (no moveToThread) - see
 * docs/api-spec/00-conventions.md and this feature's plan doc for why that's
 * safe even though some engine signals (Universe::universeWritten, MasterTimer
 * function start/stop) originate on other threads: Qt's automatic queued
 * cross-thread signal delivery already handles it, the same way existing
 * qmlui code (e.g. qmlui/contextmanager.cpp) relies on it today.
 */
class ApiServer : public QObject
{
    Q_OBJECT

public:
    explicit ApiServer(QObject *parent, Doc *doc);
    ~ApiServer();

    /** Start listening on the given port (all interfaces), or an OS-assigned
     *  ephemeral port if port is 0 (used by controlapi/test/apiiodomain).
     *  Returns false on failure (e.g. port already in use) - check
     *  errorString() for why. */
    bool listen(quint16 port = API_SERVER_DEFAULT_PORT);

    QString errorString() const;
    quint16 serverPort() const;

    Doc *doc() const;
    ApiDispatcher *dispatcher() const;

    /**
     * Push an event frame to connected clients (docs/api-spec/00-conventions.md
     * §5). Most events - both §4a structural AND low-frequency §4b live ones
     * like Grand Master/Blackout - go to every session unconditionally.
     * subscribeGated=true is only for genuinely high-frequency streams
     * (per-universe DMX deltas being the one example in this slice) that
     * would otherwise blast every connected client whether they asked or
     * not; those are only delivered to sessions that subscribed to exactly
     * this topic.
     *
     * @param topic Event topic, e.g. "io.grandMaster.changed"
     * @param data Event payload
     * @param originClientId The client whose request caused this, or a null
     *        QString for engine-internal/other causes
     * @param subscribeGated true only for high-frequency topics that must be
     *        opted into; false (the common case) delivers to everyone
     */
    void broadcast(const QString &topic, const QJsonObject &data, const QString &originClientId, bool subscribeGated);

private slots:
    void slotNewConnection();
    void slotSessionDisconnected(ApiSession *session);

private:
    void registerSessionMethods();
    QString nextClientId();

private:
    Doc *m_doc;
    QWebSocketServer *m_webSocketServer;
    ApiDispatcher *m_dispatcher;
    QHash<QString, ApiSession *> m_sessions;
    quint32 m_nextClientSeq;

    // Domain modules - each one registers its own methods into m_dispatcher
    // and connects to engine signals to broadcast() its own events. Adding a
    // future domain (fixtures, functions, virtual console, ...) means adding
    // one more of these here, nothing else in this class or in qmlui/app.cpp.
    ApiIoDomain *m_ioDomain;
    ApiCoreDomain *m_coreDomain;
};

#endif
