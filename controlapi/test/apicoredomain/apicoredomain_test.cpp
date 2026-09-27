/*
  Q Light Controller Plus - Control API unit test
  apicoredomain_test.cpp

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

#include <QSignalSpy>
#include <QJsonDocument>
#include <QJsonArray>
#include <QWebSocket>
#include <QtTest>
#include <QSettings>
#include <QCoreApplication>
#include <QDir>
#include <QTemporaryDir>

#include "apicoredomain_test.h"
#include "apiserver.h"
#include "inputoutputmap.h"
#include "mastertimer.h"
#include "doc.h"
#include "apiprojecthost.h"
#include "qlcconfig.h"

static QString buildRequest(const QString &method, const QJsonObject &params, const QString &id = QStringLiteral("t-1"))
{
    QJsonObject obj;
    obj.insert(QStringLiteral("type"), QStringLiteral("request"));
    obj.insert(QStringLiteral("id"), id);
    obj.insert(QStringLiteral("method"), method);
    obj.insert(QStringLiteral("params"), params);
    return QString::fromUtf8(QJsonDocument(obj).toJson(QJsonDocument::Compact));
}

void ApiCoreDomain_Test::initTestCase()
{
    // core.settings.set/get round-trip through a default-constructed
    // QSettings, which needs an organization name to resolve to a real
    // backing store (registry key on Windows) - QCoreApplication never
    // gets one outside a full App instance. Point QSettings at a private,
    // per-run temp location so this doesn't touch the developer's real
    // QLC+ settings.
    QVERIFY(m_settingsDir.isValid());
    QCoreApplication::setOrganizationName(QStringLiteral("qlcplus-test"));
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, m_settingsDir.path());
}

void ApiCoreDomain_Test::init()
{
    m_doc = new Doc(nullptr);
    // Note: ApiCoreDomain relies on ApiServer's parent being an App instance
    // for project lifecycle methods. Since we don't have a full App here,
    // those methods will return UNAUTHORIZED/INTERNAL error in the domain.
    // But we can still test mode and settings which only need Doc and QSettings.
    m_apiServer = new ApiServer(nullptr, m_doc);
    QVERIFY(m_apiServer->listen(0, QHostAddress::LocalHost));

    m_client = new QWebSocket();
    m_client->open(QUrl(QStringLiteral("ws://127.0.0.1:%1/qlcplusapi").arg(m_apiServer->serverPort())));
    QVERIFY(QTest::qWaitFor([this]() { return m_client->state() == QAbstractSocket::ConnectedState; }, 2000));
}

void ApiCoreDomain_Test::cleanup()
{
    delete m_client;
    m_client = nullptr;
    delete m_apiServer;
    m_apiServer = nullptr;
    delete m_host;
    m_host = nullptr;
    delete m_doc;
    m_doc = nullptr;
}

QJsonObject ApiCoreDomain_Test::sendAndWaitForReply(const QString &method, const QJsonObject &params)
{
    const QString requestId = QStringLiteral("t-1");
    QSignalSpy spy(m_client, &QWebSocket::textMessageReceived);
    m_client->sendTextMessage(buildRequest(method, params, requestId));

    QJsonObject found;
    (void)QTest::qWaitFor([&]()
    {
        for (const QList<QVariant> &frame : spy)
        {
            QJsonObject obj = QJsonDocument::fromJson(frame.at(0).toString().toUtf8()).object();
            if (obj.value(QStringLiteral("type")).toString() == QStringLiteral("response") &&
                obj.value(QStringLiteral("id")).toString() == requestId)
            {
                found = obj;
                return true;
            }
        }
        return false;
    }, 2000);
    return found;
}

QString ApiCoreDomain_Test::helloAndGetClientId()
{
    QJsonObject reply = sendAndWaitForReply(QStringLiteral("hello"), QJsonObject());
    return reply.value(QStringLiteral("result")).toObject().value(QStringLiteral("clientId")).toString();
}

void ApiCoreDomain_Test::projectGetReturnsMetadata()
{
    helloAndGetClientId();
    QJsonObject reply = sendAndWaitForReply(QStringLiteral("core.project.get"), QJsonObject());
    QCOMPARE(reply.value(QStringLiteral("ok")).toBool(), true);
    QJsonObject result = reply.value(QStringLiteral("result")).toObject();
    QVERIFY(result.contains(QStringLiteral("isModified")));
    QVERIFY(result.contains(QStringLiteral("docRevision")));
}

void ApiCoreDomain_Test::modeGetSetBroadcastsEvent()
{
    QString clientId = helloAndGetClientId();

    // 1. Get initial mode
    QJsonObject reply = sendAndWaitForReply(QStringLiteral("core.mode.get"), QJsonObject());
    QCOMPARE(reply.value(QStringLiteral("result")).toObject().value(QStringLiteral("mode")).toString(), QStringLiteral("design"));

    // 2. Subscribe to events
    QJsonObject subParams;
    subParams.insert(QStringLiteral("topics"), QJsonArray() << QStringLiteral("core.mode.changed"));
    sendAndWaitForReply(QStringLiteral("subscribe"), subParams);

    // 3. Set mode to operate
    QSignalSpy spy(m_client, &QWebSocket::textMessageReceived);
    QJsonObject setParams;
    setParams.insert(QStringLiteral("mode"), QStringLiteral("operate"));
    reply = sendAndWaitForReply(QStringLiteral("core.mode.set"), setParams);
    QCOMPARE(reply.value(QStringLiteral("ok")).toBool(), true);

    // 4. Verify broadcast
    QVERIFY(QTest::qWaitFor([&]() { return spy.count() > 0; }, 2000));
    QJsonObject event = QJsonDocument::fromJson(spy.at(0).at(0).toString().toUtf8()).object();
    QCOMPARE(event.value(QStringLiteral("topic")).toString(), QStringLiteral("core.mode.changed"));
    QCOMPARE(event.value(QStringLiteral("data")).toObject().value(QStringLiteral("mode")).toString(), QStringLiteral("operate"));
    // originClientId should be attributed to the client that made the
    // change (00-conventions.md §3/§9), not left null.
    QCOMPARE(event.value(QStringLiteral("originClientId")).toString(), clientId);
}

void ApiCoreDomain_Test::settingsGetSetBroadcastsEvent()
{
    helloAndGetClientId();

    // 1. Subscribe
    QJsonObject subParams;
    subParams.insert(QStringLiteral("topics"), QJsonArray() << QStringLiteral("core.settings.changed"));
    sendAndWaitForReply(QStringLiteral("subscribe"), subParams);

    // 2. Set setting
    QSignalSpy spy(m_client, &QWebSocket::textMessageReceived);
    QJsonObject setParams;
    setParams.insert(QStringLiteral("masterTimerFrequencyHz"), 44);
    QJsonObject reply = sendAndWaitForReply(QStringLiteral("core.settings.set"), setParams);
    QCOMPARE(reply.value(QStringLiteral("ok")).toBool(), true);
    QCOMPARE(reply.value(QStringLiteral("result")).toObject().value(QStringLiteral("masterTimerFrequencyHz")).toInt(), 44);

    // 3. Verify broadcast
    QVERIFY(QTest::qWaitFor([&]() { return spy.count() > 0; }, 2000));
    QJsonObject event = QJsonDocument::fromJson(spy.at(0).at(0).toString().toUtf8()).object();
    QCOMPARE(event.value(QStringLiteral("topic")).toString(), QStringLiteral("core.settings.changed"));
    QCOMPARE(event.value(QStringLiteral("data")).toObject().value(QStringLiteral("masterTimerFrequencyHz")).toInt(), 44);
}

void ApiCoreDomain_Test::settingsRejectInvalidMasterTimerFrequency()
{
    // Crash audit: any value giving a 0 ms MasterTimer tick (0, negative,
    // non-numeric, > 1000) was stored as-is and broke the engine on the next
    // start (integer division by zero in Script waits), across restarts.
    helloAndGetClientId();
    QJsonObject good;
    good.insert(QStringLiteral("masterTimerFrequencyHz"), 50);
    QCOMPARE(sendAndWaitForReply(QStringLiteral("core.settings.set"), good).value(QStringLiteral("ok")).toBool(), true);

    const QList<QJsonValue> bad = { QJsonValue(0), QJsonValue(-5), QJsonValue(5000), QJsonValue(2.5),
                                    QJsonValue(QStringLiteral("fast")) };
    for (const QJsonValue &value : bad)
    {
        QJsonObject params;
        params.insert(QStringLiteral("masterTimerFrequencyHz"), value);
        params.insert(QStringLiteral("locale"), QStringLiteral("xx")); // must not be applied either
        QJsonObject reply = sendAndWaitForReply(QStringLiteral("core.settings.set"), params);
        QCOMPARE(reply.value(QStringLiteral("ok")).toBool(), false);
        QCOMPARE(reply.value(QStringLiteral("error")).toObject().value(QStringLiteral("code")).toString(), QStringLiteral("INVALID_PARAMS"));
    }
    QSettings settings;
    QCOMPARE(settings.value(QStringLiteral("mastertimer/frequency")).toInt(), 50);
    QVERIFY(settings.value(QStringLiteral("ui/language")).toString() != QStringLiteral("xx"));

    // A bad value already stored (older build) is ignored by MasterTimer
    settings.setValue(QStringLiteral("mastertimer/frequency"), 0);
    Doc *doc = new Doc(nullptr);
    QVERIFY(MasterTimer::frequency() >= 1);
    QVERIFY(MasterTimer::tick() >= 1);
    delete doc;
    settings.setValue(QStringLiteral("mastertimer/frequency"), 50);
}

QList<QJsonObject> ApiCoreDomain_Test::eventsWithTopic(QSignalSpy &spy, const QString &topic)
{
    QList<QJsonObject> events;
    for (const QList<QVariant> &frame : spy)
    {
        QJsonObject obj = QJsonDocument::fromJson(frame.at(0).toString().toUtf8()).object();
        if (obj.value(QStringLiteral("type")).toString() == QStringLiteral("event") &&
            obj.value(QStringLiteral("topic")).toString() == topic)
            events.append(obj);
    }
    return events;
}

/*********************************************************************
 * Beat generator (core.bpm.*, core.beat)
 *********************************************************************/

void ApiCoreDomain_Test::bpmGetReportsDisabledGeneratorOnFreshDoc()
{
    helloAndGetClientId();
    // A bare Doc never enables a generator (qmlui's App::initDoc() is what
    // switches it to Internal at startup) - so "off", bpm 0.
    QJsonObject reply = sendAndWaitForReply(QStringLiteral("core.bpm.get"), QJsonObject());
    QCOMPARE(reply.value(QStringLiteral("ok")).toBool(), true);
    QJsonObject result = reply.value(QStringLiteral("result")).toObject();
    QCOMPARE(result.value(QStringLiteral("generator")).toString(), QStringLiteral("disabled"));
    QCOMPARE(result.value(QStringLiteral("bpm")).toInt(), 0);
}

void ApiCoreDomain_Test::bpmSetEnablesInternalGeneratorAndBroadcasts()
{
    QString clientId = helloAndGetClientId();
    QSignalSpy spy(m_client, &QWebSocket::textMessageReceived);

    QJsonObject params;
    params.insert(QStringLiteral("bpm"), 128);
    QJsonObject reply = sendAndWaitForReply(QStringLiteral("core.bpm.set"), params);
    QCOMPARE(reply.value(QStringLiteral("ok")).toBool(), true);

    // Disabled -> Internal happened implicitly, and the tempo took
    QCOMPARE(m_doc->inputOutputMap()->beatGeneratorType(), InputOutputMap::Internal);
    QCOMPARE(m_doc->inputOutputMap()->bpmNumber(), 128);
    QJsonObject get = sendAndWaitForReply(QStringLiteral("core.bpm.get"), QJsonObject()).value(QStringLiteral("result")).toObject();
    QCOMPARE(get.value(QStringLiteral("generator")).toString(), QStringLiteral("internal"));
    QCOMPARE(get.value(QStringLiteral("bpm")).toInt(), 128);

    // core.bpm.changed carries the final state and is attributed to the requester
    QVERIFY(QTest::qWaitFor([&]() { return eventsWithTopic(spy, QStringLiteral("core.bpm.changed")).isEmpty() == false; }, 2000));
    QJsonObject last = eventsWithTopic(spy, QStringLiteral("core.bpm.changed")).last();
    QCOMPARE(last.value(QStringLiteral("data")).toObject().value(QStringLiteral("bpm")).toInt(), 128);
    QCOMPARE(last.value(QStringLiteral("data")).toObject().value(QStringLiteral("generator")).toString(), QStringLiteral("internal"));
    QCOMPARE(last.value(QStringLiteral("originClientId")).toString(), clientId);
}

void ApiCoreDomain_Test::bpmSetZeroDisablesGenerator()
{
    helloAndGetClientId();
    QJsonObject params;
    params.insert(QStringLiteral("bpm"), 120);
    QCOMPARE(sendAndWaitForReply(QStringLiteral("core.bpm.set"), params).value(QStringLiteral("ok")).toBool(), true);
    QCOMPARE(m_doc->inputOutputMap()->beatGeneratorType(), InputOutputMap::Internal);

    params.insert(QStringLiteral("bpm"), 0);
    QCOMPARE(sendAndWaitForReply(QStringLiteral("core.bpm.set"), params).value(QStringLiteral("ok")).toBool(), true);
    QCOMPARE(m_doc->inputOutputMap()->beatGeneratorType(), InputOutputMap::Disabled);
    QJsonObject get = sendAndWaitForReply(QStringLiteral("core.bpm.get"), QJsonObject()).value(QStringLiteral("result")).toObject();
    QCOMPARE(get.value(QStringLiteral("generator")).toString(), QStringLiteral("disabled"));
    QCOMPARE(get.value(QStringLiteral("bpm")).toInt(), 0);

    // ...and the explicit generator param works the other way round too
    QJsonObject genParams;
    genParams.insert(QStringLiteral("generator"), QStringLiteral("internal"));
    QCOMPARE(sendAndWaitForReply(QStringLiteral("core.bpm.set"), genParams).value(QStringLiteral("ok")).toBool(), true);
    QCOMPARE(m_doc->inputOutputMap()->beatGeneratorType(), InputOutputMap::Internal);
}

void ApiCoreDomain_Test::bpmSetRejectsOutOfRangeAndEmptyParams()
{
    helloAndGetClientId();

    QJsonObject tooHigh;
    tooHigh.insert(QStringLiteral("bpm"), 5000);
    QJsonObject reply = sendAndWaitForReply(QStringLiteral("core.bpm.set"), tooHigh);
    QCOMPARE(reply.value(QStringLiteral("ok")).toBool(), false);
    QCOMPARE(reply.value(QStringLiteral("error")).toObject().value(QStringLiteral("code")).toString(), QStringLiteral("INVALID_PARAMS"));
    // Nothing was touched by the rejected call
    QCOMPARE(m_doc->inputOutputMap()->beatGeneratorType(), InputOutputMap::Disabled);

    // Values outside int's range must be rejected the same way, not rounded
    // first (qRound on 1e300 is undefined behaviour).
    for (double bad : { 1e300, -1e300, -5.0 })
    {
        QJsonObject huge;
        huge.insert(QStringLiteral("bpm"), bad);
        reply = sendAndWaitForReply(QStringLiteral("core.bpm.set"), huge);
        QCOMPARE(reply.value(QStringLiteral("ok")).toBool(), false);
        QCOMPARE(reply.value(QStringLiteral("error")).toObject().value(QStringLiteral("code")).toString(), QStringLiteral("INVALID_PARAMS"));
        QCOMPARE(m_doc->inputOutputMap()->beatGeneratorType(), InputOutputMap::Disabled);
    }

    reply = sendAndWaitForReply(QStringLiteral("core.bpm.set"), QJsonObject());
    QCOMPARE(reply.value(QStringLiteral("ok")).toBool(), false);
    QCOMPARE(reply.value(QStringLiteral("error")).toObject().value(QStringLiteral("code")).toString(), QStringLiteral("INVALID_PARAMS"));

    QJsonObject badGenerator;
    badGenerator.insert(QStringLiteral("generator"), QStringLiteral("midi"));
    reply = sendAndWaitForReply(QStringLiteral("core.bpm.set"), badGenerator);
    QCOMPARE(reply.value(QStringLiteral("ok")).toBool(), false);
    QCOMPARE(reply.value(QStringLiteral("error")).toObject().value(QStringLiteral("code")).toString(), QStringLiteral("INVALID_PARAMS"));
}

void ApiCoreDomain_Test::bpmTapDerivesTempoFromTapSpacing()
{
    helloAndGetClientId();

    // First tap of a run: enables the generator, sets no tempo yet
    QJsonObject first = sendAndWaitForReply(QStringLiteral("core.bpm.tap"), QJsonObject());
    QCOMPARE(first.value(QStringLiteral("ok")).toBool(), true);
    QCOMPARE(first.value(QStringLiteral("result")).toObject().value(QStringLiteral("tapCount")).toInt(), 1);
    QCOMPARE(m_doc->inputOutputMap()->beatGeneratorType(), InputOutputMap::Internal);

    QTest::qWait(500);
    QJsonObject second = sendAndWaitForReply(QStringLiteral("core.bpm.tap"), QJsonObject());
    QCOMPARE(second.value(QStringLiteral("ok")).toBool(), true);
    int bpm = second.value(QStringLiteral("result")).toObject().value(QStringLiteral("bpm")).toInt();
    // ~120 BPM from a 500ms gap; generous bounds for event-loop jitter
    QVERIFY2(bpm >= 95 && bpm <= 140, qPrintable(QStringLiteral("unexpected tap tempo %1").arg(bpm)));
    QCOMPARE(m_doc->inputOutputMap()->bpmNumber(), bpm);
    QCOMPARE(second.value(QStringLiteral("result")).toObject().value(QStringLiteral("tapCount")).toInt(), 2);
}

void ApiCoreDomain_Test::beatEventFollowsInternalGeneratorTicks()
{
    helloAndGetClientId();
    // Internal beats are generated by MasterTimer's tick - needs the timer
    // thread, which the other cases in this suite deliberately leave off.
    m_doc->masterTimer()->start();

    QSignalSpy spy(m_client, &QWebSocket::textMessageReceived);
    QJsonObject params;
    params.insert(QStringLiteral("bpm"), 600); // one beat every 100ms
    QCOMPARE(sendAndWaitForReply(QStringLiteral("core.bpm.set"), params).value(QStringLiteral("ok")).toBool(), true);

    QVERIFY2(QTest::qWaitFor([&]() { return eventsWithTopic(spy, QStringLiteral("core.beat")).count() >= 2; }, 3000),
             "no core.beat events from the internal generator");
    QJsonObject beat = eventsWithTopic(spy, QStringLiteral("core.beat")).first();
    QCOMPARE(beat.value(QStringLiteral("data")).toObject().value(QStringLiteral("bpm")).toInt(), 600);
    QVERIFY(beat.value(QStringLiteral("originClientId")).isNull());

    m_doc->masterTimer()->stop();
}

/*********************************************************************
 * Undo / redo - only the no-host path is reachable here: the real
 * implementation lives in qmlui's App/Tardis, which controlapi/test
 * deliberately doesn't link (see apiserver.h).
 *********************************************************************/

void ApiCoreDomain_Test::undoRedoHistoryWithoutHostIsUnsupported()
{
    helloAndGetClientId();
    for (const QString &method : { QStringLiteral("core.undo"), QStringLiteral("core.redo"), QStringLiteral("core.history.get") })
    {
        QJsonObject reply = sendAndWaitForReply(method, QJsonObject());
        QCOMPARE(reply.value(QStringLiteral("ok")).toBool(), false);
        QCOMPARE(reply.value(QStringLiteral("error")).toObject().value(QStringLiteral("code")).toString(),
                 QStringLiteral("UNSUPPORTED"));
    }
}

void ApiCoreDomain_Test::fsListRootsWhenPathEmpty()
{
    helloAndGetClientId();
    QJsonObject reply = sendAndWaitForReply(QStringLiteral("core.fs.list"), QJsonObject());
    QCOMPARE(reply.value(QStringLiteral("ok")).toBool(), true);
    QJsonObject result = reply.value(QStringLiteral("result")).toObject();
    QCOMPARE(result.value(QStringLiteral("path")).toString(), QString());
    QVERIFY(result.value(QStringLiteral("parent")).isNull());
    QCOMPARE(result.value(QStringLiteral("entries")).toArray().count(), 0);

    QJsonArray roots = result.value(QStringLiteral("roots")).toArray();
    QVERIFY(roots.count() >= 2); // Home + at least one drive / "/"
    QCOMPARE(roots.at(0).toObject().value(QStringLiteral("name")).toString(), QStringLiteral("Home"));
    QCOMPARE(roots.at(0).toObject().value(QStringLiteral("path")).toString(), QDir::homePath());
    QVERIFY(QDir(roots.at(1).toObject().value(QStringLiteral("path")).toString()).isRoot());
}

void ApiCoreDomain_Test::fsListDirectoryFiltersAndSorts()
{
    helloAndGetClientId();
    QTemporaryDir tmp;
    QVERIFY(tmp.isValid());
    QDir dir(tmp.path());
    QVERIFY(dir.mkdir(QStringLiteral("zeta-folder")));
    QVERIFY(dir.mkdir(QStringLiteral("Alpha-folder")));
    for (const QString &name : { QStringLiteral("b.mp3"), QStringLiteral("A.MP3"), QStringLiteral("notes.txt") })
    {
        QFile f(dir.filePath(name));
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write("x");
        f.close();
    }

    QJsonObject params;
    params.insert(QStringLiteral("path"), tmp.path());
    QJsonObject reply = sendAndWaitForReply(QStringLiteral("core.fs.list"), params);
    QCOMPARE(reply.value(QStringLiteral("ok")).toBool(), true);
    QJsonObject result = reply.value(QStringLiteral("result")).toObject();
    QCOMPARE(result.value(QStringLiteral("path")).toString(), dir.absolutePath());
    QCOMPARE(result.value(QStringLiteral("parent")).toString(), QFileInfo(dir.absolutePath()).absolutePath());
    QVERIFY(result.value(QStringLiteral("roots")).toArray().count() >= 1);

    QStringList names;
    for (const QJsonValue &v : result.value(QStringLiteral("entries")).toArray())
        names << v.toObject().value(QStringLiteral("name")).toString();
    // directories first, then files, both case-insensitively by name
    QCOMPARE(names, QStringList() << "Alpha-folder" << "zeta-folder" << "A.MP3" << "b.mp3" << "notes.txt");

    QJsonObject first = result.value(QStringLiteral("entries")).toArray().at(0).toObject();
    QCOMPARE(first.value(QStringLiteral("isDir")).toBool(), true);
    QCOMPARE(first.value(QStringLiteral("size")).toInt(), 0);
    QCOMPARE(first.value(QStringLiteral("path")).toString(), dir.absoluteFilePath(QStringLiteral("Alpha-folder")));
    QJsonObject file = result.value(QStringLiteral("entries")).toArray().at(2).toObject();
    QCOMPARE(file.value(QStringLiteral("isDir")).toBool(), false);
    QCOMPARE(file.value(QStringLiteral("size")).toInt(), 1);
    QVERIFY(file.value(QStringLiteral("mtime")).toDouble() > 0);

    // glob filter applies to files only, directories always pass
    params.insert(QStringLiteral("extensions"), QJsonArray() << QStringLiteral("*.mp3"));
    reply = sendAndWaitForReply(QStringLiteral("core.fs.list"), params);
    names.clear();
    for (const QJsonValue &v : reply.value(QStringLiteral("result")).toObject().value(QStringLiteral("entries")).toArray())
        names << v.toObject().value(QStringLiteral("name")).toString();
    QCOMPARE(names, QStringList() << "Alpha-folder" << "zeta-folder" << "A.MP3" << "b.mp3");

    // folder picker mode
    params.remove(QStringLiteral("extensions"));
    params.insert(QStringLiteral("includeFiles"), false);
    reply = sendAndWaitForReply(QStringLiteral("core.fs.list"), params);
    names.clear();
    for (const QJsonValue &v : reply.value(QStringLiteral("result")).toObject().value(QStringLiteral("entries")).toArray())
        names << v.toObject().value(QStringLiteral("name")).toString();
    QCOMPARE(names, QStringList() << "Alpha-folder" << "zeta-folder");

    // a drive root has no parent
    QString root = QDir(tmp.path()).rootPath();
    params = QJsonObject();
    params.insert(QStringLiteral("path"), root);
    reply = sendAndWaitForReply(QStringLiteral("core.fs.list"), params);
    QCOMPARE(reply.value(QStringLiteral("ok")).toBool(), true);
    QVERIFY(reply.value(QStringLiteral("result")).toObject().value(QStringLiteral("parent")).isNull());
}

void ApiCoreDomain_Test::fsListRejectsRelativeAndMissingPaths()
{
    helloAndGetClientId();
    QJsonObject params;
    params.insert(QStringLiteral("path"), QStringLiteral("relative/dir"));
    QJsonObject reply = sendAndWaitForReply(QStringLiteral("core.fs.list"), params);
    QCOMPARE(reply.value(QStringLiteral("ok")).toBool(), false);
    QCOMPARE(reply.value(QStringLiteral("error")).toObject().value(QStringLiteral("code")).toString(), QStringLiteral("INVALID_PARAMS"));

    QTemporaryDir tmp;
    QVERIFY(tmp.isValid());
    params.insert(QStringLiteral("path"), tmp.path() + QStringLiteral("/does-not-exist"));
    reply = sendAndWaitForReply(QStringLiteral("core.fs.list"), params);
    QCOMPARE(reply.value(QStringLiteral("ok")).toBool(), false);
    QCOMPARE(reply.value(QStringLiteral("error")).toObject().value(QStringLiteral("code")).toString(), QStringLiteral("NOT_FOUND"));

    // a file is not a directory either
    QFile f(tmp.path() + QStringLiteral("/plain.txt"));
    QVERIFY(f.open(QIODevice::WriteOnly));
    f.close();
    params.insert(QStringLiteral("path"), tmp.path() + QStringLiteral("/plain.txt"));
    reply = sendAndWaitForReply(QStringLiteral("core.fs.list"), params);
    QCOMPARE(reply.value(QStringLiteral("ok")).toBool(), false);
    QCOMPARE(reply.value(QStringLiteral("error")).toObject().value(QStringLiteral("code")).toString(), QStringLiteral("NOT_FOUND"));
}

void ApiCoreDomain_Test::fsListRootsIncludeProjectFolder()
{
    helloAndGetClientId();
    auto projectRoot = [this]() -> QString {
        QJsonObject reply = sendAndWaitForReply(QStringLiteral("core.fs.list"), QJsonObject());
        for (const QJsonValue &v : reply.value(QStringLiteral("result")).toObject().value(QStringLiteral("roots")).toArray())
            if (v.toObject().value(QStringLiteral("name")).toString() == QStringLiteral("Project"))
                return v.toObject().value(QStringLiteral("path")).toString();
        return QString();
    };
    // a never-saved project has no folder
    m_doc->setWorkspacePath(QString());
    QCOMPARE(projectRoot(), QString());

    QTemporaryDir tmp;
    QVERIFY(tmp.isValid());
    m_doc->setWorkspacePath(tmp.path());
    QCOMPARE(projectRoot(), QDir(tmp.path()).absolutePath());

    // the drives stay right after Home (pickers rely on roots[1] being a drive)
    QJsonObject reply = sendAndWaitForReply(QStringLiteral("core.fs.list"), QJsonObject());
    QJsonArray roots = reply.value(QStringLiteral("result")).toObject().value(QStringLiteral("roots")).toArray();
    QVERIFY(QDir(roots.at(1).toObject().value(QStringLiteral("path")).toString()).isRoot());
}

/*********************************************************************
 * core.project.open {source: upload}
 *********************************************************************/

namespace
{

/** Minimal ApiProjectHost: records what the domain hands it. Loading from
 *  memory only clears the Doc (like App::slotLoadDocFromMemory() does
 *  before parsing), which is all these cases need. */
class FakeProjectHost : public QObject, public ApiProjectHost
{
public:
    explicit FakeProjectHost(Doc *doc) : m_doc(doc) {}

    QString fileName() const override { return m_fileName; }
    void setFileName(const QString &fileName) override { m_fileName = fileName; }
    bool newWorkspace() override { m_doc->clearContents(); m_fileName.clear(); return true; }
    bool loadWorkspace(const QString &fileName) override { m_doc->clearContents(); m_fileName = fileName; return true; }
    bool saveWorkspace(const QString &fileName) override { m_savedTo = fileName; return true; }
    void slotLoadDocFromMemory(QByteArray &xmlData) override { m_doc->clearContents(); m_loaded = xmlData; m_loads++; }
    QStringList recentFiles() const override { return QStringList(); }
    QString workingPath() const override { return QString(); }
    void setWorkingPath(QString) override {}

    Doc *m_doc;
    QString m_fileName;
    QString m_savedTo;
    QByteArray m_loaded;
    int m_loads = 0;
};

const char *kWorkspaceXml =
    "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
    "<!DOCTYPE Workspace>\n"
    "<Workspace xmlns=\"http://www.qlcplus.org/Workspace\" CurrentWindow=\"FixtureManager\">\n"
    " <Creator>\n  <Name>Q Light Controller Plus</Name>\n  <Version>4.12.0</Version>\n  <Author>test</Author>\n </Creator>\n"
    " <Engine/>\n"
    "</Workspace>\n";

QJsonObject uploadParams(const QByteArray &content, const QString &fileName)
{
    QJsonObject params;
    params.insert(QStringLiteral("source"), QStringLiteral("upload"));
    params.insert(QStringLiteral("fileName"), fileName);
    params.insert(QStringLiteral("contentBase64"), QString::fromLatin1(content.toBase64()));
    return params;
}

} // namespace

void ApiCoreDomain_Test::useFakeHost()
{
    delete m_client;
    delete m_apiServer;
    m_host = new FakeProjectHost(m_doc);
    m_apiServer = new ApiServer(m_host, m_doc);
    QVERIFY(m_apiServer->listen(0, QHostAddress::LocalHost));
    m_client = new QWebSocket();
    m_client->open(QUrl(QStringLiteral("ws://127.0.0.1:%1/qlcplusapi").arg(m_apiServer->serverPort())));
    QVERIFY(QTest::qWaitFor([this]() { return m_client->state() == QAbstractSocket::ConnectedState; }, 2000));
}

void ApiCoreDomain_Test::projectOpenUploadRejectsNonWorkspace()
{
    useFakeHost();
    FakeProjectHost *host = static_cast<FakeProjectHost *>(m_host);
    helloAndGetClientId();

    // not base64 at all, valid base64 of non-XML, and a fixture definition
    QJsonObject params = uploadParams(QByteArray(), QStringLiteral("x.qxw"));
    params.insert(QStringLiteral("contentBase64"), QStringLiteral("***not base64***"));
    const QList<QJsonObject> bad = {
        params,
        uploadParams(QByteArray("hello world"), QStringLiteral("x.qxw")),
        uploadParams(QByteArray("<?xml version=\"1.0\"?>\n<!DOCTYPE FixtureDefinition>\n<FixtureDefinition/>\n"), QStringLiteral("x.qxf")) };
    for (const QJsonObject &p : bad)
    {
        QJsonObject reply = sendAndWaitForReply(QStringLiteral("core.project.open"), p);
        QCOMPARE(reply.value(QStringLiteral("ok")).toBool(), false);
        QCOMPARE(reply.value(QStringLiteral("error")).toObject().value(QStringLiteral("code")).toString(),
                  QStringLiteral("INVALID_PARAMS"));
    }
    // the current project was never thrown away
    QCOMPARE(host->m_loads, 0);
}

void ApiCoreDomain_Test::projectOpenUploadHasNoPathButReportsName()
{
    useFakeHost();
    FakeProjectHost *host = static_cast<FakeProjectHost *>(m_host);
    host->m_fileName = QStringLiteral("C:/shows/previous.qxw");
    helloAndGetClientId();

    QSignalSpy spy(m_client, &QWebSocket::textMessageReceived);
    QJsonObject reply = sendAndWaitForReply(QStringLiteral("core.project.open"),
                                            uploadParams(QByteArray(kWorkspaceXml), QStringLiteral("folder/My Show.qxw")));
    QCOMPARE(reply.value(QStringLiteral("ok")).toBool(), true);
    QCOMPARE(host->m_loads, 1);
    QCOMPARE(host->m_loaded, QByteArray(kWorkspaceXml));
    // no path on the engine machine: Save must not write a relative file
    QCOMPARE(host->m_fileName, QString());

    QVERIFY(QTest::qWaitFor([&]() { return eventsWithTopic(spy, QStringLiteral("core.project.loaded")).isEmpty() == false; }, 2000));
    QJsonObject loaded = eventsWithTopic(spy, QStringLiteral("core.project.loaded")).first()
                             .value(QStringLiteral("data")).toObject().value(QStringLiteral("project")).toObject();
    QVERIFY(loaded.value(QStringLiteral("filePath")).isNull());
    QCOMPARE(loaded.value(QStringLiteral("fileName")).toString(), QStringLiteral("My Show.qxw"));

    QJsonObject project = sendAndWaitForReply(QStringLiteral("core.project.get"), QJsonObject())
                              .value(QStringLiteral("result")).toObject();
    QVERIFY(project.value(QStringLiteral("filePath")).isNull());
    QCOMPARE(project.value(QStringLiteral("fileName")).toString(), QStringLiteral("My Show.qxw"));

    reply = sendAndWaitForReply(QStringLiteral("core.project.save"), QJsonObject());
    QCOMPARE(reply.value(QStringLiteral("ok")).toBool(), false);
    QCOMPARE(reply.value(QStringLiteral("error")).toObject().value(QStringLiteral("code")).toString(),
              QStringLiteral("INVALID_STATE"));
    QVERIFY(host->m_savedTo.isEmpty());

    // a new project forgets the uploaded name
    QCOMPARE(sendAndWaitForReply(QStringLiteral("core.project.new"), QJsonObject()).value(QStringLiteral("ok")).toBool(), true);
    project = sendAndWaitForReply(QStringLiteral("core.project.get"), QJsonObject()).value(QStringLiteral("result")).toObject();
    QVERIFY(project.value(QStringLiteral("fileName")).isNull());
}

QTEST_GUILESS_MAIN(ApiCoreDomain_Test)
