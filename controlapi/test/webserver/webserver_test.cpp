/*
  Q Light Controller Plus - Control API unit test
  webserver_test.cpp

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

#include <QtTest>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTcpSocket>
#include <QTemporaryDir>

#include "webserver_test.h"
#include "webserver.h"

static const QByteArray kIndexHtml = "<!doctype html><title>QLC+ web UI test</title>\n";
static const QByteArray kSvg = "<svg xmlns=\"http://www.w3.org/2000/svg\"/>\n";
static const QByteArray kJs = "export const answer = 42;\n";

void WebServer_Test::init()
{
    m_dir = new QTemporaryDir();
    QVERIFY(m_dir->isValid());

    writeFile(QStringLiteral("index.html"), kIndexHtml);
    writeFile(QStringLiteral("assets/icons/a.svg"), kSvg);
    writeFile(QStringLiteral("vendor/x.js"), kJs);
    writeFile(QStringLiteral("app.jsx"), kJs);
    writeFile(QStringLiteral("style.css"), "body{}\n");
    writeFile(QStringLiteral("data.unknownext"), "\x01\x02\x03");
    writeFile(QStringLiteral("sub/index.html"), "<p>sub</p>\n");

    m_server = new WebServer(nullptr);
    m_server->setRootDirectory(m_dir->path());
    m_server->setApiPort(9010);
    // LocalHost, port 0: same as every other controlapi suite - an
    // all-interfaces listener would trip the Windows Firewall prompt on each
    // freshly built test binary
    QVERIFY(m_server->listen(0, QHostAddress::LocalHost));
    QVERIFY(m_server->serverPort() != 0);
}

void WebServer_Test::cleanup()
{
    delete m_server;
    m_server = nullptr;
    delete m_dir;
    m_dir = nullptr;
}

void WebServer_Test::writeFile(const QString &relativePath, const QByteArray &content)
{
    QString absolute = m_dir->filePath(relativePath);
    QVERIFY(QDir().mkpath(QFileInfo(absolute).absolutePath()));
    QFile f(absolute);
    QVERIFY2(f.open(QIODevice::WriteOnly | QIODevice::Truncate), qPrintable(f.errorString()));
    QCOMPARE(f.write(content), qint64(content.size()));
}

bool WebServer_Test::request(const QByteArray &method, const QByteArray &target, HttpReply &reply)
{
    return requestRaw(method + ' ' + target + " HTTP/1.1\r\nHost: localhost\r\nUser-Agent: webserver_test\r\n\r\n", reply);
}

bool WebServer_Test::requestRaw(const QByteArray &rawRequest, HttpReply &reply)
{
    QTcpSocket sock;
    sock.connectToHost(QHostAddress::LocalHost, m_server->serverPort());
    if (QTest::qWaitFor([&sock]() { return sock.state() == QAbstractSocket::ConnectedState; }, 3000) == false)
    {
        qWarning() << "connect failed:" << sock.errorString();
        return false;
    }

    QByteArray data;
    connect(&sock, &QTcpSocket::readyRead, [&sock, &data]() { data += sock.readAll(); });
    sock.write(rawRequest);

    // Connection: close is the terminator - wait for the server to hang up
    if (QTest::qWaitFor([&sock]() { return sock.state() == QAbstractSocket::UnconnectedState; }, 5000) == false)
    {
        qWarning() << "no EOF from server within 5s, got so far:" << data;
        return false;
    }
    data += sock.readAll();

    int headEnd = data.indexOf("\r\n\r\n");
    if (headEnd < 0)
    {
        qWarning() << "no complete response head:" << data;
        return false;
    }
    QList<QByteArray> lines = data.left(headEnd).split('\r');
    for (QByteArray &l : lines)
        l = l.trimmed();
    // "HTTP/1.1 200 OK"
    QList<QByteArray> statusParts = lines.first().split(' ');
    if (statusParts.size() < 2 || statusParts.first() != "HTTP/1.1")
    {
        qWarning() << "bad status line:" << lines.first();
        return false;
    }
    reply.status = statusParts.at(1).toInt();
    reply.headers.clear();
    for (int i = 1; i < lines.size(); i++)
    {
        int colon = lines.at(i).indexOf(':');
        if (colon < 0)
            continue;
        reply.headers.insert(lines.at(i).left(colon).toLower(), lines.at(i).mid(colon + 1).trimmed());
    }
    reply.body = data.mid(headEnd + 4);
    return true;
}

/*********************************************************************
 * Cases
 *********************************************************************/

void WebServer_Test::rootServesIndexHtml()
{
    HttpReply r;
    QVERIFY(request("GET", "/", r));
    QCOMPARE(r.status, 200);
    QCOMPARE(r.headers.value("content-type"), QByteArray("text/html; charset=utf-8"));
    QCOMPARE(r.headers.value("content-length").toInt(), kIndexHtml.size());
    QCOMPARE(r.headers.value("cache-control"), QByteArray("no-cache"));
    QCOMPARE(r.headers.value("connection"), QByteArray("close"));
    QCOMPARE(r.body, kIndexHtml);

    // Explicit file name works the same
    QVERIFY(request("GET", "/index.html", r));
    QCOMPARE(r.status, 200);
    QCOMPARE(r.body, kIndexHtml);
}

void WebServer_Test::nestedFilesWithContentTypes()
{
    HttpReply r;
    QVERIFY(request("GET", "/assets/icons/a.svg", r));
    QCOMPARE(r.status, 200);
    QCOMPARE(r.headers.value("content-type"), QByteArray("image/svg+xml"));
    QCOMPARE(r.body, kSvg);

    QVERIFY(request("GET", "/vendor/x.js", r));
    QCOMPARE(r.status, 200);
    QCOMPARE(r.headers.value("content-type"), QByteArray("text/javascript; charset=utf-8"));
    QCOMPARE(r.body, kJs);

    QVERIFY(request("GET", "/app.jsx", r));
    QCOMPARE(r.status, 200);
    QCOMPARE(r.headers.value("content-type"), QByteArray("text/javascript; charset=utf-8"));

    QVERIFY(request("GET", "/style.css", r));
    QCOMPARE(r.status, 200);
    QCOMPARE(r.headers.value("content-type"), QByteArray("text/css; charset=utf-8"));

    QVERIFY(request("GET", "/data.unknownext", r));
    QCOMPARE(r.status, 200);
    QCOMPARE(r.headers.value("content-type"), QByteArray("application/octet-stream"));
    QCOMPARE(r.body, QByteArray("\x01\x02\x03"));
}

void WebServer_Test::queryStringIsIgnored()
{
    HttpReply r;
    QVERIFY(request("GET", "/vendor/x.js?v=123&cache=no", r));
    QCOMPARE(r.status, 200);
    QCOMPARE(r.body, kJs);
}

void WebServer_Test::directoryServesItsIndex()
{
    HttpReply r;
    QVERIFY(request("GET", "/sub/", r));
    QCOMPARE(r.status, 200);
    QCOMPARE(r.body, QByteArray("<p>sub</p>\n"));

    QVERIFY(request("GET", "/sub", r));
    QCOMPARE(r.status, 200);
    QCOMPARE(r.body, QByteArray("<p>sub</p>\n"));
}

void WebServer_Test::directoryWithoutIndexIs404()
{
    HttpReply r;
    QVERIFY(request("GET", "/assets/", r));
    QCOMPARE(r.status, 404);
    QVERIFY(request("GET", "/assets/icons", r));
    QCOMPARE(r.status, 404);
}

void WebServer_Test::missingFileIs404()
{
    HttpReply r;
    QVERIFY(request("GET", "/nope.html", r));
    QCOMPARE(r.status, 404);
    QCOMPARE(r.headers.value("content-type"), QByteArray("text/plain; charset=utf-8"));
    QCOMPARE(r.headers.value("content-length").toInt(), r.body.size());
    QCOMPARE(r.headers.value("cache-control"), QByteArray("no-cache"));

    QVERIFY(request("GET", "/assets/icons/missing.svg", r));
    QCOMPARE(r.status, 404);
}

void WebServer_Test::dotDotTraversalIs403()
{
    // Nothing called CMakeLists.txt exists next to the temp root either -
    // the point is that the containment check fires *before* any existence
    // check, so this is a 403 and not a 404
    HttpReply r;
    QVERIFY(request("GET", "/../CMakeLists.txt", r));
    QCOMPARE(r.status, 403);

    QVERIFY(request("GET", "/../", r));
    QCOMPARE(r.status, 403);

    QVERIFY(request("GET", "/assets/../../index.html", r));
    QCOMPARE(r.status, 403);

    QVERIFY(request("GET", "/..", r));
    QCOMPARE(r.status, 403);
}

void WebServer_Test::percentEncodedTraversalIs403()
{
    HttpReply r;
    QVERIFY(request("GET", "/%2e%2e/", r));
    QCOMPARE(r.status, 403);

    QVERIFY(request("GET", "/%2e%2e/%2e%2e/etc/passwd", r));
    QCOMPARE(r.status, 403);

    QVERIFY(request("GET", "/assets/%2E%2E/%2E%2E/CMakeLists.txt", r));
    QCOMPARE(r.status, 403);

    // Mixed: literal dot + encoded dot
    QVERIFY(request("GET", "/.%2e/index.html", r));
    QCOMPARE(r.status, 403);
}

void WebServer_Test::dotDotStayingInsideRootIsAllowed()
{
    // ".." that cleans back to somewhere inside the root is fine - that is
    // just an odd spelling of an allowed path
    HttpReply r;
    QVERIFY(request("GET", "/assets/../index.html", r));
    QCOMPARE(r.status, 200);
    QCOMPARE(r.body, kIndexHtml);

    QVERIFY(request("GET", "/assets/icons/../icons/a.svg", r));
    QCOMPARE(r.status, 200);
    QCOMPARE(r.body, kSvg);
}

void WebServer_Test::backslashIs403()
{
    HttpReply r;
    QVERIFY(request("GET", "/..\\..\\CMakeLists.txt", r));
    QCOMPARE(r.status, 403);
    QVERIFY(request("GET", "/%5c..%5cindex.html", r));
    QCOMPARE(r.status, 403);
}

void WebServer_Test::headSendsHeadersOnly()
{
    HttpReply r;
    QVERIFY(request("HEAD", "/", r));
    QCOMPARE(r.status, 200);
    QCOMPARE(r.headers.value("content-type"), QByteArray("text/html; charset=utf-8"));
    // Content-Length must be the GET length, not 0
    QCOMPARE(r.headers.value("content-length").toInt(), kIndexHtml.size());
    QVERIFY(r.body.isEmpty());

    QVERIFY(request("HEAD", "/nope.html", r));
    QCOMPARE(r.status, 404);
    QVERIFY(r.body.isEmpty());
    QVERIFY(r.headers.value("content-length").toInt() > 0);
}

void WebServer_Test::configJson()
{
    HttpReply r;
    QVERIFY(request("GET", "/qlcplus-config.json", r));
    QCOMPARE(r.status, 200);
    QCOMPARE(r.headers.value("content-type"), QByteArray("application/json; charset=utf-8"));
    QCOMPARE(r.headers.value("content-length").toInt(), r.body.size());

    QJsonParseError err;
    QJsonDocument doc = QJsonDocument::fromJson(r.body, &err);
    QVERIFY2(err.error == QJsonParseError::NoError, qPrintable(err.errorString()));
    QVERIFY(doc.isObject());
    QJsonObject obj = doc.object();
    QCOMPARE(obj.value(QStringLiteral("apiPort")).toInt(), 9010);
    QVERIFY(obj.contains(QStringLiteral("apiHost")));
    QVERIFY(obj.value(QStringLiteral("apiHost")).isNull());

    // Reflects a later change (main.cpp sets it from the *actual* API port
    // after ApiServer::listen())
    m_server->setApiPort(9020);
    QVERIFY(request("GET", "/qlcplus-config.json", r));
    QCOMPARE(QJsonDocument::fromJson(r.body).object().value(QStringLiteral("apiPort")).toInt(), 9020);

    // Generated even when a file of that name exists on disk
    writeFile(QStringLiteral("qlcplus-config.json"), "{\"apiPort\": 1}");
    QVERIFY(request("GET", "/qlcplus-config.json", r));
    QCOMPARE(QJsonDocument::fromJson(r.body).object().value(QStringLiteral("apiPort")).toInt(), 9020);
}

void WebServer_Test::postIs405()
{
    HttpReply r;
    QVERIFY(request("POST", "/", r));
    QCOMPARE(r.status, 405);
    QCOMPARE(r.headers.value("allow"), QByteArray("GET, HEAD"));

    QVERIFY(request("PUT", "/index.html", r));
    QCOMPARE(r.status, 405);
}

void WebServer_Test::malformedRequestIs400()
{
    HttpReply r;
    QVERIFY(requestRaw("GARBAGE\r\n\r\n", r));
    QCOMPARE(r.status, 400);

    QVERIFY(requestRaw("GET index.html HTTP/1.1\r\n\r\n", r));
    QCOMPARE(r.status, 400);

    QVERIFY(requestRaw("GET / SPDY/3\r\n\r\n", r));
    QCOMPARE(r.status, 400);
}

void WebServer_Test::missingRootStillListensAnd404s()
{
    // A wrong --webui-root must not prevent startup - the 404s are the
    // diagnostic. The warning it logs is checked by eye, not asserted.
    WebServer server(nullptr);
    server.setRootDirectory(m_dir->filePath(QStringLiteral("does-not-exist")));
    QVERIFY(server.listen(0, QHostAddress::LocalHost));

    WebServer *saved = m_server;
    m_server = &server;
    HttpReply r;
    bool ok = request("GET", "/", r);
    m_server = saved;
    QVERIFY(ok);
    QCOMPARE(r.status, 404);
}

void WebServer_Test::contentTypeMappings_data()
{
    QTest::addColumn<QString>("path");
    QTest::addColumn<QByteArray>("type");

    QTest::newRow("html")  << "index.html"       << QByteArray("text/html; charset=utf-8");
    QTest::newRow("HTML")  << "INDEX.HTML"       << QByteArray("text/html; charset=utf-8");
    QTest::newRow("js")    << "vendor/react.js"  << QByteArray("text/javascript; charset=utf-8");
    QTest::newRow("mjs")   << "app.mjs"          << QByteArray("text/javascript; charset=utf-8");
    QTest::newRow("jsx")   << "App.jsx"          << QByteArray("text/javascript; charset=utf-8");
    QTest::newRow("css")   << "a/b/c.css"        << QByteArray("text/css; charset=utf-8");
    QTest::newRow("svg")   << "assets/icons/x.svg" << QByteArray("image/svg+xml");
    QTest::newRow("json")  << "api/methods.json" << QByteArray("application/json; charset=utf-8");
    QTest::newRow("woff2") << "assets/fonts/f.woff2" << QByteArray("font/woff2");
    QTest::newRow("ttf")   << "assets/fonts/f.ttf" << QByteArray("font/ttf");
    QTest::newRow("otf")   << "assets/fonts/f.otf" << QByteArray("font/otf");
    QTest::newRow("png")   << "logo.png"         << QByteArray("image/png");
    QTest::newRow("none")  << "Makefile.unknownext" << QByteArray("application/octet-stream");
}

void WebServer_Test::contentTypeMappings()
{
    QFETCH(QString, path);
    QFETCH(QByteArray, type);
    QCOMPARE(WebServer::contentTypeForPath(path), type);
}

// GUILESS (not APPLESS): QTcpServer/QTcpSocket need a running QCoreApplication
// event loop, which QTest::qWaitFor() spins
QTEST_GUILESS_MAIN(WebServer_Test)
