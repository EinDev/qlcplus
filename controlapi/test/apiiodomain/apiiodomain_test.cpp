/*
  Q Light Controller Plus - Control API unit test
  apiiodomain_test.cpp

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

#include "apiiodomain_test.h"
#include "apiserver.h"
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

void ApiIoDomain_Test::init()
{
    m_doc = new Doc(nullptr);
    m_apiServer = new ApiServer(nullptr, m_doc);
    QVERIFY(m_apiServer->listen(0));

    m_client = new QWebSocket();
    m_client->open(QUrl(QStringLiteral("ws://127.0.0.1:%1/qlcplusapi").arg(m_apiServer->serverPort())));
    QVERIFY(QTest::qWaitFor([this]() { return m_client->state() == QAbstractSocket::ConnectedState; }, 2000));
}

void ApiIoDomain_Test::cleanup()
{
    delete m_client;
    m_client = nullptr;
    delete m_apiServer;
    m_apiServer = nullptr;
    delete m_doc;
    m_doc = nullptr;
}

QJsonObject ApiIoDomain_Test::sendAndWaitForReply(const QString &method, const QJsonObject &params)
{
    // A mutation's response and its broadcast event (see e.g.
    // ApiIoDomain's io.universe.create handler) can arrive in either order -
    // scan every frame received so far for the matching "response", not
    // just the first one, polling until it shows up or we time out.
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

QString ApiIoDomain_Test::helloAndGetClientId()
{
    QJsonObject reply = sendAndWaitForReply(QStringLiteral("hello"), QJsonObject());
    return reply.value(QStringLiteral("result")).toObject().value(QStringLiteral("clientId")).toString();
}

void ApiIoDomain_Test::helloReturnsWelcome()
{
    QJsonObject reply = sendAndWaitForReply(QStringLiteral("hello"), QJsonObject());
    QCOMPARE(reply.value(QStringLiteral("type")).toString(), QStringLiteral("response"));
    QCOMPARE(reply.value(QStringLiteral("ok")).toBool(), true);
    QJsonObject result = reply.value(QStringLiteral("result")).toObject();
    QVERIFY(result.value(QStringLiteral("clientId")).toString().isEmpty() == false);
    QVERIFY(result.contains(QStringLiteral("docRevision")));
    QVERIFY(result.value(QStringLiteral("serverVersion")).toString().isEmpty() == false);
}

void ApiIoDomain_Test::requestBeforeHelloIsUnauthorized()
{
    // No hello sent yet in this test (unlike the others, which call it via
    // helloAndGetClientId()/sendAndWaitForReply("hello", ...) first).
    QJsonObject reply = sendAndWaitForReply(QStringLiteral("io.blackout.get"), QJsonObject());
    QCOMPARE(reply.value(QStringLiteral("ok")).toBool(), false);
    QCOMPARE(reply.value(QStringLiteral("error")).toObject().value(QStringLiteral("code")).toString(),
              QStringLiteral("UNAUTHORIZED"));
}

void ApiIoDomain_Test::universeCreateBumpsRevision()
{
    helloAndGetClientId();

    QJsonObject before = sendAndWaitForReply(QStringLiteral("io.universe.list"), QJsonObject());
    int docRevision = before.value(QStringLiteral("result")).toObject().value(QStringLiteral("docRevision")).toInt();

    QJsonObject params;
    params.insert(QStringLiteral("baseRevision"), docRevision);
    QJsonObject reply = sendAndWaitForReply(QStringLiteral("io.universe.create"), params);

    QCOMPARE(reply.value(QStringLiteral("ok")).toBool(), true);
    int newRevision = reply.value(QStringLiteral("result")).toObject().value(QStringLiteral("docRevision")).toInt();
    QVERIFY(newRevision > docRevision);
    QCOMPARE(m_doc->docRevision(), quint32(newRevision));
}

void ApiIoDomain_Test::universeCreateWithStaleRevisionConflicts()
{
    helloAndGetClientId();

    QJsonObject before = sendAndWaitForReply(QStringLiteral("io.universe.list"), QJsonObject());
    int docRevision = before.value(QStringLiteral("result")).toObject().value(QStringLiteral("docRevision")).toInt();

    QJsonObject staleParams;
    staleParams.insert(QStringLiteral("baseRevision"), docRevision - 1); // deliberately stale
    QJsonObject reply = sendAndWaitForReply(QStringLiteral("io.universe.create"), staleParams);

    QCOMPARE(reply.value(QStringLiteral("ok")).toBool(), false);
    QJsonObject error = reply.value(QStringLiteral("error")).toObject();
    QCOMPARE(error.value(QStringLiteral("code")).toString(), QStringLiteral("CONFLICT"));
    QCOMPARE(error.value(QStringLiteral("details")).toObject().value(QStringLiteral("docRevision")).toInt(), docRevision);
}

void ApiIoDomain_Test::grandMasterSetValueBroadcastsLiveEvent()
{
    QString clientId = helloAndGetClientId();

    QSignalSpy spy(m_client, &QWebSocket::textMessageReceived);

    QJsonObject params;
    params.insert(QStringLiteral("value"), 128);
    m_client->sendTextMessage(buildRequest(QStringLiteral("io.grandMaster.setValue"), params, QStringLiteral("t-gm")));

    // Expect two frames: the bare-ack response, and the broadcast event -
    // order between them isn't guaranteed, so collect both.
    QVERIFY(QTest::qWaitFor([&]() { return spy.count() >= 2; }, 2000));

    bool sawResponse = false, sawEvent = false;
    for (const QList<QVariant> &frame : spy)
    {
        QJsonObject obj = QJsonDocument::fromJson(frame.at(0).toString().toUtf8()).object();
        QString type = obj.value(QStringLiteral("type")).toString();
        if (type == QStringLiteral("response") && obj.value(QStringLiteral("id")).toString() == QStringLiteral("t-gm"))
        {
            QCOMPARE(obj.value(QStringLiteral("ok")).toBool(), true);
            QVERIFY(obj.value(QStringLiteral("result")).toObject().contains(QStringLiteral("docRevision")) == false);
            sawResponse = true;
        }
        else if (type == QStringLiteral("event") && obj.value(QStringLiteral("topic")).toString() == QStringLiteral("io.grandMaster.changed"))
        {
            QCOMPARE(obj.value(QStringLiteral("data")).toObject().value(QStringLiteral("value")).toInt(), 128);
            // originClientId should be attributed to the client that made
            // the change (00-conventions.md §3/§9), not left null.
            QCOMPARE(obj.value(QStringLiteral("originClientId")).toString(), clientId);
            sawEvent = true;
        }
    }
    QVERIFY(sawResponse);
    QVERIFY(sawEvent);
    QCOMPARE(m_doc->inputOutputMap()->grandMasterValue(), uchar(128));
}

void ApiIoDomain_Test::blackoutToggleBroadcastsLiveEvent()
{
    QString clientId = helloAndGetClientId();
    bool before = m_doc->inputOutputMap()->blackout();

    QSignalSpy spy(m_client, &QWebSocket::textMessageReceived);
    m_client->sendTextMessage(buildRequest(QStringLiteral("io.blackout.toggle"), QJsonObject(), QStringLiteral("t-bo")));
    QVERIFY(QTest::qWaitFor([&]() { return spy.count() >= 2; }, 2000));

    bool sawEvent = false;
    for (const QList<QVariant> &frame : spy)
    {
        QJsonObject obj = QJsonDocument::fromJson(frame.at(0).toString().toUtf8()).object();
        if (obj.value(QStringLiteral("type")).toString() == QStringLiteral("event"))
        {
            QCOMPARE(obj.value(QStringLiteral("topic")).toString(), QStringLiteral("io.blackout.changed"));
            QCOMPARE(obj.value(QStringLiteral("data")).toObject().value(QStringLiteral("blackout")).toBool(), !before);
            // originClientId should be attributed to the client that made
            // the change (00-conventions.md §3/§9), not left null.
            QCOMPARE(obj.value(QStringLiteral("originClientId")).toString(), clientId);
            sawEvent = true;
        }
    }
    QVERIFY(sawEvent);
    QCOMPARE(m_doc->inputOutputMap()->blackout(), !before);
}

void ApiIoDomain_Test::dmxEventOnlyDeliveredAfterSubscribe()
{
    // Exercises ApiServer::broadcast()'s subscribeGated=true path directly
    // (the mechanism io.dmx.universe.*.changed relies on - see
    // ApiIoDomain::slotUniverseWritten) rather than driving a real per-
    // universe QThread tick cycle end-to-end: Universe only runs its worker
    // thread once InputOutputMap::startUniverses() has been called (normally
    // done by qmlui's App::initDoc(), not by a bare `new Doc()`), and
    // reliably timing a real cross-thread tick in a unit test would trade a
    // lot of complexity for coverage this already gives: that a session
    // only receives a subscribeGated topic after subscribing to it.
    helloAndGetClientId();
    const QString topic = QStringLiteral("io.dmx.universe.1.changed");
    QJsonObject data;
    data.insert(QStringLiteral("universeId"), 1);

    // Not subscribed yet - must not be delivered.
    {
        QSignalSpy spy(m_client, &QWebSocket::textMessageReceived);
        m_apiServer->broadcast(topic, data, QString(), true);
        QTest::qWait(200);
        QCOMPARE(spy.count(), 0);
    }

    // Subscribe, then the same broadcast must be delivered.
    QJsonObject subParams;
    subParams.insert(QStringLiteral("topics"), QJsonArray{topic});
    QJsonObject subReply = sendAndWaitForReply(QStringLiteral("subscribe"), subParams);
    QCOMPARE(subReply.value(QStringLiteral("ok")).toBool(), true);

    QSignalSpy spy(m_client, &QWebSocket::textMessageReceived);
    m_apiServer->broadcast(topic, data, QString(), true);
    QVERIFY(spy.wait(2000));
    QJsonObject received = QJsonDocument::fromJson(spy.at(0).at(0).toString().toUtf8()).object();
    QCOMPARE(received.value(QStringLiteral("topic")).toString(), topic);

    // unsubscribe() must turn delivery back off.
    QJsonObject unsubReply = sendAndWaitForReply(QStringLiteral("unsubscribe"), subParams);
    QCOMPARE(unsubReply.value(QStringLiteral("ok")).toBool(), true);
    QSignalSpy spy2(m_client, &QWebSocket::textMessageReceived);
    m_apiServer->broadcast(topic, data, QString(), true);
    QTest::qWait(200);
    QCOMPARE(spy2.count(), 0);
}

QTEST_MAIN(ApiIoDomain_Test)
