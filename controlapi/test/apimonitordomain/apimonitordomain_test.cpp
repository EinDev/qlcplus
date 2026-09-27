/*
  Q Light Controller Plus - Control API unit test
  apimonitordomain_test.cpp

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
#include <QtMath>

#include "apimonitordomain_test.h"
#include "apiserver.h"
#include "monitorproperties.h"
#include "monitorlayout.h"
#include "qlcfixturedef.h"
#include "qlcfixturemode.h"
#include "qlcphysical.h"
#include "qlcchannel.h"
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

static QJsonObject vec(double x, double y, double z)
{
    QJsonObject o;
    o.insert(QStringLiteral("x"), x);
    o.insert(QStringLiteral("y"), y);
    o.insert(QStringLiteral("z"), z);
    return o;
}

static QJsonObject key(quint32 id, int head = 0, int linked = 0)
{
    QJsonObject o;
    o.insert(QStringLiteral("fixtureId"), QString::number(id));
    o.insert(QStringLiteral("headIndex"), head);
    o.insert(QStringLiteral("linkedIndex"), linked);
    return o;
}

void ApiMonitorDomain_Test::init()
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

void ApiMonitorDomain_Test::cleanup()
{
    delete m_client;
    m_client = nullptr;
    delete m_apiServer;
    m_apiServer = nullptr;
    delete m_doc;
    m_doc = nullptr;
    delete m_movingHeadDef;
    m_movingHeadDef = nullptr;
}

QJsonObject ApiMonitorDomain_Test::sendAndWaitForReply(const QString &method, const QJsonObject &params)
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

QString ApiMonitorDomain_Test::helloAndGetClientId()
{
    QJsonObject reply = sendAndWaitForReply(QStringLiteral("hello"), QJsonObject());
    return reply.value(QStringLiteral("result")).toObject().value(QStringLiteral("clientId")).toString();
}

quint32 ApiMonitorDomain_Test::addGenericFixture(int channels)
{
    Fixture *fxi = new Fixture(m_doc);
    fxi->setChannels(quint32(channels));
    fxi->setAddress(m_nextAddress);
    m_nextAddress += quint32(channels);
    bool added = m_doc->addFixture(fxi);
    Q_ASSERT(added);
    return fxi->id();
}

quint32 ApiMonitorDomain_Test::addMovingHead()
{
    if (m_movingHeadDef == nullptr)
    {
        m_movingHeadDef = new QLCFixtureDef();
        m_movingHeadDef->setManufacturer(QStringLiteral("Test"));
        m_movingHeadDef->setModel(QStringLiteral("Mover"));
        m_movingHeadDef->setType(QLCFixtureDef::MovingHead);

        QLCChannel *pan = new QLCChannel();
        pan->setName(QStringLiteral("Pan"));
        pan->setGroup(QLCChannel::Pan);
        pan->setControlByte(QLCChannel::MSB);
        m_movingHeadDef->addChannel(pan);
        QLCChannel *tilt = new QLCChannel();
        tilt->setName(QStringLiteral("Tilt"));
        tilt->setGroup(QLCChannel::Tilt);
        tilt->setControlByte(QLCChannel::MSB);
        m_movingHeadDef->addChannel(tilt);
        QLCChannel *dimmer = new QLCChannel();
        dimmer->setName(QStringLiteral("Dimmer"));
        dimmer->setGroup(QLCChannel::Intensity);
        m_movingHeadDef->addChannel(dimmer);

        QLCFixtureMode *mode = new QLCFixtureMode(m_movingHeadDef);
        mode->setName(QStringLiteral("3ch"));
        mode->insertChannel(pan, 0);
        mode->insertChannel(tilt, 1);
        mode->insertChannel(dimmer, 2);
        QLCPhysical phy;
        phy.setFocusPanMax(540);
        phy.setFocusTiltMax(270);
        mode->setPhysical(phy);
        m_movingHeadDef->addMode(mode);
    }

    Fixture *fxi = new Fixture(m_doc);
    fxi->setFixtureDefinition(m_movingHeadDef, m_movingHeadDef->modes().first());
    fxi->setAddress(m_nextAddress);
    m_nextAddress += 3;
    bool added = m_doc->addFixture(fxi);
    Q_ASSERT(added);
    return fxi->id();
}

void ApiMonitorDomain_Test::getListsStageAndUnplacedFixtures()
{
    helloAndGetClientId();
    quint32 placed = addGenericFixture(4);
    quint32 unplaced = addGenericFixture(2);
    m_doc->monitorProperties()->setFixturePosition(placed, 0, 0, QVector3D(1000, 0, 2000));

    QJsonObject reply = sendAndWaitForReply(QStringLiteral("fixtures.monitor.get"), QJsonObject());
    QCOMPARE(reply.value(QStringLiteral("ok")).toBool(), true);
    QJsonObject result = reply.value(QStringLiteral("result")).toObject();
    QJsonObject stage = result.value(QStringLiteral("stage")).toObject();
    QCOMPARE(stage.value(QStringLiteral("gridUnits")).toString(), QStringLiteral("Meters"));
    QCOMPARE(stage.value(QStringLiteral("pointOfView")).toString(), QStringLiteral("Undefined"));
    QVERIFY(stage.contains(QStringLiteral("gridCenter")));

    QJsonArray items = result.value(QStringLiteral("items")).toArray();
    QCOMPARE(items.count(), 2);
    bool sawPlaced = false, sawUnplaced = false;
    for (const QJsonValue &v : items)
    {
        QJsonObject it = v.toObject();
        if (it.value(QStringLiteral("fixtureId")).toString() == QString::number(placed))
        {
            sawPlaced = true;
            QCOMPARE(it.value(QStringLiteral("placed")).toBool(), true);
            QCOMPARE(it.value(QStringLiteral("position")).toObject().value(QStringLiteral("z")).toDouble(), 2000.0);
            QCOMPARE(it.value(QStringLiteral("channels")).toInt(), 4);
            QCOMPARE(it.value(QStringLiteral("hasPan")).toBool(), false);
            // Fixture::genericDimmerMode() sizes a generic dimmer's physical
            // width by its channel count; only the depth falls back to the
            // 2D view's 300 mm default.
            QVERIFY(it.value(QStringLiteral("physical")).toObject().value(QStringLiteral("width")).toDouble() > 0);
            QCOMPARE(it.value(QStringLiteral("physical")).toObject().value(QStringLiteral("depth")).toDouble(), 300.0);
        }
        else if (it.value(QStringLiteral("fixtureId")).toString() == QString::number(unplaced))
        {
            sawUnplaced = true;
            QCOMPARE(it.value(QStringLiteral("placed")).toBool(), false);
        }
    }
    QVERIFY(sawPlaced);
    QVERIFY(sawUnplaced);
}

void ApiMonitorDomain_Test::setStageUpdatesGridAndBumpsRevision()
{
    QString clientId = helloAndGetClientId();
    int before = int(m_doc->docRevision());

    QSignalSpy spy(m_client, &QWebSocket::textMessageReceived);
    QJsonObject params;
    params.insert(QStringLiteral("gridSize"), vec(20, 4, 12));
    params.insert(QStringLiteral("gridUnits"), QStringLiteral("Feet"));
    params.insert(QStringLiteral("pointOfView"), QStringLiteral("TopView"));
    params.insert(QStringLiteral("stageType"), QStringLiteral("Box"));
    params.insert(QStringLiteral("showLabels"), true);
    params.insert(QStringLiteral("baseRevision"), before);
    QJsonObject reply = sendAndWaitForReply(QStringLiteral("fixtures.monitor.setStage"), params);
    QCOMPARE(reply.value(QStringLiteral("ok")).toBool(), true);
    QVERIFY(reply.value(QStringLiteral("result")).toObject().value(QStringLiteral("docRevision")).toInt() > before);

    MonitorProperties *mp = m_doc->monitorProperties();
    QCOMPARE(mp->gridSize(), QVector3D(20, 4, 12));
    QCOMPARE(mp->gridUnits(), MonitorProperties::Feet);
    QCOMPARE(mp->pointOfView(), MonitorProperties::TopView);
    QCOMPARE(mp->stageType(), MonitorProperties::StageBox);
    QCOMPARE(mp->labelsVisible(), true);

    bool sawEvent = false;
    QVERIFY(QTest::qWaitFor([&]()
    {
        for (const QList<QVariant> &frame : spy)
        {
            QJsonObject obj = QJsonDocument::fromJson(frame.at(0).toString().toUtf8()).object();
            if (obj.value(QStringLiteral("type")).toString() == QStringLiteral("event") &&
                obj.value(QStringLiteral("topic")).toString() == QStringLiteral("fixtures.monitor.changed"))
            {
                QJsonObject stage = obj.value(QStringLiteral("data")).toObject().value(QStringLiteral("stage")).toObject();
                sawEvent = stage.value(QStringLiteral("pointOfView")).toString() == QStringLiteral("TopView") &&
                           obj.value(QStringLiteral("originClientId")).toString() == clientId;
                return true;
            }
        }
        return false;
    }, 2000));
    QVERIFY(sawEvent);
}

void ApiMonitorDomain_Test::setStageWithStaleRevisionConflicts()
{
    helloAndGetClientId();
    QJsonObject params;
    params.insert(QStringLiteral("showLabels"), true);
    params.insert(QStringLiteral("baseRevision"), int(m_doc->docRevision()) + 5);
    QJsonObject reply = sendAndWaitForReply(QStringLiteral("fixtures.monitor.setStage"), params);
    QCOMPARE(reply.value(QStringLiteral("ok")).toBool(), false);
    QCOMPARE(reply.value(QStringLiteral("error")).toObject().value(QStringLiteral("code")).toString(), QStringLiteral("CONFLICT"));
    QCOMPARE(m_doc->monitorProperties()->labelsVisible(), false);
}

void ApiMonitorDomain_Test::setPlacementWritesPositionGelAndFlags()
{
    helloAndGetClientId();
    quint32 fid = addGenericFixture(1);
    int before = int(m_doc->docRevision());

    QJsonObject upd = key(fid);
    upd.insert(QStringLiteral("position"), vec(1500, 0, 2500));
    upd.insert(QStringLiteral("rotation"), vec(0, 90, 0));
    upd.insert(QStringLiteral("gelColor"), QStringLiteral("#ff0080"));
    upd.insert(QStringLiteral("invertPan"), true);
    upd.insert(QStringLiteral("hidden"), true);
    QJsonObject params;
    params.insert(QStringLiteral("items"), QJsonArray{ upd });
    params.insert(QStringLiteral("baseRevision"), before);
    QJsonObject reply = sendAndWaitForReply(QStringLiteral("fixtures.monitor.setPlacement"), params);
    QCOMPARE(reply.value(QStringLiteral("ok")).toBool(), true);
    QJsonArray items = reply.value(QStringLiteral("result")).toObject().value(QStringLiteral("items")).toArray();
    QCOMPARE(items.count(), 1);
    QJsonObject flags = items.at(0).toObject().value(QStringLiteral("flags")).toObject();
    QCOMPARE(flags.value(QStringLiteral("invertPan")).toBool(), true);
    QCOMPARE(flags.value(QStringLiteral("hidden")).toBool(), true);
    QCOMPARE(flags.value(QStringLiteral("locked")).toBool(), false);
    QCOMPARE(items.at(0).toObject().value(QStringLiteral("gelColor")).toString(), QStringLiteral("#ff0080"));

    MonitorProperties *mp = m_doc->monitorProperties();
    QCOMPARE(mp->fixturePosition(fid, 0, 0), QVector3D(1500, 0, 2500));
    QCOMPARE(mp->fixtureRotation(fid, 0, 0), QVector3D(0, 90, 0));
    QCOMPARE(mp->fixtureGelColor(fid, 0, 0), QColor("#ff0080"));
    QVERIFY(mp->fixtureFlags(fid, 0, 0) & MonitorProperties::InvertedPanFlag);
    QVERIFY(int(m_doc->docRevision()) > before);

    // null clears the gel
    upd = key(fid);
    upd.insert(QStringLiteral("gelColor"), QJsonValue());
    upd.insert(QStringLiteral("invertPan"), false);
    params.insert(QStringLiteral("items"), QJsonArray{ upd });
    params.insert(QStringLiteral("baseRevision"), int(m_doc->docRevision()));
    reply = sendAndWaitForReply(QStringLiteral("fixtures.monitor.setPlacement"), params);
    QCOMPARE(reply.value(QStringLiteral("ok")).toBool(), true);
    QCOMPARE(mp->fixtureGelColor(fid, 0, 0).isValid(), false);
    QVERIFY(!(mp->fixtureFlags(fid, 0, 0) & MonitorProperties::InvertedPanFlag));
    QVERIFY(mp->fixtureFlags(fid, 0, 0) & MonitorProperties::HiddenFlag);
}

void ApiMonitorDomain_Test::setPlacementSkipsLockedItems()
{
    helloAndGetClientId();
    quint32 fid = addGenericFixture(1);
    MonitorProperties *mp = m_doc->monitorProperties();
    mp->setFixturePosition(fid, 0, 0, QVector3D(100, 0, 100));
    mp->setFixtureFlags(fid, 0, 0, MonitorProperties::LockedFlag);

    QJsonObject upd = key(fid);
    upd.insert(QStringLiteral("position"), vec(900, 0, 900));
    QJsonObject params;
    params.insert(QStringLiteral("items"), QJsonArray{ upd });
    params.insert(QStringLiteral("baseRevision"), int(m_doc->docRevision()));
    QJsonObject reply = sendAndWaitForReply(QStringLiteral("fixtures.monitor.setPlacement"), params);
    QCOMPARE(reply.value(QStringLiteral("ok")).toBool(), true);
    QCOMPARE(reply.value(QStringLiteral("result")).toObject().value(QStringLiteral("skippedLocked")).toArray().count(), 1);
    QCOMPARE(mp->fixturePosition(fid, 0, 0), QVector3D(100, 0, 100));

    // unlocking in the same entry lets the move through
    upd.insert(QStringLiteral("locked"), false);
    params.insert(QStringLiteral("items"), QJsonArray{ upd });
    params.insert(QStringLiteral("baseRevision"), int(m_doc->docRevision()));
    reply = sendAndWaitForReply(QStringLiteral("fixtures.monitor.setPlacement"), params);
    QCOMPARE(reply.value(QStringLiteral("ok")).toBool(), true);
    QCOMPARE(reply.value(QStringLiteral("result")).toObject().value(QStringLiteral("skippedLocked")).toArray().count(), 0);
    QCOMPARE(mp->fixturePosition(fid, 0, 0), QVector3D(900, 0, 900));
    QVERIFY(!(mp->fixtureFlags(fid, 0, 0) & MonitorProperties::LockedFlag));
}

void ApiMonitorDomain_Test::setPlacementAddsAndRemovesLinkedCopy()
{
    helloAndGetClientId();
    quint32 fid = addGenericFixture(1);
    MonitorProperties *mp = m_doc->monitorProperties();
    mp->setFixturePosition(fid, 0, 0, QVector3D(100, 0, 100));
    mp->setFixtureGelColor(fid, 0, 0, QColor("#00ff00"));

    QJsonObject upd = key(fid, 0, 1);
    upd.insert(QStringLiteral("position"), vec(700, 0, 100));
    QJsonObject params;
    params.insert(QStringLiteral("items"), QJsonArray{ upd });
    params.insert(QStringLiteral("baseRevision"), int(m_doc->docRevision()));
    QJsonObject reply = sendAndWaitForReply(QStringLiteral("fixtures.monitor.setPlacement"), params);
    QCOMPARE(reply.value(QStringLiteral("ok")).toBool(), true);
    QVERIFY(mp->containsItem(fid, 0, 1));
    QCOMPARE(mp->fixturePosition(fid, 0, 1), QVector3D(700, 0, 100));
    QCOMPARE(mp->fixtureGelColor(fid, 0, 1), QColor("#00ff00")); // copied from the base item
    QCOMPARE(mp->fixtureIDList(fid).count(), 2);

    // removing a base item is refused
    QJsonObject rm = key(fid, 0, 0);
    rm.insert(QStringLiteral("remove"), true);
    params.insert(QStringLiteral("items"), QJsonArray{ rm });
    params.insert(QStringLiteral("baseRevision"), int(m_doc->docRevision()));
    reply = sendAndWaitForReply(QStringLiteral("fixtures.monitor.setPlacement"), params);
    QCOMPARE(reply.value(QStringLiteral("ok")).toBool(), false);
    QCOMPARE(reply.value(QStringLiteral("error")).toObject().value(QStringLiteral("code")).toString(), QStringLiteral("INVALID_PARAMS"));

    rm = key(fid, 0, 1);
    rm.insert(QStringLiteral("remove"), true);
    params.insert(QStringLiteral("items"), QJsonArray{ rm });
    params.insert(QStringLiteral("baseRevision"), int(m_doc->docRevision()));
    reply = sendAndWaitForReply(QStringLiteral("fixtures.monitor.setPlacement"), params);
    QCOMPARE(reply.value(QStringLiteral("ok")).toBool(), true);
    QVERIFY(mp->containsItem(fid, 0, 1) == false);
    QVERIFY(mp->containsItem(fid, 0, 0));
}

void ApiMonitorDomain_Test::arrangeCircleMatchesMonitorLayout()
{
    helloAndGetClientId();
    MonitorProperties *mp = m_doc->monitorProperties();
    mp->setPointOfView(MonitorProperties::TopView);
    QList<quint32> ids;
    QJsonArray keys;
    for (int i = 0; i < 4; i++)
    {
        quint32 fid = addGenericFixture(1);
        ids.append(fid);
        mp->setFixturePosition(fid, 0, 0, QVector3D(1000 + i * 10, 0, 1000));
        keys.append(key(fid));
    }
    // Lock the last one: it must stay where it is
    mp->setFixtureFlags(ids.last(), 0, 0, MonitorProperties::LockedFlag);

    // Reference computed through the same engine helper the domain uses
    QList<MonitorLayout::Item> ref;
    for (quint32 fid : ids)
    {
        MonitorLayout::Item it;
        it.fixtureId = fid;
        it.position = mp->fixturePosition(fid, 0, 0);
        it.locked = fid == ids.last();
        ref.append(it);
    }
    ref = MonitorLayout::arrangeCircle(MonitorLayout::sortedByAddress(m_doc, ref), MonitorProperties::TopView, 4000, true);

    QJsonObject args;
    args.insert(QStringLiteral("diameter"), 4000);
    args.insert(QStringLiteral("lookAtCenter"), true);
    QJsonObject params;
    params.insert(QStringLiteral("items"), keys);
    params.insert(QStringLiteral("op"), QStringLiteral("circle"));
    params.insert(QStringLiteral("args"), args);
    params.insert(QStringLiteral("baseRevision"), int(m_doc->docRevision()));
    QJsonObject reply = sendAndWaitForReply(QStringLiteral("fixtures.monitor.arrange"), params);
    QCOMPARE(reply.value(QStringLiteral("ok")).toBool(), true);
    QJsonObject result = reply.value(QStringLiteral("result")).toObject();
    QCOMPARE(result.value(QStringLiteral("items")).toArray().count(), 4);
    QCOMPARE(result.value(QStringLiteral("skippedLocked")).toArray().count(), 1);

    for (const MonitorLayout::Item &it : ref)
    {
        QVector3D actual = mp->fixturePosition(it.fixtureId, 0, 0);
        QVERIFY2(qAbs(actual.x() - it.position.x()) < 0.01 && qAbs(actual.z() - it.position.z()) < 0.01,
                 qPrintable(QStringLiteral("fixture %1: %2,%3 vs %4,%5").arg(it.fixtureId)
                            .arg(actual.x()).arg(actual.z()).arg(it.position.x()).arg(it.position.z())));
        if (it.locked)
            QCOMPARE(actual, QVector3D(1030, 0, 1000));
        else
            QVERIFY(qAbs(mp->fixtureRotation(it.fixtureId, 0, 0).y() - it.rotation.y()) < 0.01);
    }
    // The three unlocked ones sit 2000 mm from the centroid
    QVector3D centroid = MonitorLayout::centroid(ref.mid(0, 0)); // placeholder to keep API in use
    Q_UNUSED(centroid)
    for (int i = 0; i < 3; i++)
    {
        QVector3D p = mp->fixturePosition(ids.at(i), 0, 0);
        qreal d = qSqrt(qPow(p.x() - 1015, 2) + qPow(p.z() - 1000, 2));
        QVERIFY2(qAbs(d - 2000) < 0.5, qPrintable(QString::number(d)));
    }
}

void ApiMonitorDomain_Test::arrangeAlignAndDistribute()
{
    helloAndGetClientId();
    MonitorProperties *mp = m_doc->monitorProperties();
    mp->setPointOfView(MonitorProperties::TopView);
    quint32 a = addGenericFixture(1), b = addGenericFixture(1), c = addGenericFixture(1);
    mp->setFixturePosition(a, 0, 0, QVector3D(0, 0, 500));
    mp->setFixturePosition(b, 0, 0, QVector3D(3000, 0, 900));
    mp->setFixturePosition(c, 0, 0, QVector3D(6000, 0, 1300));

    QJsonObject args;
    args.insert(QStringLiteral("edge"), QStringLiteral("top"));
    QJsonObject params;
    params.insert(QStringLiteral("items"), QJsonArray{ key(a), key(b), key(c) });
    params.insert(QStringLiteral("op"), QStringLiteral("align"));
    params.insert(QStringLiteral("args"), args);
    params.insert(QStringLiteral("baseRevision"), int(m_doc->docRevision()));
    QJsonObject reply = sendAndWaitForReply(QStringLiteral("fixtures.monitor.arrange"), params);
    QCOMPARE(reply.value(QStringLiteral("ok")).toBool(), true);
    QCOMPARE(mp->fixturePosition(b, 0, 0).z(), 500.0f); // TopView: "top" aligns Z to the first item
    QCOMPARE(mp->fixturePosition(c, 0, 0).z(), 500.0f);
    QCOMPARE(mp->fixturePosition(c, 0, 0).x(), 6000.0f);

    mp->setFixturePosition(b, 0, 0, QVector3D(1000, 0, 500));
    args = QJsonObject();
    args.insert(QStringLiteral("direction"), QStringLiteral("horizontal"));
    params.insert(QStringLiteral("op"), QStringLiteral("distribute"));
    params.insert(QStringLiteral("args"), args);
    params.insert(QStringLiteral("baseRevision"), int(m_doc->docRevision()));
    reply = sendAndWaitForReply(QStringLiteral("fixtures.monitor.arrange"), params);
    QCOMPARE(reply.value(QStringLiteral("ok")).toBool(), true);
    // 300 mm footprints: span 0..6300, gap = (6300 - 900) / 2 = 2700 -> b at 3000
    QCOMPARE(mp->fixturePosition(b, 0, 0).x(), 3000.0f);
    QCOMPARE(mp->fixturePosition(a, 0, 0).x(), 0.0f);
    QCOMPARE(mp->fixturePosition(c, 0, 0).x(), 6000.0f);

    // distribute with 2 items is refused
    params.insert(QStringLiteral("items"), QJsonArray{ key(a), key(b) });
    params.insert(QStringLiteral("baseRevision"), int(m_doc->docRevision()));
    reply = sendAndWaitForReply(QStringLiteral("fixtures.monitor.arrange"), params);
    QCOMPARE(reply.value(QStringLiteral("ok")).toBool(), false);
}

void ApiMonitorDomain_Test::arrangeUnknownOpIsInvalidParams()
{
    helloAndGetClientId();
    quint32 a = addGenericFixture(1);
    QJsonObject params;
    params.insert(QStringLiteral("items"), QJsonArray{ key(a) });
    params.insert(QStringLiteral("op"), QStringLiteral("spiral"));
    params.insert(QStringLiteral("baseRevision"), int(m_doc->docRevision()));
    QJsonObject reply = sendAndWaitForReply(QStringLiteral("fixtures.monitor.arrange"), params);
    QCOMPARE(reply.value(QStringLiteral("ok")).toBool(), false);
    QCOMPARE(reply.value(QStringLiteral("error")).toObject().value(QStringLiteral("code")).toString(), QStringLiteral("INVALID_PARAMS"));

    // unknown fixture -> NOT_FOUND
    params.insert(QStringLiteral("items"), QJsonArray{ key(4242) });
    params.insert(QStringLiteral("op"), QStringLiteral("center"));
    reply = sendAndWaitForReply(QStringLiteral("fixtures.monitor.arrange"), params);
    QCOMPARE(reply.value(QStringLiteral("error")).toObject().value(QStringLiteral("code")).toString(), QStringLiteral("NOT_FOUND"));
}

void ApiMonitorDomain_Test::detectArrangementReportsCircle()
{
    helloAndGetClientId();
    MonitorProperties *mp = m_doc->monitorProperties();
    mp->setPointOfView(MonitorProperties::TopView);
    QJsonArray keys;
    for (int i = 0; i < 4; i++)
    {
        quint32 fid = addGenericFixture(1);
        qreal ang = i * M_PI / 2;
        mp->setFixturePosition(fid, 0, 0, QVector3D(5000 + 1500 * qCos(ang), 0, 5000 + 1500 * qSin(ang)));
        keys.append(key(fid));
    }
    QJsonObject params;
    params.insert(QStringLiteral("items"), keys);
    QJsonObject reply = sendAndWaitForReply(QStringLiteral("fixtures.monitor.detectArrangement"), params);
    QCOMPARE(reply.value(QStringLiteral("ok")).toBool(), true);
    QJsonObject result = reply.value(QStringLiteral("result")).toObject();
    QVERIFY(qAbs(result.value(QStringLiteral("circleDiameter")).toDouble() - 3000.0) < 0.01);
    QVERIFY(qAbs(result.value(QStringLiteral("centroid")).toObject().value(QStringLiteral("x")).toDouble() - 5000.0) < 0.01);
    QVERIFY(qAbs(result.value(QStringLiteral("lineLength")).toDouble() - 3000.0) < 0.01);
}

void ApiMonitorDomain_Test::aimAtWritesPanTiltOverrides()
{
    QString clientId = helloAndGetClientId();
    quint32 mover = addMovingHead();
    quint32 dimmer = addGenericFixture(1);
    MonitorProperties *mp = m_doc->monitorProperties();
    mp->setGridSize(QVector3D(10, 3, 10));
    mp->setFixturePosition(mover, 0, 0, QVector3D(5000, 3000, 5000)); // hanging at the stage centre, 3 m up

    QSignalSpy spy(m_client, &QWebSocket::textMessageReceived);
    QJsonObject params;
    params.insert(QStringLiteral("items"), QJsonArray{ key(mover), key(dimmer) });
    params.insert(QStringLiteral("point"), vec(5000, 0, 8000)); // straight down and 3 m towards +Z
    QJsonObject reply = sendAndWaitForReply(QStringLiteral("fixtures.monitor.aimAt"), params);
    QCOMPARE(reply.value(QStringLiteral("ok")).toBool(), true);
    QJsonObject result = reply.value(QStringLiteral("result")).toObject();
    QJsonArray fixtures = result.value(QStringLiteral("fixtures")).toArray();
    QCOMPARE(fixtures.count(), 1); // the dimmer has no pan/tilt
    QJsonObject fx = fixtures.at(0).toObject();
    QCOMPARE(fx.value(QStringLiteral("fixtureId")).toString(), QString::number(mover));
    QVERIFY(fx.contains(QStringLiteral("panDegrees")));
    QVERIFY(fx.contains(QStringLiteral("tiltDegrees")));
    // 45 degrees off straight-down; the tool maps that to focusTiltMax/2 -/+ 45
    double tilt = fx.value(QStringLiteral("tiltDegrees")).toDouble();
    QVERIFY2(qAbs(tilt - 90.0) < 0.5 || qAbs(tilt - 180.0) < 0.5, qPrintable(QString::number(tilt)));

    QJsonArray channels = result.value(QStringLiteral("channels")).toArray();
    QCOMPARE(channels.count(), 2);
    Fixture *fixture = m_doc->fixture(mover);
    for (const QJsonValue &v : channels)
    {
        int address = v.toObject().value(QStringLiteral("address")).toInt();
        QVERIFY(address == int(fixture->universeAddress()) || address == int(fixture->universeAddress()) + 1);
    }

    // The override reached the Simple Desk store: io.simpleDesk.get lists it
    // and the channelChanged events were broadcast with our client id.
    QJsonObject sd;
    sd.insert(QStringLiteral("universeId"), 0);
    reply = sendAndWaitForReply(QStringLiteral("io.simpleDesk.get"), sd);
    QCOMPARE(reply.value(QStringLiteral("result")).toObject().value(QStringLiteral("channels")).toArray().count(), 2);
    int events = 0;
    for (const QList<QVariant> &frame : spy)
    {
        QJsonObject obj = QJsonDocument::fromJson(frame.at(0).toString().toUtf8()).object();
        if (obj.value(QStringLiteral("topic")).toString() == QStringLiteral("io.simpleDesk.channelChanged") &&
            obj.value(QStringLiteral("originClientId")).toString() == clientId)
            events++;
    }
    QCOMPARE(events, 2);
}

void ApiMonitorDomain_Test::layoutSortedByAddressFollowsFixtureOrder()
{
    quint32 a = addGenericFixture(4); // address 0
    quint32 b = addGenericFixture(4); // address 4
    QList<MonitorLayout::Item> items;
    MonitorLayout::Item ib; ib.fixtureId = b; ib.headIndex = 1;
    MonitorLayout::Item ib0; ib0.fixtureId = b; ib0.headIndex = 0;
    MonitorLayout::Item ia; ia.fixtureId = a;
    items << ib << ib0 << ia;
    QList<MonitorLayout::Item> sorted = MonitorLayout::sortedByAddress(m_doc, items);
    QCOMPARE(sorted.at(0).fixtureId, a);
    QCOMPARE(sorted.at(1).fixtureId, b);
    QCOMPARE(sorted.at(1).headIndex, quint16(0));
    QCOMPARE(sorted.at(2).headIndex, quint16(1));

    // grid / line / rotate keep the geometry pure: a 2x2 grid of 2000x1000
    QList<MonitorLayout::Item> four;
    for (int i = 0; i < 4; i++) { MonitorLayout::Item it; it.fixtureId = i; it.position = QVector3D(1000, 0, 1000); four << it; }
    QList<MonitorLayout::Item> grid = MonitorLayout::arrangeGrid(four, MonitorProperties::TopView, 2000, 1000, 2, 0);
    QCOMPARE(grid.at(0).position, QVector3D(0, 0, 500));
    QCOMPARE(grid.at(1).position, QVector3D(2000, 0, 500));
    QCOMPARE(grid.at(2).position, QVector3D(0, 0, 1500));
    QCOMPARE(grid.at(3).position, QVector3D(2000, 0, 1500));
    QList<MonitorLayout::Item> line = MonitorLayout::arrangeLine(four, MonitorProperties::TopView, 3000, 0, false);
    QCOMPARE(line.at(0).position.x(), -500.0f);
    QCOMPARE(line.at(3).position.x(), 2500.0f);
    QList<MonitorLayout::Item> centred = MonitorLayout::moveToCenter(line, QVector3D(0, 0, 0));
    QVERIFY(qFuzzyIsNull(MonitorLayout::centroid(centred).x()));
}

QTEST_MAIN(ApiMonitorDomain_Test)
