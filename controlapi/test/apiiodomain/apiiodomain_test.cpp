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
#include <QDir>
#include <QCoreApplication>

#include "apiiodomain_test.h"
#include "apiserver.h"
#include "mastertimer.h"
#include "inputoutputmap.h"
#include "ioplugincache.h"
#include "qlcioplugin.h"
#include "qlcinputprofile.h"
#include "qlcfile.h"
#include "outputpatch.h"
#include "inputpatch.h"
#include "universe.h"
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

void ApiIoDomain_Test::init()
{
    // Doc(QObject*, int universes = 4) - a fresh Doc already has universes
    // 0-3, no addUniverse() needed for the simpleDesk* cases below.
    m_doc = new Doc(nullptr);
    // Needed so ApiIoDomain::writeDMX() (registered as a DMXSource on this
    // MasterTimer) actually gets ticked - the simpleDeskSetChannels* cases
    // below verify the write reaches io.dmx.universe.get, not just the
    // in-memory m_simpleDeskValues io.simpleDesk.get reads from. Same
    // pattern as apifunctionsdomain_test.cpp's init(). startUniverses() is
    // also needed here (unlike that suite): each Universe only actually
    // applies its GenericFader-held values into postGMValues on its own
    // worker QThread (Universe::processFaders(), woken by
    // MasterTimer::timerTick() calling Universe::tick() directly on the timer
    // thread), and that thread is only started by
    // InputOutputMap::startUniverses() (normally done by qmlui's
    // App::initDoc(), not by a bare `new Doc()` - see this same file's
    // older dmxEventOnlyDeliveredAfterSubscribe() comment, written before
    // this was needed).
    m_doc->masterTimer()->start();
    m_doc->inputOutputMap()->startUniverses();
    m_apiServer = new ApiServer(nullptr, m_doc);
    QVERIFY(m_apiServer->listen(0, QHostAddress::LocalHost));

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
    m_doc->masterTimer()->stop();
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
    // universe QThread tick cycle end-to-end: even though init() now starts
    // both MasterTimer and every Universe's worker thread (needed by the
    // simpleDeskSetChannels* cases below, which do assert on a real,
    // ticked-through io.dmx.universe.get read), reliably timing *this*
    // test's assertion (an event fires only after subscribing) against a
    // real cross-thread tick would still trade a lot of complexity for no
    // extra coverage - a direct broadcast() call already proves the
    // subscribe-gating behaviour this test is actually about.
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

void ApiIoDomain_Test::simpleDeskSetChannelIsReflectedInGet()
{
    helloAndGetClientId();

    QJsonObject setParams;
    setParams.insert(QStringLiteral("address"), 5);
    setParams.insert(QStringLiteral("value"), 200);
    QJsonObject setReply = sendAndWaitForReply(QStringLiteral("io.simpleDesk.setChannel"), setParams);
    QCOMPARE(setReply.value(QStringLiteral("ok")).toBool(), true);

    QJsonObject getParams;
    getParams.insert(QStringLiteral("universeId"), 0);
    QJsonObject getReply = sendAndWaitForReply(QStringLiteral("io.simpleDesk.get"), getParams);
    QCOMPARE(getReply.value(QStringLiteral("ok")).toBool(), true);

    QJsonArray channels = getReply.value(QStringLiteral("result")).toObject().value(QStringLiteral("channels")).toArray();
    QCOMPARE(channels.count(), 1);
    QJsonObject ch = channels.at(0).toObject();
    QCOMPARE(ch.value(QStringLiteral("address")).toInt(), 5);
    QCOMPARE(ch.value(QStringLiteral("universeId")).toInt(), 0);
    QCOMPARE(ch.value(QStringLiteral("channel")).toInt(), 5);
    QCOMPARE(ch.value(QStringLiteral("value")).toInt(), 200);
    QCOMPARE(ch.value(QStringLiteral("overridden")).toBool(), true);
    QVERIFY(ch.value(QStringLiteral("fixtureId")).isNull());
    QVERIFY(ch.value(QStringLiteral("group")).isNull());
}

void ApiIoDomain_Test::simpleDeskSetChannelBroadcastsOverriddenTrue()
{
    QString clientId = helloAndGetClientId();

    QSignalSpy spy(m_client, &QWebSocket::textMessageReceived);
    QJsonObject params;
    params.insert(QStringLiteral("address"), 10);
    params.insert(QStringLiteral("value"), 42);
    m_client->sendTextMessage(buildRequest(QStringLiteral("io.simpleDesk.setChannel"), params, QStringLiteral("t-sc")));
    QVERIFY(QTest::qWaitFor([&]() { return spy.count() >= 2; }, 2000));

    bool sawEvent = false;
    for (const QList<QVariant> &frame : spy)
    {
        QJsonObject obj = QJsonDocument::fromJson(frame.at(0).toString().toUtf8()).object();
        if (obj.value(QStringLiteral("type")).toString() == QStringLiteral("event") &&
            obj.value(QStringLiteral("topic")).toString() == QStringLiteral("io.simpleDesk.channelChanged"))
        {
            QJsonObject data = obj.value(QStringLiteral("data")).toObject();
            QCOMPARE(data.value(QStringLiteral("address")).toInt(), 10);
            QCOMPARE(data.value(QStringLiteral("value")).toInt(), 42);
            QCOMPARE(data.value(QStringLiteral("overridden")).toBool(), true);
            QCOMPARE(obj.value(QStringLiteral("originClientId")).toString(), clientId);
            sawEvent = true;
        }
    }
    QVERIFY(sawEvent);
}

static QJsonObject channelEntry(int address, int value)
{
    QJsonObject o;
    o.insert(QStringLiteral("address"), address);
    o.insert(QStringLiteral("value"), value);
    return o;
}

void ApiIoDomain_Test::simpleDeskSetChannelsIsReflectedInGetAndDmxUniverse()
{
    helloAndGetClientId();

    QJsonArray channels;
    channels.append(channelEntry(20, 50));
    channels.append(channelEntry(21, 100));
    channels.append(channelEntry(22, 150));

    QJsonObject setParams;
    setParams.insert(QStringLiteral("channels"), channels);
    QJsonObject setReply = sendAndWaitForReply(QStringLiteral("io.simpleDesk.setChannels"), setParams);
    QCOMPARE(setReply.value(QStringLiteral("ok")).toBool(), true);

    QJsonObject getParams;
    getParams.insert(QStringLiteral("universeId"), 0);
    QJsonObject getReply = sendAndWaitForReply(QStringLiteral("io.simpleDesk.get"), getParams);
    QCOMPARE(getReply.value(QStringLiteral("ok")).toBool(), true);

    QJsonArray got = getReply.value(QStringLiteral("result")).toObject().value(QStringLiteral("channels")).toArray();
    QCOMPARE(got.count(), 3);
    QHash<int, QJsonObject> byAddress;
    for (const QJsonValue &v : got)
    {
        QJsonObject ch = v.toObject();
        byAddress[ch.value(QStringLiteral("address")).toInt()] = ch;
    }
    QCOMPARE(byAddress.value(20).value(QStringLiteral("value")).toInt(), 50);
    QCOMPARE(byAddress.value(21).value(QStringLiteral("value")).toInt(), 100);
    QCOMPARE(byAddress.value(22).value(QStringLiteral("value")).toInt(), 150);
    QCOMPARE(byAddress.value(20).value(QStringLiteral("overridden")).toBool(), true);
    QCOMPARE(byAddress.value(21).value(QStringLiteral("overridden")).toBool(), true);
    QCOMPARE(byAddress.value(22).value(QStringLiteral("overridden")).toBool(), true);

    // writeDMX() runs on MasterTimer's own thread (registered as a DMXSource -
    // see apiiodomain.h) - poll io.dmx.universe.get until a tick has actually
    // applied all three values to the Universe's post-GM output, rather than
    // just landing in this domain's in-memory m_simpleDeskValues (which is
    // all io.simpleDesk.get above proves on its own).
    QVERIFY(QTest::qWaitFor([&]()
    {
        QJsonObject dmxReply = sendAndWaitForReply(QStringLiteral("io.dmx.universe.get"), getParams);
        QJsonArray values = dmxReply.value(QStringLiteral("result")).toObject().value(QStringLiteral("values")).toArray();
        if (values.size() < 23)
            return false;
        return values.at(20).toInt() == 50 && values.at(21).toInt() == 100 && values.at(22).toInt() == 150;
    }, 2000));
}

void ApiIoDomain_Test::simpleDeskSetChannelsBroadcastsOneEventPerEntry()
{
    QString clientId = helloAndGetClientId();

    QJsonArray channels;
    channels.append(channelEntry(30, 11));
    channels.append(channelEntry(31, 22));
    channels.append(channelEntry(32, 33));

    QJsonObject params;
    params.insert(QStringLiteral("channels"), channels);

    QSignalSpy spy(m_client, &QWebSocket::textMessageReceived);
    m_client->sendTextMessage(buildRequest(QStringLiteral("io.simpleDesk.setChannels"), params, QStringLiteral("t-scs")));
    // 1 response + 3 channelChanged events (one per entry, not one for the
    // whole batch - see this method's own handler comment) = 4 frames.
    QVERIFY(QTest::qWaitFor([&]() { return spy.count() >= 4; }, 2000));

    QHash<int, QJsonObject> eventsByAddress;
    for (const QList<QVariant> &frame : spy)
    {
        QJsonObject obj = QJsonDocument::fromJson(frame.at(0).toString().toUtf8()).object();
        if (obj.value(QStringLiteral("type")).toString() == QStringLiteral("event") &&
            obj.value(QStringLiteral("topic")).toString() == QStringLiteral("io.simpleDesk.channelChanged"))
        {
            QJsonObject data = obj.value(QStringLiteral("data")).toObject();
            eventsByAddress[data.value(QStringLiteral("address")).toInt()] = data;
            QCOMPARE(obj.value(QStringLiteral("originClientId")).toString(), clientId);
        }
    }
    QCOMPARE(eventsByAddress.count(), 3);
    QCOMPARE(eventsByAddress.value(30).value(QStringLiteral("value")).toInt(), 11);
    QCOMPARE(eventsByAddress.value(30).value(QStringLiteral("overridden")).toBool(), true);
    QCOMPARE(eventsByAddress.value(31).value(QStringLiteral("value")).toInt(), 22);
    QCOMPARE(eventsByAddress.value(32).value(QStringLiteral("value")).toInt(), 33);
}

void ApiIoDomain_Test::simpleDeskSetChannelsRejectsMalformedEntryWithoutPartialApply()
{
    helloAndGetClientId();

    QJsonObject malformedEntry;
    malformedEntry.insert(QStringLiteral("address"), 41);
    // missing "value" - the whole request must be rejected, not just this entry.

    QJsonArray channels;
    channels.append(channelEntry(40, 77));
    channels.append(malformedEntry);

    QJsonObject params;
    params.insert(QStringLiteral("channels"), channels);
    QJsonObject reply = sendAndWaitForReply(QStringLiteral("io.simpleDesk.setChannels"), params);
    QCOMPARE(reply.value(QStringLiteral("ok")).toBool(), false);
    QVERIFY(reply.value(QStringLiteral("error")).toObject().value(QStringLiteral("code")).toString().isEmpty() == false);

    QJsonObject getParams;
    getParams.insert(QStringLiteral("universeId"), 0);
    QJsonObject getReply = sendAndWaitForReply(QStringLiteral("io.simpleDesk.get"), getParams);
    // Neither entry applied - not even the well-formed one that came before
    // the malformed one in the array.
    QCOMPARE(getReply.value(QStringLiteral("result")).toObject().value(QStringLiteral("channels")).toArray().count(), 0);

    QJsonObject dmxReply = sendAndWaitForReply(QStringLiteral("io.dmx.universe.get"), getParams);
    QJsonArray values = dmxReply.value(QStringLiteral("result")).toObject().value(QStringLiteral("values")).toArray();
    QVERIFY(values.size() > 41);
    QCOMPARE(values.at(40).toInt(), 0);
    QCOMPARE(values.at(41).toInt(), 0);

    // Second malformed-entry shape: an out-of-range value rather than a
    // missing field - same all-or-nothing outcome.
    QJsonArray channels2;
    channels2.append(channelEntry(42, 88));
    channels2.append(channelEntry(43, 300)); // out of 0-255
    QJsonObject params2;
    params2.insert(QStringLiteral("channels"), channels2);
    QJsonObject reply2 = sendAndWaitForReply(QStringLiteral("io.simpleDesk.setChannels"), params2);
    QCOMPARE(reply2.value(QStringLiteral("ok")).toBool(), false);

    QJsonObject getReply2 = sendAndWaitForReply(QStringLiteral("io.simpleDesk.get"), getParams);
    QCOMPARE(getReply2.value(QStringLiteral("result")).toObject().value(QStringLiteral("channels")).toArray().count(), 0);
}

void ApiIoDomain_Test::simpleDeskResetChannelBroadcastsOverriddenFalse()
{
    helloAndGetClientId();

    QJsonObject setParams;
    setParams.insert(QStringLiteral("address"), 7);
    setParams.insert(QStringLiteral("value"), 99);
    sendAndWaitForReply(QStringLiteral("io.simpleDesk.setChannel"), setParams);

    QSignalSpy spy(m_client, &QWebSocket::textMessageReceived);
    QJsonObject resetParams;
    resetParams.insert(QStringLiteral("address"), 7);
    m_client->sendTextMessage(buildRequest(QStringLiteral("io.simpleDesk.resetChannel"), resetParams, QStringLiteral("t-rc")));
    QVERIFY(QTest::qWaitFor([&]() { return spy.count() >= 2; }, 2000));

    bool sawEvent = false;
    for (const QList<QVariant> &frame : spy)
    {
        QJsonObject obj = QJsonDocument::fromJson(frame.at(0).toString().toUtf8()).object();
        if (obj.value(QStringLiteral("type")).toString() == QStringLiteral("event") &&
            obj.value(QStringLiteral("topic")).toString() == QStringLiteral("io.simpleDesk.channelChanged"))
        {
            QJsonObject data = obj.value(QStringLiteral("data")).toObject();
            QCOMPARE(data.value(QStringLiteral("address")).toInt(), 7);
            // No fixture patched at address 7 in this test, so the "restored"
            // value is the raw-channel default (0) - see resetChannel's own
            // handler comment for why this is computed synchronously.
            QCOMPARE(data.value(QStringLiteral("value")).toInt(), 0);
            QCOMPARE(data.value(QStringLiteral("overridden")).toBool(), false);
            sawEvent = true;
        }
    }
    QVERIFY(sawEvent);

    QJsonObject getParams;
    getParams.insert(QStringLiteral("universeId"), 0);
    QJsonObject getReply = sendAndWaitForReply(QStringLiteral("io.simpleDesk.get"), getParams);
    QCOMPARE(getReply.value(QStringLiteral("result")).toObject().value(QStringLiteral("channels")).toArray().count(), 0);
}

void ApiIoDomain_Test::simpleDeskResetUniverseClearsHeldValues()
{
    helloAndGetClientId();

    QJsonObject setParams1, setParams2;
    setParams1.insert(QStringLiteral("address"), 1);
    setParams1.insert(QStringLiteral("value"), 10);
    setParams2.insert(QStringLiteral("address"), 2);
    setParams2.insert(QStringLiteral("value"), 20);
    sendAndWaitForReply(QStringLiteral("io.simpleDesk.setChannel"), setParams1);
    sendAndWaitForReply(QStringLiteral("io.simpleDesk.setChannel"), setParams2);

    QJsonObject resetParams;
    resetParams.insert(QStringLiteral("universeId"), 0);
    QJsonObject resetReply = sendAndWaitForReply(QStringLiteral("io.simpleDesk.resetUniverse"), resetParams);
    QCOMPARE(resetReply.value(QStringLiteral("ok")).toBool(), true);

    QJsonObject getParams;
    getParams.insert(QStringLiteral("universeId"), 0);
    QJsonObject getReply = sendAndWaitForReply(QStringLiteral("io.simpleDesk.get"), getParams);
    QCOMPARE(getReply.value(QStringLiteral("result")).toObject().value(QStringLiteral("channels")).toArray().count(), 0);
}

void ApiIoDomain_Test::simpleDeskSetUniverseFilterBroadcastsEvent()
{
    QString clientId = helloAndGetClientId();

    QSignalSpy spy(m_client, &QWebSocket::textMessageReceived);
    QJsonObject params;
    params.insert(QStringLiteral("universeId"), 0);
    m_client->sendTextMessage(buildRequest(QStringLiteral("io.simpleDesk.setUniverseFilter"), params, QStringLiteral("t-uf")));
    QVERIFY(QTest::qWaitFor([&]() { return spy.count() >= 2; }, 2000));

    bool sawEvent = false;
    for (const QList<QVariant> &frame : spy)
    {
        QJsonObject obj = QJsonDocument::fromJson(frame.at(0).toString().toUtf8()).object();
        if (obj.value(QStringLiteral("type")).toString() == QStringLiteral("event") &&
            obj.value(QStringLiteral("topic")).toString() == QStringLiteral("io.simpleDesk.universeFilterChanged"))
        {
            QCOMPARE(obj.value(QStringLiteral("data")).toObject().value(QStringLiteral("universeId")).toInt(), 0);
            QCOMPARE(obj.value(QStringLiteral("originClientId")).toString(), clientId);
            sawEvent = true;
        }
    }
    QVERIFY(sawEvent);
}

void ApiIoDomain_Test::simpleDeskGetOnMissingUniverseIsNotFound()
{
    helloAndGetClientId();

    QJsonObject params;
    // Doc(QObject*, int universes = 4) only patches ids 0-3 by default.
    params.insert(QStringLiteral("universeId"), 999);
    QJsonObject reply = sendAndWaitForReply(QStringLiteral("io.simpleDesk.get"), params);
    QCOMPARE(reply.value(QStringLiteral("ok")).toBool(), false);
    QCOMPARE(reply.value(QStringLiteral("error")).toObject().value(QStringLiteral("code")).toString(),
              QStringLiteral("NOT_FOUND"));
}

void ApiIoDomain_Test::simpleDeskDumpCreatesNewSceneAndBumpsRevision()
{
    helloAndGetClientId();
    quint32 before = m_doc->docRevision();

    QJsonObject params;
    params.insert(QStringLiteral("baseRevision"), int(before));
    params.insert(QStringLiteral("name"), QStringLiteral("My Dump"));
    QJsonObject reply = sendAndWaitForReply(QStringLiteral("io.simpleDesk.dump"), params);

    QCOMPARE(reply.value(QStringLiteral("ok")).toBool(), true);
    QJsonObject result = reply.value(QStringLiteral("result")).toObject();
    QVERIFY(result.value(QStringLiteral("sceneId")).toString().isEmpty() == false);
    QVERIFY(quint32(result.value(QStringLiteral("docRevision")).toInt()) > before);

    bool ok = false;
    quint32 sceneId = result.value(QStringLiteral("sceneId")).toString().toUInt(&ok);
    QVERIFY(ok);
    Scene *scene = qobject_cast<Scene *>(m_doc->function(sceneId));
    QVERIFY(scene != nullptr);
    QCOMPARE(scene->name(), QStringLiteral("My Dump"));
}

void ApiIoDomain_Test::simpleDeskDumpWithStaleRevisionConflicts()
{
    helloAndGetClientId();
    quint32 before = m_doc->docRevision();

    QJsonObject params;
    params.insert(QStringLiteral("baseRevision"), int(before) + 1); // deliberately stale
    QJsonObject reply = sendAndWaitForReply(QStringLiteral("io.simpleDesk.dump"), params);

    QCOMPARE(reply.value(QStringLiteral("ok")).toBool(), false);
    QJsonObject error = reply.value(QStringLiteral("error")).toObject();
    QCOMPARE(error.value(QStringLiteral("code")).toString(), QStringLiteral("CONFLICT"));
    QCOMPARE(error.value(QStringLiteral("details")).toObject().value(QStringLiteral("docRevision")).toInt(), int(before));
}

void ApiIoDomain_Test::simpleDeskDumpBroadcastsFunctionsCreatedEvent()
{
    QString clientId = helloAndGetClientId();
    quint32 before = m_doc->docRevision();

    QSignalSpy spy(m_client, &QWebSocket::textMessageReceived);
    QJsonObject params;
    params.insert(QStringLiteral("baseRevision"), int(before));
    params.insert(QStringLiteral("name"), QStringLiteral("Dump Two"));
    m_client->sendTextMessage(buildRequest(QStringLiteral("io.simpleDesk.dump"), params, QStringLiteral("t-dump")));
    QVERIFY(QTest::qWaitFor([&]() { return spy.count() >= 2; }, 2000));

    QString sceneIdFromResponse;
    bool sawEvent = false;
    for (const QList<QVariant> &frame : spy)
    {
        QJsonObject obj = QJsonDocument::fromJson(frame.at(0).toString().toUtf8()).object();
        QString type = obj.value(QStringLiteral("type")).toString();
        if (type == QStringLiteral("response") && obj.value(QStringLiteral("id")).toString() == QStringLiteral("t-dump"))
        {
            sceneIdFromResponse = obj.value(QStringLiteral("result")).toObject().value(QStringLiteral("sceneId")).toString();
        }
        else if (type == QStringLiteral("event") && obj.value(QStringLiteral("topic")).toString() == QStringLiteral("functions.created"))
        {
            QJsonObject data = obj.value(QStringLiteral("data")).toObject();
            QCOMPARE(data.value(QStringLiteral("function")).toObject().value(QStringLiteral("name")).toString(),
                      QStringLiteral("Dump Two"));
            QCOMPARE(data.value(QStringLiteral("function")).toObject().value(QStringLiteral("type")).toString(),
                      QStringLiteral("Scene"));
            QCOMPARE(obj.value(QStringLiteral("originClientId")).toString(), clientId);
            sawEvent = true;
        }
    }
    QVERIFY(sawEvent);
    QVERIFY(sceneIdFromResponse.isEmpty() == false);
}

void ApiIoDomain_Test::simpleDeskDumpMergeIntoExistingSceneBroadcastsFunctionsUpdated()
{
    Scene *existing = new Scene(m_doc);
    existing->setName(QStringLiteral("Existing Scene"));
    QVERIFY(m_doc->addFunction(existing));

    QString clientId = helloAndGetClientId();
    quint32 before = m_doc->docRevision();

    QSignalSpy spy(m_client, &QWebSocket::textMessageReceived);
    QJsonObject params;
    params.insert(QStringLiteral("baseRevision"), int(before));
    params.insert(QStringLiteral("targetSceneId"), QString::number(existing->id()));
    m_client->sendTextMessage(buildRequest(QStringLiteral("io.simpleDesk.dump"), params, QStringLiteral("t-merge")));
    QVERIFY(QTest::qWaitFor([&]() { return spy.count() >= 2; }, 2000));

    bool sawResponse = false, sawEvent = false;
    for (const QList<QVariant> &frame : spy)
    {
        QJsonObject obj = QJsonDocument::fromJson(frame.at(0).toString().toUtf8()).object();
        QString type = obj.value(QStringLiteral("type")).toString();
        if (type == QStringLiteral("response") && obj.value(QStringLiteral("id")).toString() == QStringLiteral("t-merge"))
        {
            QCOMPARE(obj.value(QStringLiteral("ok")).toBool(), true);
            QCOMPARE(obj.value(QStringLiteral("result")).toObject().value(QStringLiteral("sceneId")).toString(),
                      QString::number(existing->id()));
            sawResponse = true;
        }
        else if (type == QStringLiteral("event") && obj.value(QStringLiteral("topic")).toString() == QStringLiteral("functions.updated"))
        {
            QCOMPARE(obj.value(QStringLiteral("data")).toObject().value(QStringLiteral("functionId")).toString(),
                      QString::number(existing->id()));
            QCOMPARE(obj.value(QStringLiteral("originClientId")).toString(), clientId);
            sawEvent = true;
        }
    }
    QVERIFY(sawResponse);
    QVERIFY(sawEvent);
    QCOMPARE(existing->name(), QStringLiteral("Existing Scene")); // untouched - only targetSceneId, no rename
}

void ApiIoDomain_Test::simpleDeskDumpOnMissingTargetSceneIsNotFound()
{
    helloAndGetClientId();

    QJsonObject params;
    params.insert(QStringLiteral("baseRevision"), int(m_doc->docRevision()));
    params.insert(QStringLiteral("targetSceneId"), QStringLiteral("999999"));
    QJsonObject reply = sendAndWaitForReply(QStringLiteral("io.simpleDesk.dump"), params);

    QCOMPARE(reply.value(QStringLiteral("ok")).toBool(), false);
    QCOMPARE(reply.value(QStringLiteral("error")).toObject().value(QStringLiteral("code")).toString(),
              QStringLiteral("NOT_FOUND"));
}

static Fixture *addDimmerFixture(Doc *doc, const QString &name, quint32 universe, quint32 address, quint32 channels)
{
    Fixture *fixture = new Fixture(doc);
    fixture->setName(name);
    fixture->setUniverse(universe);
    fixture->setAddress(address);
    fixture->setChannels(channels); // generic dimmer
    if (doc->addFixture(fixture) == false)
    {
        delete fixture;
        return nullptr;
    }
    return fixture;
}

void ApiIoDomain_Test::simpleDeskDumpFixtureIdsLimitsToThoseFixtures()
{
    Fixture *a = addDimmerFixture(m_doc, QStringLiteral("A"), 0, 0, 2);
    Fixture *b = addDimmerFixture(m_doc, QStringLiteral("B"), 0, 10, 2);
    QVERIFY(a != nullptr && b != nullptr);
    helloAndGetClientId();

    QJsonArray channels;
    channels.append(channelEntry(0, 11));   // A ch 0
    channels.append(channelEntry(1, 12));   // A ch 1
    channels.append(channelEntry(10, 21));  // B ch 0
    QJsonObject setParams;
    setParams.insert(QStringLiteral("channels"), channels);
    QCOMPARE(sendAndWaitForReply(QStringLiteral("io.simpleDesk.setChannels"), setParams).value(QStringLiteral("ok")).toBool(), true);

    QJsonObject params;
    params.insert(QStringLiteral("baseRevision"), int(m_doc->docRevision()));
    params.insert(QStringLiteral("name"), QStringLiteral("Only B"));
    params.insert(QStringLiteral("nonZeroOnly"), true);
    params.insert(QStringLiteral("fixtureIds"), QJsonArray({ QString::number(b->id()) }));
    QJsonObject reply = sendAndWaitForReply(QStringLiteral("io.simpleDesk.dump"), params);
    QCOMPARE(reply.value(QStringLiteral("ok")).toBool(), true);

    Scene *scene = qobject_cast<Scene *>(m_doc->function(reply.value(QStringLiteral("result")).toObject()
                                                           .value(QStringLiteral("sceneId")).toString().toUInt()));
    QVERIFY(scene != nullptr);
    // nonZeroOnly drops B's ch 1 (0), fixtureIds drops both of A's
    QCOMPARE(scene->values().count(), 1);
    QCOMPARE(scene->values().first().fxi, b->id());
    QCOMPARE(int(scene->values().first().channel), 0);
    QCOMPARE(int(scene->values().first().value), 21);

    // A numeric id works as well, and both fixtures together give all three
    params.insert(QStringLiteral("baseRevision"), int(m_doc->docRevision()));
    params.insert(QStringLiteral("name"), QStringLiteral("A and B"));
    params.insert(QStringLiteral("fixtureIds"), QJsonArray({ int(a->id()), QString::number(b->id()) }));
    reply = sendAndWaitForReply(QStringLiteral("io.simpleDesk.dump"), params);
    QCOMPARE(reply.value(QStringLiteral("ok")).toBool(), true);
    scene = qobject_cast<Scene *>(m_doc->function(reply.value(QStringLiteral("result")).toObject()
                                                    .value(QStringLiteral("sceneId")).toString().toUInt()));
    QVERIFY(scene != nullptr);
    QCOMPARE(scene->values().count(), 3);
}

void ApiIoDomain_Test::simpleDeskDumpUnknownFixtureIdIsNotFound()
{
    helloAndGetClientId();
    const int functionsBefore = m_doc->functions().count();

    QJsonObject params;
    params.insert(QStringLiteral("baseRevision"), int(m_doc->docRevision()));
    params.insert(QStringLiteral("fixtureIds"), QJsonArray({ QStringLiteral("4242") }));
    QJsonObject reply = sendAndWaitForReply(QStringLiteral("io.simpleDesk.dump"), params);

    QCOMPARE(reply.value(QStringLiteral("ok")).toBool(), false);
    QCOMPARE(reply.value(QStringLiteral("error")).toObject().value(QStringLiteral("code")).toString(),
              QStringLiteral("NOT_FOUND"));
    QCOMPARE(m_doc->functions().count(), functionsBefore); // no Scene created
}

void ApiIoDomain_Test::simpleDeskOverrideOnUniverse1FixtureHitsItsChannel()
{
    // Fixture channel resolution used Fixture::address() (the low 9 bits)
    // against the universe-qualified address, so on any universe > 0 the
    // override landed on a bogus relative channel (512 + ...) and never
    // reached the output.
    Fixture *fx = addDimmerFixture(m_doc, QStringLiteral("U1"), 1, 10, 4);
    QVERIFY(fx != nullptr);
    helloAndGetClientId();

    QJsonArray channels;
    channels.append(channelEntry((1 << 9) + 12, 200)); // fixture channel 2
    QJsonObject setParams;
    setParams.insert(QStringLiteral("channels"), channels);
    QCOMPARE(sendAndWaitForReply(QStringLiteral("io.simpleDesk.setChannels"), setParams).value(QStringLiteral("ok")).toBool(), true);

    QJsonObject getParams;
    getParams.insert(QStringLiteral("universeId"), 1);
    QVERIFY(QTest::qWaitFor([&]()
    {
        QJsonObject dmxReply = sendAndWaitForReply(QStringLiteral("io.dmx.universe.get"), getParams);
        QJsonArray values = dmxReply.value(QStringLiteral("result")).toObject().value(QStringLiteral("values")).toArray();
        return values.size() > 12 && values.at(12).toInt() == 200;
    }, 2000));
}

void ApiIoDomain_Test::simpleDeskOverrideSurvivesProjectUniverseReload()
{
    helloAndGetClientId();
    QJsonArray channels;
    channels.append(channelEntry(5, 123));
    QJsonObject setParams;
    setParams.insert(QStringLiteral("channels"), channels);
    QCOMPARE(sendAndWaitForReply(QStringLiteral("io.simpleDesk.setChannels"), setParams).value(QStringLiteral("ok")).toBool(), true);

    QJsonObject getParams;
    getParams.insert(QStringLiteral("universeId"), 0);
    auto outputIs = [&](int v)
    {
        QJsonObject dmxReply = sendAndWaitForReply(QStringLiteral("io.dmx.universe.get"), getParams);
        QJsonArray values = dmxReply.value(QStringLiteral("result")).toObject().value(QStringLiteral("values")).toArray();
        return values.size() > 5 && values.at(5).toInt() == v;
    };
    QVERIFY(QTest::qWaitFor([&]() { return outputIs(123); }, 2000));

    // What InputOutputMap::loadXML() does on every project load: every
    // Universe is deleted (no universeRemoved) and new ones are added.
    InputOutputMap *ioMap = m_doc->inputOutputMap();
    ioMap->removeAllUniverses();
    for (quint32 i = 0; i < 4; i++)
        QVERIFY(ioMap->addUniverse(i));
    ioMap->startUniverses();

    // The held override is still reported and still reaches the output
    QVERIFY(QTest::qWaitFor([&]() { return outputIs(123); }, 3000));
}

/*********************************************************************
 * Plugins, patches, universe update/delete, input profiles
 *********************************************************************/

QString ApiIoDomain_Test::loadStubPlugin()
{
    // Same stub plugin engine/test/inputoutputmap uses, located relative to
    // this binary rather than the cwd so it works from ctest (cwd = binary
    // dir) and from a shell in build/ alike. add_dependencies() in this
    // suite's CMakeLists.txt makes sure it has been built.
    QDir dir(QCoreApplication::applicationDirPath() + QStringLiteral("/../../../engine/test/iopluginstub"));
    dir.setFilter(QDir::Files);
    dir.setNameFilters(QStringList() << QStringLiteral("*%1").arg(KExtPlugin));
    m_doc->ioPluginCache()->load(dir);
    QList<QLCIOPlugin *> plugins = m_doc->ioPluginCache()->plugins();
    return plugins.isEmpty() ? QString() : plugins.first()->name();
}

QList<QJsonObject> ApiIoDomain_Test::eventsWithTopic(QSignalSpy &spy, const QString &topic)
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

void ApiIoDomain_Test::pluginListDescribesStubPluginLines()
{
    QString stubName = loadStubPlugin();
    QVERIFY2(stubName.isEmpty() == false, "iopluginstub DLL not found - is engine/test built?");
    helloAndGetClientId();

    QJsonObject reply = sendAndWaitForReply(QStringLiteral("io.plugin.list"), QJsonObject());
    QCOMPARE(reply.value(QStringLiteral("ok")).toBool(), true);
    QJsonArray plugins = reply.value(QStringLiteral("result")).toObject().value(QStringLiteral("plugins")).toArray();
    QCOMPARE(plugins.count(), 1);
    QJsonObject plugin = plugins.at(0).toObject();
    QCOMPARE(plugin.value(QStringLiteral("name")).toString(), stubName);
    QCOMPARE(plugin.value(QStringLiteral("canConfigure")).toBool(), false);
    QCOMPARE(plugin.value(QStringLiteral("supportsFeedback")).toBool(), false);
    QJsonArray capabilities = plugin.value(QStringLiteral("capabilities")).toArray();
    QVERIFY(capabilities.contains(QStringLiteral("Input")));
    QVERIFY(capabilities.contains(QStringLiteral("Output")));
    QJsonArray inputLines = plugin.value(QStringLiteral("inputLines")).toArray();
    QJsonArray outputLines = plugin.value(QStringLiteral("outputLines")).toArray();
    QCOMPARE(inputLines.count(), 4);
    QCOMPARE(outputLines.count(), 4);
    QCOMPARE(outputLines.at(2).toObject().value(QStringLiteral("index")).toInt(), 2);
    QCOMPARE(outputLines.at(2).toObject().value(QStringLiteral("line")).toInt(), 2);
    QVERIFY(outputLines.at(2).toObject().value(QStringLiteral("name")).toString().isEmpty() == false);
}

void ApiIoDomain_Test::patchSetOutputBumpsRevisionAndBroadcastsUniverseUpdated()
{
    QString stubName = loadStubPlugin();
    QVERIFY(stubName.isEmpty() == false);
    QString clientId = helloAndGetClientId();
    quint32 revisionBefore = m_doc->docRevision();

    QSignalSpy spy(m_client, &QWebSocket::textMessageReceived);
    QJsonObject params;
    params.insert(QStringLiteral("universeId"), 0);
    params.insert(QStringLiteral("direction"), QStringLiteral("output"));
    params.insert(QStringLiteral("plugin"), stubName);
    params.insert(QStringLiteral("line"), 1);
    QJsonObject reply = sendAndWaitForReply(QStringLiteral("io.patch.set"), params);
    QCOMPARE(reply.value(QStringLiteral("ok")).toBool(), true);
    QCOMPARE(quint32(reply.value(QStringLiteral("result")).toObject().value(QStringLiteral("docRevision")).toInt()), revisionBefore + 1);

    Universe *universe = m_doc->inputOutputMap()->universe(0);
    QCOMPARE(universe->outputPatchesCount(), 1);
    QCOMPARE(universe->outputPatch(0)->pluginName(), stubName);
    QCOMPARE(universe->outputPatch(0)->output(), quint32(1));

    QVERIFY(QTest::qWaitFor([&]() { return eventsWithTopic(spy, QStringLiteral("io.universe.updated")).isEmpty() == false; }, 2000));
    QJsonObject event = eventsWithTopic(spy, QStringLiteral("io.universe.updated")).last();
    QCOMPARE(event.value(QStringLiteral("originClientId")).toString(), clientId);
    QJsonObject detail = event.value(QStringLiteral("data")).toObject().value(QStringLiteral("universe")).toObject();
    QCOMPARE(detail.value(QStringLiteral("id")).toInt(), 0);
    QCOMPARE(detail.value(QStringLiteral("outputPatches")).toArray().count(), 1);
    QCOMPARE(detail.value(QStringLiteral("outputPatches")).toArray().at(0).toObject().value(QStringLiteral("output")).toInt(), 1);

    // Default index 0 = replace the primary patch, not append a second one
    params.insert(QStringLiteral("line"), 3);
    QCOMPARE(sendAndWaitForReply(QStringLiteral("io.patch.set"), params).value(QStringLiteral("ok")).toBool(), true);
    QCOMPARE(universe->outputPatchesCount(), 1);
    QCOMPARE(universe->outputPatch(0)->output(), quint32(3));

    // io.yaml spelling (patchType/pluginName) with an explicit append index
    QJsonObject specParams;
    specParams.insert(QStringLiteral("universeId"), 0);
    specParams.insert(QStringLiteral("patchType"), QStringLiteral("output"));
    specParams.insert(QStringLiteral("pluginName"), stubName);
    specParams.insert(QStringLiteral("line"), 0);
    specParams.insert(QStringLiteral("index"), 1);
    QCOMPARE(sendAndWaitForReply(QStringLiteral("io.patch.set"), specParams).value(QStringLiteral("ok")).toBool(), true);
    QCOMPARE(universe->outputPatchesCount(), 2);

    // Remove without index drops every output patch
    QJsonObject removeParams;
    removeParams.insert(QStringLiteral("universeId"), 0);
    removeParams.insert(QStringLiteral("direction"), QStringLiteral("output"));
    QCOMPARE(sendAndWaitForReply(QStringLiteral("io.patch.remove"), removeParams).value(QStringLiteral("ok")).toBool(), true);
    QCOMPARE(universe->outputPatchesCount(), 0);
}

void ApiIoDomain_Test::patchSetInputWithProfileThenRemoveInput()
{
    QString stubName = loadStubPlugin();
    QVERIFY(stubName.isEmpty() == false);
    QLCInputProfile *profile = new QLCInputProfile();
    profile->setManufacturer(QStringLiteral("Acme"));
    profile->setModel(QStringLiteral("Faderbox"));
    QVERIFY(m_doc->inputOutputMap()->addProfile(profile));
    helloAndGetClientId();

    QJsonObject params;
    params.insert(QStringLiteral("universeId"), 2);
    params.insert(QStringLiteral("direction"), QStringLiteral("input"));
    params.insert(QStringLiteral("plugin"), stubName);
    params.insert(QStringLiteral("line"), 2);
    params.insert(QStringLiteral("profile"), profile->name());
    QJsonObject reply = sendAndWaitForReply(QStringLiteral("io.patch.set"), params);
    QCOMPARE(reply.value(QStringLiteral("ok")).toBool(), true);

    Universe *universe = m_doc->inputOutputMap()->universe(2);
    QVERIFY(universe->inputPatch() != nullptr);
    QCOMPARE(universe->inputPatch()->pluginName(), stubName);
    QCOMPARE(universe->inputPatch()->input(), quint32(2));
    QCOMPARE(universe->inputPatch()->profile(), profile);

    // Line change without a profile key keeps the current profile...
    QJsonObject lineOnly;
    lineOnly.insert(QStringLiteral("universeId"), 2);
    lineOnly.insert(QStringLiteral("direction"), QStringLiteral("input"));
    lineOnly.insert(QStringLiteral("plugin"), stubName);
    lineOnly.insert(QStringLiteral("line"), 3);
    QCOMPARE(sendAndWaitForReply(QStringLiteral("io.patch.set"), lineOnly).value(QStringLiteral("ok")).toBool(), true);
    QCOMPARE(universe->inputPatch()->input(), quint32(3));
    QCOMPARE(universe->inputPatch()->profile(), profile);
    // ...while an explicit "" clears it (QML setInputProfile(universe, "") semantics)
    lineOnly.insert(QStringLiteral("profile"), QString());
    QCOMPARE(sendAndWaitForReply(QStringLiteral("io.patch.set"), lineOnly).value(QStringLiteral("ok")).toBool(), true);
    QVERIFY(universe->inputPatch()->profile() == nullptr);
    QCOMPARE(sendAndWaitForReply(QStringLiteral("io.patch.set"), params).value(QStringLiteral("ok")).toBool(), true);
    QCOMPARE(universe->inputPatch()->profile(), profile);

    // Unknown profile name is rejected before anything is touched
    QJsonObject badProfile = params;
    badProfile.insert(QStringLiteral("profile"), QStringLiteral("Nope Nothing"));
    reply = sendAndWaitForReply(QStringLiteral("io.patch.set"), badProfile);
    QCOMPARE(reply.value(QStringLiteral("ok")).toBool(), false);
    QCOMPARE(reply.value(QStringLiteral("error")).toObject().value(QStringLiteral("code")).toString(), QStringLiteral("NOT_FOUND"));
    QCOMPARE(universe->inputPatch()->profile(), profile);

    QJsonObject removeParams;
    removeParams.insert(QStringLiteral("universeId"), 2);
    removeParams.insert(QStringLiteral("direction"), QStringLiteral("input"));
    reply = sendAndWaitForReply(QStringLiteral("io.patch.remove"), removeParams);
    QCOMPARE(reply.value(QStringLiteral("ok")).toBool(), true);
    QVERIFY(universe->inputPatch() == nullptr);
    QJsonObject detail = sendAndWaitForReply(QStringLiteral("io.universe.get"), removeParams).value(QStringLiteral("result")).toObject();
    QVERIFY(detail.value(QStringLiteral("inputPatch")).isNull());
}

void ApiIoDomain_Test::patchSetUnknownPluginIsNotFound()
{
    helloAndGetClientId();
    QJsonObject params;
    params.insert(QStringLiteral("universeId"), 0);
    params.insert(QStringLiteral("direction"), QStringLiteral("output"));
    params.insert(QStringLiteral("plugin"), QStringLiteral("No Such Plugin"));
    params.insert(QStringLiteral("line"), 0);
    QJsonObject reply = sendAndWaitForReply(QStringLiteral("io.patch.set"), params);
    QCOMPARE(reply.value(QStringLiteral("ok")).toBool(), false);
    QCOMPARE(reply.value(QStringLiteral("error")).toObject().value(QStringLiteral("code")).toString(), QStringLiteral("NOT_FOUND"));

    params.insert(QStringLiteral("direction"), QStringLiteral("sideways"));
    reply = sendAndWaitForReply(QStringLiteral("io.patch.set"), params);
    QCOMPARE(reply.value(QStringLiteral("ok")).toBool(), false);
    QCOMPARE(reply.value(QStringLiteral("error")).toObject().value(QStringLiteral("code")).toString(), QStringLiteral("INVALID_PARAMS"));
}

void ApiIoDomain_Test::patchSetFeedbackOnPluginWithoutFeedbackIsUnsupported()
{
    QString stubName = loadStubPlugin();
    QVERIFY(stubName.isEmpty() == false);
    helloAndGetClientId();
    QJsonObject params;
    params.insert(QStringLiteral("universeId"), 0);
    params.insert(QStringLiteral("direction"), QStringLiteral("feedback"));
    params.insert(QStringLiteral("plugin"), stubName);
    params.insert(QStringLiteral("line"), 0);
    QJsonObject reply = sendAndWaitForReply(QStringLiteral("io.patch.set"), params);
    QCOMPARE(reply.value(QStringLiteral("ok")).toBool(), false);
    QCOMPARE(reply.value(QStringLiteral("error")).toObject().value(QStringLiteral("code")).toString(), QStringLiteral("UNSUPPORTED"));

    // Out-of-range line on a finite plugin
    params.insert(QStringLiteral("direction"), QStringLiteral("output"));
    params.insert(QStringLiteral("line"), 7);
    reply = sendAndWaitForReply(QStringLiteral("io.patch.set"), params);
    QCOMPARE(reply.value(QStringLiteral("ok")).toBool(), false);
    QCOMPARE(reply.value(QStringLiteral("error")).toObject().value(QStringLiteral("code")).toString(), QStringLiteral("INVALID_PARAMS"));
}

void ApiIoDomain_Test::patchRemoveWhenNothingPatchedIsNotFound()
{
    helloAndGetClientId();
    for (const QString &direction : { QStringLiteral("input"), QStringLiteral("output"), QStringLiteral("feedback") })
    {
        QJsonObject params;
        params.insert(QStringLiteral("universeId"), 1);
        params.insert(QStringLiteral("direction"), direction);
        QJsonObject reply = sendAndWaitForReply(QStringLiteral("io.patch.remove"), params);
        QCOMPARE(reply.value(QStringLiteral("ok")).toBool(), false);
        QCOMPARE(reply.value(QStringLiteral("error")).toObject().value(QStringLiteral("code")).toString(), QStringLiteral("NOT_FOUND"));
    }
}

void ApiIoDomain_Test::universeUpdateRenamesAndSetsPassthrough()
{
    QString clientId = helloAndGetClientId();
    quint32 revisionBefore = m_doc->docRevision();
    QSignalSpy spy(m_client, &QWebSocket::textMessageReceived);

    QJsonObject params;
    params.insert(QStringLiteral("universeId"), 1);
    params.insert(QStringLiteral("name"), QStringLiteral("Stage Left"));
    params.insert(QStringLiteral("passthrough"), true);
    QJsonObject reply = sendAndWaitForReply(QStringLiteral("io.universe.update"), params);
    QCOMPARE(reply.value(QStringLiteral("ok")).toBool(), true);
    QCOMPARE(quint32(reply.value(QStringLiteral("result")).toObject().value(QStringLiteral("docRevision")).toInt()), revisionBefore + 1);
    QCOMPARE(m_doc->inputOutputMap()->universe(1)->name(), QStringLiteral("Stage Left"));
    QCOMPARE(m_doc->inputOutputMap()->universe(1)->passthrough(), true);

    QVERIFY(QTest::qWaitFor([&]() { return eventsWithTopic(spy, QStringLiteral("io.universe.updated")).isEmpty() == false; }, 2000));
    QJsonObject event = eventsWithTopic(spy, QStringLiteral("io.universe.updated")).last();
    QCOMPARE(event.value(QStringLiteral("originClientId")).toString(), clientId);
    QJsonObject detail = event.value(QStringLiteral("data")).toObject().value(QStringLiteral("universe")).toObject();
    QCOMPARE(detail.value(QStringLiteral("id")).toInt(), 1);
    QCOMPARE(detail.value(QStringLiteral("name")).toString(), QStringLiteral("Stage Left"));
    QCOMPARE(detail.value(QStringLiteral("passthrough")).toBool(), true);
}

void ApiIoDomain_Test::universeUpdateWithNoFieldsIsInvalidParams()
{
    helloAndGetClientId();
    QJsonObject params;
    params.insert(QStringLiteral("universeId"), 1);
    QJsonObject reply = sendAndWaitForReply(QStringLiteral("io.universe.update"), params);
    QCOMPARE(reply.value(QStringLiteral("ok")).toBool(), false);
    QCOMPARE(reply.value(QStringLiteral("error")).toObject().value(QStringLiteral("code")).toString(), QStringLiteral("INVALID_PARAMS"));

    params.insert(QStringLiteral("universeId"), 99);
    params.insert(QStringLiteral("name"), QStringLiteral("x"));
    reply = sendAndWaitForReply(QStringLiteral("io.universe.update"), params);
    QCOMPARE(reply.value(QStringLiteral("ok")).toBool(), false);
    QCOMPARE(reply.value(QStringLiteral("error")).toObject().value(QStringLiteral("code")).toString(), QStringLiteral("NOT_FOUND"));
}

void ApiIoDomain_Test::universeUpdateWithStaleRevisionConflicts()
{
    helloAndGetClientId();
    QJsonObject params;
    params.insert(QStringLiteral("universeId"), 1);
    params.insert(QStringLiteral("name"), QStringLiteral("Stale"));
    params.insert(QStringLiteral("baseRevision"), int(m_doc->docRevision()) + 5);
    QJsonObject reply = sendAndWaitForReply(QStringLiteral("io.universe.update"), params);
    QCOMPARE(reply.value(QStringLiteral("ok")).toBool(), false);
    QCOMPARE(reply.value(QStringLiteral("error")).toObject().value(QStringLiteral("code")).toString(), QStringLiteral("CONFLICT"));
    QVERIFY(m_doc->inputOutputMap()->universe(1)->name() != QStringLiteral("Stale"));

    // A matching baseRevision is accepted like the field being absent
    params.insert(QStringLiteral("baseRevision"), int(m_doc->docRevision()));
    reply = sendAndWaitForReply(QStringLiteral("io.universe.update"), params);
    QCOMPARE(reply.value(QStringLiteral("ok")).toBool(), true);
    QCOMPARE(m_doc->inputOutputMap()->universe(1)->name(), QStringLiteral("Stale"));
}

void ApiIoDomain_Test::universeDeleteRemovesTrailingUniverseAndBroadcasts()
{
    QString clientId = helloAndGetClientId();
    QCOMPARE(m_doc->inputOutputMap()->universesCount(), 4);
    quint32 revisionBefore = m_doc->docRevision();
    QSignalSpy spy(m_client, &QWebSocket::textMessageReceived);

    QJsonObject params;
    params.insert(QStringLiteral("universeId"), 3);
    QJsonObject reply = sendAndWaitForReply(QStringLiteral("io.universe.delete"), params);
    QCOMPARE(reply.value(QStringLiteral("ok")).toBool(), true);
    QCOMPARE(m_doc->inputOutputMap()->universesCount(), 3);
    QCOMPARE(quint32(reply.value(QStringLiteral("result")).toObject().value(QStringLiteral("docRevision")).toInt()), revisionBefore + 1);

    QVERIFY(QTest::qWaitFor([&]() { return eventsWithTopic(spy, QStringLiteral("io.universe.deleted")).isEmpty() == false; }, 2000));
    QJsonObject event = eventsWithTopic(spy, QStringLiteral("io.universe.deleted")).last();
    QCOMPARE(event.value(QStringLiteral("data")).toObject().value(QStringLiteral("universeId")).toInt(), 3);
    QCOMPARE(quint32(event.value(QStringLiteral("data")).toObject().value(QStringLiteral("docRevision")).toInt()), revisionBefore + 1);
    QCOMPARE(event.value(QStringLiteral("originClientId")).toString(), clientId);

    // And it is really gone from the API's point of view too
    reply = sendAndWaitForReply(QStringLiteral("io.universe.get"), params);
    QCOMPARE(reply.value(QStringLiteral("ok")).toBool(), false);
    QCOMPARE(reply.value(QStringLiteral("error")).toObject().value(QStringLiteral("code")).toString(), QStringLiteral("NOT_FOUND"));
}

void ApiIoDomain_Test::universeDeleteNonTrailingIsInvalidParams()
{
    helloAndGetClientId();
    QJsonObject params;
    params.insert(QStringLiteral("universeId"), 0);
    QJsonObject reply = sendAndWaitForReply(QStringLiteral("io.universe.delete"), params);
    QCOMPARE(reply.value(QStringLiteral("ok")).toBool(), false);
    QCOMPARE(reply.value(QStringLiteral("error")).toObject().value(QStringLiteral("code")).toString(), QStringLiteral("INVALID_PARAMS"));
    QCOMPARE(reply.value(QStringLiteral("error")).toObject().value(QStringLiteral("details")).toObject()
                 .value(QStringLiteral("deletableUniverseId")).toInt(), 3);
    QCOMPARE(m_doc->inputOutputMap()->universesCount(), 4);
}

void ApiIoDomain_Test::universeDeleteWithPatchedFixturesRequiresForce()
{
    helloAndGetClientId();
    Fixture *fixture = new Fixture(m_doc);
    fixture->setName(QStringLiteral("Par in U4"));
    fixture->setUniverse(3);
    fixture->setAddress(10);
    fixture->setChannels(2);
    QVERIFY(m_doc->addFixture(fixture));
    quint32 fixtureId = fixture->id();

    QJsonObject params;
    params.insert(QStringLiteral("universeId"), 3);
    QJsonObject reply = sendAndWaitForReply(QStringLiteral("io.universe.delete"), params);
    QCOMPARE(reply.value(QStringLiteral("ok")).toBool(), false);
    QCOMPARE(reply.value(QStringLiteral("error")).toObject().value(QStringLiteral("code")).toString(), QStringLiteral("INVALID_STATE"));
    QJsonArray blocking = reply.value(QStringLiteral("error")).toObject().value(QStringLiteral("details")).toObject()
                              .value(QStringLiteral("fixtureIds")).toArray();
    QCOMPARE(blocking.count(), 1);
    QCOMPARE(blocking.at(0).toString(), QString::number(fixtureId));
    QCOMPARE(m_doc->inputOutputMap()->universesCount(), 4);
    QVERIFY(m_doc->fixture(fixtureId) != nullptr);

    QSignalSpy spy(m_client, &QWebSocket::textMessageReceived);
    params.insert(QStringLiteral("force"), true);
    reply = sendAndWaitForReply(QStringLiteral("io.universe.delete"), params);
    QCOMPARE(reply.value(QStringLiteral("ok")).toBool(), true);
    QCOMPARE(m_doc->inputOutputMap()->universesCount(), 3);
    QVERIFY(m_doc->fixture(fixtureId) == nullptr);
    QVERIFY(QTest::qWaitFor([&]() { return eventsWithTopic(spy, QStringLiteral("fixtures.unpatched")).isEmpty() == false; }, 2000));
    QJsonObject unpatched = eventsWithTopic(spy, QStringLiteral("fixtures.unpatched")).first();
    QCOMPARE(unpatched.value(QStringLiteral("data")).toObject().value(QStringLiteral("fixtureIds")).toArray().at(0).toString(),
             QString::number(fixtureId));
    QVERIFY(QTest::qWaitFor([&]() { return eventsWithTopic(spy, QStringLiteral("io.universe.deleted")).isEmpty() == false; }, 2000));
}

void ApiIoDomain_Test::universeDeleteLastUniverseIsInvalidState()
{
    helloAndGetClientId();
    for (int universeId = 3; universeId >= 1; universeId--)
    {
        QJsonObject params;
        params.insert(QStringLiteral("universeId"), universeId);
        QCOMPARE(sendAndWaitForReply(QStringLiteral("io.universe.delete"), params).value(QStringLiteral("ok")).toBool(), true);
    }
    QCOMPARE(m_doc->inputOutputMap()->universesCount(), 1);

    QJsonObject params;
    params.insert(QStringLiteral("universeId"), 0);
    QJsonObject reply = sendAndWaitForReply(QStringLiteral("io.universe.delete"), params);
    QCOMPARE(reply.value(QStringLiteral("ok")).toBool(), false);
    QCOMPARE(reply.value(QStringLiteral("error")).toObject().value(QStringLiteral("code")).toString(), QStringLiteral("INVALID_STATE"));
    QCOMPARE(m_doc->inputOutputMap()->universesCount(), 1);
}

void ApiIoDomain_Test::inputProfileListReturnsLoadedProfiles()
{
    QLCInputProfile *profile = new QLCInputProfile();
    profile->setManufacturer(QStringLiteral("Acme"));
    profile->setModel(QStringLiteral("Faderbox"));
    profile->setType(QLCInputProfile::OSC);
    QVERIFY(m_doc->inputOutputMap()->addProfile(profile));
    helloAndGetClientId();

    QJsonObject reply = sendAndWaitForReply(QStringLiteral("io.inputProfile.list"), QJsonObject());
    QCOMPARE(reply.value(QStringLiteral("ok")).toBool(), true);
    QJsonArray profiles = reply.value(QStringLiteral("result")).toObject().value(QStringLiteral("profiles")).toArray();
    QCOMPARE(profiles.count(), 1);
    QJsonObject entry = profiles.at(0).toObject();
    QCOMPARE(entry.value(QStringLiteral("name")).toString(), profile->name());
    QCOMPARE(entry.value(QStringLiteral("manufacturer")).toString(), QStringLiteral("Acme"));
    QCOMPARE(entry.value(QStringLiteral("model")).toString(), QStringLiteral("Faderbox"));
    QCOMPARE(entry.value(QStringLiteral("type")).toString(), QStringLiteral("OSC"));
}

QTEST_MAIN(ApiIoDomain_Test)
