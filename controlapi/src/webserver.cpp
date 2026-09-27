/*
  Q Light Controller Plus - Control API
  webserver.cpp

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

#include <QDateTime>
#include <QDebug>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <QLocale>
#include <QMimeDatabase>
#include <QMimeType>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTimeZone>
#include <QUrl>

#include "webserver.h"

/** Requests whose head hasn't finished within this many bytes are dropped
 *  with 431 - a browser request for a static file is well under 4 KiB. */
static const int kMaxRequestHeadSize = 16 * 1024;

/** The one path that isn't looked up on disk. */
static const char kConfigPath[] = "/qlcplus-config.json";

WebServer::WebServer(QObject *parent)
    : QObject(parent)
    , m_server(new QTcpServer(this))
    , m_apiPort(0)
{
    connect(m_server, &QTcpServer::newConnection, this, &WebServer::slotNewConnection);
}

WebServer::~WebServer()
{
    close();
}

/*********************************************************************
 * Configuration
 *********************************************************************/

void WebServer::setRootDirectory(const QString &dir)
{
    // Absolute + cleaned once here so every per-request containment check
    // compares like with like (no "..", no doubled or trailing slashes).
    m_root = QDir::cleanPath(QDir(dir).absolutePath());
}

QString WebServer::rootDirectory() const
{
    return m_root;
}

void WebServer::setApiPort(quint16 port)
{
    m_apiPort = port;
}

quint16 WebServer::apiPort() const
{
    return m_apiPort;
}

/*********************************************************************
 * Listening
 *********************************************************************/

bool WebServer::listen(quint16 port, const QHostAddress &address)
{
    if (m_server->listen(address, port) == false)
        return false;

    qDebug() << "Web UI served from" << m_root << "on port" << m_server->serverPort();

    if (QDir(m_root).exists() == false)
    {
        qWarning().noquote() << QStringLiteral("Web UI root directory does not exist: %1 - every request will "
                                               "be answered with 404 (pass --webui-root <dir> or install the web UI)")
                                    .arg(m_root);
    }
    else if (QFileInfo(m_root + QStringLiteral("/index.html")).isFile() == false)
    {
        qWarning().noquote() << QStringLiteral("Web UI root has no index.html: %1 - GET / will be a 404").arg(m_root);
    }

    return true;
}

void WebServer::close()
{
    if (m_server->isListening())
        m_server->close();
}

bool WebServer::isListening() const
{
    return m_server->isListening();
}

QString WebServer::errorString() const
{
    return m_server->errorString();
}

quint16 WebServer::serverPort() const
{
    return m_server->serverPort();
}

/*********************************************************************
 * Connections
 *********************************************************************/

void WebServer::slotNewConnection()
{
    while (m_server->hasPendingConnections())
    {
        QTcpSocket *socket = m_server->nextPendingConnection();
        if (socket == nullptr)
            break;

        m_buffers.insert(socket, QByteArray());
        connect(socket, &QTcpSocket::readyRead, this, &WebServer::slotReadyRead);
        connect(socket, &QTcpSocket::disconnected, this, &WebServer::slotDisconnected);

        // Data may already be waiting if the client was quick - readyRead
        // for those bytes fired before we were connected to it
        if (socket->bytesAvailable() > 0)
            slotReadyReadFor(socket);
    }
}

void WebServer::slotReadyRead()
{
    QTcpSocket *socket = qobject_cast<QTcpSocket *>(sender());
    if (socket == nullptr)
        return;
    slotReadyReadFor(socket);
}

void WebServer::slotReadyReadFor(QTcpSocket *socket)
{
    auto it = m_buffers.find(socket);
    if (it == m_buffers.end())
    {
        // Already answered: one request per connection, ignore anything
        // the client keeps sending while we're closing
        socket->readAll();
        return;
    }

    it.value().append(socket->readAll());

    int headEnd = it.value().indexOf("\r\n\r\n");
    if (headEnd < 0)
    {
        if (it.value().size() > kMaxRequestHeadSize)
        {
            m_buffers.erase(it);
            sendError(socket, 431, false);
        }
        return;
    }

    QByteArray head = it.value().left(headEnd);
    // From here on this connection has exactly one job left: answering.
    // Dropping the buffer is what marks it as "request already consumed".
    m_buffers.erase(it);
    disconnect(socket, &QTcpSocket::readyRead, this, &WebServer::slotReadyRead);

    handleRequest(socket, head);
}

void WebServer::slotDisconnected()
{
    QTcpSocket *socket = qobject_cast<QTcpSocket *>(sender());
    if (socket == nullptr)
        return;
    m_buffers.remove(socket);
    socket->deleteLater();
}

/*********************************************************************
 * Request handling
 *********************************************************************/

void WebServer::handleRequest(QTcpSocket *socket, const QByteArray &requestHead)
{
    // Request line: METHOD SP request-target SP HTTP-version
    int lineEnd = requestHead.indexOf("\r\n");
    QByteArray requestLine = lineEnd < 0 ? requestHead : requestHead.left(lineEnd);
    QList<QByteArray> parts = requestLine.split(' ');
    parts.removeAll(QByteArray());
    if (parts.size() != 3 || parts.at(2).startsWith("HTTP/1.") == false)
    {
        sendError(socket, 400, false);
        return;
    }

    const QByteArray method = parts.at(0);
    const bool headOnly = (method == "HEAD");
    if (method != "GET" && headOnly == false)
    {
        sendError(socket, 405, false, { { "Allow", "GET, HEAD" } });
        return;
    }

    // Request target: origin-form ("/path?query") is what browsers send;
    // absolute-form ("http://host/path") is legal too, take just its path.
    QByteArray target = parts.at(1);
    if (target.startsWith("http://") || target.startsWith("https://"))
        target = QUrl::fromEncoded(target).path(QUrl::FullyEncoded).toUtf8();
    int cut = target.indexOf('?');
    if (cut >= 0)
        target.truncate(cut);
    cut = target.indexOf('#');
    if (cut >= 0)
        target.truncate(cut);
    if (target.startsWith('/') == false)
    {
        sendError(socket, 400, headOnly);
        return;
    }

    // Decode *before* the traversal check, otherwise "%2e%2e" sails through
    // cleanPath() as an innocent file name and only turns into ".." later.
    QString path = QUrl::fromPercentEncoding(target);
    if (path.contains(QChar(0)) || path.contains(QLatin1Char('\\')))
    {
        qWarning().noquote() << "Web UI: rejecting request with NUL/backslash in path:" << requestLine;
        sendError(socket, 403, headOnly);
        return;
    }

    if (path == QLatin1String(kConfigPath))
    {
        QJsonObject config;
        config.insert(QStringLiteral("apiPort"), int(m_apiPort));
        config.insert(QStringLiteral("apiHost"), QJsonValue::Null);
        QByteArray body = QJsonDocument(config).toJson(QJsonDocument::Compact);
        sendResponse(socket, 200, "application/json; charset=utf-8", body, body.size(), headOnly);
        return;
    }

    QString filePath;
    switch (resolvePath(path, filePath))
    {
        case PathStatus::Forbidden:
            qWarning().noquote() << "Web UI: rejecting path outside the root:" << requestLine;
            sendError(socket, 403, headOnly);
            return;
        case PathStatus::NotFound:
            sendError(socket, 404, headOnly);
            return;
        case PathStatus::Ok:
            break;
    }

    QFile file(filePath);
    if (file.open(QIODevice::ReadOnly) == false)
    {
        qWarning().noquote() << "Web UI: cannot read" << filePath << "-" << file.errorString();
        sendError(socket, 404, headOnly);
        return;
    }

    // Validators, taken BEFORE reading: if the file is rewritten while we
    // read it, the body is newer than the tag, so the browser's next
    // revalidation mismatches and it downloads again - never the reverse.
    // With Cache-Control: no-cache the browser revalidates every file on
    // every load, so an edited file (--webui-root on a source tree) still
    // shows up on the next reload; an unchanged one costs a 304 instead of
    // its whole body.
    const QFileInfo info(filePath);
    const QByteArray etag = entityTag(info);
    const QList<QPair<QByteArray, QByteArray>> validators = {
        { "ETag", etag },
        { "Last-Modified", httpDate(info.lastModified()) }
    };
    if (isNotModified(requestHead, etag, info.lastModified()))
    {
        sendResponse(socket, 304, QByteArray(), QByteArray(), 0, true, validators);
        return;
    }

    // Whole file in memory: web UI assets are small, and this keeps the
    // response path a single write with a Content-Length known up front.
    // For GET the length must describe the bytes actually read - the UI is
    // edited live, and a file rewritten between open() and readAll() would
    // otherwise be announced with a stale size and the browser left waiting
    // for bytes that never come (or handed a truncated body).
    QByteArray body = headOnly ? QByteArray() : file.readAll();
    sendResponse(socket, 200, contentTypeForPath(filePath), body, headOnly ? file.size() : body.size(), headOnly, validators);
}

QByteArray WebServer::headerValue(const QByteArray &requestHead, const QByteArray &name)
{
    // Header lines after the request line; names are case-insensitive.
    // Repeated headers are joined with ", " (RFC 9110 5.3).
    QByteArray value;
    bool found = false;
    const QList<QByteArray> lines = requestHead.split('\n');
    for (int i = 1; i < lines.size(); i++)
    {
        const QByteArray line = lines.at(i).trimmed();
        const int colon = line.indexOf(':');
        if (colon <= 0 || line.left(colon).trimmed().compare(name, Qt::CaseInsensitive) != 0)
            continue;
        if (found)
            value += ", ";
        value += line.mid(colon + 1).trimmed();
        found = true;
    }
    return found ? value : QByteArray();
}

QByteArray WebServer::entityTag(const QFileInfo &info)
{
    // Size + modification time in ms: cheap (no hashing of the body) and
    // changes on every save of an edited file.
    return '"' + QByteArray::number(info.size(), 16) + '-'
           + QByteArray::number(info.lastModified().toMSecsSinceEpoch(), 16) + '"';
}

QByteArray WebServer::httpDate(const QDateTime &time)
{
    return QLocale::c().toString(time.toUTC(), QStringLiteral("ddd, dd MMM yyyy HH:mm:ss 'GMT'")).toLatin1();
}

bool WebServer::isNotModified(const QByteArray &requestHead, const QByteArray &etag, const QDateTime &lastModified)
{
    // RFC 9110 13.2.2: If-None-Match wins; If-Modified-Since only counts
    // when there is no If-None-Match. Weak comparison for If-None-Match.
    const QByteArray noneMatch = headerValue(requestHead, "If-None-Match");
    if (noneMatch.isNull() == false)
    {
        for (QByteArray tag : noneMatch.split(','))
        {
            tag = tag.trimmed();
            if (tag == "*")
                return true;
            if (tag.startsWith("W/"))
                tag = tag.mid(2);
            if (tag == etag)
                return true;
        }
        return false;
    }

    const QByteArray modifiedSince = headerValue(requestHead, "If-Modified-Since");
    if (modifiedSince.isEmpty())
        return false;
    QDateTime since = QLocale::c().toDateTime(QString::fromLatin1(modifiedSince),
                                              QStringLiteral("ddd, dd MMM yyyy HH:mm:ss 'GMT'"));
    if (since.isValid() == false)
        return false;
    since.setTimeZone(QTimeZone::UTC);
    // HTTP dates have whole seconds: compare at that resolution.
    const qint64 fileSecs = lastModified.toMSecsSinceEpoch() / 1000;
    return fileSecs <= since.toMSecsSinceEpoch() / 1000;
}

WebServer::PathStatus WebServer::resolvePath(const QString &requestPath, QString &filePath) const
{
    QString wanted = requestPath;
    if (wanted.endsWith(QLatin1Char('/')))
        wanted += QStringLiteral("index.html");

    QString cleaned = QDir::cleanPath(m_root + wanted);
    if (isInsideRoot(cleaned) == false)
        return PathStatus::Forbidden;

    QFileInfo info(cleaned);
    if (info.isDir())
    {
        // "/sub" for a directory: serve its index like "/sub/" would.
        // (No redirect: keeps this simple, and relative asset URLs inside a
        // nested index.html are the UI's problem to avoid, not ours.)
        cleaned += QStringLiteral("/index.html");
        info.setFile(cleaned);
    }
    if (info.exists() == false || info.isFile() == false)
        return PathStatus::NotFound;

    // Second line of defence: a symlink (or Windows junction) inside the
    // root pointing outside it passes the lexical check above but not this
    // one. canonicalFilePath() is empty for non-existing files, which can't
    // happen here anymore, and for the root itself only if it doesn't exist -
    // in which case nothing under it exists either.
    QString canonicalRoot = QFileInfo(m_root).canonicalFilePath();
    QString canonicalFile = info.canonicalFilePath();
    if (canonicalRoot.isEmpty() || canonicalFile.isEmpty())
        return PathStatus::NotFound;
    Qt::CaseSensitivity cs = Qt::CaseSensitive;
#if defined(Q_OS_WIN) || defined(Q_OS_MACOS)
    cs = Qt::CaseInsensitive;
#endif
    if (canonicalFile.startsWith(canonicalRoot + QLatin1Char('/'), cs) == false)
        return PathStatus::Forbidden;

    filePath = cleaned;
    return PathStatus::Ok;
}

bool WebServer::isInsideRoot(const QString &cleanedAbsolutePath) const
{
    Qt::CaseSensitivity cs = Qt::CaseSensitive;
#if defined(Q_OS_WIN) || defined(Q_OS_MACOS)
    cs = Qt::CaseInsensitive;
#endif
    if (cleanedAbsolutePath.compare(m_root, cs) == 0)
        return true;
    return cleanedAbsolutePath.startsWith(m_root + QLatin1Char('/'), cs);
}

/*********************************************************************
 * Responses
 *********************************************************************/

void WebServer::sendResponse(QTcpSocket *socket, int status, const QByteArray &contentType,
                             const QByteArray &body, qint64 contentLength, bool headOnly,
                             const QList<QPair<QByteArray, QByteArray>> &extraHeaders)
{
    QByteArray response;
    response.reserve(256 + (headOnly ? 0 : body.size()));
    response += "HTTP/1.1 " + QByteArray::number(status) + ' ' + reasonPhrase(status) + "\r\n";
    // A 304 has no body and describes none: it only carries the validators
    // (extraHeaders) and the caching policy.
    if (status != 304)
    {
        response += "Content-Type: " + contentType + "\r\n";
        response += "Content-Length: " + QByteArray::number(contentLength) + "\r\n";
    }
    response += "Cache-Control: no-cache\r\n";
    response += "Connection: close\r\n";
    for (const auto &header : extraHeaders)
        response += header.first + ": " + header.second + "\r\n";
    response += "\r\n";
    if (headOnly == false)
        response += body;

    socket->write(response);
    // Flushes what's queued, then closes - the client sees EOF right after
    // the last body byte, which with Connection: close is the terminator.
    socket->disconnectFromHost();
}

void WebServer::sendError(QTcpSocket *socket, int status, bool headOnly,
                          const QList<QPair<QByteArray, QByteArray>> &extraHeaders)
{
    QByteArray body = QByteArray::number(status) + ' ' + reasonPhrase(status) + '\n';
    sendResponse(socket, status, "text/plain; charset=utf-8", body, body.size(), headOnly, extraHeaders);
}

QByteArray WebServer::reasonPhrase(int status)
{
    switch (status)
    {
        case 200: return "OK";
        case 304: return "Not Modified";
        case 400: return "Bad Request";
        case 403: return "Forbidden";
        case 404: return "Not Found";
        case 405: return "Method Not Allowed";
        case 431: return "Request Header Fields Too Large";
        case 500: return "Internal Server Error";
        default:  return "Unknown";
    }
}

QByteArray WebServer::contentTypeForPath(const QString &path)
{
    const QString suffix = QFileInfo(path).suffix().toLower();

    // Explicit first: QMimeDatabase's answers for these vary by platform
    // and database version (e.g. application/javascript vs text/javascript,
    // or no entry at all for .jsx/.woff2 on a bare Windows box), and a
    // browser refuses to run a module script served with the wrong type.
    static const QHash<QString, QByteArray> explicitTypes = {
        { QStringLiteral("html"),  "text/html; charset=utf-8" },
        { QStringLiteral("htm"),   "text/html; charset=utf-8" },
        { QStringLiteral("js"),    "text/javascript; charset=utf-8" },
        { QStringLiteral("mjs"),   "text/javascript; charset=utf-8" },
        { QStringLiteral("jsx"),   "text/javascript; charset=utf-8" },
        { QStringLiteral("css"),   "text/css; charset=utf-8" },
        { QStringLiteral("svg"),   "image/svg+xml" },
        { QStringLiteral("json"),  "application/json; charset=utf-8" },
        { QStringLiteral("map"),   "application/json; charset=utf-8" },
        { QStringLiteral("txt"),   "text/plain; charset=utf-8" },
        { QStringLiteral("xml"),   "application/xml; charset=utf-8" },
        { QStringLiteral("woff2"), "font/woff2" },
        { QStringLiteral("woff"),  "font/woff" },
        { QStringLiteral("ttf"),   "font/ttf" },
        { QStringLiteral("otf"),   "font/otf" },
        { QStringLiteral("png"),   "image/png" },
        { QStringLiteral("jpg"),   "image/jpeg" },
        { QStringLiteral("jpeg"),  "image/jpeg" },
        { QStringLiteral("gif"),   "image/gif" },
        { QStringLiteral("webp"),  "image/webp" },
        { QStringLiteral("ico"),   "image/x-icon" },
        { QStringLiteral("wasm"),  "application/wasm" },
    };
    auto it = explicitTypes.constFind(suffix);
    if (it != explicitTypes.constEnd())
        return it.value();

    // MatchExtension only: never open the file to sniff its content
    QMimeType mime = QMimeDatabase().mimeTypeForFile(path, QMimeDatabase::MatchExtension);
    if (mime.isValid() && mime.isDefault() == false)
    {
        QByteArray name = mime.name().toUtf8();
        if (name.startsWith("text/"))
            name += "; charset=utf-8";
        return name;
    }

    return "application/octet-stream";
}
