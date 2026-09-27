/*
  Q Light Controller Plus - Control API unit test
  apimediadomain_test.cpp

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

#include "apimediadomain_test.h"
#include "apiserver.h"
#include "mediaassets.h"
#include "scriptwrapper.h"
#include "audio.h"
#include "video.h"
#include "scene.h"
#include "fixture.h"
#include "doc.h"

static QString buildRequest(const QString &method, const QJsonObject &params, const QString &id = QStringLiteral("t-1"))
{
    QJsonObject obj;
    obj.insert(QStringLiteral("type"), QStringLiteral("request"));
    obj.insert(QStringLiteral("id"), id);
    obj.insert(QStringLiteral("method"), method);
    obj.insert(QStringLiteral("params"), params);
    return QString::fromUtf8(QJsonDocument(obj).toJson(QJsonDocument::Compact));
}

void ApiMediaDomain_Test::init()
{
    m_tmp = new QTemporaryDir();
    QVERIFY(m_tmp->isValid());

    m_doc = new Doc(nullptr);
    // a titled project, so the media store is <tmp>/show.qxw.assets
    m_doc->setWorkspacePath(m_tmp->path());
    m_doc->assets()->setProjectFile(m_tmp->path() + "/show.qxw");

    m_scene = new Scene(m_doc);
    m_scene->setName(QStringLiteral("Test Scene"));
    m_scene->setValue(Fixture::invalidId(), 0, 255);
    QVERIFY(m_doc->addFunction(m_scene));

    m_apiServer = new ApiServer(nullptr, m_doc);
    QVERIFY(m_apiServer->listen(0, QHostAddress::LocalHost));

    m_client = new QWebSocket();
    m_client->open(QUrl(QStringLiteral("ws://127.0.0.1:%1/qlcplusapi").arg(m_apiServer->serverPort())));
    QVERIFY(QTest::qWaitFor([this]() { return m_client->state() == QAbstractSocket::ConnectedState; }, 2000));
}

void ApiMediaDomain_Test::cleanup()
{
    delete m_client;
    m_client = nullptr;
    delete m_apiServer;
    m_apiServer = nullptr;
    delete m_doc;
    m_doc = nullptr;
    m_scene = nullptr;
    delete m_tmp;
    m_tmp = nullptr;
}

QJsonObject ApiMediaDomain_Test::sendAndWaitForReply(const QString &method, const QJsonObject &params)
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

QString ApiMediaDomain_Test::helloAndGetClientId()
{
    QJsonObject reply = sendAndWaitForReply(QStringLiteral("hello"), QJsonObject());
    return reply.value(QStringLiteral("result")).toObject().value(QStringLiteral("clientId")).toString();
}

QJsonObject ApiMediaDomain_Test::waitForEvent(QSignalSpy &spy, const QString &topic,
                                              const std::function<bool(const QJsonObject &)> &accept, int timeoutMs)
{
    QJsonObject found;
    (void)QTest::qWaitFor([&]()
    {
        for (const QList<QVariant> &frame : spy)
        {
            QJsonObject obj = QJsonDocument::fromJson(frame.at(0).toString().toUtf8()).object();
            if (obj.value(QStringLiteral("type")).toString() == QStringLiteral("event") &&
                obj.value(QStringLiteral("topic")).toString() == topic &&
                accept(obj.value(QStringLiteral("data")).toObject()))
            {
                found = obj;
                return true;
            }
        }
        return false;
    }, timeoutMs);
    return found;
}

QJsonObject ApiMediaDomain_Test::withRevision(QJsonObject params) const
{
    params.insert(QStringLiteral("baseRevision"), int(m_doc->docRevision()));
    return params;
}

QString ApiMediaDomain_Test::writeMediaFile(const QString &name, const QByteArray &content)
{
    QString path = m_tmp->path() + "/" + name;
    QFile f(path);
    if (f.open(QIODevice::WriteOnly) == false)
        return QString();
    f.write(content);
    f.close();
    return QDir::cleanPath(QFileInfo(path).absoluteFilePath());
}

Script *ApiMediaDomain_Test::addScript(const QString &source)
{
    Script *script = new Script(m_doc);
    script->setName(QStringLiteral("Test Script"));
    script->setData(source);
    if (m_doc->addFunction(script) == false)
        return nullptr;
    return script;
}

Audio *ApiMediaDomain_Test::addAudio(const QString &path)
{
    Audio *audio = new Audio(m_doc);
    audio->setSourceFileName(path);
    if (m_doc->addFunction(audio) == false)
        return nullptr;
    return audio;
}

Video *ApiMediaDomain_Test::addVideo(const QString &url)
{
    Video *video = new Video(m_doc);
    video->setSourceUrl(url);
    if (m_doc->addFunction(video) == false)
        return nullptr;
    return video;
}

/*****************************************************************************
 * Script
 *****************************************************************************/

void ApiMediaDomain_Test::scriptListCommandsReportsEngineKeywordsAndSnippets()
{
    helloAndGetClientId();
    QJsonObject reply = sendAndWaitForReply(QStringLiteral("functions.script.listCommands"), QJsonObject());
    QCOMPARE(reply.value(QStringLiteral("ok")).toBool(), true);
    QJsonObject result = reply.value(QStringLiteral("result")).toObject();

    QStringList commands;
    for (const QJsonValue &v : result.value(QStringLiteral("commands")).toArray())
        commands << v.toString();
    QVERIFY(commands.contains(Script::startFunctionCmd));
    QVERIFY(commands.contains(Script::waitCmd));
    QVERIFY(commands.contains(Script::setFixtureCmd));

    QJsonArray snippets = result.value(QStringLiteral("snippets")).toArray();
    QVERIFY(snippets.count() >= 6);
    QJsonObject first = snippets.at(0).toObject();
    QCOMPARE(first.value(QStringLiteral("label")).toString(), QStringLiteral("Start function"));
    QVERIFY(first.value(QStringLiteral("insert")).toString().startsWith(Script::startFunctionCmd));
    QVERIFY(first.contains(QStringLiteral("caretOffset")));
}

void ApiMediaDomain_Test::scriptSetSourceReplacesBodyAndBroadcasts()
{
    QString clientId = helloAndGetClientId();
    Script *script = addScript(QStringLiteral("// empty\n"));
    QVERIFY(script != nullptr);
    quint32 before = m_doc->docRevision();

    QSignalSpy spy(m_client, &QWebSocket::textMessageReceived);
    QJsonObject params;
    params.insert(QStringLiteral("functionId"), QString::number(script->id()));
    params.insert(QStringLiteral("source"), QStringLiteral("Engine.waitTime(100);\nEngine.setBlackout(true);\n"));
    QJsonObject reply = sendAndWaitForReply(QStringLiteral("functions.script.setSource"), withRevision(params));
    QCOMPARE(reply.value(QStringLiteral("ok")).toBool(), true);
    QVERIFY(quint32(reply.value(QStringLiteral("result")).toObject().value(QStringLiteral("docRevision")).toInt()) > before);
    QCOMPARE(script->data(), QStringLiteral("Engine.waitTime(100);\nEngine.setBlackout(true);\n"));

    QJsonObject ev = waitForEvent(spy, QStringLiteral("functions.script.sourceChanged"), [&](const QJsonObject &d)
    {
        return d.value(QStringLiteral("functionId")).toString() == QString::number(script->id());
    });
    QVERIFY2(ev.isEmpty() == false, "missing functions.script.sourceChanged");
    QCOMPARE(ev.value(QStringLiteral("originClientId")).toString(), clientId);
    QCOMPARE(ev.value(QStringLiteral("data")).toObject().value(QStringLiteral("source")).toString(), script->data());
    QCOMPARE(ev.value(QStringLiteral("data")).toObject().value(QStringLiteral("docRevision")).toInt(), int(m_doc->docRevision()));
}

void ApiMediaDomain_Test::scriptAppendLineAppendsAndBroadcasts()
{
    helloAndGetClientId();
    Script *script = addScript(QStringLiteral("Engine.waitTime(100);\n"));
    QVERIFY(script != nullptr);
    quint32 before = m_doc->docRevision();

    QSignalSpy spy(m_client, &QWebSocket::textMessageReceived);
    QJsonObject params;
    params.insert(QStringLiteral("functionId"), QString::number(script->id()));
    params.insert(QStringLiteral("line"), QStringLiteral("Engine.setBlackout(false);"));
    QJsonObject reply = sendAndWaitForReply(QStringLiteral("functions.script.appendLine"), withRevision(params));
    QCOMPARE(reply.value(QStringLiteral("ok")).toBool(), true);
    QVERIFY(m_doc->docRevision() > before);
    QVERIFY(script->data().startsWith(QStringLiteral("Engine.waitTime(100);\n")));
    QVERIFY(script->data().contains(QStringLiteral("Engine.setBlackout(false);")));

    QJsonObject ev = waitForEvent(spy, QStringLiteral("functions.script.sourceChanged"), [&](const QJsonObject &d)
    {
        return d.value(QStringLiteral("source")).toString() == script->data();
    });
    QVERIFY2(ev.isEmpty() == false, "missing functions.script.sourceChanged after appendLine");
}

void ApiMediaDomain_Test::scriptValidateReportsErrorLinesAndRefs()
{
    helloAndGetClientId();
    // line 1 ok, line 2 references the scene, line 3 is not JavaScript
    Script *script = addScript(QStringLiteral("Engine.waitTime(100);\nEngine.startFunction(%1);\nthis is not a script\n")
                               .arg(m_scene->id()));
    QVERIFY(script != nullptr);

    QJsonObject params;
    params.insert(QStringLiteral("functionId"), QString::number(script->id()));
    QJsonObject reply = sendAndWaitForReply(QStringLiteral("functions.script.validate"), params);
    QCOMPARE(reply.value(QStringLiteral("ok")).toBool(), true);
    QJsonObject result = reply.value(QStringLiteral("result")).toObject();

    QJsonArray lines = result.value(QStringLiteral("syntaxErrorLines")).toArray();
    QJsonArray errors = result.value(QStringLiteral("syntaxErrors")).toArray();
    QVERIFY2(lines.count() >= 1, "expected at least one syntax error line");
    QCOMPARE(lines.count(), errors.count());
    QCOMPARE(lines.at(0).toInt(), 3);
    QCOMPARE(errors.at(0).toObject().value(QStringLiteral("line")).toInt(), 3);
    QVERIFY(errors.at(0).toObject().value(QStringLiteral("message")).toString().isEmpty() == false);

    QJsonArray refs = result.value(QStringLiteral("functionRefs")).toArray();
    QCOMPARE(refs.count(), 1);
    QCOMPARE(refs.at(0).toObject().value(QStringLiteral("functionId")).toString(), QString::number(m_scene->id()));
    QCOMPARE(refs.at(0).toObject().value(QStringLiteral("line")).toInt(), 1);
    QVERIFY(result.contains(QStringLiteral("fixtureRefs")));

    // a valid script reports nothing
    script->setData(QStringLiteral("Engine.waitTime(100);\n"));
    reply = sendAndWaitForReply(QStringLiteral("functions.script.validate"), params);
    result = reply.value(QStringLiteral("result")).toObject();
    QCOMPARE(result.value(QStringLiteral("syntaxErrorLines")).toArray().count(), 0);
    QCOMPARE(result.value(QStringLiteral("syntaxErrors")).toArray().count(), 0);
}

void ApiMediaDomain_Test::scriptGetCarriesSourceOnly()
{
    helloAndGetClientId();
    // The typeDetail itself must not run the syntax check (no interrupt in
    // the QJSEngine evaluation). NOTE: the generic part of functions.get
    // still evaluates a scriptv4 body through Script::totalDuration() - a
    // pre-existing engine behaviour, so a for(;;) script cannot be used
    // here (it would spin / crash inside the engine, not in this domain).
    Script *script = addScript(QStringLiteral("Engine.waitTime(100);\n@@ nope\n"));
    QVERIFY(script != nullptr);

    QJsonObject params;
    params.insert(QStringLiteral("functionId"), QString::number(script->id()));
    QJsonObject reply = sendAndWaitForReply(QStringLiteral("functions.get"), params);
    QCOMPARE(reply.value(QStringLiteral("ok")).toBool(), true);
    QJsonObject result = reply.value(QStringLiteral("result")).toObject();
    QCOMPARE(result.value(QStringLiteral("type")).toString(), QStringLiteral("Script"));

    QJsonObject typeDetail = result.value(QStringLiteral("typeDetail")).toObject();
    QCOMPARE(typeDetail.value(QStringLiteral("functionId")).toString(), QString::number(script->id()));
    QCOMPARE(typeDetail.value(QStringLiteral("source")).toString(), script->data());
    QVERIFY(typeDetail.contains(QStringLiteral("docRevision")));
    QVERIFY(typeDetail.contains(QStringLiteral("syntaxErrors")) == false);
    QVERIFY(typeDetail.contains(QStringLiteral("syntaxErrorLines")) == false);
}

void ApiMediaDomain_Test::scriptWithEndlessLoopDoesNotHangGet()
{
    // Crash audit: functions.get evaluates a scriptv4 body on the main
    // thread (Script::totalDuration() -> ScriptRunner::collectScriptData());
    // an endless loop froze the whole application for good. The dry run is
    // now interrupted by a watchdog after 0.5 s.
    helloAndGetClientId();
    Script *script = addScript(QStringLiteral("for (;;) {}\n"));
    QVERIFY(script != nullptr);

    QElapsedTimer timer;
    timer.start();
    QJsonObject request;
    request.insert(QStringLiteral("type"), QStringLiteral("request"));
    request.insert(QStringLiteral("id"), QStringLiteral("t-loop"));
    request.insert(QStringLiteral("method"), QStringLiteral("functions.get"));
    request.insert(QStringLiteral("params"), QJsonObject{ { QStringLiteral("functionId"), QString::number(script->id()) } });
    QSignalSpy spy(m_client, &QWebSocket::textMessageReceived);
    m_client->sendTextMessage(QString::fromUtf8(QJsonDocument(request).toJson(QJsonDocument::Compact)));
    QVERIFY(QTest::qWaitFor([&]()
    {
        for (const QList<QVariant> &frame : spy)
            if (frame.at(0).toString().contains(QStringLiteral("\"t-loop\"")))
                return true;
        return false;
    }, 8000));
    QVERIFY(timer.elapsed() < 8000);

    // the syntax check runs the same dry run and reports the interruption
    QVERIFY(script->syntaxErrorsLines().isEmpty() == false);
}

void ApiMediaDomain_Test::scriptSetSourceOnStaleRevisionIsConflict()
{
    helloAndGetClientId();
    Script *script = addScript(QStringLiteral("// a\n"));
    QVERIFY(script != nullptr);

    QJsonObject params;
    params.insert(QStringLiteral("functionId"), QString::number(script->id()));
    params.insert(QStringLiteral("source"), QStringLiteral("// b\n"));
    params.insert(QStringLiteral("baseRevision"), int(m_doc->docRevision()) + 7);
    QJsonObject reply = sendAndWaitForReply(QStringLiteral("functions.script.setSource"), params);
    QCOMPARE(reply.value(QStringLiteral("ok")).toBool(), false);
    QCOMPARE(reply.value(QStringLiteral("error")).toObject().value(QStringLiteral("code")).toString(), QStringLiteral("CONFLICT"));
    QCOMPARE(script->data(), QStringLiteral("// a\n"));
}

void ApiMediaDomain_Test::scriptMethodsOnWrongTypeAreInvalidParams()
{
    helloAndGetClientId();

    QJsonObject params;
    params.insert(QStringLiteral("functionId"), QString::number(m_scene->id()));
    params.insert(QStringLiteral("source"), QStringLiteral("x"));
    QJsonObject reply = sendAndWaitForReply(QStringLiteral("functions.script.setSource"), withRevision(params));
    QCOMPARE(reply.value(QStringLiteral("ok")).toBool(), false);
    QCOMPARE(reply.value(QStringLiteral("error")).toObject().value(QStringLiteral("code")).toString(), QStringLiteral("INVALID_PARAMS"));

    params.insert(QStringLiteral("volume"), 0.5);
    reply = sendAndWaitForReply(QStringLiteral("functions.audio.setVolume"), withRevision(params));
    QCOMPARE(reply.value(QStringLiteral("error")).toObject().value(QStringLiteral("code")).toString(), QStringLiteral("INVALID_PARAMS"));

    params.insert(QStringLiteral("functionId"), QStringLiteral("99999"));
    reply = sendAndWaitForReply(QStringLiteral("functions.video.setLayer"), withRevision(params));
    QCOMPARE(reply.value(QStringLiteral("error")).toObject().value(QStringLiteral("code")).toString(), QStringLiteral("NOT_FOUND"));
}

/*****************************************************************************
 * Audio
 *****************************************************************************/

void ApiMediaDomain_Test::audioListCapabilitiesHasDefaultDevice()
{
    helloAndGetClientId();
    QJsonObject reply = sendAndWaitForReply(QStringLiteral("functions.audio.listCapabilities"), QJsonObject());
    QCOMPARE(reply.value(QStringLiteral("ok")).toBool(), true);
    QJsonObject result = reply.value(QStringLiteral("result")).toObject();
    QVERIFY(result.value(QStringLiteral("extensions")).isArray());
    QJsonArray devices = result.value(QStringLiteral("devices")).toArray();
    QVERIFY(devices.count() >= 1);
    QCOMPARE(devices.at(0).toObject().value(QStringLiteral("id")).toString(), QString());
    QCOMPARE(devices.at(0).toObject().value(QStringLiteral("name")).toString(), QStringLiteral("Default device"));
}

void ApiMediaDomain_Test::audioSetVolumeDurationDeviceBumpRevisionAndBroadcast()
{
    helloAndGetClientId();
    Audio *audio = addAudio(writeMediaFile("song.wav", "not really audio"));
    QVERIFY(audio != nullptr);
    QTRY_VERIFY_WITH_TIMEOUT(audio->bpmAnalysisState() != Audio::Analyzing, 10000);
    QString fid = QString::number(audio->id());

    QSignalSpy spy(m_client, &QWebSocket::textMessageReceived);

    quint32 before = m_doc->docRevision();
    QJsonObject params;
    params.insert(QStringLiteral("functionId"), fid);
    params.insert(QStringLiteral("volume"), 0.25);
    QJsonObject reply = sendAndWaitForReply(QStringLiteral("functions.audio.setVolume"), withRevision(params));
    QCOMPARE(reply.value(QStringLiteral("ok")).toBool(), true);
    QCOMPARE(audio->volume(), 0.25);
    QCOMPARE(m_doc->docRevision(), before + 1);
    QJsonObject ev = waitForEvent(spy, QStringLiteral("functions.audio.volumeChanged"), [&](const QJsonObject &d)
    {
        return d.value(QStringLiteral("functionId")).toString() == fid && d.value(QStringLiteral("volume")).toDouble() == 0.25;
    });
    QVERIFY2(ev.isEmpty() == false, "missing functions.audio.volumeChanged");

    before = m_doc->docRevision();
    params = QJsonObject();
    params.insert(QStringLiteral("functionId"), fid);
    params.insert(QStringLiteral("duration"), 4321);
    reply = sendAndWaitForReply(QStringLiteral("functions.audio.setDuration"), withRevision(params));
    QCOMPARE(reply.value(QStringLiteral("ok")).toBool(), true);
    QCOMPARE(audio->totalDuration(), quint32(4321));
    QCOMPARE(audio->duration(), quint32(4321));
    QCOMPARE(m_doc->docRevision(), before + 1);
    ev = waitForEvent(spy, QStringLiteral("functions.audio.durationChanged"), [&](const QJsonObject &d)
    {
        return d.value(QStringLiteral("functionId")).toString() == fid && d.value(QStringLiteral("duration")).toInt() == 4321;
    });
    QVERIFY2(ev.isEmpty() == false, "missing functions.audio.durationChanged");

    before = m_doc->docRevision();
    params = QJsonObject();
    params.insert(QStringLiteral("functionId"), fid);
    params.insert(QStringLiteral("audioDevice"), QStringLiteral("dev-private-name"));
    reply = sendAndWaitForReply(QStringLiteral("functions.audio.setDevice"), withRevision(params));
    QCOMPARE(reply.value(QStringLiteral("ok")).toBool(), true);
    QCOMPARE(audio->audioDevice(), QStringLiteral("dev-private-name"));
    QCOMPARE(m_doc->docRevision(), before + 1);
    ev = waitForEvent(spy, QStringLiteral("functions.audio.deviceChanged"), [&](const QJsonObject &d)
    {
        return d.value(QStringLiteral("audioDevice")).toString() == QStringLiteral("dev-private-name");
    });
    QVERIFY2(ev.isEmpty() == false, "missing functions.audio.deviceChanged");

    // back to the default output
    params.insert(QStringLiteral("audioDevice"), QString());
    reply = sendAndWaitForReply(QStringLiteral("functions.audio.setDevice"), withRevision(params));
    QCOMPARE(reply.value(QStringLiteral("ok")).toBool(), true);
    QVERIFY(audio->audioDevice().isEmpty());
}

void ApiMediaDomain_Test::audioSetVolumeRejectsOutOfRange()
{
    helloAndGetClientId();
    Audio *audio = addAudio(writeMediaFile("song.wav", "not really audio"));
    QVERIFY(audio != nullptr);
    QTRY_VERIFY_WITH_TIMEOUT(audio->bpmAnalysisState() != Audio::Analyzing, 10000);
    audio->setVolume(0.7);

    QJsonObject params;
    params.insert(QStringLiteral("functionId"), QString::number(audio->id()));
    params.insert(QStringLiteral("volume"), 1.5);
    QJsonObject reply = sendAndWaitForReply(QStringLiteral("functions.audio.setVolume"), withRevision(params));
    QCOMPARE(reply.value(QStringLiteral("ok")).toBool(), false);
    QCOMPARE(reply.value(QStringLiteral("error")).toObject().value(QStringLiteral("code")).toString(), QStringLiteral("INVALID_PARAMS"));
    QCOMPARE(audio->volume(), 0.7);

    params.insert(QStringLiteral("volume"), QStringLiteral("loud"));
    reply = sendAndWaitForReply(QStringLiteral("functions.audio.setVolume"), withRevision(params));
    QCOMPARE(reply.value(QStringLiteral("ok")).toBool(), false);
    QCOMPARE(audio->volume(), 0.7);
}

void ApiMediaDomain_Test::audioSetSourceImportsIntoStoreAndBroadcasts()
{
    helloAndGetClientId();
    QString first = writeMediaFile("first.wav", "first bytes");
    QString second = writeMediaFile("second.wav", "second bytes");
    Audio *audio = addAudio(first);
    QVERIFY(audio != nullptr);
    QTRY_VERIFY_WITH_TIMEOUT(audio->bpmAnalysisState() != Audio::Analyzing, 10000);
    QString fid = QString::number(audio->id());
    // an external reference until the API repoints it
    QCOMPARE(m_doc->assets()->isManaged(audio->getSourceFileName()), false);

    QSignalSpy spy(m_client, &QWebSocket::textMessageReceived);
    quint32 before = m_doc->docRevision();
    QJsonObject params;
    params.insert(QStringLiteral("functionId"), fid);
    params.insert(QStringLiteral("sourceFileName"), second);
    QJsonObject reply = sendAndWaitForReply(QStringLiteral("functions.audio.setSource"), withRevision(params));
    QCOMPARE(reply.value(QStringLiteral("ok")).toBool(), true);
    QVERIFY(m_doc->docRevision() > before);

    // copied into the store, renamed after the file like the editors' Replace
    QVERIFY(m_doc->assets()->isManaged(audio->getSourceFileName()));
    QCOMPARE(QFileInfo(audio->getSourceFileName()).fileName(), QStringLiteral("second.wav"));
    QCOMPARE(audio->name(), QStringLiteral("second.wav"));
    QVERIFY(QFile::exists(second));

    QJsonObject ev = waitForEvent(spy, QStringLiteral("functions.audio.sourceChanged"), [&](const QJsonObject &d)
    {
        return d.value(QStringLiteral("functionId")).toString() == fid;
    });
    QVERIFY2(ev.isEmpty() == false, "missing functions.audio.sourceChanged");
    QJsonObject data = ev.value(QStringLiteral("data")).toObject();
    QVERIFY(data.value(QStringLiteral("sourceFileName")).toString().startsWith(QStringLiteral("show.qxw.assets/")));
    QVERIFY(data.contains(QStringLiteral("detectedDurationMs")));
    QTRY_VERIFY_WITH_TIMEOUT(audio->bpmAnalysisState() != Audio::Analyzing, 10000);
}

void ApiMediaDomain_Test::audioSetSourceOnMissingFileIsInvalidParams()
{
    helloAndGetClientId();
    QString first = writeMediaFile("first.wav", "first bytes");
    Audio *audio = addAudio(first);
    QVERIFY(audio != nullptr);
    QTRY_VERIFY_WITH_TIMEOUT(audio->bpmAnalysisState() != Audio::Analyzing, 10000);

    QJsonObject params;
    params.insert(QStringLiteral("functionId"), QString::number(audio->id()));
    params.insert(QStringLiteral("sourceFileName"), m_tmp->path() + "/does-not-exist.wav");
    QJsonObject reply = sendAndWaitForReply(QStringLiteral("functions.audio.setSource"), withRevision(params));
    QCOMPARE(reply.value(QStringLiteral("ok")).toBool(), false);
    QCOMPARE(reply.value(QStringLiteral("error")).toObject().value(QStringLiteral("code")).toString(), QStringLiteral("INVALID_PARAMS"));
    QCOMPARE(audio->getSourceFileName(), first);

    // an Audio takes no URL
    params.insert(QStringLiteral("sourceFileName"), QStringLiteral("http://example.org/a.mp3"));
    reply = sendAndWaitForReply(QStringLiteral("functions.audio.setSource"), withRevision(params));
    QCOMPARE(reply.value(QStringLiteral("ok")).toBool(), false);
    QCOMPARE(audio->getSourceFileName(), first);
}

void ApiMediaDomain_Test::audioGetCarriesConfig()
{
    helloAndGetClientId();
    Audio *audio = addAudio(writeMediaFile("song.wav", "not really audio"));
    QVERIFY(audio != nullptr);
    QTRY_VERIFY_WITH_TIMEOUT(audio->bpmAnalysisState() != Audio::Analyzing, 10000);
    audio->setVolume(0.4);
    audio->setAudioDevice(QStringLiteral("dev-x"));
    audio->setTotalDuration(777);

    QJsonObject params;
    params.insert(QStringLiteral("functionId"), QString::number(audio->id()));
    QJsonObject reply = sendAndWaitForReply(QStringLiteral("functions.get"), params);
    QCOMPARE(reply.value(QStringLiteral("ok")).toBool(), true);
    QJsonObject cfg = reply.value(QStringLiteral("result")).toObject()
                          .value(QStringLiteral("typeDetail")).toObject()
                          .value(QStringLiteral("config")).toObject();
    QCOMPARE(cfg.value(QStringLiteral("volume")).toDouble(), 0.4);
    QCOMPARE(cfg.value(QStringLiteral("audioDevice")).toString(), QStringLiteral("dev-x"));
    QCOMPARE(cfg.value(QStringLiteral("duration")).toInt(), 777);
    QCOMPARE(cfg.value(QStringLiteral("muted")).toBool(), false);
    QVERIFY(cfg.value(QStringLiteral("bpm")).isObject());
    QVERIFY(cfg.contains(QStringLiteral("sampleRate")));
}

/*****************************************************************************
 * Video
 *****************************************************************************/

void ApiMediaDomain_Test::videoListCapabilitiesHasExtensions()
{
    helloAndGetClientId();
    QJsonObject reply = sendAndWaitForReply(QStringLiteral("functions.video.listCapabilities"), QJsonObject());
    QCOMPARE(reply.value(QStringLiteral("ok")).toBool(), true);
    QJsonObject result = reply.value(QStringLiteral("result")).toObject();
    QStringList videoExt;
    for (const QJsonValue &v : result.value(QStringLiteral("videoExtensions")).toArray())
        videoExt << v.toString();
    QVERIFY(videoExt.contains(QStringLiteral("*.mp4")));
    QStringList picExt;
    for (const QJsonValue &v : result.value(QStringLiteral("pictureExtensions")).toArray())
        picExt << v.toString();
    QVERIFY(picExt.contains(QStringLiteral("*.png")));
    // a bare QCoreApplication has no screens, but the key is always there
    QVERIFY(result.value(QStringLiteral("screens")).isArray());
    QVERIFY(result.contains(QStringLiteral("spoutAvailable")));
}

void ApiMediaDomain_Test::videoSetGeometryRotationLayer()
{
    helloAndGetClientId();
    Video *video = addVideo(QStringLiteral("http://example.org/clip.mp4"));
    QVERIFY(video != nullptr);
    QString fid = QString::number(video->id());
    QSignalSpy spy(m_client, &QWebSocket::textMessageReceived);

    quint32 before = m_doc->docRevision();
    QJsonObject geometry;
    geometry.insert(QStringLiteral("x"), 10);
    geometry.insert(QStringLiteral("y"), 20);
    geometry.insert(QStringLiteral("width"), 640);
    geometry.insert(QStringLiteral("height"), 360);
    QJsonObject params;
    params.insert(QStringLiteral("functionId"), fid);
    params.insert(QStringLiteral("customGeometry"), geometry);
    QJsonObject reply = sendAndWaitForReply(QStringLiteral("functions.video.setGeometry"), withRevision(params));
    QCOMPARE(reply.value(QStringLiteral("ok")).toBool(), true);
    QCOMPARE(video->customGeometry(), QRect(10, 20, 640, 360));
    QCOMPARE(m_doc->docRevision(), before + 1);
    QJsonObject ev = waitForEvent(spy, QStringLiteral("functions.video.geometryChanged"), [&](const QJsonObject &d)
    {
        return d.value(QStringLiteral("functionId")).toString() == fid &&
               d.value(QStringLiteral("customGeometry")).toObject().value(QStringLiteral("width")).toInt() == 640;
    });
    QVERIFY2(ev.isEmpty() == false, "missing functions.video.geometryChanged");

    // explicit null clears it, the event reports null
    params.insert(QStringLiteral("customGeometry"), QJsonValue());
    reply = sendAndWaitForReply(QStringLiteral("functions.video.setGeometry"), withRevision(params));
    QCOMPARE(reply.value(QStringLiteral("ok")).toBool(), true);
    QVERIFY(video->customGeometry().isNull());
    ev = waitForEvent(spy, QStringLiteral("functions.video.geometryChanged"), [&](const QJsonObject &d)
    {
        return d.value(QStringLiteral("functionId")).toString() == fid && d.value(QStringLiteral("customGeometry")).isNull();
    });
    QVERIFY2(ev.isEmpty() == false, "missing functions.video.geometryChanged {null}");

    // omitted leaves it alone and does not bump the revision
    before = m_doc->docRevision();
    params.remove(QStringLiteral("customGeometry"));
    reply = sendAndWaitForReply(QStringLiteral("functions.video.setGeometry"), withRevision(params));
    QCOMPARE(reply.value(QStringLiteral("ok")).toBool(), true);
    QCOMPARE(m_doc->docRevision(), before);

    before = m_doc->docRevision();
    QJsonObject rotation;
    rotation.insert(QStringLiteral("x"), 0);
    rotation.insert(QStringLiteral("y"), 90);
    rotation.insert(QStringLiteral("z"), -45);
    params = QJsonObject();
    params.insert(QStringLiteral("functionId"), fid);
    params.insert(QStringLiteral("rotation"), rotation);
    reply = sendAndWaitForReply(QStringLiteral("functions.video.setRotation"), withRevision(params));
    QCOMPARE(reply.value(QStringLiteral("ok")).toBool(), true);
    QCOMPARE(video->rotation(), QVector3D(0, 90, -45));
    QCOMPARE(m_doc->docRevision(), before + 1);
    ev = waitForEvent(spy, QStringLiteral("functions.video.rotationChanged"), [&](const QJsonObject &d)
    {
        return d.value(QStringLiteral("rotation")).toObject().value(QStringLiteral("y")).toDouble() == 90.0;
    });
    QVERIFY2(ev.isEmpty() == false, "missing functions.video.rotationChanged");

    before = m_doc->docRevision();
    params = QJsonObject();
    params.insert(QStringLiteral("functionId"), fid);
    params.insert(QStringLiteral("zIndex"), 7);
    reply = sendAndWaitForReply(QStringLiteral("functions.video.setLayer"), withRevision(params));
    QCOMPARE(reply.value(QStringLiteral("ok")).toBool(), true);
    QCOMPARE(video->zIndex(), 7);
    QCOMPARE(m_doc->docRevision(), before + 1);
    ev = waitForEvent(spy, QStringLiteral("functions.video.layerChanged"), [&](const QJsonObject &d)
    {
        return d.value(QStringLiteral("zIndex")).toInt() == 7;
    });
    QVERIFY2(ev.isEmpty() == false, "missing functions.video.layerChanged");
}

void ApiMediaDomain_Test::videoSetScreenTargetKeepsSpoutUnlessModeGiven()
{
    helloAndGetClientId();
    Video *video = addVideo(QStringLiteral("http://example.org/clip.mp4"));
    QVERIFY(video != nullptr);
    QString fid = QString::number(video->id());
    QSignalSpy spy(m_client, &QWebSocket::textMessageReceived);

    QJsonObject params;
    params.insert(QStringLiteral("functionId"), fid);
    params.insert(QStringLiteral("screen"), 1);
    params.insert(QStringLiteral("fullscreen"), true);
    QJsonObject reply = sendAndWaitForReply(QStringLiteral("functions.video.setScreenTarget"), withRevision(params));
    QCOMPARE(reply.value(QStringLiteral("ok")).toBool(), true);
    QCOMPARE(video->screen(), 1);
    QCOMPARE(video->fullscreen(), true);
    QCOMPARE(video->outputMode(), Video::Fullscreen);
    QJsonObject ev = waitForEvent(spy, QStringLiteral("functions.video.screenTargetChanged"), [&](const QJsonObject &d)
    {
        return d.value(QStringLiteral("functionId")).toString() == fid && d.value(QStringLiteral("screen")).toInt() == 1;
    });
    QVERIFY2(ev.isEmpty() == false, "missing functions.video.screenTargetChanged");
    QCOMPARE(ev.value(QStringLiteral("data")).toObject().value(QStringLiteral("outputMode")).toString(), QStringLiteral("fullscreen"));

    // the two-state flag back to windowed
    params.insert(QStringLiteral("fullscreen"), false);
    reply = sendAndWaitForReply(QStringLiteral("functions.video.setScreenTarget"), withRevision(params));
    QCOMPARE(reply.value(QStringLiteral("ok")).toBool(), true);
    QCOMPARE(video->outputMode(), Video::Windowed);

    // a Spout video is not dropped back to windowed by fullscreen=false
    video->setOutputMode(Video::Spout);
    params.insert(QStringLiteral("screen"), 0);
    reply = sendAndWaitForReply(QStringLiteral("functions.video.setScreenTarget"), withRevision(params));
    QCOMPARE(reply.value(QStringLiteral("ok")).toBool(), true);
    QCOMPARE(video->outputMode(), Video::Spout);
    QCOMPARE(video->screen(), 0);

    // ...but an explicit outputMode wins
    params.insert(QStringLiteral("outputMode"), QStringLiteral("windowed"));
    reply = sendAndWaitForReply(QStringLiteral("functions.video.setScreenTarget"), withRevision(params));
    QCOMPARE(reply.value(QStringLiteral("ok")).toBool(), true);
    QCOMPARE(video->outputMode(), Video::Windowed);

    params.insert(QStringLiteral("outputMode"), QStringLiteral("sideways"));
    reply = sendAndWaitForReply(QStringLiteral("functions.video.setScreenTarget"), withRevision(params));
    QCOMPARE(reply.value(QStringLiteral("ok")).toBool(), false);
    QCOMPARE(reply.value(QStringLiteral("error")).toObject().value(QStringLiteral("code")).toString(), QStringLiteral("INVALID_PARAMS"));
}

void ApiMediaDomain_Test::videoSetSourceUrlKeepsUrlAndBroadcasts()
{
    helloAndGetClientId();
    Video *video = addVideo(QStringLiteral("http://example.org/clip.mp4"));
    QVERIFY(video != nullptr);
    QString fid = QString::number(video->id());
    QSignalSpy spy(m_client, &QWebSocket::textMessageReceived);

    quint32 before = m_doc->docRevision();
    QJsonObject params;
    params.insert(QStringLiteral("functionId"), fid);
    params.insert(QStringLiteral("sourceUrl"), QStringLiteral("rtsp://example.org/live.sdp"));
    QJsonObject reply = sendAndWaitForReply(QStringLiteral("functions.video.setSource"), withRevision(params));
    QCOMPARE(reply.value(QStringLiteral("ok")).toBool(), true);
    QVERIFY(m_doc->docRevision() > before);
    QCOMPARE(video->sourceUrl(), QStringLiteral("rtsp://example.org/live.sdp"));
    QCOMPARE(m_doc->assets()->isManaged(video->sourceUrl()), false);

    QJsonObject ev = waitForEvent(spy, QStringLiteral("functions.video.sourceChanged"), [&](const QJsonObject &d)
    {
        return d.value(QStringLiteral("functionId")).toString() == fid;
    });
    QVERIFY2(ev.isEmpty() == false, "missing functions.video.sourceChanged");
    QJsonObject data = ev.value(QStringLiteral("data")).toObject();
    QCOMPARE(data.value(QStringLiteral("sourceUrl")).toString(), QStringLiteral("rtsp://example.org/live.sdp"));
    QCOMPARE(data.value(QStringLiteral("isPicture")).toBool(), false);

    // a host file is copied into the store
    QString png = writeMediaFile("still.png", "not really a picture");
    params.insert(QStringLiteral("sourceUrl"), png);
    reply = sendAndWaitForReply(QStringLiteral("functions.video.setSource"), withRevision(params));
    QCOMPARE(reply.value(QStringLiteral("ok")).toBool(), true);
    QVERIFY(m_doc->assets()->isManaged(video->sourceUrl()));
    QCOMPARE(QFileInfo(video->sourceUrl()).fileName(), QStringLiteral("still.png"));
    QCOMPARE(video->isPicture(), true);
}

void ApiMediaDomain_Test::videoGetCarriesConfig()
{
    helloAndGetClientId();
    Video *video = addVideo(QStringLiteral("http://example.org/clip.mp4"));
    QVERIFY(video != nullptr);
    video->setZIndex(3);
    video->setScreen(2);
    video->setCustomGeometry(QRect(1, 2, 300, 200));
    video->setRotation(QVector3D(10, 20, 30));

    QJsonObject params;
    params.insert(QStringLiteral("functionId"), QString::number(video->id()));
    QJsonObject reply = sendAndWaitForReply(QStringLiteral("functions.get"), params);
    QCOMPARE(reply.value(QStringLiteral("ok")).toBool(), true);
    QJsonObject cfg = reply.value(QStringLiteral("result")).toObject()
                          .value(QStringLiteral("typeDetail")).toObject()
                          .value(QStringLiteral("config")).toObject();
    QCOMPARE(cfg.value(QStringLiteral("sourceUrl")).toString(), QStringLiteral("http://example.org/clip.mp4"));
    QCOMPARE(cfg.value(QStringLiteral("zIndex")).toInt(), 3);
    QCOMPARE(cfg.value(QStringLiteral("screen")).toInt(), 2);
    QCOMPARE(cfg.value(QStringLiteral("fullscreen")).toBool(), false);
    QCOMPARE(cfg.value(QStringLiteral("outputMode")).toString(), QStringLiteral("windowed"));
    QJsonObject g = cfg.value(QStringLiteral("customGeometry")).toObject();
    QCOMPARE(g.value(QStringLiteral("width")).toInt(), 300);
    QCOMPARE(g.value(QStringLiteral("height")).toInt(), 200);
    QJsonObject r = cfg.value(QStringLiteral("rotation")).toObject();
    QCOMPARE(r.value(QStringLiteral("z")).toDouble(), 30.0);
    QVERIFY(cfg.value(QStringLiteral("spoutSize")).isObject());
    QVERIFY(cfg.contains(QStringLiteral("volume")));
    QVERIFY(cfg.contains(QStringLiteral("muted")));
}

QTEST_GUILESS_MAIN(ApiMediaDomain_Test)
