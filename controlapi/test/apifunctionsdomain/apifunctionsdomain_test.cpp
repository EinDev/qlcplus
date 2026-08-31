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
#include "fixture.h"
#include "scene.h"
#include "chaser.h"
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
    m_doc = new Doc(nullptr);
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
    QVERIFY(m_apiServer->listen(0));

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

QTEST_MAIN(ApiFunctionsDomain_Test)
