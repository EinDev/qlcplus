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

QTEST_MAIN(ApiFunctionsDomain_Test)
