/*
  Q Light Controller Plus - Control API unit test
  apifixturegroupdomain_test.cpp

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

#include "apifixturegroupdomain_test.h"
#include "apiserver.h"
#include "fixturegroup.h"
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

void ApiFixtureGroupDomain_Test::init()
{
    // Doc(QObject*, int universes = 4) - a fresh Doc already has universes
    // 0-3, no addUniverse() needed for anything here.
    m_doc = new Doc(nullptr);
    m_apiServer = new ApiServer(nullptr, m_doc);
    QVERIFY(m_apiServer->listen(0));

    m_client = new QWebSocket();
    m_client->open(QUrl(QStringLiteral("ws://127.0.0.1:%1/qlcplusapi").arg(m_apiServer->serverPort())));
    QVERIFY(QTest::qWaitFor([this]() { return m_client->state() == QAbstractSocket::ConnectedState; }, 2000));

    m_nextAddress = 0;
}

void ApiFixtureGroupDomain_Test::cleanup()
{
    delete m_client;
    m_client = nullptr;
    delete m_apiServer;
    m_apiServer = nullptr;
    delete m_doc;
    m_doc = nullptr;
}

QJsonObject ApiFixtureGroupDomain_Test::sendAndWaitForReply(const QString &method, const QJsonObject &params)
{
    // A mutation's response and its broadcast event can arrive in either
    // order - scan every frame received so far for the matching "response",
    // not just the first one, polling until it shows up or we time out.
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

QString ApiFixtureGroupDomain_Test::helloAndGetClientId()
{
    QJsonObject reply = sendAndWaitForReply(QStringLiteral("hello"), QJsonObject());
    return reply.value(QStringLiteral("result")).toObject().value(QStringLiteral("clientId")).toString();
}

int ApiFixtureGroupDomain_Test::currentDocRevision()
{
    return int(m_doc->docRevision());
}

quint32 ApiFixtureGroupDomain_Test::addGenericFixture(int channels)
{
    Fixture *fxi = new Fixture(m_doc);
    fxi->setChannels(quint32(channels));
    fxi->setAddress(m_nextAddress);
    m_nextAddress += quint32(channels);
    bool added = m_doc->addFixture(fxi);
    Q_ASSERT(added);
    return fxi->id();
}

void ApiFixtureGroupDomain_Test::listIsEmptyInFreshDoc()
{
    helloAndGetClientId();
    QJsonObject reply = sendAndWaitForReply(QStringLiteral("fixtures.group.list"), QJsonObject());
    QCOMPARE(reply.value(QStringLiteral("ok")).toBool(), true);
    QCOMPARE(reply.value(QStringLiteral("result")).toObject().value(QStringLiteral("groups")).toArray().count(), 0);
}

void ApiFixtureGroupDomain_Test::getOnMissingGroupIsNotFound()
{
    helloAndGetClientId();
    QJsonObject params;
    params.insert(QStringLiteral("groupId"), QStringLiteral("999"));
    QJsonObject reply = sendAndWaitForReply(QStringLiteral("fixtures.group.get"), params);
    QCOMPARE(reply.value(QStringLiteral("ok")).toBool(), false);
    QCOMPARE(reply.value(QStringLiteral("error")).toObject().value(QStringLiteral("code")).toString(),
              QStringLiteral("NOT_FOUND"));
}

void ApiFixtureGroupDomain_Test::createBumpsRevisionAndBroadcastsCreated()
{
    QString clientId = helloAndGetClientId();
    int before = currentDocRevision();

    QSignalSpy spy(m_client, &QWebSocket::textMessageReceived);
    QJsonObject params;
    params.insert(QStringLiteral("name"), QStringLiteral("Front Wash"));
    params.insert(QStringLiteral("columns"), 4);
    params.insert(QStringLiteral("rows"), 2);
    params.insert(QStringLiteral("baseRevision"), before);
    m_client->sendTextMessage(buildRequest(QStringLiteral("fixtures.group.create"), params, QStringLiteral("t-create")));
    QVERIFY(QTest::qWaitFor([&]() { return spy.count() >= 2; }, 2000));

    QString groupId;
    bool sawEvent = false;
    for (const QList<QVariant> &frame : spy)
    {
        QJsonObject obj = QJsonDocument::fromJson(frame.at(0).toString().toUtf8()).object();
        QString type = obj.value(QStringLiteral("type")).toString();
        if (type == QStringLiteral("response") && obj.value(QStringLiteral("id")).toString() == QStringLiteral("t-create"))
        {
            QCOMPARE(obj.value(QStringLiteral("ok")).toBool(), true);
            QJsonObject result = obj.value(QStringLiteral("result")).toObject();
            QVERIFY(result.value(QStringLiteral("docRevision")).toInt() > before);
            groupId = result.value(QStringLiteral("groupId")).toString();
            QVERIFY(groupId.isEmpty() == false);
        }
        else if (type == QStringLiteral("event") && obj.value(QStringLiteral("topic")).toString() == QStringLiteral("fixtures.group.created"))
        {
            QJsonObject data = obj.value(QStringLiteral("data")).toObject();
            QJsonObject group = data.value(QStringLiteral("group")).toObject();
            QCOMPARE(group.value(QStringLiteral("name")).toString(), QStringLiteral("Front Wash"));
            QCOMPARE(group.value(QStringLiteral("size")).toObject().value(QStringLiteral("columns")).toInt(), 4);
            QCOMPARE(group.value(QStringLiteral("size")).toObject().value(QStringLiteral("rows")).toInt(), 2);
            QCOMPARE(group.value(QStringLiteral("heads")).toArray().count(), 0);
            QCOMPARE(obj.value(QStringLiteral("originClientId")).toString(), clientId);
            sawEvent = true;
        }
    }
    QVERIFY(sawEvent);
    QVERIFY(groupId.isEmpty() == false);

    bool ok = false;
    quint32 id = groupId.toUInt(&ok);
    QVERIFY(ok);
    FixtureGroup *grp = m_doc->fixtureGroup(id);
    QVERIFY(grp != nullptr);
    QCOMPARE(grp->name(), QStringLiteral("Front Wash"));
    QCOMPARE(grp->size(), QSize(4, 2));
}

void ApiFixtureGroupDomain_Test::createWithStaleRevisionConflicts()
{
    helloAndGetClientId();
    int before = currentDocRevision();

    QJsonObject params;
    params.insert(QStringLiteral("name"), QStringLiteral("Stale"));
    params.insert(QStringLiteral("baseRevision"), before - 1); // deliberately stale
    QJsonObject reply = sendAndWaitForReply(QStringLiteral("fixtures.group.create"), params);

    QCOMPARE(reply.value(QStringLiteral("ok")).toBool(), false);
    QJsonObject error = reply.value(QStringLiteral("error")).toObject();
    QCOMPARE(error.value(QStringLiteral("code")).toString(), QStringLiteral("CONFLICT"));
    QCOMPARE(error.value(QStringLiteral("details")).toObject().value(QStringLiteral("docRevision")).toInt(), before);
    QCOMPARE(m_doc->fixtureGroups().count(), 0);
}

void ApiFixtureGroupDomain_Test::createRequiresName()
{
    helloAndGetClientId();
    QJsonObject params;
    params.insert(QStringLiteral("name"), QString());
    params.insert(QStringLiteral("baseRevision"), currentDocRevision());
    QJsonObject reply = sendAndWaitForReply(QStringLiteral("fixtures.group.create"), params);
    QCOMPARE(reply.value(QStringLiteral("ok")).toBool(), false);
    QCOMPARE(reply.value(QStringLiteral("error")).toObject().value(QStringLiteral("code")).toString(),
              QStringLiteral("INVALID_PARAMS"));
}

void ApiFixtureGroupDomain_Test::renameBroadcastsRenamedEventNotUpdated()
{
    FixtureGroup *grp = new FixtureGroup(m_doc);
    grp->setName(QStringLiteral("Old Name"));
    QVERIFY(m_doc->addFixtureGroup(grp));

    QString clientId = helloAndGetClientId();
    int before = currentDocRevision();

    QSignalSpy spy(m_client, &QWebSocket::textMessageReceived);
    QJsonObject params;
    params.insert(QStringLiteral("groupId"), QString::number(grp->id()));
    params.insert(QStringLiteral("name"), QStringLiteral("New Name"));
    params.insert(QStringLiteral("baseRevision"), before);
    m_client->sendTextMessage(buildRequest(QStringLiteral("fixtures.group.rename"), params, QStringLiteral("t-rename")));
    QVERIFY(QTest::qWaitFor([&]() { return spy.count() >= 2; }, 2000));

    bool sawRenamed = false, sawUpdated = false;
    for (const QList<QVariant> &frame : spy)
    {
        QJsonObject obj = QJsonDocument::fromJson(frame.at(0).toString().toUtf8()).object();
        if (obj.value(QStringLiteral("type")).toString() != QStringLiteral("event"))
            continue;
        QString topic = obj.value(QStringLiteral("topic")).toString();
        if (topic == QStringLiteral("fixtures.group.renamed"))
        {
            QJsonObject data = obj.value(QStringLiteral("data")).toObject();
            QCOMPARE(data.value(QStringLiteral("groupId")).toString(), QString::number(grp->id()));
            QCOMPARE(data.value(QStringLiteral("name")).toString(), QStringLiteral("New Name"));
            QVERIFY(data.value(QStringLiteral("docRevision")).toInt() > before);
            QCOMPARE(obj.value(QStringLiteral("originClientId")).toString(), clientId);
            sawRenamed = true;
        }
        else if (topic == QStringLiteral("fixtures.group.updated"))
        {
            sawUpdated = true;
        }
    }
    QVERIFY(sawRenamed);
    QVERIFY(sawUpdated == false); // rename must not also fire the catch-all "updated" topic
    QCOMPARE(grp->name(), QStringLiteral("New Name"));
}

void ApiFixtureGroupDomain_Test::renameToSameNameIsNoopButStillOk()
{
    FixtureGroup *grp = new FixtureGroup(m_doc);
    grp->setName(QStringLiteral("Same"));
    QVERIFY(m_doc->addFixtureGroup(grp));

    helloAndGetClientId();
    int before = currentDocRevision();

    QJsonObject params;
    params.insert(QStringLiteral("groupId"), QString::number(grp->id()));
    params.insert(QStringLiteral("name"), QStringLiteral("Same"));
    params.insert(QStringLiteral("baseRevision"), before);
    QJsonObject reply = sendAndWaitForReply(QStringLiteral("fixtures.group.rename"), params);

    QCOMPARE(reply.value(QStringLiteral("ok")).toBool(), true);
    QCOMPARE(reply.value(QStringLiteral("result")).toObject().value(QStringLiteral("docRevision")).toInt(), before);
}

void ApiFixtureGroupDomain_Test::deleteRemovesGroupAndBroadcastsDeleted()
{
    FixtureGroup *grp = new FixtureGroup(m_doc);
    grp->setName(QStringLiteral("Doomed"));
    QVERIFY(m_doc->addFixtureGroup(grp));
    quint32 groupId = grp->id();

    QString clientId = helloAndGetClientId();
    int before = currentDocRevision();

    QSignalSpy spy(m_client, &QWebSocket::textMessageReceived);
    QJsonObject params;
    params.insert(QStringLiteral("groupId"), QString::number(groupId));
    params.insert(QStringLiteral("baseRevision"), before);
    m_client->sendTextMessage(buildRequest(QStringLiteral("fixtures.group.delete"), params, QStringLiteral("t-delete")));
    QVERIFY(QTest::qWaitFor([&]() { return spy.count() >= 2; }, 2000));

    bool sawEvent = false;
    for (const QList<QVariant> &frame : spy)
    {
        QJsonObject obj = QJsonDocument::fromJson(frame.at(0).toString().toUtf8()).object();
        if (obj.value(QStringLiteral("type")).toString() == QStringLiteral("event") &&
            obj.value(QStringLiteral("topic")).toString() == QStringLiteral("fixtures.group.deleted"))
        {
            QJsonObject data = obj.value(QStringLiteral("data")).toObject();
            QCOMPARE(data.value(QStringLiteral("groupId")).toString(), QString::number(groupId));
            QVERIFY(data.value(QStringLiteral("docRevision")).toInt() > before);
            QCOMPARE(obj.value(QStringLiteral("originClientId")).toString(), clientId);
            sawEvent = true;
        }
    }
    QVERIFY(sawEvent);
    QVERIFY(m_doc->fixtureGroup(groupId) == nullptr);
}

void ApiFixtureGroupDomain_Test::setSizeUpdatesSizeAndBroadcastsUpdated()
{
    FixtureGroup *grp = new FixtureGroup(m_doc);
    grp->setSize(QSize(1, 1));
    QVERIFY(m_doc->addFixtureGroup(grp));

    QString clientId = helloAndGetClientId();
    int before = currentDocRevision();

    QSignalSpy spy(m_client, &QWebSocket::textMessageReceived);
    QJsonObject params;
    params.insert(QStringLiteral("groupId"), QString::number(grp->id()));
    params.insert(QStringLiteral("columns"), 6);
    params.insert(QStringLiteral("rows"), 3);
    params.insert(QStringLiteral("baseRevision"), before);
    m_client->sendTextMessage(buildRequest(QStringLiteral("fixtures.group.setSize"), params, QStringLiteral("t-size")));
    QVERIFY(QTest::qWaitFor([&]() { return spy.count() >= 2; }, 2000));

    bool sawEvent = false;
    for (const QList<QVariant> &frame : spy)
    {
        QJsonObject obj = QJsonDocument::fromJson(frame.at(0).toString().toUtf8()).object();
        if (obj.value(QStringLiteral("type")).toString() == QStringLiteral("event") &&
            obj.value(QStringLiteral("topic")).toString() == QStringLiteral("fixtures.group.updated"))
        {
            QJsonObject group = obj.value(QStringLiteral("data")).toObject().value(QStringLiteral("group")).toObject();
            QCOMPARE(group.value(QStringLiteral("size")).toObject().value(QStringLiteral("columns")).toInt(), 6);
            QCOMPARE(group.value(QStringLiteral("size")).toObject().value(QStringLiteral("rows")).toInt(), 3);
            QCOMPARE(obj.value(QStringLiteral("originClientId")).toString(), clientId);
            sawEvent = true;
        }
    }
    QVERIFY(sawEvent);
    QCOMPARE(grp->size(), QSize(6, 3));
}

void ApiFixtureGroupDomain_Test::setSizeRejectsZeroColumns()
{
    FixtureGroup *grp = new FixtureGroup(m_doc);
    grp->setSize(QSize(1, 1));
    QVERIFY(m_doc->addFixtureGroup(grp));

    helloAndGetClientId();
    QJsonObject params;
    params.insert(QStringLiteral("groupId"), QString::number(grp->id()));
    params.insert(QStringLiteral("columns"), 0);
    params.insert(QStringLiteral("rows"), 1);
    params.insert(QStringLiteral("baseRevision"), currentDocRevision());
    QJsonObject reply = sendAndWaitForReply(QStringLiteral("fixtures.group.setSize"), params);
    QCOMPARE(reply.value(QStringLiteral("ok")).toBool(), false);
    QCOMPARE(reply.value(QStringLiteral("error")).toObject().value(QStringLiteral("code")).toString(),
              QStringLiteral("INVALID_PARAMS"));
    QCOMPARE(grp->size(), QSize(1, 1));
}

void ApiFixtureGroupDomain_Test::assignFixtureAutoPlacesAllHeads()
{
    quint32 fxId = addGenericFixture(3); // 3 channels -> 3 heads

    FixtureGroup *grp = new FixtureGroup(m_doc);
    grp->setSize(QSize(4, 4));
    QVERIFY(m_doc->addFixtureGroup(grp));

    helloAndGetClientId();
    QJsonObject params;
    params.insert(QStringLiteral("groupId"), QString::number(grp->id()));
    params.insert(QStringLiteral("fixtureId"), QString::number(fxId));
    params.insert(QStringLiteral("baseRevision"), currentDocRevision());
    QJsonObject reply = sendAndWaitForReply(QStringLiteral("fixtures.group.assignFixture"), params);

    QCOMPARE(reply.value(QStringLiteral("ok")).toBool(), true);
    QCOMPARE(grp->headList().count(), 3);
    QVERIFY(grp->headsMap().value(QLCPoint(0, 0)) == GroupHead(fxId, 0));
    QVERIFY(grp->headsMap().value(QLCPoint(1, 0)) == GroupHead(fxId, 1));
    QVERIFY(grp->headsMap().value(QLCPoint(2, 0)) == GroupHead(fxId, 2));
}

void ApiFixtureGroupDomain_Test::assignFixtureWithUnknownFixtureIsNotFound()
{
    FixtureGroup *grp = new FixtureGroup(m_doc);
    grp->setSize(QSize(4, 4));
    QVERIFY(m_doc->addFixtureGroup(grp));

    helloAndGetClientId();
    QJsonObject params;
    params.insert(QStringLiteral("groupId"), QString::number(grp->id()));
    params.insert(QStringLiteral("fixtureId"), QStringLiteral("999999")); // does not exist - must not reach the
                                                                            // engine's Q_ASSERT(fxi != NULL)
    params.insert(QStringLiteral("baseRevision"), currentDocRevision());
    QJsonObject reply = sendAndWaitForReply(QStringLiteral("fixtures.group.assignFixture"), params);
    QCOMPARE(reply.value(QStringLiteral("ok")).toBool(), false);
    QCOMPARE(reply.value(QStringLiteral("error")).toObject().value(QStringLiteral("code")).toString(),
              QStringLiteral("NOT_FOUND"));
    QCOMPARE(grp->headList().count(), 0);
}

void ApiFixtureGroupDomain_Test::assignHeadAutoPlacesNewHead()
{
    quint32 fxId = addGenericFixture(1);

    FixtureGroup *grp = new FixtureGroup(m_doc);
    grp->setSize(QSize(2, 2));
    QVERIFY(m_doc->addFixtureGroup(grp));

    helloAndGetClientId();
    QJsonObject params;
    params.insert(QStringLiteral("groupId"), QString::number(grp->id()));
    params.insert(QStringLiteral("fixtureId"), QString::number(fxId));
    params.insert(QStringLiteral("headIndex"), 0);
    params.insert(QStringLiteral("baseRevision"), currentDocRevision());
    QJsonObject reply = sendAndWaitForReply(QStringLiteral("fixtures.group.assignHead"), params);

    QCOMPARE(reply.value(QStringLiteral("ok")).toBool(), true);
    QVERIFY(grp->headsMap().value(QLCPoint(0, 0)) == GroupHead(fxId, 0));
}

void ApiFixtureGroupDomain_Test::assignHeadToOccupiedCellSwapsPositions()
{
    // Two single-head fixtures, placed at (0,0) and (1,0) respectively.
    quint32 fxA = addGenericFixture(1);
    quint32 fxB = addGenericFixture(1);

    FixtureGroup *grp = new FixtureGroup(m_doc);
    grp->setSize(QSize(2, 2));
    QVERIFY(grp->assignHead(QLCPoint(0, 0), GroupHead(fxA, 0)));
    QVERIFY(grp->assignHead(QLCPoint(1, 0), GroupHead(fxB, 0)));
    QVERIFY(m_doc->addFixtureGroup(grp));

    helloAndGetClientId();

    // Ask fxA's head (currently at (0,0)) to move onto (1,0), which fxB's
    // head already occupies - the two must swap places (this domain's
    // resignHead/swap composition around FixtureGroup::assignHead(), see
    // apifixturegroupdomain.cpp's own comment for why the raw engine
    // primitive alone doesn't do this).
    QJsonObject params;
    params.insert(QStringLiteral("groupId"), QString::number(grp->id()));
    params.insert(QStringLiteral("fixtureId"), QString::number(fxA));
    params.insert(QStringLiteral("headIndex"), 0);
    params.insert(QStringLiteral("x"), 1);
    params.insert(QStringLiteral("y"), 0);
    params.insert(QStringLiteral("baseRevision"), currentDocRevision());
    QJsonObject reply = sendAndWaitForReply(QStringLiteral("fixtures.group.assignHead"), params);

    QCOMPARE(reply.value(QStringLiteral("ok")).toBool(), true);
    QVERIFY(grp->headsMap().value(QLCPoint(1, 0)) == GroupHead(fxA, 0));
    QVERIFY(grp->headsMap().value(QLCPoint(0, 0)) == GroupHead(fxB, 0));
    QCOMPARE(grp->headList().count(), 2); // nothing lost, nothing duplicated
}

void ApiFixtureGroupDomain_Test::assignHeadWithBadHeadIndexIsInvalidParams()
{
    quint32 fxId = addGenericFixture(1); // heads() == 1, so only headIndex 0 is valid

    FixtureGroup *grp = new FixtureGroup(m_doc);
    grp->setSize(QSize(2, 2));
    QVERIFY(m_doc->addFixtureGroup(grp));

    helloAndGetClientId();
    QJsonObject params;
    params.insert(QStringLiteral("groupId"), QString::number(grp->id()));
    params.insert(QStringLiteral("fixtureId"), QString::number(fxId));
    params.insert(QStringLiteral("headIndex"), 5);
    params.insert(QStringLiteral("baseRevision"), currentDocRevision());
    QJsonObject reply = sendAndWaitForReply(QStringLiteral("fixtures.group.assignHead"), params);
    QCOMPARE(reply.value(QStringLiteral("ok")).toBool(), false);
    QCOMPARE(reply.value(QStringLiteral("error")).toObject().value(QStringLiteral("code")).toString(),
              QStringLiteral("INVALID_PARAMS"));
}

void ApiFixtureGroupDomain_Test::unassignHeadClearsCell()
{
    quint32 fxId = addGenericFixture(1);

    FixtureGroup *grp = new FixtureGroup(m_doc);
    grp->setSize(QSize(2, 2));
    QVERIFY(grp->assignHead(QLCPoint(0, 0), GroupHead(fxId, 0)));
    QVERIFY(m_doc->addFixtureGroup(grp));

    helloAndGetClientId();
    QJsonObject params;
    params.insert(QStringLiteral("groupId"), QString::number(grp->id()));
    params.insert(QStringLiteral("x"), 0);
    params.insert(QStringLiteral("y"), 0);
    params.insert(QStringLiteral("baseRevision"), currentDocRevision());
    QJsonObject reply = sendAndWaitForReply(QStringLiteral("fixtures.group.unassignHead"), params);

    QCOMPARE(reply.value(QStringLiteral("ok")).toBool(), true);
    QVERIFY(grp->headsMap().contains(QLCPoint(0, 0)) == false);
}

void ApiFixtureGroupDomain_Test::unassignHeadOnEmptyCellIsNoopButStillOk()
{
    FixtureGroup *grp = new FixtureGroup(m_doc);
    grp->setSize(QSize(2, 2));
    QVERIFY(m_doc->addFixtureGroup(grp));

    helloAndGetClientId();
    int before = currentDocRevision();
    QJsonObject params;
    params.insert(QStringLiteral("groupId"), QString::number(grp->id()));
    params.insert(QStringLiteral("x"), 0);
    params.insert(QStringLiteral("y"), 0);
    params.insert(QStringLiteral("baseRevision"), before);
    QJsonObject reply = sendAndWaitForReply(QStringLiteral("fixtures.group.unassignHead"), params);

    QCOMPARE(reply.value(QStringLiteral("ok")).toBool(), true);
    QCOMPARE(reply.value(QStringLiteral("result")).toObject().value(QStringLiteral("docRevision")).toInt(), before);
}

void ApiFixtureGroupDomain_Test::unassignFixtureRemovesAllItsHeads()
{
    quint32 fxId = addGenericFixture(2);

    FixtureGroup *grp = new FixtureGroup(m_doc);
    grp->setSize(QSize(2, 2));
    QVERIFY(grp->assignFixture(fxId));
    QCOMPARE(grp->headList().count(), 2);
    QVERIFY(m_doc->addFixtureGroup(grp));

    helloAndGetClientId();
    QJsonObject params;
    params.insert(QStringLiteral("groupId"), QString::number(grp->id()));
    params.insert(QStringLiteral("fixtureId"), QString::number(fxId));
    params.insert(QStringLiteral("baseRevision"), currentDocRevision());
    QJsonObject reply = sendAndWaitForReply(QStringLiteral("fixtures.group.unassignFixture"), params);

    QCOMPARE(reply.value(QStringLiteral("ok")).toBool(), true);
    QCOMPARE(grp->headList().count(), 0);
}

void ApiFixtureGroupDomain_Test::swapHeadsExchangesPositions()
{
    quint32 fxA = addGenericFixture(1);
    quint32 fxB = addGenericFixture(1);

    FixtureGroup *grp = new FixtureGroup(m_doc);
    grp->setSize(QSize(2, 2));
    QVERIFY(grp->assignHead(QLCPoint(0, 0), GroupHead(fxA, 0)));
    QVERIFY(grp->assignHead(QLCPoint(1, 1), GroupHead(fxB, 0)));
    QVERIFY(m_doc->addFixtureGroup(grp));

    helloAndGetClientId();
    QJsonObject params;
    params.insert(QStringLiteral("groupId"), QString::number(grp->id()));
    params.insert(QStringLiteral("ax"), 0);
    params.insert(QStringLiteral("ay"), 0);
    params.insert(QStringLiteral("bx"), 1);
    params.insert(QStringLiteral("by"), 1);
    params.insert(QStringLiteral("baseRevision"), currentDocRevision());
    QJsonObject reply = sendAndWaitForReply(QStringLiteral("fixtures.group.swapHeads"), params);

    QCOMPARE(reply.value(QStringLiteral("ok")).toBool(), true);
    QVERIFY(grp->headsMap().value(QLCPoint(0, 0)) == GroupHead(fxB, 0));
    QVERIFY(grp->headsMap().value(QLCPoint(1, 1)) == GroupHead(fxA, 0));
}

void ApiFixtureGroupDomain_Test::resetClearsAllHeads()
{
    quint32 fxId = addGenericFixture(4);

    FixtureGroup *grp = new FixtureGroup(m_doc);
    grp->setSize(QSize(4, 4));
    QVERIFY(grp->assignFixture(fxId));
    QCOMPARE(grp->headList().count(), 4);
    QVERIFY(m_doc->addFixtureGroup(grp));

    helloAndGetClientId();
    QJsonObject params;
    params.insert(QStringLiteral("groupId"), QString::number(grp->id()));
    params.insert(QStringLiteral("baseRevision"), currentDocRevision());
    QJsonObject reply = sendAndWaitForReply(QStringLiteral("fixtures.group.reset"), params);

    QCOMPARE(reply.value(QStringLiteral("ok")).toBool(), true);
    QCOMPARE(grp->headList().count(), 0);
    QCOMPARE(grp->size(), QSize(4, 4)); // reset() preserves size
}

QTEST_MAIN(ApiFixtureGroupDomain_Test)
