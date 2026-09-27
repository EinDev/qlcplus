/*
  Q Light Controller Plus - Control API unit test
  apivclivedomain_test.cpp

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

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonValue>
#include <QSignalSpy>
#include <QWebSocket>
#include <QtTest>

#include "apivclivedomain_test.h"
#include "apiserver.h"
#include "doc.h"
#include "fakevchost.h"
#include "fixture.h"
#include "fixturegroup.h"
#include "scene.h"

static QString buildRequest(const QString &method, const QJsonObject &params, const QString &id)
{
    QJsonObject obj;
    obj.insert(QStringLiteral("type"), QStringLiteral("request"));
    obj.insert(QStringLiteral("id"), id);
    obj.insert(QStringLiteral("method"), method);
    obj.insert(QStringLiteral("params"), params);
    return QString::fromUtf8(QJsonDocument(obj).toJson(QJsonDocument::Compact));
}

/** Every "event" frame with $topic captured by $spy so far, with originClientId folded in as "_origin". */
static QList<QJsonObject> eventsWithTopic(const QSignalSpy &spy, const QString &topic)
{
    QList<QJsonObject> result;
    for (const QList<QVariant> &frame : spy)
    {
        QJsonObject obj = QJsonDocument::fromJson(frame.at(0).toString().toUtf8()).object();
        if (obj.value(QStringLiteral("type")).toString() != QStringLiteral("event") ||
            obj.value(QStringLiteral("topic")).toString() != topic)
            continue;
        QJsonObject data = obj.value(QStringLiteral("data")).toObject();
        data.insert(QStringLiteral("_origin"), obj.value(QStringLiteral("originClientId")));
        result.append(data);
    }
    return result;
}

static QString errorCode(const QJsonObject &reply)
{
    return reply.value(QStringLiteral("error")).toObject().value(QStringLiteral("code")).toString();
}

static bool isOk(const QJsonObject &reply)
{
    return reply.value(QStringLiteral("ok")).toBool();
}

static QJsonObject resultOf(const QJsonObject &reply)
{
    return reply.value(QStringLiteral("result")).toObject();
}

void ApiVcLiveDomain_Test::init()
{
    m_doc = new Doc(nullptr);
    m_vcHost = new FakeVcHost();
    m_apiServer = new ApiServer(m_vcHost, m_doc);
    QVERIFY(m_apiServer->listen(0, QHostAddress::LocalHost));

    m_client = new QWebSocket();
    m_client->open(QUrl(QStringLiteral("ws://127.0.0.1:%1/qlcplusapi").arg(m_apiServer->serverPort())));
    QVERIFY(QTest::qWaitFor([this]() { return m_client->state() == QAbstractSocket::ConnectedState; }, 2000));
}

void ApiVcLiveDomain_Test::cleanup()
{
    delete m_client;
    m_client = nullptr;
    delete m_apiServer;
    m_apiServer = nullptr;
    delete m_vcHost;
    m_vcHost = nullptr;
    delete m_doc;
    m_doc = nullptr;
}

QJsonObject ApiVcLiveDomain_Test::sendAndWaitForReply(const QString &method, const QJsonObject &params, const QString &requestId)
{
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

QString ApiVcLiveDomain_Test::helloAndGetClientId()
{
    QJsonObject reply = sendAndWaitForReply(QStringLiteral("hello"), QJsonObject(), QStringLiteral("t-hello"));
    return resultOf(reply).value(QStringLiteral("clientId")).toString();
}

int ApiVcLiveDomain_Test::currentDocRevision()
{
    QJsonObject reply = sendAndWaitForReply(QStringLiteral("hello"), QJsonObject(), QStringLiteral("t-rev"));
    return resultOf(reply).value(QStringLiteral("docRevision")).toInt();
}

QString ApiVcLiveDomain_Test::createWidget(const QString &widgetType, const QJsonObject &typeConfig)
{
    QJsonObject g;
    g.insert(QStringLiteral("x"), 0); g.insert(QStringLiteral("y"), 0);
    g.insert(QStringLiteral("width"), 100); g.insert(QStringLiteral("height"), 100);
    QJsonObject params;
    params.insert(QStringLiteral("widgetType"), widgetType);
    params.insert(QStringLiteral("page"), 0);
    params.insert(QStringLiteral("geometry"), g);
    if (typeConfig.isEmpty() == false)
        params.insert(QStringLiteral("typeConfig"), typeConfig);
    params.insert(QStringLiteral("baseRevision"), currentDocRevision());
    QJsonObject reply = sendAndWaitForReply(QStringLiteral("vc.widget.create"), params, QStringLiteral("t-create"));
    return resultOf(reply).value(QStringLiteral("widgetId")).toString();
}

QJsonObject ApiVcLiveDomain_Test::typeConfigOf(const QString &widgetId)
{
    QJsonObject p;
    p.insert(QStringLiteral("widgetId"), widgetId);
    QJsonObject reply = sendAndWaitForReply(QStringLiteral("vc.widget.get"), p, QStringLiteral("t-get"));
    return resultOf(reply).value(QStringLiteral("typeConfig")).toObject();
}

QJsonObject ApiVcLiveDomain_Test::structural(const QString &method, const QString &widgetId, const QJsonObject &extra, const QString &requestId)
{
    QJsonObject p = extra;
    p.insert(QStringLiteral("widgetId"), widgetId);
    p.insert(QStringLiteral("baseRevision"), currentDocRevision());
    return sendAndWaitForReply(method, p, requestId);
}

QList<quint32> ApiVcLiveDomain_Test::addFixtures(int count)
{
    QList<quint32> ids;
    for (int i = 0; i < count; i++)
    {
        Fixture *fixture = new Fixture(m_doc);
        fixture->setName(QStringLiteral("Mover %1").arg(i + 1));
        fixture->setChannels(4);
        fixture->setAddress(quint32(i * 4));
        fixture->setUniverse(0);
        if (m_doc->addFixture(fixture) == false)
            return ids;
        ids.append(fixture->id());
    }
    return ids;
}

static QJsonObject head(quint32 fixtureId, int headIndex)
{
    QJsonObject h;
    h.insert(QStringLiteral("fixtureId"), QString::number(fixtureId));
    h.insert(QStringLiteral("headIndex"), headIndex);
    return h;
}

/*****************************************************************************
 * XY Pad fixtures
 *****************************************************************************/

void ApiVcLiveDomain_Test::xyPadFixtureAddRemoveAndRange()
{
    QString clientId = helloAndGetClientId();
    QList<quint32> fx = addFixtures(2);
    QCOMPARE(fx.size(), 2);
    QString pad = createWidget(QStringLiteral("XYPad"));
    QVERIFY(pad.isEmpty() == false);

    QSignalSpy spy(m_client, &QWebSocket::textMessageReceived);

    // Whole fixture, then a single head of the other one
    QJsonObject add; add.insert(QStringLiteral("fixtureId"), QString::number(fx.at(0)));
    QJsonObject reply = structural(QStringLiteral("vc.xyPad.fixture.add"), pad, add, QStringLiteral("t-a1"));
    QVERIFY2(isOk(reply), qPrintable(QJsonDocument(reply).toJson()));
    QCOMPARE(resultOf(reply).value(QStringLiteral("docRevision")).toInt(), int(m_doc->docRevision()));

    QJsonObject addHead = head(fx.at(1), 0);
    QVERIFY(isOk(structural(QStringLiteral("vc.xyPad.fixture.add"), pad, addHead, QStringLiteral("t-a2"))));

    QJsonArray fixtures = typeConfigOf(pad).value(QStringLiteral("fixtures")).toArray();
    QCOMPARE(fixtures.size(), 2);
    QCOMPARE(fixtures.at(0).toObject().value(QStringLiteral("fixtureId")).toString(), QString::number(fx.at(0)));
    QCOMPARE(fixtures.at(1).toObject().value(QStringLiteral("headIndex")).toInt(), 0);

    // Both adds broadcast fixturesChanged (full list + revision, requester origin) and configChanged
    QList<QJsonObject> events = eventsWithTopic(spy, QStringLiteral("vc.xyPad.fixturesChanged"));
    QCOMPARE(events.size(), 2);
    QCOMPARE(events.at(1).value(QStringLiteral("fixtures")).toArray().size(), 2);
    QCOMPARE(events.at(1).value(QStringLiteral("widgetId")).toString(), pad);
    QCOMPARE(events.at(1).value(QStringLiteral("docRevision")).toInt(), int(m_doc->docRevision()));
    QCOMPARE(events.at(1).value(QStringLiteral("_origin")).toString(), clientId);
    QCOMPARE(eventsWithTopic(spy, QStringLiteral("vc.widget.configChanged")).size(), 2);

    // Adding the same head twice is refused (the engine silently skips it - the API says so)
    QCOMPARE(errorCode(structural(QStringLiteral("vc.xyPad.fixture.add"), pad, addHead, QStringLiteral("t-a3"))), QStringLiteral("INVALID_PARAMS"));

    // Range of the first entry, in display units
    QJsonObject range;
    range.insert(QStringLiteral("heads"), QJsonArray() << head(fx.at(0), 0));
    range.insert(QStringLiteral("xMin"), 10); range.insert(QStringLiteral("xMax"), 90); range.insert(QStringLiteral("xReverse"), true);
    range.insert(QStringLiteral("yMin"), 0); range.insert(QStringLiteral("yMax"), 50); range.insert(QStringLiteral("yReverse"), false);
    QVERIFY(isOk(structural(QStringLiteral("vc.xyPad.setHeadsRange"), pad, range, QStringLiteral("t-r1"))));
    QJsonObject first = typeConfigOf(pad).value(QStringLiteral("fixtures")).toArray().at(0).toObject();
    QCOMPARE(first.value(QStringLiteral("xRange")).toObject().value(QStringLiteral("max")).toInt(), 90);
    QCOMPARE(first.value(QStringLiteral("xRange")).toObject().value(QStringLiteral("reverse")).toBool(), true);
    QCOMPARE(first.value(QStringLiteral("yRange")).toObject().value(QStringLiteral("max")).toInt(), 50);

    // Missing booleans / a head that is not a real fixture
    QJsonObject bad = range; bad.remove(QStringLiteral("xReverse"));
    QCOMPARE(errorCode(structural(QStringLiteral("vc.xyPad.setHeadsRange"), pad, bad, QStringLiteral("t-r2"))), QStringLiteral("INVALID_PARAMS"));
    QJsonObject unknown = range; unknown.insert(QStringLiteral("heads"), QJsonArray() << head(9999, 0));
    QCOMPARE(errorCode(structural(QStringLiteral("vc.xyPad.setHeadsRange"), pad, unknown, QStringLiteral("t-r3"))), QStringLiteral("INVALID_PARAMS"));

    // Remove one, then a head that is not on the pad
    QJsonObject remove; remove.insert(QStringLiteral("heads"), QJsonArray() << head(fx.at(0), 0));
    QVERIFY(isOk(structural(QStringLiteral("vc.xyPad.fixture.remove"), pad, remove, QStringLiteral("t-d1"))));
    QCOMPARE(typeConfigOf(pad).value(QStringLiteral("fixtures")).toArray().size(), 1);
    QCOMPARE(errorCode(structural(QStringLiteral("vc.xyPad.fixture.remove"), pad, remove, QStringLiteral("t-d2"))), QStringLiteral("NOT_FOUND"));
    QJsonObject emptyHeads; emptyHeads.insert(QStringLiteral("heads"), QJsonArray());
    QCOMPARE(errorCode(structural(QStringLiteral("vc.xyPad.fixture.remove"), pad, emptyHeads, QStringLiteral("t-d3"))), QStringLiteral("INVALID_PARAMS"));
}

void ApiVcLiveDomain_Test::xyPadFixtureAddValidates()
{
    helloAndGetClientId();
    QList<quint32> fx = addFixtures(1);
    QString pad = createWidget(QStringLiteral("XYPad"));

    // Exactly one selector
    QCOMPARE(errorCode(structural(QStringLiteral("vc.xyPad.fixture.add"), pad, QJsonObject(), QStringLiteral("t-v1"))), QStringLiteral("INVALID_PARAMS"));
    QJsonObject two; two.insert(QStringLiteral("fixtureId"), QString::number(fx.at(0))); two.insert(QStringLiteral("universe"), 0);
    QCOMPARE(errorCode(structural(QStringLiteral("vc.xyPad.fixture.add"), pad, two, QStringLiteral("t-v2"))), QStringLiteral("INVALID_PARAMS"));
    // Unknown fixture / group / universe
    QJsonObject nofx; nofx.insert(QStringLiteral("fixtureId"), QStringLiteral("777"));
    QCOMPARE(errorCode(structural(QStringLiteral("vc.xyPad.fixture.add"), pad, nofx, QStringLiteral("t-v3"))), QStringLiteral("NOT_FOUND"));
    QJsonObject nogrp; nogrp.insert(QStringLiteral("fixtureGroupId"), QStringLiteral("777"));
    QCOMPARE(errorCode(structural(QStringLiteral("vc.xyPad.fixture.add"), pad, nogrp, QStringLiteral("t-v4"))), QStringLiteral("NOT_FOUND"));
    QJsonObject nouni; nouni.insert(QStringLiteral("universe"), 99);
    QCOMPARE(errorCode(structural(QStringLiteral("vc.xyPad.fixture.add"), pad, nouni, QStringLiteral("t-v5"))), QStringLiteral("NOT_FOUND"));
    // headIndex outside the fixture
    QCOMPARE(errorCode(structural(QStringLiteral("vc.xyPad.fixture.add"), pad, head(fx.at(0), 7), QStringLiteral("t-v6"))), QStringLiteral("INVALID_PARAMS"));
    // A universe is expanded server-side
    QJsonObject uni; uni.insert(QStringLiteral("universe"), 0);
    QVERIFY(isOk(structural(QStringLiteral("vc.xyPad.fixture.add"), pad, uni, QStringLiteral("t-v7"))));
    QCOMPARE(typeConfigOf(pad).value(QStringLiteral("fixtures")).toArray().size(), 1);
}

/*****************************************************************************
 * XY Pad presets (move / rename here; add / remove / apply are ApiVcDomain's generic methods)
 *****************************************************************************/

void ApiVcLiveDomain_Test::xyPadGroupAddCreatesPresetAndPresetsMoveRename()
{
    QString clientId = helloAndGetClientId();
    QList<quint32> fx = addFixtures(2);
    FixtureGroup *grp = new FixtureGroup(m_doc);
    grp->setName(QStringLiteral("Movers"));
    QVERIFY(m_doc->addFixtureGroup(grp));
    grp->assignFixture(fx.at(0));
    grp->assignFixture(fx.at(1));
    QString pad = createWidget(QStringLiteral("XYPad"));

    QSignalSpy spy(m_client, &QWebSocket::textMessageReceived);

    // Dropping a group also creates its preset: both lists change, the new preset id comes back
    QJsonObject add; add.insert(QStringLiteral("fixtureGroupId"), QString::number(grp->id()));
    QJsonObject reply = structural(QStringLiteral("vc.xyPad.fixture.add"), pad, add, QStringLiteral("t-g1"));
    QVERIFY(isOk(reply));
    int groupPreset = resultOf(reply).value(QStringLiteral("presetId")).toInt(-1);
    QVERIFY(groupPreset >= 0);
    QCOMPARE(eventsWithTopic(spy, QStringLiteral("vc.xyPad.fixturesChanged")).size(), 1);
    QList<QJsonObject> presetEvents = eventsWithTopic(spy, QStringLiteral("vc.xyPad.presetsChanged"));
    QCOMPARE(presetEvents.size(), 1);
    QCOMPARE(presetEvents.at(0).value(QStringLiteral("presets")).toArray().size(), 1);
    QCOMPARE(presetEvents.at(0).value(QStringLiteral("_origin")).toString(), clientId);
    QCOMPARE(typeConfigOf(pad).value(QStringLiteral("fixtures")).toArray().at(0).toObject().value(QStringLiteral("fixtureGroupId")).toString(), QString::number(grp->id()));

    // A second preset through the generic vc.widget.preset.add, then reorder + rename
    QJsonObject pos; pos.insert(QStringLiteral("presetType"), QStringLiteral("position"));
    QJsonObject padd; padd.insert(QStringLiteral("preset"), pos);
    reply = structural(QStringLiteral("vc.widget.preset.add"), pad, padd, QStringLiteral("t-g2"));
    QVERIFY2(isOk(reply), qPrintable(QJsonDocument(reply).toJson()));
    int posPreset = resultOf(reply).value(QStringLiteral("presetId")).toInt(-1);
    QVERIFY(posPreset > groupPreset);

    QJsonObject move; move.insert(QStringLiteral("presetId"), posPreset); move.insert(QStringLiteral("direction"), QStringLiteral("up"));
    reply = structural(QStringLiteral("vc.xyPad.preset.move"), pad, move, QStringLiteral("t-g3"));
    QVERIFY(isOk(reply));
    // The engine swaps ids: the moved preset now carries the group preset's old id
    QCOMPARE(resultOf(reply).value(QStringLiteral("presetId")).toInt(), groupPreset);
    QJsonArray presets = typeConfigOf(pad).value(QStringLiteral("presets")).toArray();
    QCOMPARE(presets.size(), 2);
    QCOMPARE(presets.at(0).toObject().value(QStringLiteral("presetType")).toString(), QStringLiteral("position"));
    QCOMPARE(presets.at(0).toObject().value(QStringLiteral("presetId")).toInt(), groupPreset);
    QCOMPARE(eventsWithTopic(spy, QStringLiteral("vc.xyPad.presetsChanged")).size(), 3); // group add, preset add, move

    // Moving the first one further up is a no-op that keeps the id
    move.insert(QStringLiteral("presetId"), groupPreset);
    reply = structural(QStringLiteral("vc.xyPad.preset.move"), pad, move, QStringLiteral("t-g4"));
    QVERIFY(isOk(reply));
    QCOMPARE(resultOf(reply).value(QStringLiteral("presetId")).toInt(), groupPreset);

    QJsonObject rename; rename.insert(QStringLiteral("presetId"), groupPreset); rename.insert(QStringLiteral("name"), QStringLiteral("Centre"));
    QVERIFY(isOk(structural(QStringLiteral("vc.xyPad.preset.rename"), pad, rename, QStringLiteral("t-g5"))));
    QCOMPARE(typeConfigOf(pad).value(QStringLiteral("presets")).toArray().at(0).toObject().value(QStringLiteral("name")).toString(), QStringLiteral("Centre"));

    // Validation: unknown preset, bad direction, empty name
    rename.insert(QStringLiteral("presetId"), 250);
    QCOMPARE(errorCode(structural(QStringLiteral("vc.xyPad.preset.rename"), pad, rename, QStringLiteral("t-g6"))), QStringLiteral("NOT_FOUND"));
    move.insert(QStringLiteral("direction"), QStringLiteral("sideways"));
    QCOMPARE(errorCode(structural(QStringLiteral("vc.xyPad.preset.move"), pad, move, QStringLiteral("t-g7"))), QStringLiteral("INVALID_PARAMS"));
    rename.insert(QStringLiteral("presetId"), groupPreset); rename.insert(QStringLiteral("name"), QStringLiteral("  "));
    QCOMPARE(errorCode(structural(QStringLiteral("vc.xyPad.preset.rename"), pad, rename, QStringLiteral("t-g8"))), QStringLiteral("INVALID_PARAMS"));
}

/*****************************************************************************
 * XY Pad floor position
 *****************************************************************************/

void ApiVcLiveDomain_Test::xyPadSetFloorPositionBroadcasts()
{
    QString clientId = helloAndGetClientId();
    QString pad = createWidget(QStringLiteral("XYPad"));

    QJsonObject p;
    p.insert(QStringLiteral("widgetId"), pad);
    p.insert(QStringLiteral("x"), 2.5); p.insert(QStringLiteral("y"), 1.0); p.insert(QStringLiteral("z"), 4.0);
    // Floor control off -> INVALID_STATE (the pad ignores the point then)
    QCOMPARE(errorCode(sendAndWaitForReply(QStringLiteral("vc.xyPad.setFloorPosition"), p, QStringLiteral("t-f1"))), QStringLiteral("INVALID_STATE"));

    QJsonObject cfg; cfg.insert(QStringLiteral("floorControl"), true);
    QJsonObject sc; sc.insert(QStringLiteral("config"), cfg);
    QVERIFY(isOk(structural(QStringLiteral("vc.widget.setConfig"), pad, sc, QStringLiteral("t-f2"))));

    QSignalSpy spy(m_client, &QWebSocket::textMessageReceived);
    QVERIFY(isOk(sendAndWaitForReply(QStringLiteral("vc.xyPad.setFloorPosition"), p, QStringLiteral("t-f3"))));
    QList<QJsonObject> events = eventsWithTopic(spy, QStringLiteral("vc.xyPad.floorPositionChanged"));
    QCOMPARE(events.size(), 1);
    QCOMPARE(events.at(0).value(QStringLiteral("x")).toDouble(), 2.5);
    QCOMPARE(events.at(0).value(QStringLiteral("z")).toDouble(), 4.0);
    QCOMPARE(events.at(0).value(QStringLiteral("_origin")).toString(), clientId);
    // Same point again: no change, no event
    QVERIFY(isOk(sendAndWaitForReply(QStringLiteral("vc.xyPad.setFloorPosition"), p, QStringLiteral("t-f4"))));
    QCOMPARE(eventsWithTopic(spy, QStringLiteral("vc.xyPad.floorPositionChanged")).size(), 1);

    // Non-numeric coordinate
    p.insert(QStringLiteral("y"), QStringLiteral("high"));
    QCOMPARE(errorCode(sendAndWaitForReply(QStringLiteral("vc.xyPad.setFloorPosition"), p, QStringLiteral("t-f5"))), QStringLiteral("INVALID_PARAMS"));

    // Disabled widget refuses live input
    QJsonObject dis; dis.insert(QStringLiteral("isDisabled"), true);
    QVERIFY(isOk(structural(QStringLiteral("vc.widget.update"), pad, dis, QStringLiteral("t-f6"))));
    p.insert(QStringLiteral("y"), 0.0);
    QCOMPARE(errorCode(sendAndWaitForReply(QStringLiteral("vc.xyPad.setFloorPosition"), p, QStringLiteral("t-f7"))), QStringLiteral("INVALID_STATE"));
}

/*****************************************************************************
 * Clock
 *****************************************************************************/

void ApiVcLiveDomain_Test::clockPlayPauseResetAndGatedTimeChanged()
{
    QString clientId = helloAndGetClientId();
    QString clock = createWidget(QStringLiteral("Clock"));
    QJsonObject p; p.insert(QStringLiteral("widgetId"), clock);

    // A plain Clock has no timer
    QCOMPARE(errorCode(sendAndWaitForReply(QStringLiteral("vc.clock.playPause"), p, QStringLiteral("t-c1"))), QStringLiteral("INVALID_STATE"));
    QCOMPARE(errorCode(sendAndWaitForReply(QStringLiteral("vc.clock.reset"), p, QStringLiteral("t-c2"))), QStringLiteral("INVALID_STATE"));

    QJsonObject cfg; cfg.insert(QStringLiteral("clockType"), QStringLiteral("Stopwatch"));
    QJsonObject sc; sc.insert(QStringLiteral("config"), cfg);
    QVERIFY(isOk(structural(QStringLiteral("vc.widget.setConfig"), clock, sc, QStringLiteral("t-c3"))));

    // Not subscribed: the start is acked but timeChanged is not delivered
    QSignalSpy spy(m_client, &QWebSocket::textMessageReceived);
    QVERIFY(isOk(sendAndWaitForReply(QStringLiteral("vc.clock.playPause"), p, QStringLiteral("t-c4"))));
    QTest::qWait(50);
    QCOMPARE(eventsWithTopic(spy, QStringLiteral("vc.clock.timeChanged")).size(), 0);

    QJsonObject sub; sub.insert(QStringLiteral("topics"), QJsonArray() << QStringLiteral("vc.clock.timeChanged"));
    QVERIFY(isOk(sendAndWaitForReply(QStringLiteral("subscribe"), sub, QStringLiteral("t-c5"))));

    // Pause -> event with the requester's origin and running=false
    QVERIFY(isOk(sendAndWaitForReply(QStringLiteral("vc.clock.playPause"), p, QStringLiteral("t-c6"))));
    QList<QJsonObject> events = eventsWithTopic(spy, QStringLiteral("vc.clock.timeChanged"));
    QCOMPARE(events.size(), 1);
    QCOMPARE(events.at(0).value(QStringLiteral("running")).toBool(), false);
    QCOMPARE(events.at(0).value(QStringLiteral("_origin")).toString(), clientId);

    // The engine's own tick: null origin
    m_vcHost->simulateClockTick(clock.toUInt(), 4200, true);
    QVERIFY(QTest::qWaitFor([&]() { return eventsWithTopic(spy, QStringLiteral("vc.clock.timeChanged")).size() == 2; }, 2000));
    events = eventsWithTopic(spy, QStringLiteral("vc.clock.timeChanged"));
    QCOMPARE(events.at(1).value(QStringLiteral("currentTime")).toInt(), 4200);
    QCOMPARE(events.at(1).value(QStringLiteral("running")).toBool(), true);
    QVERIFY(events.at(1).value(QStringLiteral("_origin")).isNull());

    // Reset -> stopped at 0; the live seed follows
    QVERIFY(isOk(sendAndWaitForReply(QStringLiteral("vc.clock.reset"), p, QStringLiteral("t-c7"))));
    events = eventsWithTopic(spy, QStringLiteral("vc.clock.timeChanged"));
    QCOMPARE(events.size(), 3);
    QCOMPARE(events.at(2).value(QStringLiteral("currentTime")).toInt(), 0);
    QJsonObject get = resultOf(sendAndWaitForReply(QStringLiteral("vc.widget.get"), p, QStringLiteral("t-c8")));
    QCOMPARE(get.value(QStringLiteral("running")).toBool(), false);
    QCOMPARE(get.value(QStringLiteral("currentTime")).toInt(), 0);
}

void ApiVcLiveDomain_Test::clockSchedulesAddUpdateRemove()
{
    helloAndGetClientId();
    Scene *s1 = new Scene(m_doc); s1->setName(QStringLiteral("Morning")); QVERIFY(m_doc->addFunction(s1));
    Scene *s2 = new Scene(m_doc); s2->setName(QStringLiteral("Evening")); QVERIFY(m_doc->addFunction(s2));
    QString clock = createWidget(QStringLiteral("Clock"));

    QSignalSpy spy(m_client, &QWebSocket::textMessageReceived);

    // Unknown function id -> nothing added
    QJsonObject bad; bad.insert(QStringLiteral("functionIds"), QJsonArray() << QStringLiteral("4242"));
    QCOMPARE(errorCode(structural(QStringLiteral("vc.clock.schedule.add"), clock, bad, QStringLiteral("t-s1"))), QStringLiteral("NOT_FOUND"));
    QJsonObject none; none.insert(QStringLiteral("functionIds"), QJsonArray());
    QCOMPARE(errorCode(structural(QStringLiteral("vc.clock.schedule.add"), clock, none, QStringLiteral("t-s2"))), QStringLiteral("INVALID_PARAMS"));

    QJsonObject add;
    add.insert(QStringLiteral("functionIds"), QJsonArray() << QString::number(s1->id()) << QString::number(s2->id()));
    QVERIFY(isOk(structural(QStringLiteral("vc.clock.schedule.add"), clock, add, QStringLiteral("t-s3"))));
    QJsonArray schedules = typeConfigOf(clock).value(QStringLiteral("schedules")).toArray();
    QCOMPARE(schedules.size(), 2);
    QCOMPARE(schedules.at(1).toObject().value(QStringLiteral("functionID")).toString(), QString::number(s2->id()));
    QCOMPARE(schedules.at(1).toObject().value(QStringLiteral("stopTime")).toInt(), -1);
    QList<QJsonObject> events = eventsWithTopic(spy, QStringLiteral("vc.clock.schedulesChanged"));
    QCOMPARE(events.size(), 1);
    QCOMPARE(events.at(0).value(QStringLiteral("schedules")).toArray().size(), 2);
    QCOMPARE(eventsWithTopic(spy, QStringLiteral("vc.widget.configChanged")).size(), 1);

    // Update the second one: 08:30 start, 18:00 stop, Mon+Fri, repeat
    QJsonObject upd;
    upd.insert(QStringLiteral("index"), 1);
    upd.insert(QStringLiteral("startTime"), 8 * 3600 + 30 * 60);
    upd.insert(QStringLiteral("stopTime"), 18 * 3600);
    upd.insert(QStringLiteral("weekFlags"), 0x01 | 0x10 | 0x80);
    QVERIFY(isOk(structural(QStringLiteral("vc.clock.schedule.update"), clock, upd, QStringLiteral("t-s4"))));
    QJsonObject second = typeConfigOf(clock).value(QStringLiteral("schedules")).toArray().at(1).toObject();
    QCOMPARE(second.value(QStringLiteral("startTime")).toInt(), 30600);
    QCOMPARE(second.value(QStringLiteral("stopTime")).toInt(), 64800);
    QCOMPARE(second.value(QStringLiteral("weekFlags")).toInt(), 0x91);

    // Validation: bad index, out-of-range values, empty patch
    upd.insert(QStringLiteral("index"), 5);
    QCOMPARE(errorCode(structural(QStringLiteral("vc.clock.schedule.update"), clock, upd, QStringLiteral("t-s5"))), QStringLiteral("NOT_FOUND"));
    upd.insert(QStringLiteral("index"), 1); upd.insert(QStringLiteral("weekFlags"), 256);
    QCOMPARE(errorCode(structural(QStringLiteral("vc.clock.schedule.update"), clock, upd, QStringLiteral("t-s6"))), QStringLiteral("INVALID_PARAMS"));
    upd.remove(QStringLiteral("weekFlags")); upd.insert(QStringLiteral("startTime"), 90000);
    QCOMPARE(errorCode(structural(QStringLiteral("vc.clock.schedule.update"), clock, upd, QStringLiteral("t-s7"))), QStringLiteral("INVALID_PARAMS"));
    QJsonObject emptyPatch; emptyPatch.insert(QStringLiteral("index"), 1);
    QCOMPARE(errorCode(structural(QStringLiteral("vc.clock.schedule.update"), clock, emptyPatch, QStringLiteral("t-s8"))), QStringLiteral("INVALID_PARAMS"));

    // Remove the first: the second becomes index 0
    QJsonObject rm; rm.insert(QStringLiteral("index"), 0);
    QVERIFY(isOk(structural(QStringLiteral("vc.clock.schedule.remove"), clock, rm, QStringLiteral("t-s9"))));
    schedules = typeConfigOf(clock).value(QStringLiteral("schedules")).toArray();
    QCOMPARE(schedules.size(), 1);
    QCOMPARE(schedules.at(0).toObject().value(QStringLiteral("index")).toInt(), 0);
    QCOMPARE(schedules.at(0).toObject().value(QStringLiteral("functionID")).toString(), QString::number(s2->id()));
    rm.insert(QStringLiteral("index"), 1);
    QCOMPARE(errorCode(structural(QStringLiteral("vc.clock.schedule.remove"), clock, rm, QStringLiteral("t-s10"))), QStringLiteral("NOT_FOUND"));
}

/*****************************************************************************
 * Animation
 *****************************************************************************/

void ApiVcLiveDomain_Test::animationFaderLevelAndKnob()
{
    QString clientId = helloAndGetClientId();
    QString anim = createWidget(QStringLiteral("Animation"));
    QJsonObject p; p.insert(QStringLiteral("widgetId"), anim); p.insert(QStringLiteral("level"), 128);

    // No matrix attached: the engine would drop the level silently - the API refuses
    QCOMPARE(errorCode(sendAndWaitForReply(QStringLiteral("vc.animation.setFaderLevel"), p, QStringLiteral("t-n1"))), QStringLiteral("INVALID_STATE"));

    QJsonObject cfg; cfg.insert(QStringLiteral("functionID"), QStringLiteral("12"));
    QJsonObject sc; sc.insert(QStringLiteral("config"), cfg);
    QVERIFY(isOk(structural(QStringLiteral("vc.widget.setConfig"), anim, sc, QStringLiteral("t-n2"))));

    p.insert(QStringLiteral("level"), 300);
    QCOMPARE(errorCode(sendAndWaitForReply(QStringLiteral("vc.animation.setFaderLevel"), p, QStringLiteral("t-n3"))), QStringLiteral("INVALID_PARAMS"));

    QSignalSpy spy(m_client, &QWebSocket::textMessageReceived);
    p.insert(QStringLiteral("level"), 128);
    QVERIFY(isOk(sendAndWaitForReply(QStringLiteral("vc.animation.setFaderLevel"), p, QStringLiteral("t-n4"))));
    QList<QJsonObject> events = eventsWithTopic(spy, QStringLiteral("vc.animation.faderLevelChanged"));
    QCOMPARE(events.size(), 1);
    QCOMPARE(events.at(0).value(QStringLiteral("level")).toInt(), 128);
    QCOMPARE(events.at(0).value(QStringLiteral("_origin")).toString(), clientId);
    QJsonObject get = resultOf(sendAndWaitForReply(QStringLiteral("vc.widget.get"), p, QStringLiteral("t-n5")));
    QCOMPARE(get.value(QStringLiteral("faderLevel")).toInt(), 128);

    // Knobs: a colorKnobs preset accepts a knob value, a colour preset does not
    QJsonObject knobs; knobs.insert(QStringLiteral("presetType"), QStringLiteral("colorKnobs")); knobs.insert(QStringLiteral("colorIndex"), 0);
    QJsonObject padd; padd.insert(QStringLiteral("preset"), knobs);
    QJsonObject reply = structural(QStringLiteral("vc.widget.preset.add"), anim, padd, QStringLiteral("t-n6"));
    QVERIFY(isOk(reply));
    int knobId = resultOf(reply).value(QStringLiteral("presetId")).toInt();
    QJsonObject color; color.insert(QStringLiteral("presetType"), QStringLiteral("color")); color.insert(QStringLiteral("colorIndex"), 1); color.insert(QStringLiteral("color"), QStringLiteral("#ff0000"));
    padd.insert(QStringLiteral("preset"), color);
    reply = structural(QStringLiteral("vc.widget.preset.add"), anim, padd, QStringLiteral("t-n7"));
    QVERIFY(isOk(reply));
    int colorId = resultOf(reply).value(QStringLiteral("presetId")).toInt();

    QJsonObject kv; kv.insert(QStringLiteral("widgetId"), anim); kv.insert(QStringLiteral("presetId"), knobId); kv.insert(QStringLiteral("value"), 200);
    QVERIFY(isOk(sendAndWaitForReply(QStringLiteral("vc.animation.setPresetKnobValue"), kv, QStringLiteral("t-n8"))));
    events = eventsWithTopic(spy, QStringLiteral("vc.animation.activePresetChanged"));
    QCOMPARE(events.size(), 1);
    QCOMPARE(events.at(0).value(QStringLiteral("knobPresetId")).toInt(), knobId);
    QCOMPARE(events.at(0).value(QStringLiteral("knobValue")).toInt(), 200);
    kv.insert(QStringLiteral("presetId"), colorId);
    QCOMPARE(errorCode(sendAndWaitForReply(QStringLiteral("vc.animation.setPresetKnobValue"), kv, QStringLiteral("t-n9"))), QStringLiteral("INVALID_PARAMS"));
    kv.insert(QStringLiteral("presetId"), 250);
    QCOMPARE(errorCode(sendAndWaitForReply(QStringLiteral("vc.animation.setPresetKnobValue"), kv, QStringLiteral("t-n10"))), QStringLiteral("NOT_FOUND"));
    kv.insert(QStringLiteral("presetId"), knobId); kv.insert(QStringLiteral("value"), -1);
    QCOMPARE(errorCode(sendAndWaitForReply(QStringLiteral("vc.animation.setPresetKnobValue"), kv, QStringLiteral("t-n11"))), QStringLiteral("INVALID_PARAMS"));
}

void ApiVcLiveDomain_Test::animationPresetMove()
{
    helloAndGetClientId();
    QString anim = createWidget(QStringLiteral("Animation"));
    int ids[2] = { -1, -1 };
    for (int i = 0; i < 2; i++)
    {
        QJsonObject text; text.insert(QStringLiteral("presetType"), QStringLiteral("text")); text.insert(QStringLiteral("text"), QStringLiteral("T%1").arg(i));
        QJsonObject padd; padd.insert(QStringLiteral("preset"), text);
        QJsonObject reply = structural(QStringLiteral("vc.widget.preset.add"), anim, padd, QStringLiteral("t-m%1").arg(i));
        QVERIFY(isOk(reply));
        ids[i] = resultOf(reply).value(QStringLiteral("presetId")).toInt();
    }

    QSignalSpy spy(m_client, &QWebSocket::textMessageReceived);
    QJsonObject move; move.insert(QStringLiteral("presetId"), ids[1]); move.insert(QStringLiteral("direction"), QStringLiteral("up"));
    QJsonObject reply = structural(QStringLiteral("vc.animation.preset.move"), anim, move, QStringLiteral("t-m3"));
    QVERIFY(isOk(reply));
    QCOMPARE(resultOf(reply).value(QStringLiteral("presetId")).toInt(), ids[0]);
    QJsonArray presets = typeConfigOf(anim).value(QStringLiteral("presets")).toArray();
    QCOMPARE(presets.at(0).toObject().value(QStringLiteral("text")).toString(), QStringLiteral("T1"));
    QList<QJsonObject> events = eventsWithTopic(spy, QStringLiteral("vc.animation.presetsChanged"));
    QCOMPARE(events.size(), 1);
    QCOMPARE(events.at(0).value(QStringLiteral("presets")).toArray().size(), 2);
    QCOMPARE(eventsWithTopic(spy, QStringLiteral("vc.widget.configChanged")).size(), 1);

    // Down past the end keeps the id; an XYPad-only method on an Animation is INVALID_PARAMS
    move.insert(QStringLiteral("presetId"), ids[1]); move.insert(QStringLiteral("direction"), QStringLiteral("down"));
    reply = structural(QStringLiteral("vc.animation.preset.move"), anim, move, QStringLiteral("t-m4"));
    QVERIFY(isOk(reply));
    QCOMPARE(resultOf(reply).value(QStringLiteral("presetId")).toInt(), ids[1]);
    QJsonObject rename; rename.insert(QStringLiteral("presetId"), ids[1]); rename.insert(QStringLiteral("name"), QStringLiteral("x"));
    QCOMPARE(errorCode(structural(QStringLiteral("vc.xyPad.preset.rename"), anim, rename, QStringLiteral("t-m5"))), QStringLiteral("INVALID_PARAMS"));
}

/*****************************************************************************
 * Audio triggers
 *****************************************************************************/

void ApiVcLiveDomain_Test::audioTriggersCaptureAndLevels()
{
    QString clientId = helloAndGetClientId();
    QString at = createWidget(QStringLiteral("AudioTriggers"));
    QJsonObject p; p.insert(QStringLiteral("widgetId"), at);

    QSignalSpy spy(m_client, &QWebSocket::textMessageReceived);
    p.insert(QStringLiteral("enabled"), QStringLiteral("yes"));
    QCOMPARE(errorCode(sendAndWaitForReply(QStringLiteral("vc.audioTriggers.setCaptureEnabled"), p, QStringLiteral("t-t1"))), QStringLiteral("INVALID_PARAMS"));
    p.insert(QStringLiteral("enabled"), true);
    QVERIFY(isOk(sendAndWaitForReply(QStringLiteral("vc.audioTriggers.setCaptureEnabled"), p, QStringLiteral("t-t2"))));
    QList<QJsonObject> events = eventsWithTopic(spy, QStringLiteral("vc.audioTriggers.captureEnabledChanged"));
    QCOMPARE(events.size(), 1);
    QCOMPARE(events.at(0).value(QStringLiteral("enabled")).toBool(), true);
    QCOMPARE(events.at(0).value(QStringLiteral("_origin")).toString(), clientId);
    QCOMPARE(resultOf(sendAndWaitForReply(QStringLiteral("vc.widget.get"), p, QStringLiteral("t-t3"))).value(QStringLiteral("captureEnabled")).toBool(), true);
    // Same value again: no event
    QVERIFY(isOk(sendAndWaitForReply(QStringLiteral("vc.audioTriggers.setCaptureEnabled"), p, QStringLiteral("t-t4"))));
    QCOMPARE(eventsWithTopic(spy, QStringLiteral("vc.audioTriggers.captureEnabledChanged")).size(), 1);

    // Levels are subscribe-gated
    m_vcHost->simulateAudioLevels(at.toUInt(), QList<int>() << 200 << 10 << 20 << 30);
    QTest::qWait(50);
    QCOMPARE(eventsWithTopic(spy, QStringLiteral("vc.audioTriggers.levelsChanged")).size(), 0);
    QJsonObject sub; sub.insert(QStringLiteral("topics"), QJsonArray() << QStringLiteral("vc.audioTriggers.levelsChanged"));
    QVERIFY(isOk(sendAndWaitForReply(QStringLiteral("subscribe"), sub, QStringLiteral("t-t5"))));
    m_vcHost->simulateAudioLevels(at.toUInt(), QList<int>() << 200 << 10 << 20 << 30);
    QVERIFY(QTest::qWaitFor([&]() { return eventsWithTopic(spy, QStringLiteral("vc.audioTriggers.levelsChanged")).size() == 1; }, 2000));
    events = eventsWithTopic(spy, QStringLiteral("vc.audioTriggers.levelsChanged"));
    QCOMPARE(events.at(0).value(QStringLiteral("levels")).toArray().size(), 4);
    QCOMPARE(events.at(0).value(QStringLiteral("levels")).toArray().at(0).toInt(), 200);
    QVERIFY(events.at(0).value(QStringLiteral("_origin")).isNull());
}

void ApiVcLiveDomain_Test::audioTriggersSetBarConfig()
{
    helloAndGetClientId();
    QList<quint32> fx = addFixtures(1);
    Scene *scene = new Scene(m_doc); scene->setName(QStringLiteral("Kick")); QVERIFY(m_doc->addFunction(scene));
    QString at = createWidget(QStringLiteral("AudioTriggers"));
    QString button = createWidget(QStringLiteral("Button"));

    QSignalSpy spy(m_client, &QWebSocket::textMessageReceived);

    // Index / type / cross-type validation
    QJsonObject bad; bad.insert(QStringLiteral("index"), 9); bad.insert(QStringLiteral("type"), QStringLiteral("DMXBar"));
    QCOMPARE(errorCode(structural(QStringLiteral("vc.audioTriggers.setBarConfig"), at, bad, QStringLiteral("t-b1"))), QStringLiteral("NOT_FOUND"));
    bad.insert(QStringLiteral("index"), 1); bad.insert(QStringLiteral("type"), QStringLiteral("Laser"));
    QCOMPARE(errorCode(structural(QStringLiteral("vc.audioTriggers.setBarConfig"), at, bad, QStringLiteral("t-b2"))), QStringLiteral("INVALID_PARAMS"));
    QJsonObject fnOnNone; fnOnNone.insert(QStringLiteral("index"), 1); fnOnNone.insert(QStringLiteral("functionId"), QString::number(scene->id()));
    QCOMPARE(errorCode(structural(QStringLiteral("vc.audioTriggers.setBarConfig"), at, fnOnNone, QStringLiteral("t-b3"))), QStringLiteral("INVALID_PARAMS"));
    QJsonObject nothing; nothing.insert(QStringLiteral("index"), 1);
    QCOMPARE(errorCode(structural(QStringLiteral("vc.audioTriggers.setBarConfig"), at, nothing, QStringLiteral("t-b4"))), QStringLiteral("INVALID_PARAMS"));

    // Function bar with thresholds in one call
    QJsonObject fn;
    fn.insert(QStringLiteral("index"), 1); fn.insert(QStringLiteral("type"), QStringLiteral("FunctionBar"));
    fn.insert(QStringLiteral("functionId"), QString::number(scene->id()));
    fn.insert(QStringLiteral("minThreshold"), 40); fn.insert(QStringLiteral("maxThreshold"), 220);
    QVERIFY(isOk(structural(QStringLiteral("vc.audioTriggers.setBarConfig"), at, fn, QStringLiteral("t-b5"))));
    QJsonArray bars = typeConfigOf(at).value(QStringLiteral("bars")).toArray();
    QCOMPARE(bars.at(1).toObject().value(QStringLiteral("type")).toString(), QStringLiteral("FunctionBar"));
    QCOMPARE(bars.at(1).toObject().value(QStringLiteral("functionId")).toString(), QString::number(scene->id()));
    QCOMPARE(bars.at(1).toObject().value(QStringLiteral("maxThreshold")).toInt(), 220);
    QList<QJsonObject> events = eventsWithTopic(spy, QStringLiteral("vc.audioTriggers.barsChanged"));
    QCOMPARE(events.size(), 1);
    QCOMPARE(events.at(0).value(QStringLiteral("bars")).toArray().size(), 4);
    QCOMPARE(eventsWithTopic(spy, QStringLiteral("vc.widget.configChanged")).size(), 1);
    fn.insert(QStringLiteral("functionId"), QStringLiteral("4242"));
    QCOMPARE(errorCode(structural(QStringLiteral("vc.audioTriggers.setBarConfig"), at, fn, QStringLiteral("t-b6"))), QStringLiteral("NOT_FOUND"));

    // DMX bar with two channels of the fixture; a channel outside it is refused
    QJsonObject ch0, ch1;
    ch0.insert(QStringLiteral("fixtureId"), QString::number(fx.at(0))); ch0.insert(QStringLiteral("channel"), 0);
    ch1.insert(QStringLiteral("fixtureId"), QString::number(fx.at(0))); ch1.insert(QStringLiteral("channel"), 3);
    QJsonObject dmx;
    dmx.insert(QStringLiteral("index"), 2); dmx.insert(QStringLiteral("type"), QStringLiteral("DMXBar"));
    dmx.insert(QStringLiteral("dmxChannels"), QJsonArray() << ch0 << ch1);
    QVERIFY(isOk(structural(QStringLiteral("vc.audioTriggers.setBarConfig"), at, dmx, QStringLiteral("t-b7"))));
    bars = typeConfigOf(at).value(QStringLiteral("bars")).toArray();
    QCOMPARE(bars.at(2).toObject().value(QStringLiteral("dmxChannels")).toArray().size(), 2);
    ch1.insert(QStringLiteral("channel"), 4);
    dmx.insert(QStringLiteral("dmxChannels"), QJsonArray() << ch1);
    QCOMPARE(errorCode(structural(QStringLiteral("vc.audioTriggers.setBarConfig"), at, dmx, QStringLiteral("t-b8"))), QStringLiteral("INVALID_PARAMS"));

    // Widget bar: another widget is fine, itself is not
    QJsonObject wb;
    wb.insert(QStringLiteral("index"), 3); wb.insert(QStringLiteral("type"), QStringLiteral("VCWidgetBar"));
    wb.insert(QStringLiteral("triggeredWidgetId"), at);
    QCOMPARE(errorCode(structural(QStringLiteral("vc.audioTriggers.setBarConfig"), at, wb, QStringLiteral("t-b9"))), QStringLiteral("INVALID_PARAMS"));
    wb.insert(QStringLiteral("triggeredWidgetId"), button);
    QVERIFY(isOk(structural(QStringLiteral("vc.audioTriggers.setBarConfig"), at, wb, QStringLiteral("t-b10"))));
    QCOMPARE(typeConfigOf(at).value(QStringLiteral("bars")).toArray().at(3).toObject().value(QStringLiteral("triggeredWidgetId")).toString(), button);

    // Back to None resets the bar
    QJsonObject off; off.insert(QStringLiteral("index"), 1); off.insert(QStringLiteral("type"), QStringLiteral("None"));
    QVERIFY(isOk(structural(QStringLiteral("vc.audioTriggers.setBarConfig"), at, off, QStringLiteral("t-b11"))));
    QCOMPARE(typeConfigOf(at).value(QStringLiteral("bars")).toArray().at(1).toObject().contains(QStringLiteral("functionId")), false);
}

/*****************************************************************************
 * Cross-cutting
 *****************************************************************************/

void ApiVcLiveDomain_Test::wrongWidgetTypeIsInvalidParams()
{
    helloAndGetClientId();
    QString button = createWidget(QStringLiteral("Button"));
    QJsonObject p; p.insert(QStringLiteral("widgetId"), button); p.insert(QStringLiteral("level"), 1);
    QJsonObject reply = sendAndWaitForReply(QStringLiteral("vc.animation.setFaderLevel"), p, QStringLiteral("t-w1"));
    QCOMPARE(errorCode(reply), QStringLiteral("INVALID_PARAMS"));
    QCOMPARE(reply.value(QStringLiteral("error")).toObject().value(QStringLiteral("details")).toObject().value(QStringLiteral("widgetType")).toString(), QStringLiteral("Button"));
    QCOMPARE(errorCode(sendAndWaitForReply(QStringLiteral("vc.clock.playPause"), p, QStringLiteral("t-w2"))), QStringLiteral("INVALID_PARAMS"));
    p.insert(QStringLiteral("widgetId"), QStringLiteral("999"));
    QCOMPARE(errorCode(sendAndWaitForReply(QStringLiteral("vc.clock.playPause"), p, QStringLiteral("t-w3"))), QStringLiteral("NOT_FOUND"));
}

void ApiVcLiveDomain_Test::structuralMethodsConflictOnStaleRevision()
{
    helloAndGetClientId();
    QList<quint32> fx = addFixtures(1);
    QString pad = createWidget(QStringLiteral("XYPad"));
    QString clock = createWidget(QStringLiteral("Clock"));

    struct Case { const char *method; QString widget; QJsonObject extra; };
    QJsonObject add; add.insert(QStringLiteral("fixtureId"), QString::number(fx.at(0)));
    QJsonObject rm; rm.insert(QStringLiteral("index"), 0);
    QJsonObject bar; bar.insert(QStringLiteral("index"), 0); bar.insert(QStringLiteral("type"), QStringLiteral("None"));
    const Case cases[] = {
        { "vc.xyPad.fixture.add", pad, add },
        { "vc.clock.schedule.remove", clock, rm },
        { "vc.audioTriggers.setBarConfig", pad, bar }
    };
    for (const Case &c : cases)
    {
        QJsonObject p = c.extra;
        p.insert(QStringLiteral("widgetId"), c.widget);
        p.insert(QStringLiteral("baseRevision"), currentDocRevision() + 100);
        QJsonObject reply = sendAndWaitForReply(QString::fromLatin1(c.method), p, QStringLiteral("t-x"));
        QCOMPARE(errorCode(reply), QStringLiteral("CONFLICT"));
        QCOMPARE(reply.value(QStringLiteral("error")).toObject().value(QStringLiteral("details")).toObject().value(QStringLiteral("docRevision")).toInt(), int(m_doc->docRevision()));
    }
}

QTEST_GUILESS_MAIN(ApiVcLiveDomain_Test)
