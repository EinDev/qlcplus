/*
  Q Light Controller Plus - Control API unit test
  apifixturesdomain_test.cpp

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

#include "apifixturesdomain_test.h"
#include "apiserver.h"
#include "qlcfixturedefcache.h"
#include "qlcfixturedef.h"
#include "qlcfixturemode.h"
#include "qlccapability.h"
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

static QJsonObject genericDefinition(int channels)
{
    QJsonObject generic;
    generic.insert(QStringLiteral("channels"), channels);
    QJsonObject definition;
    definition.insert(QStringLiteral("generic"), generic);
    return definition;
}

void ApiFixturesDomain_Test::init()
{
    // Doc(QObject*, int universes = 4) - a fresh Doc already has universes
    // 0-3 patched, no io.universe.create needed for these tests.
    m_doc = new Doc(nullptr);
    m_apiServer = new ApiServer(nullptr, m_doc);
    QVERIFY(m_apiServer->listen(0, QHostAddress::LocalHost));

    m_client = new QWebSocket();
    m_client->open(QUrl(QStringLiteral("ws://127.0.0.1:%1/qlcplusapi").arg(m_apiServer->serverPort())));
    QVERIFY(QTest::qWaitFor([this]() { return m_client->state() == QAbstractSocket::ConnectedState; }, 2000));
}

void ApiFixturesDomain_Test::cleanup()
{
    delete m_client;
    m_client = nullptr;
    delete m_apiServer;
    m_apiServer = nullptr;
    delete m_doc;
    m_doc = nullptr;
}

QJsonObject ApiFixturesDomain_Test::sendAndWaitForReply(const QString &method, const QJsonObject &params)
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

QString ApiFixturesDomain_Test::helloAndGetClientId()
{
    QJsonObject reply = sendAndWaitForReply(QStringLiteral("hello"), QJsonObject());
    return reply.value(QStringLiteral("result")).toObject().value(QStringLiteral("clientId")).toString();
}

quint32 ApiFixturesDomain_Test::patchGenericFixture(int universeId, int address, int channels, const QString &name)
{
    QJsonObject params;
    params.insert(QStringLiteral("universe"), universeId);
    params.insert(QStringLiteral("address"), address);
    params.insert(QStringLiteral("definition"), genericDefinition(channels));
    params.insert(QStringLiteral("name"), name);
    params.insert(QStringLiteral("baseRevision"), int(m_doc->docRevision()));

    QJsonObject reply = sendAndWaitForReply(QStringLiteral("fixtures.patch"), params);
    QJsonArray ids = reply.value(QStringLiteral("result")).toObject().value(QStringLiteral("fixtureIds")).toArray();
    if (ids.isEmpty())
        return Fixture::invalidId();
    return ids.at(0).toString().toUInt();
}

/* ------------------------------------------------------------------ */
/* fixtures.patch                                                      */
/* ------------------------------------------------------------------ */

void ApiFixturesDomain_Test::patchGenericCreatesFixtureAndBumpsRevision()
{
    helloAndGetClientId();
    quint32 before = m_doc->docRevision();

    QJsonObject params;
    params.insert(QStringLiteral("universe"), 0);
    params.insert(QStringLiteral("address"), 10);
    params.insert(QStringLiteral("definition"), genericDefinition(4));
    params.insert(QStringLiteral("name"), QStringLiteral("My Dimmer"));
    params.insert(QStringLiteral("baseRevision"), int(before));

    QJsonObject reply = sendAndWaitForReply(QStringLiteral("fixtures.patch"), params);
    QCOMPARE(reply.value(QStringLiteral("ok")).toBool(), true);

    QJsonObject result = reply.value(QStringLiteral("result")).toObject();
    QVERIFY(quint32(result.value(QStringLiteral("docRevision")).toInt()) > before);
    QJsonArray ids = result.value(QStringLiteral("fixtureIds")).toArray();
    QCOMPARE(ids.count(), 1);

    bool ok = false;
    quint32 fxId = ids.at(0).toString().toUInt(&ok);
    QVERIFY(ok);

    Fixture *fixture = m_doc->fixture(fxId);
    QVERIFY(fixture != nullptr);
    // The engine appends " [<id+1>]" unconditionally - fixtures.yaml's own
    // FixturesPatchRequest.name doc comment, mirroring
    // FixtureManager::addFixture() exactly.
    QCOMPARE(fixture->name(), QStringLiteral("My Dimmer [%1]").arg(fxId + 1));
    QCOMPARE(fixture->universe(), quint32(0));
    QCOMPARE(fixture->address(), quint32(10));
    QCOMPARE(fixture->channels(), quint32(4));
}

void ApiFixturesDomain_Test::patchGenericBroadcastsPatchedEvent()
{
    QString clientId = helloAndGetClientId();

    QSignalSpy spy(m_client, &QWebSocket::textMessageReceived);
    QJsonObject params;
    params.insert(QStringLiteral("universe"), 0);
    params.insert(QStringLiteral("address"), 20);
    params.insert(QStringLiteral("definition"), genericDefinition(2));
    params.insert(QStringLiteral("baseRevision"), int(m_doc->docRevision()));
    m_client->sendTextMessage(buildRequest(QStringLiteral("fixtures.patch"), params, QStringLiteral("t-patch")));
    QVERIFY(QTest::qWaitFor([&]() { return spy.count() >= 2; }, 2000));

    bool sawEvent = false;
    for (const QList<QVariant> &frame : spy)
    {
        QJsonObject obj = QJsonDocument::fromJson(frame.at(0).toString().toUtf8()).object();
        if (obj.value(QStringLiteral("type")).toString() == QStringLiteral("event") &&
            obj.value(QStringLiteral("topic")).toString() == QStringLiteral("fixtures.patched"))
        {
            QJsonObject data = obj.value(QStringLiteral("data")).toObject();
            QJsonArray fixtures = data.value(QStringLiteral("fixtures")).toArray();
            QCOMPARE(fixtures.count(), 1);
            QJsonObject fx = fixtures.at(0).toObject();
            QCOMPARE(fx.value(QStringLiteral("universe")).toInt(), 0);
            QCOMPARE(fx.value(QStringLiteral("address")).toInt(), 20);
            QCOMPARE(fx.value(QStringLiteral("channels")).toInt(), 2);
            QCOMPARE(fx.value(QStringLiteral("isGeneric")).toBool(), true);
            QCOMPARE(obj.value(QStringLiteral("originClientId")).toString(), clientId);
            sawEvent = true;
        }
    }
    QVERIFY(sawEvent);
}

void ApiFixturesDomain_Test::patchWithStaleRevisionConflicts()
{
    helloAndGetClientId();

    QJsonObject params;
    params.insert(QStringLiteral("universe"), 0);
    params.insert(QStringLiteral("address"), 30);
    params.insert(QStringLiteral("definition"), genericDefinition(2));
    params.insert(QStringLiteral("baseRevision"), int(m_doc->docRevision()) + 1); // deliberately stale

    QJsonObject reply = sendAndWaitForReply(QStringLiteral("fixtures.patch"), params);
    QCOMPARE(reply.value(QStringLiteral("ok")).toBool(), false);
    QJsonObject error = reply.value(QStringLiteral("error")).toObject();
    QCOMPARE(error.value(QStringLiteral("code")).toString(), QStringLiteral("CONFLICT"));
    QVERIFY(m_doc->fixtures().isEmpty());
}

void ApiFixturesDomain_Test::patchRejectsBothGenericAndNamedDefinition()
{
    helloAndGetClientId();

    QJsonObject definition = genericDefinition(2);
    definition.insert(QStringLiteral("manufacturer"), QStringLiteral("Acme"));
    definition.insert(QStringLiteral("model"), QStringLiteral("Foo"));
    definition.insert(QStringLiteral("mode"), QStringLiteral("Default"));

    QJsonObject params;
    params.insert(QStringLiteral("universe"), 0);
    params.insert(QStringLiteral("address"), 30);
    params.insert(QStringLiteral("definition"), definition);
    params.insert(QStringLiteral("baseRevision"), int(m_doc->docRevision()));

    QJsonObject reply = sendAndWaitForReply(QStringLiteral("fixtures.patch"), params);
    QCOMPARE(reply.value(QStringLiteral("ok")).toBool(), false);
    QCOMPARE(reply.value(QStringLiteral("error")).toObject().value(QStringLiteral("code")).toString(),
              QStringLiteral("INVALID_PARAMS"));
    QVERIFY(m_doc->fixtures().isEmpty());
}

void ApiFixturesDomain_Test::patchRejectsNeitherGenericNorNamedDefinition()
{
    helloAndGetClientId();

    QJsonObject params;
    params.insert(QStringLiteral("universe"), 0);
    params.insert(QStringLiteral("address"), 30);
    params.insert(QStringLiteral("definition"), QJsonObject());
    params.insert(QStringLiteral("baseRevision"), int(m_doc->docRevision()));

    QJsonObject reply = sendAndWaitForReply(QStringLiteral("fixtures.patch"), params);
    QCOMPARE(reply.value(QStringLiteral("ok")).toBool(), false);
    QCOMPARE(reply.value(QStringLiteral("error")).toObject().value(QStringLiteral("code")).toString(),
              QStringLiteral("INVALID_PARAMS"));
}

void ApiFixturesDomain_Test::patchRejectsOverlappingAddress()
{
    helloAndGetClientId();
    QVERIFY(patchGenericFixture(0, 40, 4) != Fixture::invalidId());

    QJsonObject params;
    params.insert(QStringLiteral("universe"), 0);
    params.insert(QStringLiteral("address"), 42); // overlaps [40,44)
    params.insert(QStringLiteral("definition"), genericDefinition(2));
    params.insert(QStringLiteral("baseRevision"), int(m_doc->docRevision()));

    QJsonObject reply = sendAndWaitForReply(QStringLiteral("fixtures.patch"), params);
    QCOMPARE(reply.value(QStringLiteral("ok")).toBool(), false);
    QCOMPARE(reply.value(QStringLiteral("error")).toObject().value(QStringLiteral("code")).toString(),
              QStringLiteral("FIXTURES_ADDRESS_OVERLAP"));
    QCOMPARE(m_doc->fixtures().count(), 1);
}

void ApiFixturesDomain_Test::patchBulkQuantityAssignsSequentialAddresses()
{
    helloAndGetClientId();

    QJsonObject params;
    params.insert(QStringLiteral("universe"), 0);
    params.insert(QStringLiteral("address"), 50);
    params.insert(QStringLiteral("definition"), genericDefinition(2));
    params.insert(QStringLiteral("name"), QStringLiteral("Bulk"));
    params.insert(QStringLiteral("quantity"), 3);
    params.insert(QStringLiteral("gap"), 1);
    params.insert(QStringLiteral("baseRevision"), int(m_doc->docRevision()));

    QJsonObject reply = sendAndWaitForReply(QStringLiteral("fixtures.patch"), params);
    QCOMPARE(reply.value(QStringLiteral("ok")).toBool(), true);
    QJsonArray ids = reply.value(QStringLiteral("result")).toObject().value(QStringLiteral("fixtureIds")).toArray();
    QCOMPARE(ids.count(), 3);

    // channels(2) + gap(1) = 3 apart, starting at 50.
    const int expectedAddresses[3] = { 50, 53, 56 };
    for (int i = 0; i < 3; i++)
    {
        bool ok = false;
        quint32 fxId = ids.at(i).toString().toUInt(&ok);
        QVERIFY(ok);
        Fixture *fixture = m_doc->fixture(fxId);
        QVERIFY(fixture != nullptr);
        QCOMPARE(int(fixture->address()), expectedAddresses[i]);
    }
}

void ApiFixturesDomain_Test::patchNamedDefinitionUsesRealFixtureDef()
{
    // Doc(nullptr) starts with a fixtureDefCache holding no manufacturers at
    // all (QLCFixtureDefCache::loadMap() is never called) - register a
    // minimal synthetic definition directly, the same way
    // engine/test/qlcfixturedefcache/qlcfixturedefcache_test.cpp's own
    // add()/reload() cases build QLCFixtureDef instances by hand, rather than
    // depending on any specific real-world fixture file being present in
    // this build's resources/fixtures/ library.
    QLCFixtureDef *def = new QLCFixtureDef();
    def->setManufacturer(QStringLiteral("Acme"));
    def->setModel(QStringLiteral("TestPar"));
    def->setType(QLCFixtureDef::Dimmer);
    def->setAuthor(QStringLiteral("Test"));

    QLCChannel *ch0 = new QLCChannel();
    ch0->setName(QStringLiteral("Intensity"));
    ch0->setGroup(QLCChannel::Intensity);
    def->addChannel(ch0);

    QLCChannel *ch1 = new QLCChannel();
    ch1->setName(QStringLiteral("Colour"));
    ch1->setGroup(QLCChannel::Colour);
    def->addChannel(ch1);

    QLCFixtureMode *mode = new QLCFixtureMode(def);
    mode->setName(QStringLiteral("2-channel"));
    mode->insertChannel(ch0, 0);
    mode->insertChannel(ch1, 1);
    def->addMode(mode);

    QVERIFY(m_doc->fixtureDefCache()->addFixtureDef(def));

    helloAndGetClientId();

    QJsonObject definition;
    definition.insert(QStringLiteral("manufacturer"), QStringLiteral("Acme"));
    definition.insert(QStringLiteral("model"), QStringLiteral("TestPar"));
    definition.insert(QStringLiteral("mode"), QStringLiteral("2-channel"));

    QJsonObject params;
    params.insert(QStringLiteral("universe"), 0);
    params.insert(QStringLiteral("address"), 60);
    params.insert(QStringLiteral("definition"), definition);
    params.insert(QStringLiteral("baseRevision"), int(m_doc->docRevision()));

    QJsonObject reply = sendAndWaitForReply(QStringLiteral("fixtures.patch"), params);
    QCOMPARE(reply.value(QStringLiteral("ok")).toBool(), true);

    QJsonArray ids = reply.value(QStringLiteral("result")).toObject().value(QStringLiteral("fixtureIds")).toArray();
    QCOMPARE(ids.count(), 1);
    bool ok = false;
    quint32 fxId = ids.at(0).toString().toUInt(&ok);
    QVERIFY(ok);

    Fixture *fixture = m_doc->fixture(fxId);
    QVERIFY(fixture != nullptr);
    QCOMPARE(fixture->channels(), quint32(2));
    QCOMPARE(fixture->fixtureDef()->manufacturer(), QStringLiteral("Acme"));
    QCOMPARE(fixture->fixtureDef()->model(), QStringLiteral("TestPar"));
    QCOMPARE(fixture->fixtureMode()->name(), QStringLiteral("2-channel"));
}

void ApiFixturesDomain_Test::patchWithUnknownDefinitionIsNotFound()
{
    helloAndGetClientId();

    QJsonObject definition;
    definition.insert(QStringLiteral("manufacturer"), QStringLiteral("NoSuchManufacturer"));
    definition.insert(QStringLiteral("model"), QStringLiteral("NoSuchModel"));
    definition.insert(QStringLiteral("mode"), QStringLiteral("NoSuchMode"));

    QJsonObject params;
    params.insert(QStringLiteral("universe"), 0);
    params.insert(QStringLiteral("address"), 60);
    params.insert(QStringLiteral("definition"), definition);
    params.insert(QStringLiteral("baseRevision"), int(m_doc->docRevision()));

    QJsonObject reply = sendAndWaitForReply(QStringLiteral("fixtures.patch"), params);
    QCOMPARE(reply.value(QStringLiteral("ok")).toBool(), false);
    QCOMPARE(reply.value(QStringLiteral("error")).toObject().value(QStringLiteral("code")).toString(),
              QStringLiteral("NOT_FOUND"));
}

/* ------------------------------------------------------------------ */
/* fixtures.list / fixtures.get                                        */
/* ------------------------------------------------------------------ */

void ApiFixturesDomain_Test::listReturnsPatchedFixtures()
{
    helloAndGetClientId();
    patchGenericFixture(0, 0, 2);
    patchGenericFixture(1, 0, 3);

    QJsonObject reply = sendAndWaitForReply(QStringLiteral("fixtures.list"), QJsonObject());
    QCOMPARE(reply.value(QStringLiteral("ok")).toBool(), true);
    QJsonArray fixtures = reply.value(QStringLiteral("result")).toObject().value(QStringLiteral("fixtures")).toArray();
    QCOMPARE(fixtures.count(), 2);
}

void ApiFixturesDomain_Test::listFiltersByUniverse()
{
    helloAndGetClientId();
    patchGenericFixture(0, 0, 2);
    patchGenericFixture(1, 0, 3);

    QJsonObject params;
    params.insert(QStringLiteral("universe"), 1);
    QJsonObject reply = sendAndWaitForReply(QStringLiteral("fixtures.list"), params);
    QJsonArray fixtures = reply.value(QStringLiteral("result")).toObject().value(QStringLiteral("fixtures")).toArray();
    QCOMPARE(fixtures.count(), 1);
    QCOMPARE(fixtures.at(0).toObject().value(QStringLiteral("universe")).toInt(), 1);
}

void ApiFixturesDomain_Test::getReturnsFixtureDetailWithChannelList()
{
    helloAndGetClientId();
    quint32 fxId = patchGenericFixture(0, 5, 3);
    QVERIFY(fxId != Fixture::invalidId());

    QJsonObject params;
    params.insert(QStringLiteral("fixtureId"), QString::number(fxId));
    QJsonObject reply = sendAndWaitForReply(QStringLiteral("fixtures.get"), params);
    QCOMPARE(reply.value(QStringLiteral("ok")).toBool(), true);

    QJsonObject result = reply.value(QStringLiteral("result")).toObject();
    QCOMPARE(result.value(QStringLiteral("id")).toString(), QString::number(fxId));
    QCOMPARE(result.value(QStringLiteral("channels")).toInt(), 3);

    QJsonArray channelList = result.value(QStringLiteral("channelList")).toArray();
    QCOMPARE(channelList.count(), 3);
    for (int i = 0; i < 3; i++)
    {
        QJsonObject ch = channelList.at(i).toObject();
        QCOMPARE(ch.value(QStringLiteral("index")).toInt(), i);
        QCOMPARE(ch.value(QStringLiteral("absoluteAddress")).toInt(), 5 + i);
    }
}

void ApiFixturesDomain_Test::getOnMissingFixtureIsNotFound()
{
    helloAndGetClientId();

    QJsonObject params;
    params.insert(QStringLiteral("fixtureId"), QStringLiteral("999999"));
    QJsonObject reply = sendAndWaitForReply(QStringLiteral("fixtures.get"), params);
    QCOMPARE(reply.value(QStringLiteral("ok")).toBool(), false);
    QCOMPARE(reply.value(QStringLiteral("error")).toObject().value(QStringLiteral("code")).toString(),
              QStringLiteral("NOT_FOUND"));
}

/* ------------------------------------------------------------------ */
/* fixtures.update                                                      */
/* ------------------------------------------------------------------ */

void ApiFixturesDomain_Test::updateRenameBroadcastsUpdatedEvent()
{
    QString clientId = helloAndGetClientId();
    quint32 fxId = patchGenericFixture(0, 0, 2, QStringLiteral("Old Name"));

    QSignalSpy spy(m_client, &QWebSocket::textMessageReceived);
    QJsonObject params;
    params.insert(QStringLiteral("fixtureId"), QString::number(fxId));
    params.insert(QStringLiteral("name"), QStringLiteral("New Name"));
    params.insert(QStringLiteral("baseRevision"), int(m_doc->docRevision()));
    m_client->sendTextMessage(buildRequest(QStringLiteral("fixtures.update"), params, QStringLiteral("t-upd")));
    QVERIFY(QTest::qWaitFor([&]() { return spy.count() >= 2; }, 2000));

    bool sawEvent = false;
    for (const QList<QVariant> &frame : spy)
    {
        QJsonObject obj = QJsonDocument::fromJson(frame.at(0).toString().toUtf8()).object();
        if (obj.value(QStringLiteral("type")).toString() == QStringLiteral("event") &&
            obj.value(QStringLiteral("topic")).toString() == QStringLiteral("fixtures.updated"))
        {
            QJsonObject data = obj.value(QStringLiteral("data")).toObject();
            QCOMPARE(data.value(QStringLiteral("fixture")).toObject().value(QStringLiteral("name")).toString(),
                      QStringLiteral("New Name"));
            QCOMPARE(obj.value(QStringLiteral("originClientId")).toString(), clientId);
            sawEvent = true;
        }
    }
    QVERIFY(sawEvent);

    Fixture *fixture = m_doc->fixture(fxId);
    QVERIFY(fixture != nullptr);
    QCOMPARE(fixture->name(), QStringLiteral("New Name"));
}

void ApiFixturesDomain_Test::updateMoveAddressRejectsOverlap()
{
    helloAndGetClientId();
    quint32 fxA = patchGenericFixture(0, 0, 4);
    quint32 fxB = patchGenericFixture(0, 10, 4);
    Q_UNUSED(fxB)

    QJsonObject params;
    params.insert(QStringLiteral("fixtureId"), QString::number(fxA));
    params.insert(QStringLiteral("address"), 12); // overlaps fxB's [10,14)
    params.insert(QStringLiteral("baseRevision"), int(m_doc->docRevision()));

    QJsonObject reply = sendAndWaitForReply(QStringLiteral("fixtures.update"), params);
    QCOMPARE(reply.value(QStringLiteral("ok")).toBool(), false);
    QCOMPARE(reply.value(QStringLiteral("error")).toObject().value(QStringLiteral("code")).toString(),
              QStringLiteral("FIXTURES_ADDRESS_OVERLAP"));
    QCOMPARE(m_doc->fixture(fxA)->address(), quint32(0));

    // Moving to a fully free address (not overlapping itself or fxB) must
    // still succeed - exercises the self-exclusion in the overlap check.
    QJsonObject moveParams;
    moveParams.insert(QStringLiteral("fixtureId"), QString::number(fxA));
    moveParams.insert(QStringLiteral("address"), 0); // fxA's own current address - no-op move
    moveParams.insert(QStringLiteral("baseRevision"), int(m_doc->docRevision()));
    QJsonObject moveReply = sendAndWaitForReply(QStringLiteral("fixtures.update"), moveParams);
    QCOMPARE(moveReply.value(QStringLiteral("ok")).toBool(), true);
}

void ApiFixturesDomain_Test::updateMoveToOtherUniverseIgnoresOldUniverseOccupant()
{
    // Regression: moving universe AND address used to call setAddress() then
    // setUniverse(), each emitting changed(); after the first one the fixture
    // was tracked at (new address, OLD universe) - here fxB's channels - and
    // Doc::slotFixtureChanged()'s Q_ASSERT(!m_addresses.contains(i)) aborted
    // this Debug build. The target (universe 1, address 20) is free.
    helloAndGetClientId();
    quint32 fxA = patchGenericFixture(0, 0, 4);
    quint32 fxB = patchGenericFixture(0, 20, 4);

    QJsonObject params;
    params.insert(QStringLiteral("fixtureId"), QString::number(fxA));
    params.insert(QStringLiteral("universe"), 1);
    params.insert(QStringLiteral("address"), 20); // occupied by fxB in universe 0, free in universe 1
    params.insert(QStringLiteral("baseRevision"), int(m_doc->docRevision()));

    QJsonObject reply = sendAndWaitForReply(QStringLiteral("fixtures.update"), params);
    QCOMPARE(reply.value(QStringLiteral("ok")).toBool(), true);
    QCOMPARE(m_doc->fixture(fxA)->universe(), quint32(1));
    QCOMPARE(m_doc->fixture(fxA)->address(), quint32(20));

    // Address tracking is exact: fxB still owns its channels in universe 0,
    // fxA owns the new ones in universe 1, and fxA's old range is free.
    for (quint32 i = 0; i < 4; i++)
    {
        QCOMPARE(m_doc->fixtureForAddress((0 << 9) + 20 + i), fxB);
        QCOMPARE(m_doc->fixtureForAddress((1 << 9) + 20 + i), fxA);
        QCOMPARE(m_doc->fixtureForAddress((0 << 9) + i), Fixture::invalidId());
    }
}

void ApiFixturesDomain_Test::updateWithNoFieldsIsInvalidParams()
{
    helloAndGetClientId();
    quint32 fxId = patchGenericFixture(0, 0, 2);

    QJsonObject params;
    params.insert(QStringLiteral("fixtureId"), QString::number(fxId));
    params.insert(QStringLiteral("baseRevision"), int(m_doc->docRevision()));

    QJsonObject reply = sendAndWaitForReply(QStringLiteral("fixtures.update"), params);
    QCOMPARE(reply.value(QStringLiteral("ok")).toBool(), false);
    QCOMPARE(reply.value(QStringLiteral("error")).toObject().value(QStringLiteral("code")).toString(),
              QStringLiteral("INVALID_PARAMS"));
}

void ApiFixturesDomain_Test::updateWithStaleRevisionConflicts()
{
    helloAndGetClientId();
    quint32 fxId = patchGenericFixture(0, 0, 2);

    QJsonObject params;
    params.insert(QStringLiteral("fixtureId"), QString::number(fxId));
    params.insert(QStringLiteral("name"), QStringLiteral("Should Not Apply"));
    params.insert(QStringLiteral("baseRevision"), int(m_doc->docRevision()) - 1); // deliberately stale

    QJsonObject reply = sendAndWaitForReply(QStringLiteral("fixtures.update"), params);
    QCOMPARE(reply.value(QStringLiteral("ok")).toBool(), false);
    QCOMPARE(reply.value(QStringLiteral("error")).toObject().value(QStringLiteral("code")).toString(),
              QStringLiteral("CONFLICT"));
    QVERIFY(m_doc->fixture(fxId)->name() != QStringLiteral("Should Not Apply"));
}

/* ------------------------------------------------------------------ */
/* fixtures.unpatch                                                     */
/* ------------------------------------------------------------------ */

void ApiFixturesDomain_Test::unpatchDeletesFixtureAndBumpsRevision()
{
    helloAndGetClientId();
    quint32 fxId = patchGenericFixture(0, 0, 2);
    quint32 before = m_doc->docRevision();

    QJsonObject params;
    QJsonArray ids;
    ids.append(QString::number(fxId));
    params.insert(QStringLiteral("fixtureIds"), ids);
    params.insert(QStringLiteral("baseRevision"), int(before));

    QJsonObject reply = sendAndWaitForReply(QStringLiteral("fixtures.unpatch"), params);
    QCOMPARE(reply.value(QStringLiteral("ok")).toBool(), true);
    QVERIFY(quint32(reply.value(QStringLiteral("result")).toObject().value(QStringLiteral("docRevision")).toInt()) > before);
    QVERIFY(m_doc->fixture(fxId) == nullptr);
}

void ApiFixturesDomain_Test::unpatchBroadcastsUnpatchedEvent()
{
    QString clientId = helloAndGetClientId();
    quint32 fxId = patchGenericFixture(0, 0, 2);

    QSignalSpy spy(m_client, &QWebSocket::textMessageReceived);
    QJsonObject params;
    QJsonArray ids;
    ids.append(QString::number(fxId));
    params.insert(QStringLiteral("fixtureIds"), ids);
    params.insert(QStringLiteral("baseRevision"), int(m_doc->docRevision()));
    m_client->sendTextMessage(buildRequest(QStringLiteral("fixtures.unpatch"), params, QStringLiteral("t-unp")));
    QVERIFY(QTest::qWaitFor([&]() { return spy.count() >= 2; }, 2000));

    bool sawEvent = false;
    for (const QList<QVariant> &frame : spy)
    {
        QJsonObject obj = QJsonDocument::fromJson(frame.at(0).toString().toUtf8()).object();
        if (obj.value(QStringLiteral("type")).toString() == QStringLiteral("event") &&
            obj.value(QStringLiteral("topic")).toString() == QStringLiteral("fixtures.unpatched"))
        {
            QJsonArray idsArray = obj.value(QStringLiteral("data")).toObject().value(QStringLiteral("fixtureIds")).toArray();
            QCOMPARE(idsArray.count(), 1);
            QCOMPARE(idsArray.at(0).toString(), QString::number(fxId));
            QCOMPARE(obj.value(QStringLiteral("originClientId")).toString(), clientId);
            sawEvent = true;
        }
    }
    QVERIFY(sawEvent);
}

void ApiFixturesDomain_Test::unpatchOnMissingFixtureIsNotFound()
{
    helloAndGetClientId();

    QJsonObject params;
    QJsonArray ids;
    ids.append(QStringLiteral("999999"));
    params.insert(QStringLiteral("fixtureIds"), ids);
    params.insert(QStringLiteral("baseRevision"), int(m_doc->docRevision()));

    QJsonObject reply = sendAndWaitForReply(QStringLiteral("fixtures.unpatch"), params);
    QCOMPARE(reply.value(QStringLiteral("ok")).toBool(), false);
    QCOMPARE(reply.value(QStringLiteral("error")).toObject().value(QStringLiteral("code")).toString(),
              QStringLiteral("NOT_FOUND"));
}

/* ------------------------------------------------------------------ */
/* fixtures.findAvailableAddress                                       */
/* ------------------------------------------------------------------ */

void ApiFixturesDomain_Test::findAvailableAddressReturnsRequestedWhenFree()
{
    helloAndGetClientId();

    QJsonObject params;
    params.insert(QStringLiteral("universe"), 0);
    params.insert(QStringLiteral("channels"), 4);
    params.insert(QStringLiteral("requestedAddress"), 100);

    QJsonObject reply = sendAndWaitForReply(QStringLiteral("fixtures.findAvailableAddress"), params);
    QCOMPARE(reply.value(QStringLiteral("ok")).toBool(), true);
    QJsonObject result = reply.value(QStringLiteral("result")).toObject();
    QCOMPARE(result.value(QStringLiteral("available")).toBool(), true);
    QCOMPARE(result.value(QStringLiteral("address")).toInt(), 100);
}

void ApiFixturesDomain_Test::findAvailableAddressScansWhenRequestedTaken()
{
    helloAndGetClientId();
    QVERIFY(patchGenericFixture(0, 0, 512) != Fixture::invalidId()); // occupies the entire universe 0

    QJsonObject params;
    params.insert(QStringLiteral("universe"), 0);
    params.insert(QStringLiteral("channels"), 4);
    params.insert(QStringLiteral("requestedAddress"), 100);

    QJsonObject reply = sendAndWaitForReply(QStringLiteral("fixtures.findAvailableAddress"), params);
    QCOMPARE(reply.value(QStringLiteral("ok")).toBool(), true);
    QJsonObject result = reply.value(QStringLiteral("result")).toObject();
    QCOMPARE(result.value(QStringLiteral("available")).toBool(), false);
}

void ApiFixturesDomain_Test::hugeAddressesAndCountsAreRejectedNotOverflowed()
{
    // Crash audit: address + channels (and channels*quantity) were summed in
    // int, so values near INT_MAX wrapped negative and passed the 512-channel
    // check - letting a ~2^31-channel generic definition be allocated, the
    // overlap check test the wrong range, or findAvailableAddress scan ~4e9
    // addresses on the main thread.
    helloAndGetClientId();

    QJsonObject params;
    params.insert(QStringLiteral("universe"), 0);
    params.insert(QStringLiteral("address"), 1);
    params.insert(QStringLiteral("definition"), genericDefinition(2147483647));
    params.insert(QStringLiteral("baseRevision"), int(m_doc->docRevision()));
    QJsonObject reply = sendAndWaitForReply(QStringLiteral("fixtures.patch"), params);
    QCOMPARE(reply.value(QStringLiteral("error")).toObject().value(QStringLiteral("code")).toString(), QStringLiteral("INVALID_PARAMS"));

    params.insert(QStringLiteral("address"), 2147483000);
    params.insert(QStringLiteral("definition"), genericDefinition(1000));
    reply = sendAndWaitForReply(QStringLiteral("fixtures.patch"), params);
    QCOMPARE(reply.value(QStringLiteral("error")).toObject().value(QStringLiteral("code")).toString(), QStringLiteral("INVALID_PARAMS"));
    QCOMPARE(m_doc->fixtures().count(), 0);

    QJsonObject find;
    find.insert(QStringLiteral("universe"), 0);
    find.insert(QStringLiteral("channels"), 65536);
    find.insert(QStringLiteral("quantity"), 65536);
    find.insert(QStringLiteral("requestedAddress"), 2147483000);
    QElapsedTimer timer;
    timer.start();
    reply = sendAndWaitForReply(QStringLiteral("fixtures.findAvailableAddress"), find);
    QCOMPARE(reply.value(QStringLiteral("ok")).toBool(), true);
    QCOMPARE(reply.value(QStringLiteral("result")).toObject().value(QStringLiteral("available")).toBool(), false);
    QVERIFY(timer.elapsed() < 1500);

    quint32 fxId = patchGenericFixture(0, 10, 4, QStringLiteral("A"));
    QVERIFY(fxId != Fixture::invalidId());
    QJsonObject update;
    update.insert(QStringLiteral("fixtureId"), QString::number(fxId));
    update.insert(QStringLiteral("address"), 2147483647);
    update.insert(QStringLiteral("baseRevision"), int(m_doc->docRevision()));
    reply = sendAndWaitForReply(QStringLiteral("fixtures.update"), update);
    QCOMPARE(reply.value(QStringLiteral("ok")).toBool(), false);
    QCOMPARE(m_doc->fixture(fxId)->address(), quint32(10));
}

/*********************************************************************
 * Fixture definition library browsing (fixtures.defs.*)
 *********************************************************************/

void ApiFixturesDomain_Test::addAcmeTestParDefinition()
{
    QLCFixtureDef *def = new QLCFixtureDef();
    def->setManufacturer(QStringLiteral("Acme"));
    def->setModel(QStringLiteral("TestPar"));
    def->setType(QLCFixtureDef::Dimmer);
    def->setAuthor(QStringLiteral("Test"));

    QLCChannel *ch0 = new QLCChannel();
    ch0->setName(QStringLiteral("Intensity"));
    ch0->setGroup(QLCChannel::Intensity);
    ch0->setControlByte(QLCChannel::MSB);
    ch0->setDefaultValue(0);
    QLCCapability *full = new QLCCapability(0, 255, QStringLiteral("Dimmer"));
    ch0->addCapability(full);
    def->addChannel(ch0);

    QLCChannel *ch1 = new QLCChannel();
    ch1->setName(QStringLiteral("Colour"));
    ch1->setGroup(QLCChannel::Colour);
    ch1->setDefaultValue(12);
    def->addChannel(ch1);

    QLCFixtureMode *mode = new QLCFixtureMode(def);
    mode->setName(QStringLiteral("2-channel"));
    mode->insertChannel(ch0, 0);
    mode->insertChannel(ch1, 1);
    def->addMode(mode);

    QVERIFY(m_doc->fixtureDefCache()->addFixtureDef(def));
}

void ApiFixturesDomain_Test::defsListManufacturersIncludesRegisteredDefinition()
{
    addAcmeTestParDefinition();
    helloAndGetClientId();
    QJsonObject reply = sendAndWaitForReply(QStringLiteral("fixtures.defs.listManufacturers"), QJsonObject());
    QCOMPARE(reply.value(QStringLiteral("ok")).toBool(), true);
    QJsonArray manufacturers = reply.value(QStringLiteral("result")).toObject().value(QStringLiteral("manufacturers")).toArray();
    QVERIFY(manufacturers.contains(QStringLiteral("Acme")));
}

void ApiFixturesDomain_Test::defsListModelsReturnsNamesAndDetails()
{
    addAcmeTestParDefinition();
    helloAndGetClientId();
    QJsonObject params;
    params.insert(QStringLiteral("manufacturer"), QStringLiteral("Acme"));
    QJsonObject reply = sendAndWaitForReply(QStringLiteral("fixtures.defs.listModels"), params);
    QCOMPARE(reply.value(QStringLiteral("ok")).toBool(), true);
    QJsonObject result = reply.value(QStringLiteral("result")).toObject();
    QCOMPARE(result.value(QStringLiteral("manufacturer")).toString(), QStringLiteral("Acme"));
    QJsonArray models = result.value(QStringLiteral("models")).toArray();
    QCOMPARE(models.count(), 1);
    QCOMPARE(models.at(0).toString(), QStringLiteral("TestPar"));
    QJsonArray details = result.value(QStringLiteral("modelDetails")).toArray();
    QCOMPARE(details.count(), 1);
    QCOMPARE(details.at(0).toObject().value(QStringLiteral("model")).toString(), QStringLiteral("TestPar"));
    QCOMPARE(details.at(0).toObject().value(QStringLiteral("isUser")).toBool(), false);
}

void ApiFixturesDomain_Test::defsListModelsUnknownManufacturerIsNotFound()
{
    helloAndGetClientId();
    QJsonObject params;
    params.insert(QStringLiteral("manufacturer"), QStringLiteral("Nobody"));
    QJsonObject reply = sendAndWaitForReply(QStringLiteral("fixtures.defs.listModels"), params);
    QCOMPARE(reply.value(QStringLiteral("ok")).toBool(), false);
    QCOMPARE(reply.value(QStringLiteral("error")).toObject().value(QStringLiteral("code")).toString(), QStringLiteral("NOT_FOUND"));
}

void ApiFixturesDomain_Test::defsGetModelReturnsModesWithChannels()
{
    addAcmeTestParDefinition();
    helloAndGetClientId();
    QJsonObject params;
    params.insert(QStringLiteral("manufacturer"), QStringLiteral("Acme"));
    params.insert(QStringLiteral("model"), QStringLiteral("TestPar"));
    QJsonObject reply = sendAndWaitForReply(QStringLiteral("fixtures.defs.getModel"), params);
    QCOMPARE(reply.value(QStringLiteral("ok")).toBool(), true);
    QJsonObject result = reply.value(QStringLiteral("result")).toObject();
    QCOMPARE(result.value(QStringLiteral("manufacturer")).toString(), QStringLiteral("Acme"));
    QCOMPARE(result.value(QStringLiteral("model")).toString(), QStringLiteral("TestPar"));
    QCOMPARE(result.value(QStringLiteral("type")).toString(), QLCFixtureDef::typeToString(QLCFixtureDef::Dimmer));
    QCOMPARE(result.value(QStringLiteral("fixtureType")).toString(), result.value(QStringLiteral("type")).toString());
    QCOMPARE(result.value(QStringLiteral("author")).toString(), QStringLiteral("Test"));
    QVERIFY(result.contains(QStringLiteral("physical")));

    QJsonArray modes = result.value(QStringLiteral("modes")).toArray();
    QCOMPARE(modes.count(), 1);
    QJsonObject mode = modes.at(0).toObject();
    QCOMPARE(mode.value(QStringLiteral("name")).toString(), QStringLiteral("2-channel"));
    QCOMPARE(mode.value(QStringLiteral("channelCount")).toInt(), 2);
    QJsonArray channels = mode.value(QStringLiteral("channels")).toArray();
    QCOMPARE(channels.count(), 2);
    QJsonObject ch0 = channels.at(0).toObject();
    QCOMPARE(ch0.value(QStringLiteral("index")).toInt(), 0);
    QCOMPARE(ch0.value(QStringLiteral("name")).toString(), QStringLiteral("Intensity"));
    QCOMPARE(ch0.value(QStringLiteral("group")).toString(), QStringLiteral("Intensity"));
    QCOMPARE(ch0.value(QStringLiteral("controlByte")).toString(), QStringLiteral("MSB"));
    QJsonObject ch1 = channels.at(1).toObject();
    QCOMPARE(ch1.value(QStringLiteral("index")).toInt(), 1);
    QCOMPARE(ch1.value(QStringLiteral("group")).toString(), QStringLiteral("Colour"));
    QCOMPARE(ch1.value(QStringLiteral("defaultValue")).toInt(), 12);
}

void ApiFixturesDomain_Test::defsGetModelUnknownIsNotFound()
{
    addAcmeTestParDefinition();
    helloAndGetClientId();
    QJsonObject params;
    params.insert(QStringLiteral("manufacturer"), QStringLiteral("Acme"));
    params.insert(QStringLiteral("model"), QStringLiteral("NoSuchPar"));
    QJsonObject reply = sendAndWaitForReply(QStringLiteral("fixtures.defs.getModel"), params);
    QCOMPARE(reply.value(QStringLiteral("ok")).toBool(), false);
    QCOMPARE(reply.value(QStringLiteral("error")).toObject().value(QStringLiteral("code")).toString(), QStringLiteral("NOT_FOUND"));
}

void ApiFixturesDomain_Test::defsGetModeReturnsChannelDetail()
{
    addAcmeTestParDefinition();
    helloAndGetClientId();
    QJsonObject params;
    params.insert(QStringLiteral("manufacturer"), QStringLiteral("Acme"));
    params.insert(QStringLiteral("model"), QStringLiteral("TestPar"));
    params.insert(QStringLiteral("mode"), QStringLiteral("2-channel"));
    QJsonObject reply = sendAndWaitForReply(QStringLiteral("fixtures.defs.getMode"), params);
    QCOMPARE(reply.value(QStringLiteral("ok")).toBool(), true);
    QJsonObject result = reply.value(QStringLiteral("result")).toObject();
    QCOMPARE(result.value(QStringLiteral("mode")).toString(), QStringLiteral("2-channel"));
    QCOMPARE(result.value(QStringLiteral("channelCount")).toInt(), 2);
    QCOMPARE(result.value(QStringLiteral("masterIntensityChannel")).toInt(), 0);
    QVERIFY(result.contains(QStringLiteral("heads")));
    QJsonArray channels = result.value(QStringLiteral("channels")).toArray();
    QCOMPARE(channels.count(), 2);
    QJsonArray capabilities = channels.at(0).toObject().value(QStringLiteral("capabilities")).toArray();
    QCOMPARE(capabilities.count(), 1);
    QCOMPARE(capabilities.at(0).toObject().value(QStringLiteral("min")).toInt(), 0);
    QCOMPARE(capabilities.at(0).toObject().value(QStringLiteral("max")).toInt(), 255);
    QCOMPARE(capabilities.at(0).toObject().value(QStringLiteral("name")).toString(), QStringLiteral("Dimmer"));

    params.insert(QStringLiteral("mode"), QStringLiteral("9-channel"));
    reply = sendAndWaitForReply(QStringLiteral("fixtures.defs.getMode"), params);
    QCOMPARE(reply.value(QStringLiteral("ok")).toBool(), false);
    QCOMPARE(reply.value(QStringLiteral("error")).toObject().value(QStringLiteral("code")).toString(), QStringLiteral("NOT_FOUND"));
}

void ApiFixturesDomain_Test::patchAcceptsFlatManufacturerModelMode()
{
    addAcmeTestParDefinition();
    helloAndGetClientId();

    // Web UI contract spelling: no "definition" object, the fields are flat
    QJsonObject params;
    params.insert(QStringLiteral("manufacturer"), QStringLiteral("Acme"));
    params.insert(QStringLiteral("model"), QStringLiteral("TestPar"));
    params.insert(QStringLiteral("mode"), QStringLiteral("2-channel"));
    params.insert(QStringLiteral("universe"), 0);
    params.insert(QStringLiteral("address"), 100);
    params.insert(QStringLiteral("quantity"), 2);
    params.insert(QStringLiteral("gap"), 1);
    params.insert(QStringLiteral("name"), QStringLiteral("Flat Par"));
    params.insert(QStringLiteral("baseRevision"), int(m_doc->docRevision()));
    QJsonObject reply = sendAndWaitForReply(QStringLiteral("fixtures.patch"), params);
    QCOMPARE(reply.value(QStringLiteral("ok")).toBool(), true);
    QJsonArray fixtureIds = reply.value(QStringLiteral("result")).toObject().value(QStringLiteral("fixtureIds")).toArray();
    QCOMPARE(fixtureIds.count(), 2);
    QCOMPARE(m_doc->fixtures().count(), 2);
    QList<Fixture *> patched = m_doc->fixtures();
    QCOMPARE(patched.at(0)->fixtureDef()->model(), QStringLiteral("TestPar"));
    QCOMPARE(patched.at(0)->fixtureMode()->name(), QStringLiteral("2-channel"));
    QCOMPARE(patched.at(0)->address(), quint32(100));
    QCOMPARE(patched.at(1)->address(), quint32(103)); // 2 channels + gap 1
}

QTEST_MAIN(ApiFixturesDomain_Test)
