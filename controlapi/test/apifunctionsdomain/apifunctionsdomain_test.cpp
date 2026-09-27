/*
  Q Light Controller Plus - Control API unit test
  apifunctionsdomain_test.cpp

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

#include "apifunctionsdomain_test.h"
#include "apiserver.h"
#include "mastertimer.h"
#include "mediaassets.h"
#include "audio.h"
#include "video.h"
#include "fixture.h"
#include "scene.h"
#include "chaser.h"
#include "collection.h"
#include "sequence.h"
#include "universe.h"
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

void ApiFunctionsDomain_Test::init()
{
    m_tmp = new QTemporaryDir();
    QVERIFY(m_tmp->isValid());

    m_doc = new Doc(nullptr);
    // a titled project, so the media store is <tmp>/show.qxw.assets
    m_doc->setWorkspacePath(m_tmp->path());
    m_doc->assets()->setProjectFile(m_tmp->path() + "/show.qxw");
    // Unlike apiiodomain_test, this suite needs MasterTimer's own thread
    // actually running: Function::start()/stop() only queue the request
    // (MasterTimer::startFunction()/its stop counterpart) - the running/
    // paused state transition itself happens on MasterTimer's next tick.
    m_doc->masterTimer()->start();

    m_scene = new Scene(m_doc);
    m_scene->setName(QStringLiteral("Test Scene"));
    // A Scene with no values at all self-stops the instant it starts
    // (Scene::write(): "m_values.count() == 0 && m_palettes.count() == 0").
    // A raw, non-fixture channel value (Fixture::invalidId(), matching
    // SimpleDesk's own raw-channel convention) is enough to keep it running
    // for this suite's purposes without needing a patched fixture/universe -
    // Scene::processValue() no-ops on it (no Fixture to resolve), so it
    // never actually drives DMX output, which this suite doesn't need.
    m_scene->setValue(Fixture::invalidId(), 0, 255);
    QVERIFY(m_doc->addFunction(m_scene));

    m_apiServer = new ApiServer(nullptr, m_doc);
    QVERIFY(m_apiServer->listen(0, QHostAddress::LocalHost));

    m_client = new QWebSocket();
    m_client->open(QUrl(QStringLiteral("ws://127.0.0.1:%1/qlcplusapi").arg(m_apiServer->serverPort())));
    QVERIFY(QTest::qWaitFor([this]() { return m_client->state() == QAbstractSocket::ConnectedState; }, 2000));
}

void ApiFunctionsDomain_Test::cleanup()
{
    delete m_client;
    m_client = nullptr;
    delete m_apiServer;
    m_apiServer = nullptr;
    m_doc->masterTimer()->stop();
    delete m_doc; // also deletes m_scene, which Doc owns after addFunction()
    m_doc = nullptr;
    m_scene = nullptr;
    delete m_tmp;
    m_tmp = nullptr;
}

QString ApiFunctionsDomain_Test::writeMediaFile(const QString &name, const QByteArray &content)
{
    QString path = m_tmp->path() + "/" + name;
    QFile f(path);
    if (f.open(QIODevice::WriteOnly) == false)
        return QString();
    f.write(content);
    f.close();
    return QDir::cleanPath(QFileInfo(path).absoluteFilePath());
}

QJsonObject ApiFunctionsDomain_Test::sendAndWaitForReply(const QString &method, const QJsonObject &params)
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

QString ApiFunctionsDomain_Test::helloAndGetClientId()
{
    QJsonObject reply = sendAndWaitForReply(QStringLiteral("hello"), QJsonObject());
    return reply.value(QStringLiteral("result")).toObject().value(QStringLiteral("clientId")).toString();
}

QString ApiFunctionsDomain_Test::createFunctionViaApi(const QString &type, const QJsonObject &extraParams)
{
    QJsonObject params = extraParams;
    params.insert(QStringLiteral("type"), type);
    params.insert(QStringLiteral("baseRevision"), int(m_doc->docRevision()));
    QJsonObject reply = sendAndWaitForReply(QStringLiteral("functions.create"), params);
    if (reply.value(QStringLiteral("ok")).toBool() == false)
        return QString();
    return reply.value(QStringLiteral("result")).toObject().value(QStringLiteral("functionId")).toString();
}

void ApiFunctionsDomain_Test::startRunsFunction()
{
    helloAndGetClientId();

    QJsonObject params;
    params.insert(QStringLiteral("functionId"), QString::number(m_scene->id()));
    QJsonObject reply = sendAndWaitForReply(QStringLiteral("functions.start"), params);

    QCOMPARE(reply.value(QStringLiteral("ok")).toBool(), true);
    QVERIFY(QTest::qWaitFor([this]() { return m_scene->isRunning(); }, 2000));
}

void ApiFunctionsDomain_Test::startOnMissingFunctionIsNotFound()
{
    helloAndGetClientId();

    QJsonObject params;
    params.insert(QStringLiteral("functionId"), QStringLiteral("999999"));
    QJsonObject reply = sendAndWaitForReply(QStringLiteral("functions.start"), params);

    QCOMPARE(reply.value(QStringLiteral("ok")).toBool(), false);
    QCOMPARE(reply.value(QStringLiteral("error")).toObject().value(QStringLiteral("code")).toString(),
              QStringLiteral("NOT_FOUND"));

    // Numeric ids outside quint32's range must be NOT_FOUND too, never cast
    // (double -> quint32 out of range is undefined behaviour).
    for (double bad : { 1e300, 4294967296.0, -1.0 })
    {
        QJsonObject numeric;
        numeric.insert(QStringLiteral("functionId"), bad);
        reply = sendAndWaitForReply(QStringLiteral("functions.start"), numeric);
        QCOMPARE(reply.value(QStringLiteral("ok")).toBool(), false);
        QCOMPARE(reply.value(QStringLiteral("error")).toObject().value(QStringLiteral("code")).toString(),
                  QStringLiteral("NOT_FOUND"));
    }
}

void ApiFunctionsDomain_Test::stopStopsFunction()
{
    helloAndGetClientId();

    QJsonObject startParams;
    startParams.insert(QStringLiteral("functionId"), QString::number(m_scene->id()));
    sendAndWaitForReply(QStringLiteral("functions.start"), startParams);
    QVERIFY(QTest::qWaitFor([this]() { return m_scene->isRunning(); }, 2000));

    QJsonObject stopReply = sendAndWaitForReply(QStringLiteral("functions.stop"), startParams);
    QCOMPARE(stopReply.value(QStringLiteral("ok")).toBool(), true);
    QVERIFY(QTest::qWaitFor([this]() { return m_scene->isRunning() == false; }, 2000));
}

void ApiFunctionsDomain_Test::setPausePausesRunningFunction()
{
    helloAndGetClientId();

    QJsonObject startParams;
    startParams.insert(QStringLiteral("functionId"), QString::number(m_scene->id()));
    sendAndWaitForReply(QStringLiteral("functions.start"), startParams);
    QVERIFY(QTest::qWaitFor([this]() { return m_scene->isRunning(); }, 2000));

    QJsonObject pauseParams = startParams;
    pauseParams.insert(QStringLiteral("paused"), true);
    QJsonObject reply = sendAndWaitForReply(QStringLiteral("functions.setPause"), pauseParams);

    QCOMPARE(reply.value(QStringLiteral("ok")).toBool(), true);
    // Function::setPause() sets the flag synchronously (unlike start/stop,
    // it doesn't go through MasterTimer's queue), so no polling wait needed.
    QCOMPARE(m_scene->isPaused(), true);
}

void ApiFunctionsDomain_Test::createSceneAddsFunctionAndBumpsRevision()
{
    helloAndGetClientId();
    quint32 baseRevision = m_doc->docRevision();

    QJsonObject params;
    params.insert(QStringLiteral("type"), QStringLiteral("Scene"));
    params.insert(QStringLiteral("name"), QStringLiteral("My New Scene"));
    params.insert(QStringLiteral("baseRevision"), int(baseRevision));
    QJsonObject reply = sendAndWaitForReply(QStringLiteral("functions.create"), params);

    QCOMPARE(reply.value(QStringLiteral("ok")).toBool(), true);
    QString functionIdStr = reply.value(QStringLiteral("result")).toObject().value(QStringLiteral("functionId")).toString();
    QVERIFY(functionIdStr.isEmpty() == false);
    QVERIFY(quint32(reply.value(QStringLiteral("result")).toObject().value(QStringLiteral("docRevision")).toInt()) > baseRevision);

    bool ok = false;
    quint32 functionId = functionIdStr.toUInt(&ok);
    QVERIFY(ok);

    Scene *created = qobject_cast<Scene *>(m_doc->function(functionId));
    QVERIFY(created != nullptr);
    QCOMPARE(created->name(), QStringLiteral("My New Scene"));
}

void ApiFunctionsDomain_Test::createBroadcastsFunctionsCreatedEvent()
{
    QString clientId = helloAndGetClientId();

    // Unlike sendAndWaitForReply() (which only surfaces the correlated
    // response), this test needs to see the unsolicited event frame that
    // the same request also triggers (00-conventions.md §3's "response to
    // the requester AND broadcasts... to all subscribed clients" rule) - so
    // it drives its own QSignalSpy over the whole request instead.
    QSignalSpy spy(m_client, &QWebSocket::textMessageReceived);
    QJsonObject params;
    params.insert(QStringLiteral("type"), QStringLiteral("Scene"));
    params.insert(QStringLiteral("baseRevision"), int(m_doc->docRevision()));
    m_client->sendTextMessage(buildRequest(QStringLiteral("functions.create"), params, QStringLiteral("t-1")));

    QJsonObject eventObj;
    QVERIFY(QTest::qWaitFor([&]()
    {
        for (const QList<QVariant> &frame : spy)
        {
            QJsonObject obj = QJsonDocument::fromJson(frame.at(0).toString().toUtf8()).object();
            if (obj.value(QStringLiteral("type")).toString() == QStringLiteral("event") &&
                obj.value(QStringLiteral("topic")).toString() == QStringLiteral("functions.created"))
            {
                eventObj = obj;
                return true;
            }
        }
        return false;
    }, 2000));

    QJsonObject data = eventObj.value(QStringLiteral("data")).toObject();
    QJsonObject functionSummary = data.value(QStringLiteral("function")).toObject();
    QVERIFY(functionSummary.value(QStringLiteral("id")).toString().isEmpty() == false);
    QCOMPARE(functionSummary.value(QStringLiteral("type")).toString(), QStringLiteral("Scene"));
    QCOMPARE(int(data.value(QStringLiteral("docRevision")).toInt()), int(m_doc->docRevision()));
    QCOMPARE(eventObj.value(QStringLiteral("originClientId")).toString(), clientId);
}

void ApiFunctionsDomain_Test::createOnStaleRevisionIsConflict()
{
    helloAndGetClientId();

    QJsonObject params;
    params.insert(QStringLiteral("type"), QStringLiteral("Scene"));
    params.insert(QStringLiteral("baseRevision"), int(m_doc->docRevision()) + 999);
    QJsonObject reply = sendAndWaitForReply(QStringLiteral("functions.create"), params);

    QCOMPARE(reply.value(QStringLiteral("ok")).toBool(), false);
    QCOMPARE(reply.value(QStringLiteral("error")).toObject().value(QStringLiteral("code")).toString(),
              QStringLiteral("CONFLICT"));
}

void ApiFunctionsDomain_Test::createSequenceAutoCreatesHiddenBoundScene()
{
    helloAndGetClientId();

    QString sequenceIdStr = createFunctionViaApi(QStringLiteral("Sequence"));
    QVERIFY(sequenceIdStr.isEmpty() == false);

    Sequence *sequence = qobject_cast<Sequence *>(m_doc->function(sequenceIdStr.toUInt()));
    QVERIFY(sequence != nullptr);

    Scene *boundScene = qobject_cast<Scene *>(m_doc->function(sequence->boundSceneID()));
    QVERIFY(boundScene != nullptr);
    QCOMPARE(boundScene->isVisible(), false);
}

void ApiFunctionsDomain_Test::getReturnsGenericAndSceneTypeDetail()
{
    helloAndGetClientId();
    m_scene->setValue(1, 2, 200);

    QJsonObject params;
    params.insert(QStringLiteral("functionId"), QString::number(m_scene->id()));
    QJsonObject reply = sendAndWaitForReply(QStringLiteral("functions.get"), params);

    QCOMPARE(reply.value(QStringLiteral("ok")).toBool(), true);
    QJsonObject result = reply.value(QStringLiteral("result")).toObject();
    QCOMPARE(result.value(QStringLiteral("type")).toString(), QStringLiteral("Scene"));
    QCOMPARE(result.value(QStringLiteral("name")).toString(), QStringLiteral("Test Scene"));

    QJsonObject typeDetail = result.value(QStringLiteral("typeDetail")).toObject();
    QCOMPARE(typeDetail.value(QStringLiteral("functionId")).toString(), QString::number(m_scene->id()));
    QCOMPARE(typeDetail.value(QStringLiteral("values")).toObject().value(QStringLiteral("1.2")).toInt(), 200);
}

void ApiFunctionsDomain_Test::listFiltersByType()
{
    helloAndGetClientId();
    QVERIFY(createFunctionViaApi(QStringLiteral("Chaser")).isEmpty() == false);

    QJsonObject params;
    params.insert(QStringLiteral("typeFilter"), QJsonArray{ QStringLiteral("Scene") });
    QJsonObject reply = sendAndWaitForReply(QStringLiteral("functions.list"), params);

    QCOMPARE(reply.value(QStringLiteral("ok")).toBool(), true);
    QJsonArray functions = reply.value(QStringLiteral("result")).toObject().value(QStringLiteral("functions")).toArray();
    QCOMPARE(functions.count(), 1);
    QCOMPARE(functions.at(0).toObject().value(QStringLiteral("type")).toString(), QStringLiteral("Scene"));
}

void ApiFunctionsDomain_Test::deleteRemovesFunction()
{
    helloAndGetClientId();
    QString functionIdStr = createFunctionViaApi(QStringLiteral("Chaser"));
    QVERIFY(functionIdStr.isEmpty() == false);
    quint32 functionId = functionIdStr.toUInt();

    QJsonObject params;
    params.insert(QStringLiteral("functionId"), functionIdStr);
    params.insert(QStringLiteral("baseRevision"), int(m_doc->docRevision()));
    QJsonObject reply = sendAndWaitForReply(QStringLiteral("functions.delete"), params);

    QCOMPARE(reply.value(QStringLiteral("ok")).toBool(), true);
    QVERIFY(m_doc->function(functionId) == nullptr);
}

void ApiFunctionsDomain_Test::deleteRunningFunctionStopsItFirst()
{
    // Crash audit: Doc::deleteFunction() frees the Function without
    // stopping it, and MasterTimer keeps raw Function* in its running list
    // and start queue - deleting a running (or just-started) function left
    // a dangling pointer the MasterTimer thread dereferenced on its next tick.
    helloAndGetClientId();
    QJsonObject idParams;
    idParams.insert(QStringLiteral("functionId"), QString::number(m_scene->id()));
    QCOMPARE(sendAndWaitForReply(QStringLiteral("functions.start"), idParams).value(QStringLiteral("ok")).toBool(), true);
    QVERIFY(QTest::qWaitFor([this]() { return m_scene->isRunning(); }, 2000));
    QCOMPARE(m_doc->masterTimer()->runningFunctions(), 1);

    QJsonObject params = idParams;
    params.insert(QStringLiteral("baseRevision"), int(m_doc->docRevision()));
    QJsonObject reply = sendAndWaitForReply(QStringLiteral("functions.delete"), params);
    QCOMPARE(reply.value(QStringLiteral("ok")).toBool(), true);
    // Must be off MasterTimer's list by the time the delete is acknowledged
    QCOMPARE(m_doc->masterTimer()->runningFunctions(), 0);
    m_scene = nullptr;

    // Same thing for a function whose start is still queued: start + delete
    // back to back, before MasterTimer's next tick picks the start up.
    Scene *second = new Scene(m_doc);
    second->setValue(Fixture::invalidId(), 0, 255);
    QVERIFY(m_doc->addFunction(second));
    QJsonObject secondId;
    secondId.insert(QStringLiteral("functionId"), QString::number(second->id()));
    QJsonObject secondDelete = secondId;
    secondDelete.insert(QStringLiteral("baseRevision"), int(m_doc->docRevision()));
    m_client->sendTextMessage(buildRequest(QStringLiteral("functions.start"), secondId, QStringLiteral("t-start")));
    reply = sendAndWaitForReply(QStringLiteral("functions.delete"), secondDelete);
    QCOMPARE(reply.value(QStringLiteral("ok")).toBool(), true);
    QCOMPARE(m_doc->masterTimer()->runningFunctions(), 0);
    QTest::qWait(100); // a few MasterTimer ticks: must not touch the freed function
    QCOMPARE(m_doc->masterTimer()->runningFunctions(), 0);
}

void ApiFunctionsDomain_Test::deleteChildOfRunningParentStopsParent()
{
    // Crash audit: a running Show/Collection/Chaser keeps playing its child
    // by id or by raw pointer (ShowRunner caches Function*); deleting the
    // child left the parent calling into freed memory or Q_ASSERTing on the
    // id no longer resolving (Collection::postRun/write/setPause).
    helloAndGetClientId();
    Collection *collection = new Collection(m_doc);
    QVERIFY(m_doc->addFunction(collection));
    QVERIFY(collection->addFunction(m_scene->id()));

    QJsonObject startParams;
    startParams.insert(QStringLiteral("functionId"), QString::number(collection->id()));
    QCOMPARE(sendAndWaitForReply(QStringLiteral("functions.start"), startParams).value(QStringLiteral("ok")).toBool(), true);
    QVERIFY(QTest::qWaitFor([&]() { return collection->isRunning() && m_scene->isRunning(); }, 2000));

    QJsonObject params;
    params.insert(QStringLiteral("functionId"), QString::number(m_scene->id()));
    params.insert(QStringLiteral("baseRevision"), int(m_doc->docRevision()));
    QJsonObject reply = sendAndWaitForReply(QStringLiteral("functions.delete"), params);
    QCOMPARE(reply.value(QStringLiteral("ok")).toBool(), true);
    m_scene = nullptr;
    QCOMPARE(collection->isRunning(), false);
    QCOMPARE(m_doc->masterTimer()->runningFunctions(), 0);
    QTest::qWait(100);
    QCOMPARE(collection->functions().count(), 0);
}

void ApiFunctionsDomain_Test::renameChangesName()
{
    helloAndGetClientId();

    QJsonObject params;
    params.insert(QStringLiteral("functionId"), QString::number(m_scene->id()));
    params.insert(QStringLiteral("name"), QStringLiteral("Renamed Scene"));
    params.insert(QStringLiteral("baseRevision"), int(m_doc->docRevision()));
    QJsonObject reply = sendAndWaitForReply(QStringLiteral("functions.rename"), params);

    QCOMPARE(reply.value(QStringLiteral("ok")).toBool(), true);
    QCOMPARE(m_scene->name(), QStringLiteral("Renamed Scene"));
}

void ApiFunctionsDomain_Test::moveChangesPath()
{
    helloAndGetClientId();

    QJsonObject params;
    params.insert(QStringLiteral("functionIds"), QJsonArray{ QString::number(m_scene->id()) });
    params.insert(QStringLiteral("path"), QStringLiteral("MyFolder"));
    params.insert(QStringLiteral("baseRevision"), int(m_doc->docRevision()));
    QJsonObject reply = sendAndWaitForReply(QStringLiteral("functions.move"), params);

    QCOMPARE(reply.value(QStringLiteral("ok")).toBool(), true);
    QCOMPARE(m_scene->path(true), QStringLiteral("MyFolder"));
}

void ApiFunctionsDomain_Test::updateChangesGenericProperties()
{
    helloAndGetClientId();

    QJsonObject params;
    params.insert(QStringLiteral("functionId"), QString::number(m_scene->id()));
    params.insert(QStringLiteral("runOrder"), QStringLiteral("PingPong"));
    params.insert(QStringLiteral("blendMode"), QStringLiteral("Additive"));
    params.insert(QStringLiteral("baseRevision"), int(m_doc->docRevision()));
    QJsonObject reply = sendAndWaitForReply(QStringLiteral("functions.update"), params);

    QCOMPARE(reply.value(QStringLiteral("ok")).toBool(), true);
    QCOMPARE(m_scene->runOrder(), Function::PingPong);
    QCOMPARE(m_scene->blendMode(), Universe::AdditiveBlend);
}

void ApiFunctionsDomain_Test::createAudioWithSourceImportsIntoStore()
{
    helloAndGetClientId();
    QString wav = writeMediaFile("song.wav", "not really audio");

    QJsonObject extra;
    extra.insert(QStringLiteral("name"), QStringLiteral("Intro song"));
    extra.insert(QStringLiteral("source"), wav);
    QString fid = createFunctionViaApi(QStringLiteral("Audio"), extra);
    QVERIFY(fid.isEmpty() == false);

    Function *function = m_doc->function(fid.toUInt());
    QVERIFY(function != nullptr);
    QCOMPARE(function->type(), Function::AudioType);
    Audio *audio = static_cast<Audio *>(function);
    // the function points at the copy in the store, not at the picked file,
    // and an explicit name survives the setter's rename-after-file
    QVERIFY(m_doc->assets()->isManaged(audio->getSourceFileName()));
    QVERIFY(QFile::exists(audio->getSourceFileName()));
    QCOMPARE(QFileInfo(audio->getSourceFileName()).fileName(), QString("song.wav"));
    QCOMPARE(audio->name(), QString("Intro song"));
    QVERIFY(QFile::exists(wav));

    // Creating the Audio also kicked off its offline BPM analysis; when that
    // finishes (quickly, for a file that isn't audio at all) the function
    // emits changed() and the Doc revision goes up. Let it settle before the
    // next revision-checked call, or baseRevision below is stale by the time
    // the server handles it - which is exactly what happened whenever the
    // decoder libraries were already warm from an earlier test binary.
    QTRY_VERIFY_WITH_TIMEOUT(audio->bpmAnalysisState() != Audio::Analyzing, 10000);

    // without a name the file name is used, like the editors do
    QJsonObject extra2;
    extra2.insert(QStringLiteral("source"), wav);
    QString fid2 = createFunctionViaApi(QStringLiteral("Audio"), extra2);
    QVERIFY(fid2.isEmpty() == false);
    QCOMPARE(m_doc->function(fid2.toUInt())->name(), QString("song.wav"));
    // same content: one stored copy for both
    QCOMPARE(static_cast<Audio *>(m_doc->function(fid2.toUInt()))->getSourceFileName(), audio->getSourceFileName());
}

void ApiFunctionsDomain_Test::createVideoWithUrlSourceKeepsUrl()
{
    helloAndGetClientId();

    QJsonObject extra;
    extra.insert(QStringLiteral("source"), QStringLiteral("rtsp://camera.local/stream"));
    QString fid = createFunctionViaApi(QStringLiteral("Video"), extra);
    QVERIFY(fid.isEmpty() == false);
    Video *video = static_cast<Video *>(m_doc->function(fid.toUInt()));
    QCOMPARE(video->sourceUrl(), QString("rtsp://camera.local/stream"));
    QCOMPARE(m_doc->assets()->isManaged(video->sourceUrl()), false);

    QJsonObject params;
    params.insert(QStringLiteral("functionId"), fid);
    QJsonObject reply = sendAndWaitForReply(QStringLiteral("functions.get"), params);
    QJsonObject typeDetail = reply.value(QStringLiteral("result")).toObject().value(QStringLiteral("typeDetail")).toObject();
    QCOMPARE(typeDetail.value(QStringLiteral("source")).toString(), QString("rtsp://camera.local/stream"));
    QCOMPARE(typeDetail.value(QStringLiteral("managed")).toBool(), false);
}

void ApiFunctionsDomain_Test::createWithMissingSourceIsInvalidParams()
{
    helloAndGetClientId();
    int functionsBefore = m_doc->functions().count();

    QJsonObject params;
    params.insert(QStringLiteral("type"), QStringLiteral("Video"));
    params.insert(QStringLiteral("source"), m_tmp->path() + "/nope.mp4");
    params.insert(QStringLiteral("baseRevision"), int(m_doc->docRevision()));
    QJsonObject reply = sendAndWaitForReply(QStringLiteral("functions.create"), params);
    QCOMPARE(reply.value(QStringLiteral("ok")).toBool(), false);
    QCOMPARE(reply.value(QStringLiteral("error")).toObject().value(QStringLiteral("code")).toString(),
             QStringLiteral("INVALID_PARAMS"));
    // nothing was created
    QCOMPARE(m_doc->functions().count(), functionsBefore);

    // source on a type that takes none
    params.insert(QStringLiteral("type"), QStringLiteral("Scene"));
    params.insert(QStringLiteral("source"), writeMediaFile("a.wav", "x"));
    reply = sendAndWaitForReply(QStringLiteral("functions.create"), params);
    QCOMPARE(reply.value(QStringLiteral("ok")).toBool(), false);
    QCOMPARE(m_doc->functions().count(), functionsBefore);
}

void ApiFunctionsDomain_Test::updateSourceReplacesMediaFile()
{
    helloAndGetClientId();
    QString first = writeMediaFile("first.mp4", "first clip");
    QString second = writeMediaFile("second.mp4", "second clip");

    QJsonObject extra;
    extra.insert(QStringLiteral("source"), first);
    QString fid = createFunctionViaApi(QStringLiteral("Video"), extra);
    Video *video = static_cast<Video *>(m_doc->function(fid.toUInt()));
    QString storedFirst = video->sourceUrl();
    QVERIFY(m_doc->assets()->isManaged(storedFirst));

    QJsonObject params;
    params.insert(QStringLiteral("functionId"), fid);
    params.insert(QStringLiteral("source"), second);
    params.insert(QStringLiteral("baseRevision"), int(m_doc->docRevision()));
    QJsonObject reply = sendAndWaitForReply(QStringLiteral("functions.update"), params);
    QCOMPARE(reply.value(QStringLiteral("ok")).toBool(), true);
    QVERIFY(m_doc->assets()->isManaged(video->sourceUrl()));
    QCOMPARE(QFileInfo(video->sourceUrl()).fileName(), QString("second.mp4"));
    QCOMPARE(video->name(), QString("second.mp4"));
    // the previous copy is left in place (only "Remove unused media" deletes)
    QVERIFY(QFile::exists(storedFirst));
    QCOMPARE(m_doc->assets()->unreferenced(), QStringList() << storedFirst);

    // a missing file is refused and changes nothing
    params.insert(QStringLiteral("source"), m_tmp->path() + "/nope.mp4");
    params.insert(QStringLiteral("baseRevision"), int(m_doc->docRevision()));
    reply = sendAndWaitForReply(QStringLiteral("functions.update"), params);
    QCOMPARE(reply.value(QStringLiteral("ok")).toBool(), false);
    QCOMPARE(QFileInfo(video->sourceUrl()).fileName(), QString("second.mp4"));
}

void ApiFunctionsDomain_Test::getReturnsAudioVideoSourceDetail()
{
    helloAndGetClientId();
    QString wav = writeMediaFile("song.wav", "audio bytes");

    QJsonObject extra;
    extra.insert(QStringLiteral("source"), wav);
    QString fid = createFunctionViaApi(QStringLiteral("Audio"), extra);
    Audio *audio = static_cast<Audio *>(m_doc->function(fid.toUInt()));

    QJsonObject params;
    params.insert(QStringLiteral("functionId"), fid);
    QJsonObject reply = sendAndWaitForReply(QStringLiteral("functions.get"), params);
    QCOMPARE(reply.value(QStringLiteral("ok")).toBool(), true);
    QJsonObject result = reply.value(QStringLiteral("result")).toObject();
    QCOMPARE(result.value(QStringLiteral("type")).toString(), QStringLiteral("Audio"));

    QJsonObject typeDetail = result.value(QStringLiteral("typeDetail")).toObject();
    QCOMPARE(typeDetail.value(QStringLiteral("functionId")).toString(), fid);
    QCOMPARE(typeDetail.value(QStringLiteral("managed")).toBool(), true);
    QCOMPARE(typeDetail.value(QStringLiteral("importPending")).toBool(), false);
    // the managed path is reported relative to the workspace, as saved
    QCOMPARE(typeDetail.value(QStringLiteral("source")).toString(),
             m_doc->normalizeComponentPath(audio->getSourceFileName()));
    QVERIFY(typeDetail.value(QStringLiteral("source")).toString().startsWith("show.qxw.assets/"));
    QVERIFY(typeDetail.contains(QStringLiteral("docRevision")));

    // an external reference comes back as the raw absolute path
    Audio *external = new Audio(m_doc);
    external->setSourceFileName(wav);
    QVERIFY(m_doc->addFunction(external));
    params.insert(QStringLiteral("functionId"), QString::number(external->id()));
    reply = sendAndWaitForReply(QStringLiteral("functions.get"), params);
    typeDetail = reply.value(QStringLiteral("result")).toObject().value(QStringLiteral("typeDetail")).toObject();
    QCOMPARE(typeDetail.value(QStringLiteral("managed")).toBool(), false);
    QCOMPARE(typeDetail.value(QStringLiteral("source")).toString(), wav);
}

void ApiFunctionsDomain_Test::sceneSetValuesReplacesValueList()
{
    helloAndGetClientId();
    m_scene->setValue(1, 1, 50);

    QJsonObject values;
    values.insert(QStringLiteral("2.3"), 128);
    QJsonObject params;
    params.insert(QStringLiteral("functionId"), QString::number(m_scene->id()));
    params.insert(QStringLiteral("values"), values);
    params.insert(QStringLiteral("baseRevision"), int(m_doc->docRevision()));
    QJsonObject reply = sendAndWaitForReply(QStringLiteral("functions.scene.setValues"), params);

    QCOMPARE(reply.value(QStringLiteral("ok")).toBool(), true);
    QCOMPARE(int(m_scene->value(2, 3)), 128);
    // The previously-set (1,1) value must be gone - full replacement, not a merge.
    QVERIFY(m_scene->checkValue(SceneValue(1, 1, 0)) == false);
}

void ApiFunctionsDomain_Test::sceneSetValueAndUnsetValueEmitSinglePatchOps()
{
    helloAndGetClientId();

    QJsonObject setParams;
    setParams.insert(QStringLiteral("functionId"), QString::number(m_scene->id()));
    setParams.insert(QStringLiteral("fixture"), QStringLiteral("7"));
    setParams.insert(QStringLiteral("channel"), 4);
    setParams.insert(QStringLiteral("value"), 99);
    setParams.insert(QStringLiteral("baseRevision"), int(m_doc->docRevision()));
    QJsonObject setReply = sendAndWaitForReply(QStringLiteral("functions.scene.setValue"), setParams);

    QCOMPARE(setReply.value(QStringLiteral("ok")).toBool(), true);
    QCOMPARE(int(m_scene->value(7, 4)), 99);

    QJsonObject unsetParams;
    unsetParams.insert(QStringLiteral("functionId"), QString::number(m_scene->id()));
    unsetParams.insert(QStringLiteral("fixture"), QStringLiteral("7"));
    unsetParams.insert(QStringLiteral("channel"), 4);
    unsetParams.insert(QStringLiteral("baseRevision"), int(m_doc->docRevision()));
    QJsonObject unsetReply = sendAndWaitForReply(QStringLiteral("functions.scene.unsetValue"), unsetParams);

    QCOMPARE(unsetReply.value(QStringLiteral("ok")).toBool(), true);
    QVERIFY(m_scene->checkValue(SceneValue(7, 4, 0)) == false);
}

void ApiFunctionsDomain_Test::sceneSetMembersReplacesFixtureList()
{
    helloAndGetClientId();
    m_scene->addFixture(1);

    QJsonObject params;
    params.insert(QStringLiteral("functionId"), QString::number(m_scene->id()));
    params.insert(QStringLiteral("fixtures"), QJsonArray{ QStringLiteral("2"), QStringLiteral("3") });
    params.insert(QStringLiteral("baseRevision"), int(m_doc->docRevision()));
    QJsonObject reply = sendAndWaitForReply(QStringLiteral("functions.scene.setMembers"), params);

    QCOMPARE(reply.value(QStringLiteral("ok")).toBool(), true);
    QList<quint32> fixtures = m_scene->fixtures();
    QVERIFY(fixtures.contains(1) == false);
    QVERIFY(fixtures.contains(2));
    QVERIFY(fixtures.contains(3));
}

void ApiFunctionsDomain_Test::chaserStepsAddReplaceRemoveMove()
{
    helloAndGetClientId();
    QString chaserIdStr = createFunctionViaApi(QStringLiteral("Chaser"));
    QVERIFY(chaserIdStr.isEmpty() == false);
    Chaser *chaser = qobject_cast<Chaser *>(m_doc->function(chaserIdStr.toUInt()));
    QVERIFY(chaser != nullptr);

    // addStep (twice, so there's something to move/remove)
    QJsonObject step1;
    step1.insert(QStringLiteral("targetFunctionId"), QString::number(m_scene->id()));
    step1.insert(QStringLiteral("fadeIn"), 100);
    step1.insert(QStringLiteral("hold"), 500);
    step1.insert(QStringLiteral("fadeOut"), 100);
    step1.insert(QStringLiteral("duration"), 700);

    QJsonObject addParams1;
    addParams1.insert(QStringLiteral("functionId"), chaserIdStr);
    addParams1.insert(QStringLiteral("step"), step1);
    addParams1.insert(QStringLiteral("baseRevision"), int(m_doc->docRevision()));
    QJsonObject addReply1 = sendAndWaitForReply(QStringLiteral("functions.steps.addStep"), addParams1);
    QCOMPARE(addReply1.value(QStringLiteral("ok")).toBool(), true);
    QCOMPARE(chaser->stepsCount(), 1);
    // duration is server-derived from fadeIn+hold, not trusted from the client.
    QCOMPARE(chaser->stepAt(0)->duration, uint(600));

    QJsonObject step2 = step1;
    QJsonObject addParams2;
    addParams2.insert(QStringLiteral("functionId"), chaserIdStr);
    addParams2.insert(QStringLiteral("step"), step2);
    addParams2.insert(QStringLiteral("baseRevision"), int(m_doc->docRevision()));
    sendAndWaitForReply(QStringLiteral("functions.steps.addStep"), addParams2);
    QCOMPARE(chaser->stepsCount(), 2);

    // replaceStep
    QJsonObject replacedStep = step1;
    replacedStep.insert(QStringLiteral("fadeIn"), 50);
    replacedStep.insert(QStringLiteral("hold"), 50);
    QJsonObject replaceParams;
    replaceParams.insert(QStringLiteral("functionId"), chaserIdStr);
    replaceParams.insert(QStringLiteral("index"), 0);
    replaceParams.insert(QStringLiteral("step"), replacedStep);
    replaceParams.insert(QStringLiteral("baseRevision"), int(m_doc->docRevision()));
    QJsonObject replaceReply = sendAndWaitForReply(QStringLiteral("functions.steps.replaceStep"), replaceParams);
    QCOMPARE(replaceReply.value(QStringLiteral("ok")).toBool(), true);
    QCOMPARE(chaser->stepAt(0)->fadeIn, uint(50));

    // moveStep
    QJsonObject moveParams;
    moveParams.insert(QStringLiteral("functionId"), chaserIdStr);
    moveParams.insert(QStringLiteral("sourceIndex"), 0);
    moveParams.insert(QStringLiteral("destIndex"), 1);
    moveParams.insert(QStringLiteral("baseRevision"), int(m_doc->docRevision()));
    QJsonObject moveReply = sendAndWaitForReply(QStringLiteral("functions.steps.moveStep"), moveParams);
    QCOMPARE(moveReply.value(QStringLiteral("ok")).toBool(), true);
    QCOMPARE(chaser->stepAt(1)->fadeIn, uint(50));

    // removeStep
    QJsonObject removeParams;
    removeParams.insert(QStringLiteral("functionId"), chaserIdStr);
    removeParams.insert(QStringLiteral("index"), 0);
    removeParams.insert(QStringLiteral("baseRevision"), int(m_doc->docRevision()));
    QJsonObject removeReply = sendAndWaitForReply(QStringLiteral("functions.steps.removeStep"), removeParams);
    QCOMPARE(removeReply.value(QStringLiteral("ok")).toBool(), true);
    QCOMPARE(chaser->stepsCount(), 1);
}

void ApiFunctionsDomain_Test::chaserStepsRejectCycles()
{
    // Crash audit: a Chaser step targeting the Chaser itself (replaceStep
    // had no self check at all) or a function that already contains it made
    // Chaser::contains() recurse forever (stack overflow) and a self-stepping
    // Chaser deadlock MasterTimer once started.
    helloAndGetClientId();
    QString aId = createFunctionViaApi(QStringLiteral("Chaser"));
    QString bId = createFunctionViaApi(QStringLiteral("Chaser"));
    Chaser *a = qobject_cast<Chaser *>(m_doc->function(aId.toUInt()));
    Chaser *b = qobject_cast<Chaser *>(m_doc->function(bId.toUInt()));
    QVERIFY(a != nullptr && b != nullptr);

    auto stepOn = [&](const QString &method, const QString &chaserId, const QString &targetId) {
        QJsonObject step;
        step.insert(QStringLiteral("targetFunctionId"), targetId);
        QJsonObject params;
        params.insert(QStringLiteral("functionId"), chaserId);
        params.insert(QStringLiteral("index"), 0);
        params.insert(QStringLiteral("step"), step);
        params.insert(QStringLiteral("baseRevision"), int(m_doc->docRevision()));
        return sendAndWaitForReply(method, params);
    };

    // A -> scene, then replace that step with A itself
    QCOMPARE(stepOn(QStringLiteral("functions.steps.addStep"), aId, QString::number(m_scene->id())).value(QStringLiteral("ok")).toBool(), true);
    QJsonObject reply = stepOn(QStringLiteral("functions.steps.replaceStep"), aId, aId);
    QCOMPARE(reply.value(QStringLiteral("ok")).toBool(), false);
    QCOMPARE(reply.value(QStringLiteral("error")).toObject().value(QStringLiteral("code")).toString(), QStringLiteral("INVALID_PARAMS"));
    QCOMPARE(a->stepAt(0)->fid, m_scene->id());

    // A -> B is fine; B -> A (add or replace) would close the cycle
    QCOMPARE(stepOn(QStringLiteral("functions.steps.addStep"), aId, bId).value(QStringLiteral("ok")).toBool(), true);
    QCOMPARE(stepOn(QStringLiteral("functions.steps.addStep"), bId, QString::number(m_scene->id())).value(QStringLiteral("ok")).toBool(), true);
    reply = stepOn(QStringLiteral("functions.steps.addStep"), bId, aId);
    QCOMPARE(reply.value(QStringLiteral("ok")).toBool(), false);
    reply = stepOn(QStringLiteral("functions.steps.replaceStep"), bId, aId);
    QCOMPARE(reply.value(QStringLiteral("ok")).toBool(), false);
    QCOMPARE(b->stepsCount(), 1);
    QVERIFY(a->contains(9999) == false); // terminates
}

/*********************************************************************
 * Live run-state: running/paused flags, functions.status.changed,
 * functions.pause alias, functions.stopAll
 *********************************************************************/

QJsonObject ApiFunctionsDomain_Test::waitForEvent(QSignalSpy &spy, const QString &topic,
                                                  const std::function<bool(const QJsonObject &)> &accept, int timeoutMs)
{
    QJsonObject found;
    (void)QTest::qWaitFor([&]()
    {
        for (const QList<QVariant> &frame : spy)
        {
            QJsonObject obj = QJsonDocument::fromJson(frame.at(0).toString().toUtf8()).object();
            if (obj.value(QStringLiteral("type")).toString() != QStringLiteral("event") ||
                obj.value(QStringLiteral("topic")).toString() != topic)
                continue;
            if (accept(obj.value(QStringLiteral("data")).toObject()))
            {
                found = obj;
                return true;
            }
        }
        return false;
    }, timeoutMs);
    return found;
}

void ApiFunctionsDomain_Test::listAndGetCarryRunningAndPaused()
{
    helloAndGetClientId();
    QString sceneId = QString::number(m_scene->id());

    QJsonObject list = sendAndWaitForReply(QStringLiteral("functions.list"), QJsonObject());
    QJsonArray functions = list.value(QStringLiteral("result")).toObject().value(QStringLiteral("functions")).toArray();
    QCOMPARE(functions.count(), 1);
    QJsonObject summary = functions.at(0).toObject();
    QVERIFY(summary.contains(QStringLiteral("running")));
    QVERIFY(summary.contains(QStringLiteral("paused")));
    QCOMPARE(summary.value(QStringLiteral("running")).toBool(), false);
    QCOMPARE(summary.value(QStringLiteral("paused")).toBool(), false);

    // The "id" alias of functionId is accepted by every functions.* method
    QJsonObject idParams;
    idParams.insert(QStringLiteral("id"), sceneId);
    QCOMPARE(sendAndWaitForReply(QStringLiteral("functions.start"), idParams).value(QStringLiteral("ok")).toBool(), true);
    QVERIFY(QTest::qWaitFor([this]() { return m_scene->isRunning(); }, 2000));

    QJsonObject get = sendAndWaitForReply(QStringLiteral("functions.get"), idParams);
    QCOMPARE(get.value(QStringLiteral("result")).toObject().value(QStringLiteral("running")).toBool(), true);
    QCOMPARE(get.value(QStringLiteral("result")).toObject().value(QStringLiteral("paused")).toBool(), false);
}

void ApiFunctionsDomain_Test::startAndStopBroadcastStatusChanged()
{
    helloAndGetClientId();
    QString sceneId = QString::number(m_scene->id());
    QJsonObject params;
    params.insert(QStringLiteral("functionId"), sceneId);

    QSignalSpy spy(m_client, &QWebSocket::textMessageReceived);
    QCOMPARE(sendAndWaitForReply(QStringLiteral("functions.start"), params).value(QStringLiteral("ok")).toBool(), true);

    QJsonObject started = waitForEvent(spy, QStringLiteral("functions.status.changed"), [&](const QJsonObject &data)
    {
        return data.value(QStringLiteral("id")).toString() == sceneId && data.value(QStringLiteral("running")).toBool();
    });
    QVERIFY2(started.isEmpty() == false, "no functions.status.changed {running:true} received");
    QJsonObject data = started.value(QStringLiteral("data")).toObject();
    // Both id spellings, per FunctionsStatusData (functionId) and the web UI contract (id)
    QCOMPARE(data.value(QStringLiteral("functionId")).toString(), sceneId);
    QCOMPARE(data.value(QStringLiteral("paused")).toBool(), false);
    QVERIFY(data.contains(QStringLiteral("elapsed")));
    // Engine-driven (MasterTimer) - not attributed to the requesting client
    QVERIFY(started.value(QStringLiteral("originClientId")).isNull());

    QCOMPARE(sendAndWaitForReply(QStringLiteral("functions.stop"), params).value(QStringLiteral("ok")).toBool(), true);
    QJsonObject stopped = waitForEvent(spy, QStringLiteral("functions.status.changed"), [&](const QJsonObject &d)
    {
        return d.value(QStringLiteral("id")).toString() == sceneId && d.value(QStringLiteral("running")).toBool() == false;
    });
    QVERIFY2(stopped.isEmpty() == false, "no functions.status.changed {running:false} received");
    QVERIFY(m_scene->isRunning() == false);
}

void ApiFunctionsDomain_Test::pauseAliasBroadcastsPausedStatus()
{
    helloAndGetClientId();
    QString sceneId = QString::number(m_scene->id());
    QJsonObject startParams;
    startParams.insert(QStringLiteral("functionId"), sceneId);
    QCOMPARE(sendAndWaitForReply(QStringLiteral("functions.start"), startParams).value(QStringLiteral("ok")).toBool(), true);
    QVERIFY(QTest::qWaitFor([this]() { return m_scene->isRunning(); }, 2000));

    QSignalSpy spy(m_client, &QWebSocket::textMessageReceived);
    QJsonObject pauseParams;
    pauseParams.insert(QStringLiteral("id"), sceneId);
    pauseParams.insert(QStringLiteral("paused"), true);
    QCOMPARE(sendAndWaitForReply(QStringLiteral("functions.pause"), pauseParams).value(QStringLiteral("ok")).toBool(), true);
    QVERIFY(m_scene->isPaused());

    QJsonObject paused = waitForEvent(spy, QStringLiteral("functions.status.changed"), [&](const QJsonObject &d)
    {
        return d.value(QStringLiteral("id")).toString() == sceneId && d.value(QStringLiteral("paused")).toBool();
    });
    QVERIFY2(paused.isEmpty() == false, "no functions.status.changed {paused:true} received");
    QCOMPARE(paused.value(QStringLiteral("data")).toObject().value(QStringLiteral("running")).toBool(), true);

    // Resuming through the original spelling flips it back and notifies again
    pauseParams.insert(QStringLiteral("paused"), false);
    QCOMPARE(sendAndWaitForReply(QStringLiteral("functions.setPause"), pauseParams).value(QStringLiteral("ok")).toBool(), true);
    QJsonObject resumed = waitForEvent(spy, QStringLiteral("functions.status.changed"), [&](const QJsonObject &d)
    {
        return d.value(QStringLiteral("id")).toString() == sceneId && d.value(QStringLiteral("paused")).toBool() == false
               && d.value(QStringLiteral("running")).toBool();
    });
    QVERIFY2(resumed.isEmpty() == false, "no functions.status.changed {paused:false} received");
    QVERIFY(m_scene->isPaused() == false);
}

void ApiFunctionsDomain_Test::pauseAliasOnMissingFunctionIsNotFound()
{
    helloAndGetClientId();
    QJsonObject params;
    params.insert(QStringLiteral("id"), QStringLiteral("4242"));
    params.insert(QStringLiteral("paused"), true);
    QJsonObject reply = sendAndWaitForReply(QStringLiteral("functions.pause"), params);
    QCOMPARE(reply.value(QStringLiteral("ok")).toBool(), false);
    QCOMPARE(reply.value(QStringLiteral("error")).toObject().value(QStringLiteral("code")).toString(),
             QStringLiteral("NOT_FOUND"));
}

void ApiFunctionsDomain_Test::pauseWithNonBooleanIsInvalidParams()
{
    helloAndGetClientId();
    QJsonObject params;
    params.insert(QStringLiteral("id"), QString::number(m_scene->id()));
    params.insert(QStringLiteral("paused"), QStringLiteral("yes"));
    QJsonObject reply = sendAndWaitForReply(QStringLiteral("functions.pause"), params);
    QCOMPARE(reply.value(QStringLiteral("ok")).toBool(), false);
    QCOMPARE(reply.value(QStringLiteral("error")).toObject().value(QStringLiteral("code")).toString(),
             QStringLiteral("INVALID_PARAMS"));
}

void ApiFunctionsDomain_Test::stopAllStopsEveryRunningFunction()
{
    helloAndGetClientId();

    Scene *second = new Scene(m_doc);
    second->setName(QStringLiteral("Second Scene"));
    second->setValue(Fixture::invalidId(), 1, 200);
    QVERIFY(m_doc->addFunction(second));

    for (Scene *scene : { m_scene, second })
    {
        QJsonObject params;
        params.insert(QStringLiteral("functionId"), QString::number(scene->id()));
        QCOMPARE(sendAndWaitForReply(QStringLiteral("functions.start"), params).value(QStringLiteral("ok")).toBool(), true);
    }
    QVERIFY(QTest::qWaitFor([&]() { return m_scene->isRunning() && second->isRunning(); }, 2000));

    QSignalSpy spy(m_client, &QWebSocket::textMessageReceived);
    QJsonObject reply = sendAndWaitForReply(QStringLiteral("functions.stopAll"), QJsonObject());
    QCOMPARE(reply.value(QStringLiteral("ok")).toBool(), true);
    QVERIFY(QTest::qWaitFor([&]() { return m_scene->isRunning() == false && second->isRunning() == false; }, 2000));
    QCOMPARE(m_doc->masterTimer()->runningFunctions(), 0);

    // One stopped notification per function
    for (Scene *scene : { m_scene, second })
    {
        QString sceneId = QString::number(scene->id());
        QJsonObject stopped = waitForEvent(spy, QStringLiteral("functions.status.changed"), [&](const QJsonObject &d)
        {
            return d.value(QStringLiteral("id")).toString() == sceneId && d.value(QStringLiteral("running")).toBool() == false;
        });
        QVERIFY2(stopped.isEmpty() == false, "missing functions.status.changed {running:false} after stopAll");
    }
}

QTEST_MAIN(ApiFunctionsDomain_Test)
