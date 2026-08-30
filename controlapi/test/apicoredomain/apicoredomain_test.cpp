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

#include "apicoredomain_test.h"
#include "apiserver.h"
#include "doc.h"
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

void ApiCoreDomain_Test::init()
{
    m_doc = new Doc(nullptr);
    // Note: ApiCoreDomain relies on ApiServer's parent being an App instance
    // for project lifecycle methods. Since we don't have a full App here,
    // those methods will return UNAUTHORIZED/INTERNAL error in the domain.
    // But we can still test mode and settings which only need Doc and QSettings.
    m_apiServer = new ApiServer(nullptr, m_doc);
    QVERIFY(m_apiServer->listen(0));

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

QTEST_GUILESS_MAIN(ApiCoreDomain_Test)
