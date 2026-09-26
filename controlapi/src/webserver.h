/*
  Q Light Controller Plus - Control API
  webserver.h

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

#ifndef WEBSERVER_H
#define WEBSERVER_H

#include <QByteArray>
#include <QHash>
#include <QHostAddress>
#include <QObject>
#include <QString>

class QTcpServer;
class QTcpSocket;

/** Default port for the web UI's HTTP server. Next to API_SERVER_DEFAULT_PORT
 *  (9010, apiserver.h); distinct from webaccess's legacy remote (9999) and
 *  Tardis's native peer-sync server (9998). */
#define WEB_SERVER_DEFAULT_PORT 9011

/**
 * Minimal HTTP/1.1 static-file server that lets QLC+ itself hand a browser
 * the web UI living in the repository's webui/ directory (installed to
 * WEBUIDIR, see variables.cmake / docs/webui.md). The UI then talks to the
 * WebSocket control API (ApiServer) directly; this class only serves files.
 *
 * Scope, deliberately small (simple and robust over fast):
 *  - GET and HEAD only; anything else is a 405 with an Allow header.
 *  - One request per TCP connection, always answered with
 *    "Connection: close" - no keep-alive, no pipelining, no ranges.
 *  - "/" (and any path ending in "/") maps to that directory's index.html.
 *  - Path traversal is rejected with 403: the percent-decoded request path is
 *    appended to the root, run through QDir::cleanPath(), and the result must
 *    still lie inside the root (checked *before* any existence test, so
 *    "/../secret" is a 403 even when nothing is there). Symlinks pointing out
 *    of the root are caught by a second, canonical-path check.
 *  - Every response carries Content-Type, Content-Length and
 *    "Cache-Control: no-cache" (the UI is edited live during development and
 *    must never be served stale from the browser cache).
 *  - GET /qlcplus-config.json is generated, not read from disk:
 *    {"apiPort": <apiPort()>, "apiHost": null} so the UI can find the
 *    WebSocket API without hard-coding a port.
 *
 * Built on QTcpServer/QTcpSocket rather than the Qt HttpServer module (not
 * available in this project's MSYS2 Qt) and without any dependency on
 * webaccess/'s bundled qhttpserver - controlapi must keep building on its own
 * (see apiserver.h). No engine or qmlui dependency either: the *default*
 * root directory is resolved by the caller (qmlui/main.cpp) from WEBUIDIR,
 * this class just serves whatever directory it is given.
 *
 * Runs entirely on the thread it's constructed on, like ApiServer.
 */
class WebServer : public QObject
{
    Q_OBJECT

public:
    explicit WebServer(QObject *parent = nullptr);
    ~WebServer();

    /** Directory whose contents are served. Stored cleaned and absolute
     *  (relative paths resolve against the current working directory once,
     *  here, not per request). May be changed while listening. */
    void setRootDirectory(const QString &dir);
    QString rootDirectory() const;

    /** Port reported by GET /qlcplus-config.json. Set this from
     *  ApiServer::serverPort() *after* the API server started listening, so
     *  the UI sees the port actually in use rather than the one requested. */
    void setApiPort(quint16 port);
    quint16 apiPort() const;

    /** Start listening. Same signature/argument order as ApiServer::listen()
     *  and for the same reason (a port-second signature would let listen(0)
     *  silently compile as "bind 0.0.0.0 on the default port"). Port 0 asks
     *  the OS for an ephemeral port - read serverPort() afterwards. Logs what
     *  it is serving on success, plus a warning when the root directory or
     *  its index.html is missing (it still starts: the resulting 404s are
     *  more informative than no server at all). Returns false when the port
     *  can't be bound - see errorString(). */
    bool listen(quint16 port = WEB_SERVER_DEFAULT_PORT,
                const QHostAddress &address = QHostAddress::Any);

    /** Stop accepting connections. Connections already accepted finish
     *  their (single) response on their own. */
    void close();

    bool isListening() const;
    QString errorString() const;
    quint16 serverPort() const;

    /** Content-Type for a file path, by extension. Explicit mappings for
     *  everything a web UI ships (html, js/mjs/jsx, css, svg, json, fonts,
     *  images, wasm), QMimeDatabase for the rest, application/octet-stream
     *  as the last resort. Text types carry "; charset=utf-8". Public and
     *  static so the test suite can pin the mappings down directly. */
    static QByteArray contentTypeForPath(const QString &path);

private slots:
    void slotNewConnection();
    void slotReadyRead();
    void slotDisconnected();

private:
    /** Outcome of mapping a request path onto the filesystem. */
    enum class PathStatus { Ok, Forbidden, NotFound };

    /** Resolve an already percent-decoded request path (starting with "/")
     *  to an absolute file path under the root. Directory paths resolve to
     *  their index.html. Forbidden means "escapes the root", NotFound means
     *  "inside the root, but no such regular file". */
    PathStatus resolvePath(const QString &requestPath, QString &filePath) const;

    /** True when path is m_root itself or lies inside it. Case-insensitive
     *  where the filesystem is (Windows, macOS). */
    bool isInsideRoot(const QString &cleanedAbsolutePath) const;

    /** Append whatever the socket has to its buffer and, once the request
     *  head is complete, answer it. */
    void slotReadyReadFor(QTcpSocket *socket);

    /** Parse and answer the complete request head in the socket's buffer. */
    void handleRequest(QTcpSocket *socket, const QByteArray &requestHead);

    /** Write a full response and close the connection. For HEAD requests
     *  (headOnly) contentLength is still sent but the body is not. */
    void sendResponse(QTcpSocket *socket, int status, const QByteArray &contentType,
                      const QByteArray &body, qint64 contentLength, bool headOnly,
                      const QList<QPair<QByteArray, QByteArray>> &extraHeaders = {});

    /** Convenience for the plain-text 4xx/5xx replies. */
    void sendError(QTcpSocket *socket, int status, bool headOnly,
                   const QList<QPair<QByteArray, QByteArray>> &extraHeaders = {});

    static QByteArray reasonPhrase(int status);

private:
    QTcpServer *m_server;
    QString m_root;
    quint16 m_apiPort;

    /** Per-connection receive buffer, until the request head is complete. */
    QHash<QTcpSocket *, QByteArray> m_buffers;
};

#endif
