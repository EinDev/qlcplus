/*
  Q Light Controller Plus - Control API unit test
  apifixtureremapdomain_test.cpp

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

#include "apifixtureremapdomain_test.h"
#include "apiserver.h"
#include "monitorproperties.h"
#include "qlcfixturedef.h"
#include "qlcfixturemode.h"
#include "qlcchannel.h"
#include "fixturegroup.h"
#include "grouphead.h"
#include "qlcpoint.h"
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

static QJsonObject genericDef(int channels)
{
    QJsonObject g;
    g.insert(QStringLiteral("channels"), channels);
    QJsonObject d;
    d.insert(QStringLiteral("generic"), g);
    return d;
}

void ApiFixtureRemapDomain_Test::init()
{
    m_doc = new Doc(nullptr);
    m_apiServer = new ApiServer(nullptr, m_doc);
    QVERIFY(m_apiServer->listen(0, QHostAddress::LocalHost));

    m_client = new QWebSocket();
    m_client->open(QUrl(QStringLiteral("ws://127.0.0.1:%1/qlcplusapi").arg(m_apiServer->serverPort())));
    QVERIFY(QTest::qWaitFor([this]() { return m_client->state() == QAbstractSocket::ConnectedState; }, 2000));

    m_nextAddress = 0;
    m_movingHeadDef = nullptr;
}

void ApiFixtureRemapDomain_Test::cleanup()
{
    delete m_client;
    m_client = nullptr;
    delete m_apiServer;
    m_apiServer = nullptr;
    delete m_doc; // owns the def cache, which owns m_movingHeadDef once added
    m_doc = nullptr;
    m_movingHeadDef = nullptr;
}

QJsonObject ApiFixtureRemapDomain_Test::sendAndWaitForReply(const QString &method, const QJsonObject &params)
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

QString ApiFixtureRemapDomain_Test::helloAndGetClientId()
{
    QJsonObject reply = sendAndWaitForReply(QStringLiteral("hello"), QJsonObject());
    return reply.value(QStringLiteral("result")).toObject().value(QStringLiteral("clientId")).toString();
}

quint32 ApiFixtureRemapDomain_Test::addGenericFixture(int channels)
{
    Fixture *fxi = new Fixture(m_doc);
    fxi->setChannels(quint32(channels));
    fxi->setAddress(m_nextAddress);
    fxi->setName(QStringLiteral("Dimmer %1").arg(m_nextAddress));
    m_nextAddress += quint32(channels);
    bool added = m_doc->addFixture(fxi);
    Q_ASSERT(added);
    return fxi->id();
}

quint32 ApiFixtureRemapDomain_Test::addMovingHead()
{
    if (m_movingHeadDef == nullptr)
    {
        m_movingHeadDef = new QLCFixtureDef();
        m_movingHeadDef->setManufacturer(QStringLiteral("Test"));
        m_movingHeadDef->setModel(QStringLiteral("Mover"));
        m_movingHeadDef->setType(QLCFixtureDef::MovingHead);
        QLCChannel *pan = new QLCChannel(); pan->setName(QStringLiteral("Pan")); pan->setGroup(QLCChannel::Pan);
        QLCChannel *tilt = new QLCChannel(); tilt->setName(QStringLiteral("Tilt")); tilt->setGroup(QLCChannel::Tilt);
        QLCChannel *dim = new QLCChannel(); dim->setName(QStringLiteral("Dimmer")); dim->setGroup(QLCChannel::Intensity);
        QLCChannel *red = new QLCChannel(); red->setName(QStringLiteral("Red")); red->setGroup(QLCChannel::Intensity); red->setColour(QLCChannel::Red);
        for (QLCChannel *c : { pan, tilt, dim, red })
            m_movingHeadDef->addChannel(c);
        QLCFixtureMode *mode = new QLCFixtureMode(m_movingHeadDef);
        mode->setName(QStringLiteral("4ch"));
        mode->insertChannel(pan, 0);
        mode->insertChannel(tilt, 1);
        mode->insertChannel(dim, 2);
        mode->insertChannel(red, 3);
        m_movingHeadDef->addMode(mode);
        // A second mode with the channels shuffled, to test semantic matching
        QLCFixtureMode *mode2 = new QLCFixtureMode(m_movingHeadDef);
        mode2->setName(QStringLiteral("4ch shuffled"));
        mode2->insertChannel(red, 0);
        mode2->insertChannel(dim, 1);
        mode2->insertChannel(tilt, 2);
        mode2->insertChannel(pan, 3);
        m_movingHeadDef->addMode(mode2);
        m_doc->fixtureDefCache()->addFixtureDef(m_movingHeadDef);
    }
    Fixture *fxi = new Fixture(m_doc);
    fxi->setFixtureDefinition(m_movingHeadDef, m_movingHeadDef->modes().first());
    fxi->setAddress(m_nextAddress);
    fxi->setName(QStringLiteral("Mover"));
    m_nextAddress += 4;
    bool added = m_doc->addFixture(fxi);
    Q_ASSERT(added);
    return fxi->id();
}

static QJsonArray identityChannelMap(int channels)
{
    QJsonArray arr;
    for (int i = 0; i < channels; i++)
    {
        QJsonObject e;
        e.insert(QStringLiteral("sourceChannel"), i);
        e.insert(QStringLiteral("targetChannel"), i);
        arr.append(e);
    }
    return arr;
}

void ApiFixtureRemapDomain_Test::suggestChannelMapGenericIsOneToOne()
{
    helloAndGetClientId();
    quint32 fid = addGenericFixture(4);
    QJsonObject params;
    params.insert(QStringLiteral("sourceFixtureId"), QString::number(fid));
    params.insert(QStringLiteral("target"), genericDef(2));
    QJsonObject reply = sendAndWaitForReply(QStringLiteral("fixtures.remap.suggestChannelMap"), params);
    QCOMPARE(reply.value(QStringLiteral("ok")).toBool(), true);
    QJsonArray map = reply.value(QStringLiteral("result")).toObject().value(QStringLiteral("channelMap")).toArray();
    QCOMPARE(map.count(), 2); // only the channels the target has
    QCOMPARE(map.at(1).toObject().value(QStringLiteral("sourceChannel")).toInt(), 1);
    QCOMPARE(map.at(1).toObject().value(QStringLiteral("targetChannel")).toInt(), 1);

    params.insert(QStringLiteral("sourceFixtureId"), QStringLiteral("99"));
    reply = sendAndWaitForReply(QStringLiteral("fixtures.remap.suggestChannelMap"), params);
    QCOMPARE(reply.value(QStringLiteral("error")).toObject().value(QStringLiteral("code")).toString(), QStringLiteral("NOT_FOUND"));
}

void ApiFixtureRemapDomain_Test::suggestChannelMapMatchesByGroup()
{
    helloAndGetClientId();
    quint32 mover = addMovingHead();
    QJsonObject target;
    target.insert(QStringLiteral("manufacturer"), QStringLiteral("Test"));
    target.insert(QStringLiteral("model"), QStringLiteral("Mover"));
    target.insert(QStringLiteral("mode"), QStringLiteral("4ch shuffled"));
    QJsonObject params;
    params.insert(QStringLiteral("sourceFixtureId"), QString::number(mover));
    params.insert(QStringLiteral("target"), target);
    QJsonObject reply = sendAndWaitForReply(QStringLiteral("fixtures.remap.suggestChannelMap"), params);
    QCOMPARE(reply.value(QStringLiteral("ok")).toBool(), true);
    QJsonArray map = reply.value(QStringLiteral("result")).toObject().value(QStringLiteral("channelMap")).toArray();
    QCOMPARE(map.count(), 4);
    QMap<int, int> m;
    for (const QJsonValue &v : map)
        m.insert(v.toObject().value(QStringLiteral("sourceChannel")).toInt(), v.toObject().value(QStringLiteral("targetChannel")).toInt());
    QCOMPARE(m.value(0), 3); // Pan -> Pan
    QCOMPARE(m.value(1), 2); // Tilt -> Tilt
    QCOMPARE(m.value(2), 1); // Dimmer (no colour) -> Dimmer
    QCOMPARE(m.value(3), 0); // Red -> Red
}

void ApiFixtureRemapDomain_Test::applyMovesSceneGroupAndMonitorToNewFixture()
{
    QString clientId = helloAndGetClientId();
    quint32 a = addGenericFixture(4); // address 0..3
    quint32 b = addGenericFixture(2); // address 4..5

    Scene *scene = new Scene(m_doc);
    scene->setName(QStringLiteral("Look"));
    for (quint32 ch = 0; ch < 4; ch++)
        scene->setValue(a, ch, uchar(10 * (ch + 1)));
    scene->setValue(b, 0, 200);
    QVERIFY(m_doc->addFunction(scene));
    quint32 sceneId = scene->id();

    FixtureGroup *grp = new FixtureGroup(m_doc);
    grp->setName(QStringLiteral("Grp"));
    grp->setSize(QSize(4, 1));
    grp->assignFixture(a);
    m_doc->addFixtureGroup(grp);
    quint32 grpId = grp->id();

    m_doc->monitorProperties()->setFixturePosition(a, 0, 0, QVector3D(1234, 0, 5678));
    m_doc->monitorProperties()->setFixturePosition(b, 0, 0, QVector3D(1, 0, 2));

    QSignalSpy spy(m_client, &QWebSocket::textMessageReceived);
    QJsonObject mapping;
    mapping.insert(QStringLiteral("sourceFixtureId"), QString::number(a));
    mapping.insert(QStringLiteral("universe"), 0);
    mapping.insert(QStringLiteral("address"), 100);
    mapping.insert(QStringLiteral("definition"), genericDef(6));
    mapping.insert(QStringLiteral("name"), QStringLiteral("Bigger"));
    mapping.insert(QStringLiteral("channelMap"), identityChannelMap(4));
    QJsonObject params;
    params.insert(QStringLiteral("mappings"), QJsonArray{ mapping });
    params.insert(QStringLiteral("baseRevision"), int(m_doc->docRevision()));
    int before = int(m_doc->docRevision());
    QJsonObject reply = sendAndWaitForReply(QStringLiteral("fixtures.remap.apply"), params);
    QCOMPARE(reply.value(QStringLiteral("ok")).toBool(), true);
    QVERIFY(reply.value(QStringLiteral("result")).toObject().value(QStringLiteral("docRevision")).toInt() > before);
    QJsonArray newIds = reply.value(QStringLiteral("result")).toObject().value(QStringLiteral("fixtureIds")).toArray();
    QCOMPARE(newIds.count(), 1);
    quint32 newId = newIds.at(0).toString().toUInt();
    QVERIFY(newId != a);

    // Old fixture gone, new one patched at 100 with 6 channels, b untouched
    QVERIFY(m_doc->fixture(a) == nullptr);
    Fixture *created = m_doc->fixture(newId);
    QVERIFY(created != nullptr);
    QCOMPARE(created->address(), quint32(100));
    QCOMPARE(created->channels(), quint32(6));
    QCOMPARE(created->name(), QStringLiteral("Bigger"));
    QVERIFY(m_doc->fixture(b) != nullptr);
    QCOMPARE(m_doc->fixture(b)->address(), quint32(4));
    QCOMPARE(m_doc->fixture(b)->channels(), quint32(2));
    QCOMPARE(m_doc->fixtures().count(), 2);

    // Scene values followed the remap; b's value survived (identity map)
    Scene *s = qobject_cast<Scene *>(m_doc->function(sceneId));
    QVERIFY(s != nullptr);
    QCOMPARE(s->value(newId, 2), uchar(30));
    QCOMPARE(s->value(newId, 3), uchar(40));
    QCOMPARE(s->value(b, 0), uchar(200));
    QCOMPARE(s->values().count(), 5);
    QVERIFY(s->fixtures().contains(newId));
    QVERIFY(s->fixtures().contains(a) == false);

    // Group heads point at the new id
    FixtureGroup *g = m_doc->fixtureGroup(grpId);
    QVERIFY(g != nullptr);
    QVERIFY(g->fixtureList().contains(newId));
    QVERIFY(g->fixtureList().contains(a) == false);

    // Monitor placement carried over
    MonitorProperties *mp = m_doc->monitorProperties();
    QCOMPARE(mp->fixturePosition(newId, 0, 0), QVector3D(1234, 0, 5678));
    QCOMPARE(mp->fixturePosition(b, 0, 0), QVector3D(1, 0, 2));
    QVERIFY(mp->containsFixture(a) == false);

    bool sawApplied = false, sawMonitor = false;
    QVERIFY(QTest::qWaitFor([&]()
    {
        for (const QList<QVariant> &frame : spy)
        {
            QJsonObject obj = QJsonDocument::fromJson(frame.at(0).toString().toUtf8()).object();
            if (obj.value(QStringLiteral("type")).toString() != QStringLiteral("event"))
                continue;
            QString topic = obj.value(QStringLiteral("topic")).toString();
            if (topic == QStringLiteral("fixtures.remap.applied"))
            {
                QJsonObject data = obj.value(QStringLiteral("data")).toObject();
                sawApplied = data.value(QStringLiteral("replacedFixtureIds")).toArray().at(0).toString() == QString::number(a) &&
                             data.value(QStringLiteral("fixtures")).toArray().count() == 1 &&
                             obj.value(QStringLiteral("originClientId")).toString() == clientId;
            }
            if (topic == QStringLiteral("fixtures.monitor.changed"))
                sawMonitor = true;
        }
        return sawApplied && sawMonitor;
    }, 2000));
}

void ApiFixtureRemapDomain_Test::applyDeletesUnmappedSources()
{
    helloAndGetClientId();
    quint32 a = addGenericFixture(1);
    quint32 b = addGenericFixture(1);
    quint32 c = addGenericFixture(1);

    QJsonObject mapping;
    mapping.insert(QStringLiteral("sourceFixtureId"), QString::number(a));
    mapping.insert(QStringLiteral("universe"), 1);
    mapping.insert(QStringLiteral("address"), 0);
    mapping.insert(QStringLiteral("definition"), genericDef(1));
    mapping.insert(QStringLiteral("channelMap"), identityChannelMap(1));
    QJsonObject params;
    params.insert(QStringLiteral("mappings"), QJsonArray{ mapping });
    params.insert(QStringLiteral("unmappedSourceFixtureIds"), QJsonArray{ QString::number(b) });
    params.insert(QStringLiteral("baseRevision"), int(m_doc->docRevision()));
    QJsonObject reply = sendAndWaitForReply(QStringLiteral("fixtures.remap.apply"), params);
    QCOMPARE(reply.value(QStringLiteral("ok")).toBool(), true);
    QCOMPARE(m_doc->fixtures().count(), 2);
    QVERIFY(m_doc->fixture(b) == nullptr);
    QVERIFY(m_doc->fixture(c) != nullptr);
    QVERIFY(m_doc->fixture(a) == nullptr);
    bool sawNew = false;
    for (Fixture *f : m_doc->fixtures())
        if (f->universe() == 1 && f->address() == 0)
            sawNew = true;
    QVERIFY(sawNew);
}

void ApiFixtureRemapDomain_Test::applyRefusesOverlapAndStaleRevision()
{
    helloAndGetClientId();
    quint32 a = addGenericFixture(2); // 0..1
    quint32 b = addGenericFixture(2); // 2..3

    QJsonObject mapping;
    mapping.insert(QStringLiteral("sourceFixtureId"), QString::number(a));
    mapping.insert(QStringLiteral("universe"), 0);
    mapping.insert(QStringLiteral("address"), 3); // collides with b's second channel
    mapping.insert(QStringLiteral("definition"), genericDef(2));
    mapping.insert(QStringLiteral("channelMap"), identityChannelMap(2));
    QJsonObject params;
    params.insert(QStringLiteral("mappings"), QJsonArray{ mapping });
    params.insert(QStringLiteral("baseRevision"), int(m_doc->docRevision()));
    QJsonObject reply = sendAndWaitForReply(QStringLiteral("fixtures.remap.apply"), params);
    QCOMPARE(reply.value(QStringLiteral("ok")).toBool(), false);
    QCOMPARE(reply.value(QStringLiteral("error")).toObject().value(QStringLiteral("code")).toString(),
             QStringLiteral("FIXTURES_ADDRESS_OVERLAP"));
    QVERIFY(m_doc->fixture(a) != nullptr);
    QCOMPARE(m_doc->fixtures().count(), 2);

    // Moving a onto its OWN old range is fine (the source is replaced)
    mapping.insert(QStringLiteral("address"), 0);
    params.insert(QStringLiteral("mappings"), QJsonArray{ mapping });
    params.insert(QStringLiteral("baseRevision"), int(m_doc->docRevision()) - 1);
    reply = sendAndWaitForReply(QStringLiteral("fixtures.remap.apply"), params);
    QCOMPARE(reply.value(QStringLiteral("error")).toObject().value(QStringLiteral("code")).toString(), QStringLiteral("CONFLICT"));

    params.insert(QStringLiteral("baseRevision"), int(m_doc->docRevision()));
    reply = sendAndWaitForReply(QStringLiteral("fixtures.remap.apply"), params);
    QCOMPARE(reply.value(QStringLiteral("ok")).toBool(), true);
    QCOMPARE(m_doc->fixtures().count(), 2);
    QVERIFY(m_doc->fixture(b) != nullptr);
    Q_UNUSED(b)
}

QTEST_MAIN(ApiFixtureRemapDomain_Test)
