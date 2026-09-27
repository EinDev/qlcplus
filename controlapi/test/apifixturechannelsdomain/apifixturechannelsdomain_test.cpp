/*
  Q Light Controller Plus - Control API unit test
  apifixturechannelsdomain_test.cpp

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
#include <QTemporaryDir>
#include <QWebSocket>
#include <QtTest>

#include "apifixturechannelsdomain_test.h"
#include "apiserver.h"
#include "qlcmodifierscache.h"
#include "channelmodifier.h"
#include "inputoutputmap.h"
#include "universe.h"
#include "fixture.h"
#include "doc.h"

static QString buildRequest(const QString &method, const QJsonObject &params, const QString &id)
{
    QJsonObject obj;
    obj.insert(QStringLiteral("type"), QStringLiteral("request"));
    obj.insert(QStringLiteral("id"), id);
    obj.insert(QStringLiteral("method"), method);
    obj.insert(QStringLiteral("params"), params);
    return QString::fromUtf8(QJsonDocument(obj).toJson(QJsonDocument::Compact));
}

static QString errorCode(const QJsonObject &reply)
{
    return reply.value(QStringLiteral("error")).toObject().value(QStringLiteral("code")).toString();
}

void ApiFixtureChannelsDomain_Test::initTestCase()
{
    m_modifiersDir = nullptr;
}

void ApiFixtureChannelsDomain_Test::cleanupTestCase()
{
    qunsetenv("QLCPLUS_USER_MODIFIERS_DIR");
}

void ApiFixtureChannelsDomain_Test::init()
{
    // A fresh, empty user modifiers folder per test.
    m_modifiersDir = new QTemporaryDir();
    QVERIFY(m_modifiersDir->isValid());
    qputenv("QLCPLUS_USER_MODIFIERS_DIR", m_modifiersDir->path().toUtf8());

    m_doc = new Doc(nullptr);
    // App loads the caches at startup; the test does it by hand. A system
    // template is added directly (the test binary has no system folder).
    m_doc->modifiersCache()->load(QLCModifiersCache::userTemplateDirectory());
    ChannelModifier *system = new ChannelModifier();
    system->setName(QStringLiteral("Always_Full"));
    system->setType(ChannelModifier::SystemTemplate);
    system->setModifierMap(QList<QPair<uchar, uchar>>() << qMakePair(uchar(0), uchar(255)) << qMakePair(uchar(255), uchar(255)));
    QVERIFY(m_doc->modifiersCache()->addModifier(system));

    m_apiServer = new ApiServer(nullptr, m_doc);
    QVERIFY(m_apiServer->listen(0, QHostAddress::LocalHost));
    m_client = new QWebSocket();
    m_client->open(QUrl(QStringLiteral("ws://127.0.0.1:%1/qlcplusapi").arg(m_apiServer->serverPort())));
    QVERIFY(QTest::qWaitFor([this]() { return m_client->state() == QAbstractSocket::ConnectedState; }, 2000));
    sendAndWaitForReply(QStringLiteral("hello"), QJsonObject());
}

void ApiFixtureChannelsDomain_Test::cleanup()
{
    delete m_client;
    m_client = nullptr;
    delete m_apiServer;
    m_apiServer = nullptr;
    delete m_doc;
    m_doc = nullptr;
    delete m_modifiersDir;
    m_modifiersDir = nullptr;
}

QJsonObject ApiFixtureChannelsDomain_Test::sendAndWaitForReply(const QString &method, const QJsonObject &params)
{
    static int counter = 0;
    const QString requestId = QStringLiteral("t-%1").arg(++counter);
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

quint32 ApiFixtureChannelsDomain_Test::patchGeneric(int address, int channels, int quantity)
{
    QJsonObject generic;
    generic.insert(QStringLiteral("channels"), channels);
    QJsonObject definition;
    definition.insert(QStringLiteral("generic"), generic);
    QJsonObject params;
    params.insert(QStringLiteral("universe"), 0);
    params.insert(QStringLiteral("address"), address);
    params.insert(QStringLiteral("definition"), definition);
    params.insert(QStringLiteral("quantity"), quantity);
    params.insert(QStringLiteral("baseRevision"), int(m_doc->docRevision()));
    QJsonObject reply = sendAndWaitForReply(QStringLiteral("fixtures.patch"), params);
    QJsonArray ids = reply.value(QStringLiteral("result")).toObject().value(QStringLiteral("fixtureIds")).toArray();
    return ids.isEmpty() ? Fixture::invalidId() : ids.at(0).toString().toUInt();
}

QJsonObject ApiFixtureChannelsDomain_Test::saveTemplate(const QString &name, const QList<QPair<int, int>> &points)
{
    QJsonArray pts;
    for (const QPair<int, int> &p : points)
    {
        QJsonObject pt;
        pt.insert(QStringLiteral("original"), p.first);
        pt.insert(QStringLiteral("modified"), p.second);
        pts.append(pt);
    }
    QJsonObject params;
    params.insert(QStringLiteral("name"), name);
    params.insert(QStringLiteral("points"), pts);
    return sendAndWaitForReply(QStringLiteral("fixtures.modifiers.save"), params);
}

static QJsonObject behaviour(quint32 fixtureId, int channel, quint32 revision)
{
    QJsonObject params;
    params.insert(QStringLiteral("fixtureId"), QString::number(fixtureId));
    params.insert(QStringLiteral("channel"), channel);
    params.insert(QStringLiteral("baseRevision"), int(revision));
    return params;
}

/* ------------------------------------------------------------------ */

void ApiFixtureChannelsDomain_Test::userDirectoryFollowsEnvironment()
{
    QCOMPARE(QDir(QLCModifiersCache::userTemplateDirectory().absolutePath()),
             QDir(m_modifiersDir->path()));
}

void ApiFixtureChannelsDomain_Test::setBehaviourForcesLtpAndExcludesFade()
{
    quint32 fx = patchGeneric(0, 2);
    quint32 before = m_doc->docRevision();
    QJsonObject params = behaviour(fx, 1, before);
    params.insert(QStringLiteral("precedence"), QStringLiteral("ltp"));
    params.insert(QStringLiteral("canFade"), false);

    QJsonObject reply = sendAndWaitForReply(QStringLiteral("fixtures.channel.setBehaviour"), params);
    QCOMPARE(reply.value(QStringLiteral("ok")).toBool(), true);
    QVERIFY(quint32(reply.value(QStringLiteral("result")).toObject().value(QStringLiteral("docRevision")).toInt()) > before);

    Fixture *fixture = m_doc->fixture(fx);
    QCOMPARE(fixture->forcedLTPChannels(), QList<int>() << 1);
    QCOMPARE(fixture->channelCanFade(1), false);
    QCOMPARE(fixture->channelCanFade(0), true);

    // The universe knows the channel is LTP now.
    QList<Universe *> universes = m_doc->inputOutputMap()->claimUniverses();
    uchar caps = universes.at(0)->channelCapabilities(1);
    m_doc->inputOutputMap()->releaseUniverses(false);
    QVERIFY(caps & Universe::LTP);
    QVERIFY((caps & Universe::HTP) == 0);

    // fixtures.get reports it; back to auto clears it.
    QJsonObject getParams;
    getParams.insert(QStringLiteral("fixtureId"), QString::number(fx));
    QJsonObject ch1 = sendAndWaitForReply(QStringLiteral("fixtures.get"), getParams).value(QStringLiteral("result")).toObject()
                          .value(QStringLiteral("channelList")).toArray().at(1).toObject();
    QCOMPARE(ch1.value(QStringLiteral("precedence")).toString(), QStringLiteral("ltp"));
    QCOMPARE(ch1.value(QStringLiteral("canFade")).toBool(), false);

    params = behaviour(fx, 1, m_doc->docRevision());
    params.insert(QStringLiteral("precedence"), QStringLiteral("auto"));
    QCOMPARE(sendAndWaitForReply(QStringLiteral("fixtures.channel.setBehaviour"), params).value(QStringLiteral("ok")).toBool(), true);
    QVERIFY(fixture->forcedLTPChannels().isEmpty());
}

void ApiFixtureChannelsDomain_Test::setBehaviourRejectsImpossiblePrecedence()
{
    // Generic dimmer channels are intensity: HTP by nature, only LTP can be forced.
    quint32 fx = patchGeneric(0, 1);
    quint32 before = m_doc->docRevision();
    QJsonObject params = behaviour(fx, 0, before);
    params.insert(QStringLiteral("precedence"), QStringLiteral("htp"));
    QJsonObject reply = sendAndWaitForReply(QStringLiteral("fixtures.channel.setBehaviour"), params);
    QCOMPARE(reply.value(QStringLiteral("ok")).toBool(), false);
    QCOMPARE(errorCode(reply), QStringLiteral("INVALID_PARAMS"));
    QCOMPARE(m_doc->docRevision(), before);

    params = behaviour(fx, 5, before);
    params.insert(QStringLiteral("canFade"), false);
    QCOMPARE(errorCode(sendAndWaitForReply(QStringLiteral("fixtures.channel.setBehaviour"), params)), QStringLiteral("INVALID_PARAMS"));
}

void ApiFixtureChannelsDomain_Test::setBehaviourAppliesToSameType()
{
    // One bulk patch shares one generic definition + mode across instances.
    quint32 first = patchGeneric(0, 3, 3);
    quint32 other = patchGeneric(100, 4);
    QList<Fixture *> sameType;
    for (Fixture *f : m_doc->fixtures())
        if (f->id() != other)
            sameType.append(f);
    QCOMPARE(sameType.count(), 3);

    QJsonObject params = behaviour(first, 2, m_doc->docRevision());
    params.insert(QStringLiteral("canFade"), false);
    params.insert(QStringLiteral("applyToSameType"), true);
    QJsonObject reply = sendAndWaitForReply(QStringLiteral("fixtures.channel.setBehaviour"), params);
    QCOMPARE(reply.value(QStringLiteral("ok")).toBool(), true);
    QCOMPARE(reply.value(QStringLiteral("result")).toObject().value(QStringLiteral("fixtureIds")).toArray().count(), 3);
    for (Fixture *f : sameType)
        QCOMPARE(f->channelCanFade(2), false);
    QCOMPARE(m_doc->fixture(other)->channelCanFade(2), true);
}

void ApiFixtureChannelsDomain_Test::setBehaviourWithStaleRevisionConflicts()
{
    quint32 fx = patchGeneric(0, 1);
    QJsonObject params = behaviour(fx, 0, m_doc->docRevision() + 5);
    params.insert(QStringLiteral("canFade"), false);
    QCOMPARE(errorCode(sendAndWaitForReply(QStringLiteral("fixtures.channel.setBehaviour"), params)), QStringLiteral("CONFLICT"));
    QCOMPARE(m_doc->fixture(fx)->channelCanFade(0), true);
}

void ApiFixtureChannelsDomain_Test::setBehaviourAttachesModifierToUniverse()
{
    quint32 fx = patchGeneric(10, 2);
    QJsonObject params = behaviour(fx, 1, m_doc->docRevision());
    params.insert(QStringLiteral("modifier"), QStringLiteral("Always_Full"));
    QCOMPARE(sendAndWaitForReply(QStringLiteral("fixtures.channel.setBehaviour"), params).value(QStringLiteral("ok")).toBool(), true);

    ChannelModifier *mod = m_doc->modifiersCache()->modifier(QStringLiteral("Always_Full"));
    QCOMPARE(m_doc->fixture(fx)->channelModifier(1), mod);
    QList<Universe *> universes = m_doc->inputOutputMap()->claimUniverses();
    ChannelModifier *onUniverse = universes.at(0)->channelModifier(11);
    m_doc->inputOutputMap()->releaseUniverses(false);
    QCOMPARE(onUniverse, mod);

    // null detaches it again.
    params = behaviour(fx, 1, m_doc->docRevision());
    params.insert(QStringLiteral("modifier"), QJsonValue());
    QCOMPARE(sendAndWaitForReply(QStringLiteral("fixtures.channel.setBehaviour"), params).value(QStringLiteral("ok")).toBool(), true);
    QVERIFY(m_doc->fixture(fx)->channelModifier(1) == nullptr);
}

void ApiFixtureChannelsDomain_Test::setBehaviourUnknownModifierIsNotFound()
{
    quint32 fx = patchGeneric(0, 1);
    QJsonObject params = behaviour(fx, 0, m_doc->docRevision());
    params.insert(QStringLiteral("modifier"), QStringLiteral("Nope"));
    QCOMPARE(errorCode(sendAndWaitForReply(QStringLiteral("fixtures.channel.setBehaviour"), params)), QStringLiteral("NOT_FOUND"));
}

void ApiFixtureChannelsDomain_Test::modifiersSaveWritesUserFileAndLists()
{
    QJsonObject reply = saveTemplate(QStringLiteral("My Curve"), { {0, 0}, {128, 32}, {255, 255} });
    QCOMPARE(reply.value(QStringLiteral("ok")).toBool(), true);
    QCOMPARE(reply.value(QStringLiteral("result")).toObject().value(QStringLiteral("created")).toBool(), true);
    QVERIFY(QFile::exists(m_modifiersDir->filePath(QStringLiteral("My Curve.qxmt"))));

    QJsonArray templates = sendAndWaitForReply(QStringLiteral("fixtures.modifiers.list"), QJsonObject())
                               .value(QStringLiteral("result")).toObject().value(QStringLiteral("templates")).toArray();
    QCOMPARE(templates.count(), 2);
    QCOMPARE(templates.at(0).toObject().value(QStringLiteral("name")).toString(), QStringLiteral("Always_Full"));
    QCOMPARE(templates.at(0).toObject().value(QStringLiteral("isUser")).toBool(), false);
    QCOMPARE(templates.at(1).toObject().value(QStringLiteral("name")).toString(), QStringLiteral("My Curve"));
    QCOMPARE(templates.at(1).toObject().value(QStringLiteral("isUser")).toBool(), true);

    QJsonObject params;
    params.insert(QStringLiteral("name"), QStringLiteral("My Curve"));
    QJsonArray points = sendAndWaitForReply(QStringLiteral("fixtures.modifiers.get"), params)
                            .value(QStringLiteral("result")).toObject().value(QStringLiteral("points")).toArray();
    QCOMPARE(points.count(), 3);
    QCOMPARE(points.at(1).toObject().value(QStringLiteral("original")).toInt(), 128);
    QCOMPARE(points.at(1).toObject().value(QStringLiteral("modified")).toInt(), 32);

    // The written file loads back as the same curve.
    ChannelModifier reloaded;
    QCOMPARE(reloaded.loadXML(m_modifiersDir->filePath(QStringLiteral("My Curve.qxmt")), ChannelModifier::UserTemplate), QFile::NoError);
    QCOMPARE(reloaded.name(), QStringLiteral("My Curve"));
    QCOMPARE(reloaded.getValue(128), uchar(32));
}

void ApiFixtureChannelsDomain_Test::modifiersSaveRejectsSystemTemplateAndBadPoints()
{
    QCOMPARE(errorCode(saveTemplate(QStringLiteral("Always_Full"), { {0, 0}, {255, 0} })), QStringLiteral("INVALID_PARAMS"));
    QCOMPARE(errorCode(saveTemplate(QStringLiteral("None"), { {0, 0}, {255, 0} })), QStringLiteral("INVALID_PARAMS"));
    QCOMPARE(errorCode(saveTemplate(QStringLiteral("a/b"), { {0, 0}, {255, 0} })), QStringLiteral("INVALID_PARAMS"));
    QCOMPARE(errorCode(saveTemplate(QStringLiteral("Bad"), { {10, 0}, {255, 0} })), QStringLiteral("INVALID_PARAMS"));
    QCOMPARE(errorCode(saveTemplate(QStringLiteral("Bad"), { {0, 0}, {200, 5}, {100, 5}, {255, 0} })), QStringLiteral("INVALID_PARAMS"));
    QCOMPARE(errorCode(saveTemplate(QStringLiteral("Bad"), { {0, 0}, {255, 300} })), QStringLiteral("INVALID_PARAMS"));
    QVERIFY(QDir(m_modifiersDir->path()).entryList(QDir::Files).isEmpty());
}

void ApiFixtureChannelsDomain_Test::modifiersSaveUpdatesAttachedModifierInPlace()
{
    QCOMPARE(saveTemplate(QStringLiteral("Curve"), { {0, 0}, {255, 255} }).value(QStringLiteral("ok")).toBool(), true);
    ChannelModifier *mod = m_doc->modifiersCache()->modifier(QStringLiteral("Curve"));
    quint32 fx = patchGeneric(0, 1);
    QJsonObject params = behaviour(fx, 0, m_doc->docRevision());
    params.insert(QStringLiteral("modifier"), QStringLiteral("Curve"));
    QCOMPARE(sendAndWaitForReply(QStringLiteral("fixtures.channel.setBehaviour"), params).value(QStringLiteral("ok")).toBool(), true);

    QJsonObject reply = saveTemplate(QStringLiteral("Curve"), { {0, 255}, {255, 0} });
    QCOMPARE(reply.value(QStringLiteral("result")).toObject().value(QStringLiteral("created")).toBool(), false);
    QCOMPARE(m_doc->modifiersCache()->modifier(QStringLiteral("Curve")), mod);
    QCOMPARE(m_doc->fixture(fx)->channelModifier(0)->getValue(0), uchar(255));
}

void ApiFixtureChannelsDomain_Test::modifiersRenameMovesFileAndFollowsFixtures()
{
    QCOMPARE(saveTemplate(QStringLiteral("Old"), { {0, 0}, {255, 128} }).value(QStringLiteral("ok")).toBool(), true);
    quint32 fx = patchGeneric(0, 1);
    QJsonObject params = behaviour(fx, 0, m_doc->docRevision());
    params.insert(QStringLiteral("modifier"), QStringLiteral("Old"));
    QCOMPARE(sendAndWaitForReply(QStringLiteral("fixtures.channel.setBehaviour"), params).value(QStringLiteral("ok")).toBool(), true);
    quint32 before = m_doc->docRevision();

    QJsonObject rename;
    rename.insert(QStringLiteral("name"), QStringLiteral("Old"));
    rename.insert(QStringLiteral("newName"), QStringLiteral("New"));
    QJsonObject reply = sendAndWaitForReply(QStringLiteral("fixtures.modifiers.rename"), rename);
    QCOMPARE(reply.value(QStringLiteral("ok")).toBool(), true);
    QVERIFY(m_doc->docRevision() > before); // the project stores the name
    QVERIFY(QFile::exists(m_modifiersDir->filePath(QStringLiteral("New.qxmt"))));
    QVERIFY(QFile::exists(m_modifiersDir->filePath(QStringLiteral("Old.qxmt"))) == false);
    QVERIFY(m_doc->modifiersCache()->modifier(QStringLiteral("Old")) == nullptr);
    QCOMPARE(m_doc->fixture(fx)->channelModifier(0)->name(), QStringLiteral("New"));

    rename.insert(QStringLiteral("name"), QStringLiteral("Always_Full"));
    rename.insert(QStringLiteral("newName"), QStringLiteral("Mine"));
    QCOMPARE(errorCode(sendAndWaitForReply(QStringLiteral("fixtures.modifiers.rename"), rename)), QStringLiteral("INVALID_PARAMS"));
}

void ApiFixtureChannelsDomain_Test::modifiersDeleteDetachesFixtures()
{
    QCOMPARE(saveTemplate(QStringLiteral("Gone"), { {0, 0}, {255, 64} }).value(QStringLiteral("ok")).toBool(), true);
    quint32 fx = patchGeneric(20, 2);
    QJsonObject params = behaviour(fx, 1, m_doc->docRevision());
    params.insert(QStringLiteral("modifier"), QStringLiteral("Gone"));
    QCOMPARE(sendAndWaitForReply(QStringLiteral("fixtures.channel.setBehaviour"), params).value(QStringLiteral("ok")).toBool(), true);
    quint32 before = m_doc->docRevision();

    QJsonObject del;
    del.insert(QStringLiteral("name"), QStringLiteral("Gone"));
    QJsonObject reply = sendAndWaitForReply(QStringLiteral("fixtures.modifiers.delete"), del);
    QCOMPARE(reply.value(QStringLiteral("ok")).toBool(), true);
    QCOMPARE(reply.value(QStringLiteral("result")).toObject().value(QStringLiteral("detachedFixtureIds")).toArray().count(), 1);
    QVERIFY(m_doc->docRevision() > before);
    QVERIFY(m_doc->fixture(fx)->channelModifier(1) == nullptr);
    QVERIFY(m_doc->modifiersCache()->modifier(QStringLiteral("Gone")) == nullptr);
    QVERIFY(QFile::exists(m_modifiersDir->filePath(QStringLiteral("Gone.qxmt"))) == false);

    QList<Universe *> universes = m_doc->inputOutputMap()->claimUniverses();
    ChannelModifier *onUniverse = universes.at(0)->channelModifier(21);
    m_doc->inputOutputMap()->releaseUniverses(false);
    QVERIFY(onUniverse == nullptr);
}

void ApiFixtureChannelsDomain_Test::modifiersDeleteSystemTemplateIsRejected()
{
    QJsonObject del;
    del.insert(QStringLiteral("name"), QStringLiteral("Always_Full"));
    QCOMPARE(errorCode(sendAndWaitForReply(QStringLiteral("fixtures.modifiers.delete"), del)), QStringLiteral("INVALID_PARAMS"));
    QVERIFY(m_doc->modifiersCache()->modifier(QStringLiteral("Always_Full")) != nullptr);
    del.insert(QStringLiteral("name"), QStringLiteral("Unknown"));
    QCOMPARE(errorCode(sendAndWaitForReply(QStringLiteral("fixtures.modifiers.delete"), del)), QStringLiteral("NOT_FOUND"));
}

void ApiFixtureChannelsDomain_Test::colorFiltersListAnswers()
{
    QJsonObject reply = sendAndWaitForReply(QStringLiteral("fixtures.colorFilters.list"), QJsonObject());
    QCOMPARE(reply.value(QStringLiteral("ok")).toBool(), true);
    QVERIFY(reply.value(QStringLiteral("result")).toObject().value(QStringLiteral("files")).isArray());
}

QTEST_MAIN(ApiFixtureChannelsDomain_Test)
