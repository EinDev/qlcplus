/*
  Q Light Controller Plus - Control API unit test
  apifixturedefsdomain_test.cpp

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

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QSignalSpy>
#include <QWebSocket>
#include <QtTest>

#include "apifixturedefsdomain_test.h"
#include "apiserver.h"
#include "doc.h"
#include "fixture.h"
#include "qlcfixturedefcache.h"
#include "qlcfixturedef.h"
#include "qlcfixturemode.h"
#include "qlcchannel.h"
#include "qlccapability.h"
#include "qlcconfig.h"
#include "qlcfile.h"

namespace {

const QString SysMan = QStringLiteral("SysMan");
const QString SysModel = QStringLiteral("SysModel");

QString buildRequest(const QString &method, const QJsonObject &params, const QString &id)
{
    QJsonObject obj;
    obj.insert(QStringLiteral("type"), QStringLiteral("request"));
    obj.insert(QStringLiteral("id"), id);
    obj.insert(QStringLiteral("method"), method);
    obj.insert(QStringLiteral("params"), params);
    return QString::fromUtf8(QJsonDocument(obj).toJson(QJsonDocument::Compact));
}

QJsonObject frameToObject(const QList<QVariant> &frame)
{
    return QJsonDocument::fromJson(frame.at(0).toString().toUtf8()).object();
}

/** A bundled-style definition: one dimmer channel, one mode, isUser=false. */
QLCFixtureDef *makeSystemDef()
{
    QLCFixtureDef *def = new QLCFixtureDef();
    def->setManufacturer(SysMan);
    def->setModel(SysModel);
    def->setType(QLCFixtureDef::Dimmer);
    def->setAuthor(QStringLiteral("QLC+ tests"));
    QLCChannel *dimmer = new QLCChannel();
    dimmer->setName(QStringLiteral("Dimmer"));
    dimmer->setGroup(QLCChannel::Intensity);
    dimmer->addCapability(new QLCCapability(0, 255, QStringLiteral("Intensity")));
    def->addChannel(dimmer);
    QLCFixtureMode *mode = new QLCFixtureMode(def);
    mode->setName(QStringLiteral("1 Channel"));
    mode->insertChannel(dimmer, 0);
    def->addMode(mode);
    def->setIsUser(false);
    def->setLoaded(true);
    return def;
}

QJsonObject findChannel(const QJsonObject &definition, const QString &channelId)
{
    for (const QJsonValue &v : definition.value(QStringLiteral("channels")).toArray())
    {
        if (v.toObject().value(QStringLiteral("channelId")).toString() == channelId)
            return v.toObject();
    }
    return QJsonObject();
}

QJsonObject findMode(const QJsonObject &definition, const QString &modeId)
{
    for (const QJsonValue &v : definition.value(QStringLiteral("modes")).toArray())
    {
        if (v.toObject().value(QStringLiteral("modeId")).toString() == modeId)
            return v.toObject();
    }
    return QJsonObject();
}

QJsonArray strings(const QStringList &list)
{
    return QJsonArray::fromStringList(list);
}

} // namespace

void ApiFixtureDefsDomain_Test::init()
{
    m_userDir = new QTemporaryDir();
    QVERIFY(m_userDir->isValid());
    QLCFixtureDefCache::setUserDefinitionDirectoryOverride(m_userDir->path());

    m_doc = new Doc(nullptr);
    QVERIFY(m_doc->fixtureDefCache()->addFixtureDef(makeSystemDef()));

    m_apiServer = new ApiServer(nullptr, m_doc);
    QVERIFY(m_apiServer->listen(0, QHostAddress::LocalHost));

    m_client = new QWebSocket();
    m_client->open(QUrl(QStringLiteral("ws://127.0.0.1:%1/qlcplusapi").arg(m_apiServer->serverPort())));
    QVERIFY(QTest::qWaitFor([this]() { return m_client->state() == QAbstractSocket::ConnectedState; }, 2000));
}

void ApiFixtureDefsDomain_Test::cleanup()
{
    delete m_client;
    m_client = nullptr;
    delete m_apiServer;
    m_apiServer = nullptr;
    delete m_doc;
    m_doc = nullptr;
    QLCFixtureDefCache::setUserDefinitionDirectoryOverride(QString());
    delete m_userDir;
    m_userDir = nullptr;
}

QJsonObject ApiFixtureDefsDomain_Test::sendAndWaitForReply(const QString &method, const QJsonObject &params, const QString &requestId)
{
    QSignalSpy spy(m_client, &QWebSocket::textMessageReceived);
    m_client->sendTextMessage(buildRequest(method, params, requestId));
    QJsonObject found;
    (void)QTest::qWaitFor([&]()
    {
        for (const QList<QVariant> &frame : spy)
        {
            QJsonObject obj = frameToObject(frame);
            if (obj.value(QStringLiteral("type")).toString() == QStringLiteral("response") &&
                obj.value(QStringLiteral("id")).toString() == requestId)
            {
                found = obj;
                return true;
            }
        }
        return false;
    }, 3000);
    return found;
}

QJsonObject ApiFixtureDefsDomain_Test::call(const QString &method, const QJsonObject &params)
{
    static int seq = 0;
    return sendAndWaitForReply(method, params, QStringLiteral("t-%1").arg(++seq));
}

QJsonObject ApiFixtureDefsDomain_Test::callOk(const QString &method, const QJsonObject &params)
{
    QJsonObject reply = call(method, params);
    if (reply.value(QStringLiteral("ok")).toBool() == false)
        qWarning() << method << "failed:" << QJsonDocument(reply).toJson(QJsonDocument::Compact);
    return reply.value(QStringLiteral("result")).toObject();
}

QString ApiFixtureDefsDomain_Test::callError(const QString &method, const QJsonObject &params, QJsonObject *details)
{
    QJsonObject reply = call(method, params);
    if (reply.value(QStringLiteral("ok")).toBool())
        return QStringLiteral("<ok>");
    QJsonObject error = reply.value(QStringLiteral("error")).toObject();
    if (details != nullptr)
        *details = error.value(QStringLiteral("details")).toObject();
    return error.value(QStringLiteral("code")).toString();
}

QString ApiFixtureDefsDomain_Test::hello()
{
    return callOk(QStringLiteral("hello"), QJsonObject()).value(QStringLiteral("clientId")).toString();
}

QJsonObject ApiFixtureDefsDomain_Test::waitForEvent(QSignalSpy &spy, const QString &topic)
{
    QJsonObject data;
    (void)QTest::qWaitFor([&]()
    {
        for (const QList<QVariant> &frame : spy)
        {
            QJsonObject obj = frameToObject(frame);
            if (obj.value(QStringLiteral("type")).toString() == QStringLiteral("event") &&
                obj.value(QStringLiteral("topic")).toString() == topic)
            {
                data = obj.value(QStringLiteral("data")).toObject();
                data.insert(QStringLiteral("__originClientId"), obj.value(QStringLiteral("originClientId")));
                return true;
            }
        }
        return false;
    }, 3000);
    return data;
}

QJsonObject ApiFixtureDefsDomain_Test::lastUpdatedDefinition(QSignalSpy &spy)
{
    QJsonObject definition;
    for (const QList<QVariant> &frame : spy)
    {
        QJsonObject obj = frameToObject(frame);
        if (obj.value(QStringLiteral("type")).toString() == QStringLiteral("event") &&
            obj.value(QStringLiteral("topic")).toString() == QStringLiteral("fixturedefs.session.updated"))
            definition = obj.value(QStringLiteral("data")).toObject().value(QStringLiteral("definition")).toObject();
    }
    return definition;
}

QJsonObject ApiFixtureDefsDomain_Test::openSession(const QString &manufacturer, const QString &model)
{
    QJsonObject p;
    p.insert(QStringLiteral("manufacturer"), manufacturer);
    p.insert(QStringLiteral("model"), model);
    return callOk(QStringLiteral("fixturedefs.session.open"), p);
}

QJsonObject ApiFixtureDefsDomain_Test::createSession()
{
    return callOk(QStringLiteral("fixturedefs.session.create"), QJsonObject());
}

QJsonObject ApiFixtureDefsDomain_Test::params(const QString &sessionId, int baseRevision)
{
    QJsonObject p;
    p.insert(QStringLiteral("sessionId"), sessionId);
    p.insert(QStringLiteral("baseRevision"), baseRevision);
    return p;
}

// ---------------------------------------------------------------------------

void ApiFixtureDefsDomain_Test::listIncludesSeededDefinition()
{
    hello();
    QJsonArray entries = callOk(QStringLiteral("fixturedefs.list"), QJsonObject()).value(QStringLiteral("entries")).toArray();
    QCOMPARE(entries.count(), 1);
    QJsonObject entry = entries.first().toObject();
    QCOMPARE(entry.value(QStringLiteral("manufacturer")).toString(), SysMan);
    QCOMPARE(entry.value(QStringLiteral("model")).toString(), SysModel);
    QCOMPARE(entry.value(QStringLiteral("type")).toString(), QStringLiteral("Dimmer"));
    QCOMPARE(entry.value(QStringLiteral("author")).toString(), QStringLiteral("QLC+ tests"));
    QCOMPARE(entry.value(QStringLiteral("isUser")).toBool(), false);
    QCOMPARE(entry.value(QStringLiteral("channelCount")).toInt(), 1);
    QCOMPARE(entry.value(QStringLiteral("modeCount")).toInt(), 1);
    QCOMPARE(entry.value(QStringLiteral("defRevision")).toInt(), 0);

    QJsonObject filter;
    filter.insert(QStringLiteral("manufacturer"), QStringLiteral("Nobody"));
    QCOMPARE(callOk(QStringLiteral("fixturedefs.list"), filter).value(QStringLiteral("entries")).toArray().count(), 0);
    filter.insert(QStringLiteral("manufacturer"), SysMan);
    QCOMPARE(callOk(QStringLiteral("fixturedefs.list"), filter).value(QStringLiteral("entries")).toArray().count(), 1);
}

void ApiFixtureDefsDomain_Test::getReturnsDefinitionWithIds()
{
    hello();
    QJsonObject p;
    p.insert(QStringLiteral("manufacturer"), SysMan);
    p.insert(QStringLiteral("model"), SysModel);
    QJsonObject result = callOk(QStringLiteral("fixturedefs.get"), p);
    QCOMPARE(result.value(QStringLiteral("defRevision")).toInt(), 0);
    QJsonObject def = result.value(QStringLiteral("definition")).toObject();
    QCOMPARE(def.value(QStringLiteral("manufacturer")).toString(), SysMan);
    QCOMPARE(def.value(QStringLiteral("isUser")).toBool(), false);
    QVERIFY(def.value(QStringLiteral("sourceFile")).isNull()); // in-memory seed has no file
    QJsonArray channels = def.value(QStringLiteral("channels")).toArray();
    QCOMPARE(channels.count(), 1);
    QJsonObject ch = channels.first().toObject();
    QCOMPARE(ch.value(QStringLiteral("channelId")).toString(), QStringLiteral("ch-1"));
    QCOMPARE(ch.value(QStringLiteral("group")).toString(), QStringLiteral("Intensity"));
    QCOMPARE(ch.value(QStringLiteral("colour")).toString(), QStringLiteral("Generic"));
    QCOMPARE(ch.value(QStringLiteral("controlByte")).toString(), QStringLiteral("MSB"));
    QJsonObject cap = ch.value(QStringLiteral("capabilities")).toArray().first().toObject();
    QCOMPARE(cap.value(QStringLiteral("min")).toInt(), 0);
    QCOMPARE(cap.value(QStringLiteral("max")).toInt(), 255);
    QCOMPARE(cap.value(QStringLiteral("preset")).toString(), QStringLiteral("Custom"));
    QCOMPARE(cap.value(QStringLiteral("warning")).toString(), QStringLiteral("NoWarning"));
    QJsonObject mode = def.value(QStringLiteral("modes")).toArray().first().toObject();
    QCOMPARE(mode.value(QStringLiteral("modeId")).toString(), QStringLiteral("mode-1"));
    QCOMPARE(mode.value(QStringLiteral("useGlobalPhysical")).toBool(), true);
    QVERIFY(mode.value(QStringLiteral("physical")).isNull());
    QJsonObject slot = mode.value(QStringLiteral("channels")).toArray().first().toObject();
    QCOMPARE(slot.value(QStringLiteral("channelId")).toString(), QStringLiteral("ch-1"));
    QVERIFY(slot.value(QStringLiteral("actsOnChannelId")).isNull());
    QVERIFY(def.value(QStringLiteral("physical")).toObject().contains(QStringLiteral("bulbColourTemperature")));
}

void ApiFixtureDefsDomain_Test::getMissingIsNotFound()
{
    hello();
    QJsonObject p;
    p.insert(QStringLiteral("manufacturer"), QStringLiteral("Generic"));
    p.insert(QStringLiteral("model"), QStringLiteral("Generic"));
    QCOMPARE(callError(QStringLiteral("fixturedefs.get"), p), QStringLiteral("NOT_FOUND"));
}

void ApiFixtureDefsDomain_Test::sessionCreateIsBlankUserSession()
{
    QString clientId = hello();
    QSignalSpy spy(m_client, &QWebSocket::textMessageReceived);
    QJsonObject result = createSession();
    QVERIFY(result.value(QStringLiteral("sessionId")).toString().startsWith(QStringLiteral("fxs-")));
    QCOMPARE(result.value(QStringLiteral("sessionRevision")).toInt(), 0);
    QCOMPARE(result.value(QStringLiteral("isUser")).toBool(), true);
    QVERIFY(result.value(QStringLiteral("baseRevision")).isNull());
    QJsonObject def = result.value(QStringLiteral("definition")).toObject();
    QCOMPARE(def.value(QStringLiteral("channels")).toArray().count(), 0);
    QCOMPARE(def.value(QStringLiteral("modes")).toArray().count(), 0);
    QVERIFY(def.value(QStringLiteral("sourceFile")).isNull());

    QJsonObject event = waitForEvent(spy, QStringLiteral("fixturedefs.session.opened"));
    QCOMPARE(event.value(QStringLiteral("source")).toString(), QStringLiteral("created"));
    QCOMPARE(event.value(QStringLiteral("sessionId")).toString(), result.value(QStringLiteral("sessionId")).toString());
    QCOMPARE(event.value(QStringLiteral("__originClientId")).toString(), clientId);
}

void ApiFixtureDefsDomain_Test::sessionOpenClonesLibraryDefinition()
{
    hello();
    QSignalSpy spy(m_client, &QWebSocket::textMessageReceived);
    QJsonObject result = openSession(SysMan, SysModel);
    QCOMPARE(result.value(QStringLiteral("isUser")).toBool(), false);
    QCOMPARE(result.value(QStringLiteral("baseRevision")).toInt(), 0);
    QCOMPARE(result.value(QStringLiteral("sessionRevision")).toInt(), 0);
    QJsonObject def = result.value(QStringLiteral("definition")).toObject();
    QCOMPARE(def.value(QStringLiteral("channels")).toArray().count(), 1);
    QCOMPARE(def.value(QStringLiteral("modes")).toArray().count(), 1);
    QCOMPARE(waitForEvent(spy, QStringLiteral("fixturedefs.session.opened")).value(QStringLiteral("source")).toString(),
             QStringLiteral("opened"));

    // Editing the session must not touch the library copy.
    QString sid = result.value(QStringLiteral("sessionId")).toString();
    QJsonObject p = params(sid, 0);
    p.insert(QStringLiteral("model"), QStringLiteral("Renamed"));
    callOk(QStringLiteral("fixturedefs.session.update"), p);
    QVERIFY(m_doc->fixtureDefCache()->fixtureDef(SysMan, SysModel) != nullptr);
    QCOMPARE(m_doc->fixtureDefCache()->fixtureDef(SysMan, SysModel)->model(), SysModel);

    QJsonObject missing;
    missing.insert(QStringLiteral("manufacturer"), QStringLiteral("X"));
    missing.insert(QStringLiteral("model"), QStringLiteral("Y"));
    QCOMPARE(callError(QStringLiteral("fixturedefs.session.open"), missing), QStringLiteral("NOT_FOUND"));
}

void ApiFixtureDefsDomain_Test::sessionListAndClose()
{
    QString clientId = hello();
    QString a = createSession().value(QStringLiteral("sessionId")).toString();
    QString b = openSession(SysMan, SysModel).value(QStringLiteral("sessionId")).toString();

    QJsonArray sessions = callOk(QStringLiteral("fixturedefs.session.list"), QJsonObject()).value(QStringLiteral("sessions")).toArray();
    QCOMPARE(sessions.count(), 2);
    QJsonObject infoB;
    for (const QJsonValue &v : sessions)
        if (v.toObject().value(QStringLiteral("sessionId")).toString() == b)
            infoB = v.toObject();
    QCOMPARE(infoB.value(QStringLiteral("manufacturer")).toString(), SysMan);
    QCOMPARE(infoB.value(QStringLiteral("isModified")).toBool(), false);
    QCOMPARE(infoB.value(QStringLiteral("isUser")).toBool(), false);
    QCOMPARE(infoB.value(QStringLiteral("baseRevision")).toInt(), 0);

    QSignalSpy spy(m_client, &QWebSocket::textMessageReceived);
    QJsonObject closeParams;
    closeParams.insert(QStringLiteral("sessionId"), a);
    QCOMPARE(callOk(QStringLiteral("fixturedefs.session.close"), closeParams).value(QStringLiteral("sessionId")).toString(), a);
    QJsonObject event = waitForEvent(spy, QStringLiteral("fixturedefs.session.closed"));
    QCOMPARE(event.value(QStringLiteral("sessionId")).toString(), a);
    QCOMPARE(event.value(QStringLiteral("__originClientId")).toString(), clientId);

    sessions = callOk(QStringLiteral("fixturedefs.session.list"), QJsonObject()).value(QStringLiteral("sessions")).toArray();
    QCOMPARE(sessions.count(), 1);
    QCOMPARE(sessions.first().toObject().value(QStringLiteral("sessionId")).toString(), b);
    QCOMPARE(callError(QStringLiteral("fixturedefs.session.close"), closeParams), QStringLiteral("NOT_FOUND"));
}

void ApiFixtureDefsDomain_Test::sessionGetReturnsSnapshot()
{
    hello();
    QString sid = openSession(SysMan, SysModel).value(QStringLiteral("sessionId")).toString();

    // Untouched: the snapshot equals what session.open answered, and reading is not a mutation.
    QJsonObject sidOnly;
    sidOnly.insert(QStringLiteral("sessionId"), sid);
    QJsonObject snap = callOk(QStringLiteral("fixturedefs.session.get"), sidOnly);
    QCOMPARE(snap.value(QStringLiteral("sessionId")).toString(), sid);
    QCOMPARE(snap.value(QStringLiteral("sessionRevision")).toInt(), 0);
    QCOMPARE(snap.value(QStringLiteral("isModified")).toBool(), false);
    QCOMPARE(snap.value(QStringLiteral("isUser")).toBool(), false);
    QCOMPARE(snap.value(QStringLiteral("baseRevision")).toInt(), 0);
    QCOMPARE(snap.value(QStringLiteral("definition")).toObject().value(QStringLiteral("model")).toString(), SysModel);

    // After an edit the snapshot carries the new state and revision.
    QJsonObject p = params(sid, 0);
    p.insert(QStringLiteral("model"), QStringLiteral("Renamed"));
    callOk(QStringLiteral("fixturedefs.session.update"), p);
    QJsonObject added = callOk(QStringLiteral("fixturedefs.channel.add"), params(sid, 1));
    snap = callOk(QStringLiteral("fixturedefs.session.get"), sidOnly);
    QCOMPARE(snap.value(QStringLiteral("sessionRevision")).toInt(), 2);
    QCOMPARE(snap.value(QStringLiteral("isModified")).toBool(), true);
    QJsonObject def = snap.value(QStringLiteral("definition")).toObject();
    QCOMPARE(def.value(QStringLiteral("model")).toString(), QStringLiteral("Renamed"));
    QCOMPARE(def.value(QStringLiteral("channels")).toArray().count(), 2);
    bool sawNewChannel = false;
    for (const QJsonValue &v : def.value(QStringLiteral("channels")).toArray())
        if (v.toObject().value(QStringLiteral("channelId")).toString() == added.value(QStringLiteral("channelId")).toString())
            sawNewChannel = true;
    QVERIFY(sawNewChannel);
    // A get never bumps the revision.
    snap = callOk(QStringLiteral("fixturedefs.session.get"), sidOnly);
    QCOMPARE(snap.value(QStringLiteral("sessionRevision")).toInt(), 2);

    QJsonObject missing;
    missing.insert(QStringLiteral("sessionId"), QStringLiteral("nope"));
    QCOMPARE(callError(QStringLiteral("fixturedefs.session.get"), missing), QStringLiteral("NOT_FOUND"));
}

void ApiFixtureDefsDomain_Test::sessionUpdateBumpsRevisionAndConflicts()
{
    QString clientId = hello();
    QString sid = createSession().value(QStringLiteral("sessionId")).toString();

    QSignalSpy spy(m_client, &QWebSocket::textMessageReceived);
    QJsonObject p = params(sid, 0);
    p.insert(QStringLiteral("manufacturer"), QStringLiteral("Acme"));
    p.insert(QStringLiteral("model"), QStringLiteral("Beam"));
    p.insert(QStringLiteral("type"), QStringLiteral("Moving Head"));
    p.insert(QStringLiteral("author"), QStringLiteral("me"));
    QJsonObject result = callOk(QStringLiteral("fixturedefs.session.update"), p);
    QCOMPARE(result.value(QStringLiteral("sessionRevision")).toInt(), 1);

    QJsonObject event = waitForEvent(spy, QStringLiteral("fixturedefs.session.updated"));
    QCOMPARE(event.value(QStringLiteral("changeKind")).toString(), QStringLiteral("metadata"));
    QCOMPARE(event.value(QStringLiteral("sessionRevision")).toInt(), 1);
    QCOMPARE(event.value(QStringLiteral("__originClientId")).toString(), clientId);
    QJsonObject def = event.value(QStringLiteral("definition")).toObject();
    QCOMPARE(def.value(QStringLiteral("manufacturer")).toString(), QStringLiteral("Acme"));
    QCOMPARE(def.value(QStringLiteral("type")).toString(), QStringLiteral("Moving Head"));
    QCOMPARE(def.value(QStringLiteral("author")).toString(), QStringLiteral("me"));

    // Stale revision -> CONFLICT with the current state in details.
    QJsonObject details;
    QCOMPARE(callError(QStringLiteral("fixturedefs.session.update"), p, &details), QStringLiteral("CONFLICT"));
    QCOMPARE(details.value(QStringLiteral("sessionRevision")).toInt(), 1);
    QCOMPARE(details.value(QStringLiteral("definition")).toObject().value(QStringLiteral("model")).toString(), QStringLiteral("Beam"));

    QJsonArray sessions = callOk(QStringLiteral("fixturedefs.session.list"), QJsonObject()).value(QStringLiteral("sessions")).toArray();
    QCOMPARE(sessions.first().toObject().value(QStringLiteral("isModified")).toBool(), true);
}

void ApiFixtureDefsDomain_Test::sessionSetPhysicalMergesPartially()
{
    hello();
    QString sid = createSession().value(QStringLiteral("sessionId")).toString();
    QSignalSpy spy(m_client, &QWebSocket::textMessageReceived);

    QJsonObject phy;
    phy.insert(QStringLiteral("bulbType"), QStringLiteral("LED"));
    phy.insert(QStringLiteral("weight"), 3.5);
    phy.insert(QStringLiteral("layoutWidth"), 4);
    phy.insert(QStringLiteral("layoutHeight"), 1);
    QJsonObject p = params(sid, 0);
    p.insert(QStringLiteral("physical"), phy);
    callOk(QStringLiteral("fixturedefs.session.setPhysical"), p);

    QJsonObject second;
    second.insert(QStringLiteral("powerConsumption"), 120);
    p = params(sid, 1);
    p.insert(QStringLiteral("physical"), second);
    callOk(QStringLiteral("fixturedefs.session.setPhysical"), p);

    QVERIFY(QTest::qWaitFor([&]() { return spy.count() >= 4; }, 3000));
    QJsonObject result = lastUpdatedDefinition(spy).value(QStringLiteral("physical")).toObject();
    QCOMPARE(result.value(QStringLiteral("bulbType")).toString(), QStringLiteral("LED"));
    QCOMPARE(result.value(QStringLiteral("weight")).toDouble(), 3.5);
    QCOMPARE(result.value(QStringLiteral("layoutWidth")).toInt(), 4);
    QCOMPARE(result.value(QStringLiteral("powerConsumption")).toInt(), 120);

    QCOMPARE(callError(QStringLiteral("fixturedefs.session.setPhysical"), params(sid, 2)), QStringLiteral("INVALID_PARAMS"));
}

void ApiFixtureDefsDomain_Test::channelAddUpdateRemove()
{
    hello();
    QString sid = createSession().value(QStringLiteral("sessionId")).toString();
    QSignalSpy spy(m_client, &QWebSocket::textMessageReceived);
    int rev = 0;

    // Blank channel: default name, one full-range empty capability.
    QJsonObject result = callOk(QStringLiteral("fixturedefs.channel.add"), params(sid, rev++));
    QString blankId = result.value(QStringLiteral("channelId")).toString();
    QCOMPARE(blankId, QStringLiteral("ch-1"));
    QCOMPARE(result.value(QStringLiteral("sessionRevision")).toInt(), rev);
    QVERIFY(QTest::qWaitFor([&]() { return !lastUpdatedDefinition(spy).isEmpty(); }, 3000));
    QJsonObject blank = findChannel(lastUpdatedDefinition(spy), blankId);
    QCOMPARE(blank.value(QStringLiteral("name")).toString(), QStringLiteral("New channel 1"));
    QJsonArray caps = blank.value(QStringLiteral("capabilities")).toArray();
    QCOMPARE(caps.count(), 1);
    QCOMPARE(caps.first().toObject().value(QStringLiteral("min")).toInt(), 0);
    QCOMPARE(caps.first().toObject().value(QStringLiteral("max")).toInt(), 255);
    QCOMPARE(caps.first().toObject().value(QStringLiteral("warning")).toString(), QStringLiteral("EmptyName"));

    // Preset channel: group/colour/capability derived from the preset.
    QJsonObject p = params(sid, rev++);
    p.insert(QStringLiteral("name"), QStringLiteral("Red"));
    p.insert(QStringLiteral("preset"), QStringLiteral("IntensityRed"));
    result = callOk(QStringLiteral("fixturedefs.channel.add"), p);
    QString redId = result.value(QStringLiteral("channelId")).toString();
    QVERIFY(QTest::qWaitFor([&]() { return !findChannel(lastUpdatedDefinition(spy), redId).isEmpty(); }, 3000));
    QJsonObject red = findChannel(lastUpdatedDefinition(spy), redId);
    QCOMPARE(red.value(QStringLiteral("group")).toString(), QStringLiteral("Intensity"));
    QCOMPARE(red.value(QStringLiteral("colour")).toString(), QStringLiteral("Red"));
    QCOMPARE(red.value(QStringLiteral("preset")).toString(), QStringLiteral("IntensityRed"));
    caps = red.value(QStringLiteral("capabilities")).toArray();
    QCOMPARE(caps.count(), 1);
    QCOMPARE(caps.first().toObject().value(QStringLiteral("name")).toString(), QStringLiteral("Red intensity (0 - 100%)"));

    // Duplicate names and unknown enum strings are rejected.
    p = params(sid, rev);
    p.insert(QStringLiteral("name"), QStringLiteral("Red"));
    QCOMPARE(callError(QStringLiteral("fixturedefs.channel.add"), p), QStringLiteral("INVALID_PARAMS"));
    p = params(sid, rev);
    p.insert(QStringLiteral("group"), QStringLiteral("Bogus"));
    QCOMPARE(callError(QStringLiteral("fixturedefs.channel.add"), p), QStringLiteral("INVALID_PARAMS"));

    // Update: rename + group + colour + default + control byte.
    p = params(sid, rev++);
    p.insert(QStringLiteral("channelId"), blankId);
    p.insert(QStringLiteral("name"), QStringLiteral("Pan fine"));
    p.insert(QStringLiteral("group"), QStringLiteral("Pan"));
    p.insert(QStringLiteral("defaultValue"), 128);
    p.insert(QStringLiteral("controlByte"), QStringLiteral("LSB"));
    result = callOk(QStringLiteral("fixturedefs.channel.update"), p);
    QCOMPARE(result.value(QStringLiteral("sessionRevision")).toInt(), rev);
    QVERIFY(QTest::qWaitFor([&]() {
        return findChannel(lastUpdatedDefinition(spy), blankId).value(QStringLiteral("name")).toString() == QStringLiteral("Pan fine"); }, 3000));
    QJsonObject pan = findChannel(lastUpdatedDefinition(spy), blankId);
    QCOMPARE(pan.value(QStringLiteral("group")).toString(), QStringLiteral("Pan"));
    QCOMPARE(pan.value(QStringLiteral("defaultValue")).toInt(), 128);
    QCOMPARE(pan.value(QStringLiteral("controlByte")).toString(), QStringLiteral("LSB"));

    // Update with a preset replaces the capability list.
    p = params(sid, rev++);
    p.insert(QStringLiteral("channelId"), blankId);
    p.insert(QStringLiteral("preset"), QStringLiteral("PositionPanFine"));
    callOk(QStringLiteral("fixturedefs.channel.update"), p);
    QVERIFY(QTest::qWaitFor([&]() {
        return findChannel(lastUpdatedDefinition(spy), blankId).value(QStringLiteral("preset")).toString() == QStringLiteral("PositionPanFine"); }, 3000));
    pan = findChannel(lastUpdatedDefinition(spy), blankId);
    QCOMPARE(pan.value(QStringLiteral("controlByte")).toString(), QStringLiteral("LSB"));
    QCOMPARE(pan.value(QStringLiteral("capabilities")).toArray().count(), 1);
    QVERIFY(pan.value(QStringLiteral("capabilities")).toArray().first().toObject().value(QStringLiteral("name")).toString().isEmpty() == false);

    p = params(sid, rev);
    p.insert(QStringLiteral("channelId"), QStringLiteral("ch-99"));
    QCOMPARE(callError(QStringLiteral("fixturedefs.channel.update"), p), QStringLiteral("NOT_FOUND"));

    // Remove both.
    p = params(sid, rev++);
    p.insert(QStringLiteral("channelIds"), strings({ blankId, redId }));
    result = callOk(QStringLiteral("fixturedefs.channel.remove"), p);
    QCOMPARE(result.value(QStringLiteral("sessionRevision")).toInt(), rev);
    QVERIFY(QTest::qWaitFor([&]() { return lastUpdatedDefinition(spy).value(QStringLiteral("channels")).toArray().isEmpty(); }, 3000));
    p = params(sid, rev);
    p.insert(QStringLiteral("channelIds"), strings({ blankId }));
    QCOMPARE(callError(QStringLiteral("fixturedefs.channel.remove"), p), QStringLiteral("NOT_FOUND"));
}

void ApiFixtureDefsDomain_Test::channelRemoveCascadesToModesAndAliases()
{
    hello();
    QString sid = createSession().value(QStringLiteral("sessionId")).toString();
    QSignalSpy spy(m_client, &QWebSocket::textMessageReceived);
    int rev = 0;
    QStringList ids;
    for (const QString &name : { QStringLiteral("A"), QStringLiteral("B"), QStringLiteral("C") })
    {
        QJsonObject p = params(sid, rev++);
        p.insert(QStringLiteral("name"), name);
        ids << callOk(QStringLiteral("fixturedefs.channel.add"), p).value(QStringLiteral("channelId")).toString();
    }
    QJsonObject p = params(sid, rev++);
    p.insert(QStringLiteral("name"), QStringLiteral("M"));
    QString modeId = callOk(QStringLiteral("fixturedefs.mode.add"), p).value(QStringLiteral("modeId")).toString();

    // Mode A, B(acts on A), C with a head over [B, C]
    QJsonArray slotArray;
    QJsonObject sa; sa.insert(QStringLiteral("channelId"), ids[0]); slotArray.append(sa);
    QJsonObject sb; sb.insert(QStringLiteral("channelId"), ids[1]); sb.insert(QStringLiteral("actsOnChannelId"), ids[0]); slotArray.append(sb);
    QJsonObject sc; sc.insert(QStringLiteral("channelId"), ids[2]); slotArray.append(sc);
    p = params(sid, rev++);
    p.insert(QStringLiteral("modeId"), modeId);
    p.insert(QStringLiteral("channels"), slotArray);
    callOk(QStringLiteral("fixturedefs.mode.setChannels"), p);
    p = params(sid, rev++);
    p.insert(QStringLiteral("modeId"), modeId);
    p.insert(QStringLiteral("channelIds"), strings({ ids[1], ids[2] }));
    callOk(QStringLiteral("fixturedefs.mode.head.add"), p);

    // Alias on C's capability targeting A (so removing A must drop it).
    p = params(sid, rev++);
    p.insert(QStringLiteral("channelId"), ids[2]);
    p.insert(QStringLiteral("capabilityIndex"), 0);
    p.insert(QStringLiteral("preset"), QStringLiteral("Alias"));
    callOk(QStringLiteral("fixturedefs.channel.capability.update"), p);
    p = params(sid, rev++);
    p.insert(QStringLiteral("channelId"), ids[2]);
    p.insert(QStringLiteral("capabilityIndex"), 0);
    p.insert(QStringLiteral("targetMode"), QStringLiteral("M"));
    p.insert(QStringLiteral("targetChannel"), QStringLiteral("A"));
    callOk(QStringLiteral("fixturedefs.channel.capability.alias.add"), p);

    // Remove A.
    p = params(sid, rev++);
    p.insert(QStringLiteral("channelIds"), strings({ ids[0] }));
    callOk(QStringLiteral("fixturedefs.channel.remove"), p);
    QVERIFY(QTest::qWaitFor([&]() { return lastUpdatedDefinition(spy).value(QStringLiteral("channels")).toArray().count() == 2; }, 3000));
    QJsonObject def = lastUpdatedDefinition(spy);
    QJsonObject mode = findMode(def, modeId);
    QJsonArray modeSlots = mode.value(QStringLiteral("channels")).toArray();
    QCOMPARE(modeSlots.count(), 2);
    QCOMPARE(modeSlots.at(0).toObject().value(QStringLiteral("channelId")).toString(), ids[1]);
    QVERIFY(modeSlots.at(0).toObject().value(QStringLiteral("actsOnChannelId")).isNull()); // acted on the removed A
    QCOMPARE(modeSlots.at(1).toObject().value(QStringLiteral("channelId")).toString(), ids[2]);
    QJsonArray heads = mode.value(QStringLiteral("heads")).toArray();
    QCOMPARE(heads.count(), 1);
    QCOMPARE(heads.first().toObject().value(QStringLiteral("channelIds")).toArray(), strings({ ids[1], ids[2] }));
    QJsonObject c = findChannel(def, ids[2]);
    QCOMPARE(c.value(QStringLiteral("capabilities")).toArray().first().toObject().value(QStringLiteral("aliases")).toArray().count(), 0);
}

void ApiFixtureDefsDomain_Test::capabilityAddUpdateRemove()
{
    hello();
    QString sid = createSession().value(QStringLiteral("sessionId")).toString();
    QSignalSpy spy(m_client, &QWebSocket::textMessageReceived);
    int rev = 0;
    QJsonObject p = params(sid, rev++);
    p.insert(QStringLiteral("name"), QStringLiteral("Color"));
    p.insert(QStringLiteral("group"), QStringLiteral("Colour"));
    QString chId = callOk(QStringLiteral("fixturedefs.channel.add"), p).value(QStringLiteral("channelId")).toString();

    // The blank channel starts with one 0-255 capability: shrink it first.
    p = params(sid, rev++);
    p.insert(QStringLiteral("channelId"), chId);
    p.insert(QStringLiteral("capabilityIndex"), 0);
    p.insert(QStringLiteral("max"), 9);
    p.insert(QStringLiteral("name"), QStringLiteral("Open"));
    callOk(QStringLiteral("fixturedefs.channel.capability.update"), p);

    // Default range continues after the last capability.
    p = params(sid, rev++);
    p.insert(QStringLiteral("channelId"), chId);
    p.insert(QStringLiteral("name"), QStringLiteral("red"));
    QJsonObject result = callOk(QStringLiteral("fixturedefs.channel.capability.add"), p);
    QCOMPARE(result.value(QStringLiteral("capabilityIndex")).toInt(), 1);
    QVERIFY(QTest::qWaitFor([&]() { return findChannel(lastUpdatedDefinition(spy), chId).value(QStringLiteral("capabilities")).toArray().count() == 2; }, 3000));
    QJsonObject cap = findChannel(lastUpdatedDefinition(spy), chId).value(QStringLiteral("capabilities")).toArray().at(1).toObject();
    QCOMPARE(cap.value(QStringLiteral("min")).toInt(), 10);
    QCOMPARE(cap.value(QStringLiteral("max")).toInt(), 255);

    // Explicit overlapping range -> domain error.
    p = params(sid, rev);
    p.insert(QStringLiteral("channelId"), chId);
    p.insert(QStringLiteral("min"), 5);
    p.insert(QStringLiteral("max"), 20);
    QCOMPARE(callError(QStringLiteral("fixturedefs.channel.capability.add"), p), QStringLiteral("FIXTUREDEFS_RANGE_OVERLAP"));
    p.insert(QStringLiteral("min"), 300);
    QCOMPARE(callError(QStringLiteral("fixturedefs.channel.capability.add"), p), QStringLiteral("INVALID_PARAMS"));
    // Channel is full (last max = 255): the implicit default range is refused.
    p = params(sid, rev);
    p.insert(QStringLiteral("channelId"), chId);
    QCOMPARE(callError(QStringLiteral("fixturedefs.channel.capability.add"), p), QStringLiteral("INVALID_PARAMS"));

    // Update range/name/preset/resources.
    p = params(sid, rev++);
    p.insert(QStringLiteral("channelId"), chId);
    p.insert(QStringLiteral("capabilityIndex"), 1);
    p.insert(QStringLiteral("max"), 100);
    p.insert(QStringLiteral("name"), QStringLiteral("Red"));
    p.insert(QStringLiteral("preset"), QStringLiteral("ColorMacro"));
    p.insert(QStringLiteral("resources"), QJsonArray{ QStringLiteral("#ff0000") });
    callOk(QStringLiteral("fixturedefs.channel.capability.update"), p);
    QVERIFY(QTest::qWaitFor([&]() {
        return findChannel(lastUpdatedDefinition(spy), chId).value(QStringLiteral("capabilities")).toArray().at(1).toObject().value(QStringLiteral("max")).toInt() == 100; }, 3000));
    cap = findChannel(lastUpdatedDefinition(spy), chId).value(QStringLiteral("capabilities")).toArray().at(1).toObject();
    QCOMPARE(cap.value(QStringLiteral("name")).toString(), QStringLiteral("Red"));
    QCOMPARE(cap.value(QStringLiteral("preset")).toString(), QStringLiteral("ColorMacro"));
    QCOMPARE(cap.value(QStringLiteral("resources")).toArray(), QJsonArray{ QStringLiteral("#ff0000") });
    QCOMPARE(cap.value(QStringLiteral("warning")).toString(), QStringLiteral("NoWarning"));

    // Numeric resources for a frequency preset; a bad type is rejected.
    p = params(sid, rev++);
    p.insert(QStringLiteral("channelId"), chId);
    p.insert(QStringLiteral("capabilityIndex"), 1);
    p.insert(QStringLiteral("preset"), QStringLiteral("StrobeFreqRange"));
    p.insert(QStringLiteral("resources"), QJsonArray{ 1.5, 20 });
    callOk(QStringLiteral("fixturedefs.channel.capability.update"), p);
    QVERIFY(QTest::qWaitFor([&]() {
        return findChannel(lastUpdatedDefinition(spy), chId).value(QStringLiteral("capabilities")).toArray().at(1).toObject().value(QStringLiteral("preset")).toString() == QStringLiteral("StrobeFreqRange"); }, 3000));
    cap = findChannel(lastUpdatedDefinition(spy), chId).value(QStringLiteral("capabilities")).toArray().at(1).toObject();
    QCOMPARE(cap.value(QStringLiteral("resources")).toArray().count(), 2);
    QCOMPARE(cap.value(QStringLiteral("resources")).toArray().at(0).toDouble(), 1.5);
    QCOMPARE(cap.value(QStringLiteral("resources")).toArray().at(1).toDouble(), 20.0);
    p = params(sid, rev);
    p.insert(QStringLiteral("channelId"), chId);
    p.insert(QStringLiteral("capabilityIndex"), 1);
    p.insert(QStringLiteral("preset"), QStringLiteral("ColorMacro"));
    p.insert(QStringLiteral("resources"), QJsonArray{ QStringLiteral("not a colour") });
    QCOMPARE(callError(QStringLiteral("fixturedefs.channel.capability.update"), p), QStringLiteral("INVALID_PARAMS"));
    p.remove(QStringLiteral("resources"));
    p.insert(QStringLiteral("preset"), QStringLiteral("NoSuchPreset"));
    QCOMPARE(callError(QStringLiteral("fixturedefs.channel.capability.update"), p), QStringLiteral("INVALID_PARAMS"));
    p.remove(QStringLiteral("preset"));
    p.insert(QStringLiteral("min"), 0); // overlaps "Open" 0-9
    QCOMPARE(callError(QStringLiteral("fixturedefs.channel.capability.update"), p), QStringLiteral("FIXTUREDEFS_RANGE_OVERLAP"));

    // Remove.
    p = params(sid, rev++);
    p.insert(QStringLiteral("channelId"), chId);
    p.insert(QStringLiteral("capabilityIndex"), 0);
    callOk(QStringLiteral("fixturedefs.channel.capability.remove"), p);
    QVERIFY(QTest::qWaitFor([&]() { return findChannel(lastUpdatedDefinition(spy), chId).value(QStringLiteral("capabilities")).toArray().count() == 1; }, 3000));
    QCOMPARE(findChannel(lastUpdatedDefinition(spy), chId).value(QStringLiteral("capabilities")).toArray().first().toObject().value(QStringLiteral("name")).toString(),
             QStringLiteral("Red"));
    p = params(sid, rev);
    p.insert(QStringLiteral("channelId"), chId);
    p.insert(QStringLiteral("capabilityIndex"), 5);
    QCOMPARE(callError(QStringLiteral("fixturedefs.channel.capability.remove"), p), QStringLiteral("NOT_FOUND"));
}

void ApiFixtureDefsDomain_Test::capabilityWizardCreatesRangesAndRejectsOverlap()
{
    hello();
    QString sid = createSession().value(QStringLiteral("sessionId")).toString();
    QSignalSpy spy(m_client, &QWebSocket::textMessageReceived);
    int rev = 0;
    QJsonObject p = params(sid, rev++);
    p.insert(QStringLiteral("name"), QStringLiteral("Gobo"));
    p.insert(QStringLiteral("group"), QStringLiteral("Gobo"));
    QString chId = callOk(QStringLiteral("fixturedefs.channel.add"), p).value(QStringLiteral("channelId")).toString();
    p = params(sid, rev++);
    p.insert(QStringLiteral("channelId"), chId);
    p.insert(QStringLiteral("capabilityIndex"), 0);
    p.insert(QStringLiteral("max"), 15);
    p.insert(QStringLiteral("name"), QStringLiteral("Open"));
    callOk(QStringLiteral("fixturedefs.channel.capability.update"), p);

    p = params(sid, rev);
    p.insert(QStringLiteral("channelId"), chId);
    p.insert(QStringLiteral("start"), 10);
    p.insert(QStringLiteral("width"), 16);
    p.insert(QStringLiteral("amount"), 3);
    QCOMPARE(callError(QStringLiteral("fixturedefs.channel.capability.wizard"), p), QStringLiteral("FIXTUREDEFS_RANGE_OVERLAP"));
    p.insert(QStringLiteral("start"), 16);
    p.insert(QStringLiteral("amount"), 16);
    QCOMPARE(callError(QStringLiteral("fixturedefs.channel.capability.wizard"), p), QStringLiteral("INVALID_PARAMS")); // beyond 255

    p = params(sid, rev++);
    p.insert(QStringLiteral("channelId"), chId);
    p.insert(QStringLiteral("start"), 16);
    p.insert(QStringLiteral("width"), 16);
    p.insert(QStringLiteral("amount"), 3);
    p.insert(QStringLiteral("label"), QStringLiteral("Gobo #"));
    QJsonObject result = callOk(QStringLiteral("fixturedefs.channel.capability.wizard"), p);
    QCOMPARE(result.value(QStringLiteral("capabilityIndexes")).toArray(), (QJsonArray{ 1, 2, 3 }));
    QVERIFY(QTest::qWaitFor([&]() { return findChannel(lastUpdatedDefinition(spy), chId).value(QStringLiteral("capabilities")).toArray().count() == 4; }, 3000));
    QJsonArray caps = findChannel(lastUpdatedDefinition(spy), chId).value(QStringLiteral("capabilities")).toArray();
    QCOMPARE(caps.at(1).toObject().value(QStringLiteral("name")).toString(), QStringLiteral("Gobo 1"));
    QCOMPARE(caps.at(1).toObject().value(QStringLiteral("min")).toInt(), 16);
    QCOMPARE(caps.at(1).toObject().value(QStringLiteral("max")).toInt(), 31);
    QCOMPARE(caps.at(3).toObject().value(QStringLiteral("name")).toString(), QStringLiteral("Gobo 3"));
    QCOMPARE(caps.at(3).toObject().value(QStringLiteral("min")).toInt(), 48);
    QCOMPARE(caps.at(3).toObject().value(QStringLiteral("max")).toInt(), 63);
}

void ApiFixtureDefsDomain_Test::channelWizardCreatesCompoundChannels()
{
    hello();
    QString sid = createSession().value(QStringLiteral("sessionId")).toString();
    QSignalSpy spy(m_client, &QWebSocket::textMessageReceived);

    QJsonObject p = params(sid, 0);
    p.insert(QStringLiteral("amount"), 2);
    p.insert(QStringLiteral("type"), QStringLiteral("RGB"));
    QJsonObject result = callOk(QStringLiteral("fixturedefs.channel.wizard"), p);
    QCOMPARE(result.value(QStringLiteral("channelIds")).toArray().count(), 6);
    QVERIFY(QTest::qWaitFor([&]() { return lastUpdatedDefinition(spy).value(QStringLiteral("channels")).toArray().count() == 6; }, 3000));
    QJsonArray channels = lastUpdatedDefinition(spy).value(QStringLiteral("channels")).toArray();
    QCOMPARE(channels.at(0).toObject().value(QStringLiteral("name")).toString(), QStringLiteral("Red 1"));
    QCOMPARE(channels.at(0).toObject().value(QStringLiteral("colour")).toString(), QStringLiteral("Red"));
    QCOMPARE(channels.at(0).toObject().value(QStringLiteral("preset")).toString(), QStringLiteral("IntensityRed"));
    QCOMPARE(channels.at(5).toObject().value(QStringLiteral("name")).toString(), QStringLiteral("Blue 2"));
    QCOMPARE(channels.at(5).toObject().value(QStringLiteral("colour")).toString(), QStringLiteral("Blue"));

    p = params(sid, 1);
    p.insert(QStringLiteral("amount"), 2);
    p.insert(QStringLiteral("type"), QStringLiteral("Pan"));
    p.insert(QStringLiteral("label"), QStringLiteral("Pan #"));
    result = callOk(QStringLiteral("fixturedefs.channel.wizard"), p);
    QCOMPARE(result.value(QStringLiteral("channelIds")).toArray().count(), 2);
    QVERIFY(QTest::qWaitFor([&]() { return lastUpdatedDefinition(spy).value(QStringLiteral("channels")).toArray().count() == 8; }, 3000));
    channels = lastUpdatedDefinition(spy).value(QStringLiteral("channels")).toArray();
    QCOMPARE(channels.at(6).toObject().value(QStringLiteral("name")).toString(), QStringLiteral("Pan 1"));
    QCOMPARE(channels.at(6).toObject().value(QStringLiteral("group")).toString(), QStringLiteral("Pan"));
    QCOMPARE(channels.at(6).toObject().value(QStringLiteral("preset")).toString(), QStringLiteral("PositionPan"));

    // Same labels again -> nothing created, everything rejected up front.
    p.insert(QStringLiteral("baseRevision"), 2);
    QCOMPARE(callError(QStringLiteral("fixturedefs.channel.wizard"), p), QStringLiteral("INVALID_PARAMS"));
    p = params(sid, 2);
    p.insert(QStringLiteral("type"), QStringLiteral("Nonsense"));
    QCOMPARE(callError(QStringLiteral("fixturedefs.channel.wizard"), p), QStringLiteral("INVALID_PARAMS"));
}

void ApiFixtureDefsDomain_Test::autoPatchColorsDetectsNamedColors()
{
    // The detection reads the bundled namedrgb.qxcf from the system colour
    // filter directory (applicationDirPath-relative on Windows/macOS): stage a
    // small one there so the colour path is exercised, not just title-casing.
    // On Windows/macOS that directory is under the build tree (applicationDirPath-
    // relative); on Linux COLORFILTERSDIR is the absolute install prefix, which a
    // test must not (and usually cannot) write into - skip rather than litter it.
    QDir filtersDir = QLCFile::systemDirectory(QString(COLORFILTERSDIR), QString(KExtColorFilters));
    QDir().mkpath(filtersDir.absolutePath());
    QFile filters(filtersDir.absoluteFilePath(QStringLiteral("namedrgb.qxcf")));
    if (filtersDir.exists() == false || filters.open(QIODevice::WriteOnly | QIODevice::Text) == false)
        QSKIP("system colour-filter directory is not writable here; colour detection is covered on Windows/macOS");
    filters.write("<?xml version='1.0' encoding='UTF-8'?>\n<!DOCTYPE ColorFilters>\n"
                  "<ColorFilters xmlns=\"http://www.qlcplus.org/ColorFilters\">\n <Name>Named RGB</Name>\n"
                  " <Color RGB=\"#FF0000\" Name=\"Red\" />\n <Color RGB=\"#00FF00\" Name=\"Green\" />\n"
                  " <Color RGB=\"#0000FF\" Name=\"Blue\" />\n <Color RGB=\"#FFFF00\" Name=\"Yellow\" />\n"
                  " <Color RGB=\"#FF00FF\" Name=\"Magenta\" />\n</ColorFilters>\n");
    filters.close();

    hello();
    QString sid = createSession().value(QStringLiteral("sessionId")).toString();
    QSignalSpy spy(m_client, &QWebSocket::textMessageReceived);
    int rev = 0;
    QJsonObject p = params(sid, rev++);
    p.insert(QStringLiteral("name"), QStringLiteral("Color wheel"));
    p.insert(QStringLiteral("group"), QStringLiteral("Colour"));
    QString chId = callOk(QStringLiteral("fixturedefs.channel.add"), p).value(QStringLiteral("channelId")).toString();
    p = params(sid, rev++);
    p.insert(QStringLiteral("channelId"), chId);
    p.insert(QStringLiteral("capabilityIndex"), 0);
    p.insert(QStringLiteral("max"), 9);
    p.insert(QStringLiteral("name"), QStringLiteral("open"));
    callOk(QStringLiteral("fixturedefs.channel.capability.update"), p);
    p = params(sid, rev++);
    p.insert(QStringLiteral("channelId"), chId);
    p.insert(QStringLiteral("start"), 10);
    p.insert(QStringLiteral("width"), 10);
    p.insert(QStringLiteral("amount"), 1);
    p.insert(QStringLiteral("label"), QStringLiteral("red"));
    callOk(QStringLiteral("fixturedefs.channel.capability.wizard"), p);
    p = params(sid, rev++);
    p.insert(QStringLiteral("channelId"), chId);
    p.insert(QStringLiteral("min"), 20);
    p.insert(QStringLiteral("max"), 29);
    p.insert(QStringLiteral("name"), QStringLiteral("Blue / Yellow"));
    callOk(QStringLiteral("fixturedefs.channel.capability.add"), p);

    p = params(sid, rev++);
    p.insert(QStringLiteral("channelId"), chId);
    QJsonObject result = callOk(QStringLiteral("fixturedefs.channel.capability.autoPatchColors"), p);
    QCOMPARE(result.value(QStringLiteral("sessionRevision")).toInt(), rev);
    QVERIFY(QTest::qWaitFor([&]() {
        return findChannel(lastUpdatedDefinition(spy), chId).value(QStringLiteral("capabilities")).toArray().at(1).toObject().value(QStringLiteral("preset")).toString() == QStringLiteral("ColorMacro"); }, 3000));
    QJsonArray caps = findChannel(lastUpdatedDefinition(spy), chId).value(QStringLiteral("capabilities")).toArray();
    QCOMPARE(caps.at(0).toObject().value(QStringLiteral("name")).toString(), QStringLiteral("Open")); // title-cased, no colour
    QCOMPARE(caps.at(0).toObject().value(QStringLiteral("preset")).toString(), QStringLiteral("Custom"));
    QCOMPARE(caps.at(1).toObject().value(QStringLiteral("name")).toString(), QStringLiteral("Red"));
    QCOMPARE(caps.at(1).toObject().value(QStringLiteral("resources")).toArray(), QJsonArray{ QStringLiteral("#ff0000") });
    QCOMPARE(caps.at(2).toObject().value(QStringLiteral("preset")).toString(), QStringLiteral("ColorDoubleMacro"));
    QCOMPARE(caps.at(2).toObject().value(QStringLiteral("resources")).toArray(), (QJsonArray{ QStringLiteral("#0000ff"), QStringLiteral("#ffff00") }));

    // Non-colour channel: ok, nothing changes, no revision bump.
    p = params(sid, rev++);
    p.insert(QStringLiteral("name"), QStringLiteral("Dim"));
    QString dimId = callOk(QStringLiteral("fixturedefs.channel.add"), p).value(QStringLiteral("channelId")).toString();
    p = params(sid, rev);
    p.insert(QStringLiteral("channelId"), dimId);
    result = callOk(QStringLiteral("fixturedefs.channel.capability.autoPatchColors"), p);
    QCOMPARE(result.value(QStringLiteral("sessionRevision")).toInt(), rev);
}

void ApiFixtureDefsDomain_Test::aliasAddUpdateRemoveApplyToAllModes()
{
    hello();
    QString sid = createSession().value(QStringLiteral("sessionId")).toString();
    QSignalSpy spy(m_client, &QWebSocket::textMessageReceived);
    int rev = 0;
    QStringList ids;
    for (const QString &name : { QStringLiteral("Function"), QStringLiteral("Speed"), QStringLiteral("Strobe") })
    {
        QJsonObject p = params(sid, rev++);
        p.insert(QStringLiteral("name"), name);
        ids << callOk(QStringLiteral("fixturedefs.channel.add"), p).value(QStringLiteral("channelId")).toString();
    }
    QStringList modeIds;
    for (const QString &name : { QStringLiteral("Basic"), QStringLiteral("Extended"), QStringLiteral("NoFunction") })
    {
        QJsonObject p = params(sid, rev++);
        p.insert(QStringLiteral("name"), name);
        modeIds << callOk(QStringLiteral("fixturedefs.mode.add"), p).value(QStringLiteral("modeId")).toString();
    }
    // Basic and Extended contain Function; NoFunction does not.
    for (int i = 0; i < 2; i++)
    {
        QJsonArray slotArray;
        for (const QString &chId : ids) { QJsonObject s; s.insert(QStringLiteral("channelId"), chId); slotArray.append(s); }
        QJsonObject p = params(sid, rev++);
        p.insert(QStringLiteral("modeId"), modeIds[i]);
        p.insert(QStringLiteral("channels"), slotArray);
        callOk(QStringLiteral("fixturedefs.mode.setChannels"), p);
    }

    // Not an Alias capability yet.
    QJsonObject p = params(sid, rev);
    p.insert(QStringLiteral("channelId"), ids[0]);
    p.insert(QStringLiteral("capabilityIndex"), 0);
    p.insert(QStringLiteral("targetMode"), QStringLiteral("Basic"));
    p.insert(QStringLiteral("targetChannel"), QStringLiteral("Speed"));
    QCOMPARE(callError(QStringLiteral("fixturedefs.channel.capability.alias.add"), p), QStringLiteral("INVALID_PARAMS"));

    QJsonObject up = params(sid, rev++);
    up.insert(QStringLiteral("channelId"), ids[0]);
    up.insert(QStringLiteral("capabilityIndex"), 0);
    up.insert(QStringLiteral("preset"), QStringLiteral("Alias"));
    up.insert(QStringLiteral("name"), QStringLiteral("Speed control"));
    callOk(QStringLiteral("fixturedefs.channel.capability.update"), up);

    p.insert(QStringLiteral("baseRevision"), rev++);
    QJsonObject result = callOk(QStringLiteral("fixturedefs.channel.capability.alias.add"), p);
    QCOMPARE(result.value(QStringLiteral("aliasIndex")).toInt(), 0);
    QCOMPARE(result.value(QStringLiteral("sessionRevision")).toInt(), rev);
    // Identical alias: no-op, same index, no revision bump.
    p.insert(QStringLiteral("baseRevision"), rev);
    result = callOk(QStringLiteral("fixturedefs.channel.capability.alias.add"), p);
    QCOMPARE(result.value(QStringLiteral("aliasIndex")).toInt(), 0);
    QCOMPARE(result.value(QStringLiteral("sessionRevision")).toInt(), rev);
    // Mode without the channel / unknown target channel.
    p.insert(QStringLiteral("targetMode"), QStringLiteral("NoFunction"));
    QCOMPARE(callError(QStringLiteral("fixturedefs.channel.capability.alias.add"), p), QStringLiteral("INVALID_PARAMS"));
    p.insert(QStringLiteral("targetMode"), QStringLiteral("Basic"));
    p.insert(QStringLiteral("targetChannel"), QStringLiteral("Ghost"));
    QCOMPARE(callError(QStringLiteral("fixturedefs.channel.capability.alias.add"), p), QStringLiteral("INVALID_PARAMS"));

    // applyToAllModes adds one for Extended only (Basic already has one).
    p = params(sid, rev++);
    p.insert(QStringLiteral("channelId"), ids[0]);
    p.insert(QStringLiteral("capabilityIndex"), 0);
    p.insert(QStringLiteral("targetChannel"), QStringLiteral("Strobe"));
    result = callOk(QStringLiteral("fixturedefs.channel.capability.alias.applyToAllModes"), p);
    QCOMPARE(result.value(QStringLiteral("addedCount")).toInt(), 1);
    QVERIFY(QTest::qWaitFor([&]() {
        return findChannel(lastUpdatedDefinition(spy), ids[0]).value(QStringLiteral("capabilities")).toArray().first().toObject().value(QStringLiteral("aliases")).toArray().count() == 2; }, 3000));
    QJsonArray aliases = findChannel(lastUpdatedDefinition(spy), ids[0]).value(QStringLiteral("capabilities")).toArray().first().toObject().value(QStringLiteral("aliases")).toArray();
    QCOMPARE(aliases.at(0).toObject().value(QStringLiteral("targetMode")).toString(), QStringLiteral("Basic"));
    QCOMPARE(aliases.at(0).toObject().value(QStringLiteral("targetChannel")).toString(), QStringLiteral("Speed"));
    QCOMPARE(aliases.at(1).toObject().value(QStringLiteral("targetMode")).toString(), QStringLiteral("Extended"));
    QCOMPARE(aliases.at(1).toObject().value(QStringLiteral("targetChannel")).toString(), QStringLiteral("Strobe"));

    // Update index 0 to target Strobe, then remove index 1.
    p = params(sid, rev++);
    p.insert(QStringLiteral("channelId"), ids[0]);
    p.insert(QStringLiteral("capabilityIndex"), 0);
    p.insert(QStringLiteral("aliasIndex"), 0);
    p.insert(QStringLiteral("targetChannel"), QStringLiteral("Strobe"));
    callOk(QStringLiteral("fixturedefs.channel.capability.alias.update"), p);
    p = params(sid, rev++);
    p.insert(QStringLiteral("channelId"), ids[0]);
    p.insert(QStringLiteral("capabilityIndex"), 0);
    p.insert(QStringLiteral("aliasIndex"), 1);
    callOk(QStringLiteral("fixturedefs.channel.capability.alias.remove"), p);
    QVERIFY(QTest::qWaitFor([&]() {
        return findChannel(lastUpdatedDefinition(spy), ids[0]).value(QStringLiteral("capabilities")).toArray().first().toObject().value(QStringLiteral("aliases")).toArray().count() == 1; }, 3000));
    aliases = findChannel(lastUpdatedDefinition(spy), ids[0]).value(QStringLiteral("capabilities")).toArray().first().toObject().value(QStringLiteral("aliases")).toArray();
    QCOMPARE(aliases.at(0).toObject().value(QStringLiteral("targetChannel")).toString(), QStringLiteral("Strobe"));
    p.insert(QStringLiteral("baseRevision"), rev);
    p.insert(QStringLiteral("aliasIndex"), 7);
    QCOMPARE(callError(QStringLiteral("fixturedefs.channel.capability.alias.remove"), p), QStringLiteral("NOT_FOUND"));
}

void ApiFixtureDefsDomain_Test::modeAddRenameSetChannelsRemove()
{
    hello();
    QString sid = createSession().value(QStringLiteral("sessionId")).toString();
    QSignalSpy spy(m_client, &QWebSocket::textMessageReceived);
    int rev = 0;
    QStringList ids;
    for (const QString &name : { QStringLiteral("Pan"), QStringLiteral("Pan fine") })
    {
        QJsonObject p = params(sid, rev++);
        p.insert(QStringLiteral("name"), name);
        ids << callOk(QStringLiteral("fixturedefs.channel.add"), p).value(QStringLiteral("channelId")).toString();
    }

    QJsonObject result = callOk(QStringLiteral("fixturedefs.mode.add"), params(sid, rev++));
    QString modeId = result.value(QStringLiteral("modeId")).toString();
    QCOMPARE(modeId, QStringLiteral("mode-1"));
    QString second = callOk(QStringLiteral("fixturedefs.mode.add"), params(sid, rev++)).value(QStringLiteral("modeId")).toString();
    QVERIFY(QTest::qWaitFor([&]() { return lastUpdatedDefinition(spy).value(QStringLiteral("modes")).toArray().count() == 2; }, 3000));
    QCOMPARE(findMode(lastUpdatedDefinition(spy), modeId).value(QStringLiteral("name")).toString(), QStringLiteral("New mode"));
    QCOMPARE(findMode(lastUpdatedDefinition(spy), second).value(QStringLiteral("name")).toString(), QStringLiteral("New mode 2"));

    QJsonObject p = params(sid, rev++);
    p.insert(QStringLiteral("modeId"), modeId);
    p.insert(QStringLiteral("name"), QStringLiteral("16 bit"));
    callOk(QStringLiteral("fixturedefs.mode.rename"), p);
    p = params(sid, rev);
    p.insert(QStringLiteral("modeId"), second);
    p.insert(QStringLiteral("name"), QStringLiteral("16 bit"));
    QCOMPARE(callError(QStringLiteral("fixturedefs.mode.rename"), p), QStringLiteral("INVALID_PARAMS"));
    p = params(sid, rev);
    p.insert(QStringLiteral("name"), QStringLiteral("16 bit"));
    QCOMPARE(callError(QStringLiteral("fixturedefs.mode.add"), p), QStringLiteral("INVALID_PARAMS"));

    QJsonArray slotArray;
    QJsonObject s0; s0.insert(QStringLiteral("channelId"), ids[0]); slotArray.append(s0);
    QJsonObject s1; s1.insert(QStringLiteral("channelId"), ids[1]); s1.insert(QStringLiteral("actsOnChannelId"), ids[0]); slotArray.append(s1);
    p = params(sid, rev++);
    p.insert(QStringLiteral("modeId"), modeId);
    p.insert(QStringLiteral("channels"), slotArray);
    callOk(QStringLiteral("fixturedefs.mode.setChannels"), p);
    QVERIFY(QTest::qWaitFor([&]() { return findMode(lastUpdatedDefinition(spy), modeId).value(QStringLiteral("channels")).toArray().count() == 2; }, 3000));
    QJsonArray modeSlots = findMode(lastUpdatedDefinition(spy), modeId).value(QStringLiteral("channels")).toArray();
    QCOMPARE(modeSlots.at(1).toObject().value(QStringLiteral("actsOnChannelId")).toString(), ids[0]);
    QVERIFY(modeSlots.at(0).toObject().value(QStringLiteral("actsOnChannelId")).isNull());

    // Unknown channel / channel twice / acts-on outside the mode.
    QJsonObject bad; bad.insert(QStringLiteral("channelId"), QStringLiteral("ch-42"));
    p = params(sid, rev);
    p.insert(QStringLiteral("modeId"), modeId);
    p.insert(QStringLiteral("channels"), QJsonArray{ bad });
    QCOMPARE(callError(QStringLiteral("fixturedefs.mode.setChannels"), p), QStringLiteral("NOT_FOUND"));
    p.insert(QStringLiteral("channels"), QJsonArray{ s0, s0 });
    QCOMPARE(callError(QStringLiteral("fixturedefs.mode.setChannels"), p), QStringLiteral("INVALID_PARAMS"));
    p.insert(QStringLiteral("channels"), QJsonArray{ s1 });
    QCOMPARE(callError(QStringLiteral("fixturedefs.mode.setChannels"), p), QStringLiteral("INVALID_PARAMS"));

    p = params(sid, rev++);
    p.insert(QStringLiteral("modeId"), second);
    callOk(QStringLiteral("fixturedefs.mode.remove"), p);
    QVERIFY(QTest::qWaitFor([&]() { return lastUpdatedDefinition(spy).value(QStringLiteral("modes")).toArray().count() == 1; }, 3000));
    p.insert(QStringLiteral("baseRevision"), rev);
    QCOMPARE(callError(QStringLiteral("fixturedefs.mode.remove"), p), QStringLiteral("NOT_FOUND"));
}

void ApiFixtureDefsDomain_Test::modeSetChannelsRejectsActsOnSelf()
{
    hello();
    QString sid = createSession().value(QStringLiteral("sessionId")).toString();
    QJsonObject p = params(sid, 0);
    p.insert(QStringLiteral("name"), QStringLiteral("Tilt"));
    QString chId = callOk(QStringLiteral("fixturedefs.channel.add"), p).value(QStringLiteral("channelId")).toString();
    QString modeId = callOk(QStringLiteral("fixturedefs.mode.add"), params(sid, 1)).value(QStringLiteral("modeId")).toString();
    QJsonObject slot;
    slot.insert(QStringLiteral("channelId"), chId);
    slot.insert(QStringLiteral("actsOnChannelId"), chId);
    p = params(sid, 2);
    p.insert(QStringLiteral("modeId"), modeId);
    p.insert(QStringLiteral("channels"), QJsonArray{ slot });
    QCOMPARE(callError(QStringLiteral("fixturedefs.mode.setChannels"), p), QStringLiteral("FIXTUREDEFS_ACTS_ON_SELF"));
}

void ApiFixtureDefsDomain_Test::modeSetPhysicalOverrideAndReset()
{
    hello();
    QString sid = createSession().value(QStringLiteral("sessionId")).toString();
    QSignalSpy spy(m_client, &QWebSocket::textMessageReceived);
    int rev = 0;
    QJsonObject phy;
    phy.insert(QStringLiteral("width"), 300);
    QJsonObject p = params(sid, rev++);
    p.insert(QStringLiteral("physical"), phy);
    callOk(QStringLiteral("fixturedefs.session.setPhysical"), p);
    QString modeId = callOk(QStringLiteral("fixturedefs.mode.add"), params(sid, rev++)).value(QStringLiteral("modeId")).toString();

    // Override starts from the global values, then applies the partial.
    QJsonObject override;
    override.insert(QStringLiteral("height"), 150);
    p = params(sid, rev++);
    p.insert(QStringLiteral("modeId"), modeId);
    p.insert(QStringLiteral("useGlobalPhysical"), false);
    p.insert(QStringLiteral("physical"), override);
    callOk(QStringLiteral("fixturedefs.mode.setPhysical"), p);
    QVERIFY(QTest::qWaitFor([&]() { return findMode(lastUpdatedDefinition(spy), modeId).value(QStringLiteral("useGlobalPhysical")).toBool() == false; }, 3000));
    QJsonObject mode = findMode(lastUpdatedDefinition(spy), modeId);
    QCOMPARE(mode.value(QStringLiteral("physical")).toObject().value(QStringLiteral("width")).toInt(), 300);
    QCOMPARE(mode.value(QStringLiteral("physical")).toObject().value(QStringLiteral("height")).toInt(), 150);

    p = params(sid, rev++);
    p.insert(QStringLiteral("modeId"), modeId);
    p.insert(QStringLiteral("useGlobalPhysical"), true);
    callOk(QStringLiteral("fixturedefs.mode.setPhysical"), p);
    QVERIFY(QTest::qWaitFor([&]() { return findMode(lastUpdatedDefinition(spy), modeId).value(QStringLiteral("useGlobalPhysical")).toBool(); }, 3000));
    QVERIFY(findMode(lastUpdatedDefinition(spy), modeId).value(QStringLiteral("physical")).isNull());

    p = params(sid, rev);
    p.insert(QStringLiteral("modeId"), modeId);
    QCOMPARE(callError(QStringLiteral("fixturedefs.mode.setPhysical"), p), QStringLiteral("INVALID_PARAMS"));
}

void ApiFixtureDefsDomain_Test::headsSurviveChannelReorder()
{
    hello();
    QString sid = createSession().value(QStringLiteral("sessionId")).toString();
    QSignalSpy spy(m_client, &QWebSocket::textMessageReceived);
    int rev = 0;
    QStringList ids;
    for (const QString &name : { QStringLiteral("A"), QStringLiteral("B"), QStringLiteral("C") })
    {
        QJsonObject p = params(sid, rev++);
        p.insert(QStringLiteral("name"), name);
        ids << callOk(QStringLiteral("fixturedefs.channel.add"), p).value(QStringLiteral("channelId")).toString();
    }
    QString modeId = callOk(QStringLiteral("fixturedefs.mode.add"), params(sid, rev++)).value(QStringLiteral("modeId")).toString();
    auto setChannels = [&](const QStringList &order)
    {
        QJsonArray slotArray;
        for (const QString &chId : order) { QJsonObject s; s.insert(QStringLiteral("channelId"), chId); slotArray.append(s); }
        QJsonObject p = params(sid, rev++);
        p.insert(QStringLiteral("modeId"), modeId);
        p.insert(QStringLiteral("channels"), slotArray);
        return callOk(QStringLiteral("fixturedefs.mode.setChannels"), p);
    };
    setChannels({ ids[0], ids[1], ids[2] });

    // Head over [A, B]; a head with a channel outside the mode is refused.
    QJsonObject p = params(sid, rev);
    p.insert(QStringLiteral("modeId"), modeId);
    p.insert(QStringLiteral("channelIds"), strings({ ids[0], QStringLiteral("ch-9") }));
    QCOMPARE(callError(QStringLiteral("fixturedefs.mode.head.add"), p), QStringLiteral("INVALID_PARAMS"));
    p = params(sid, rev++);
    p.insert(QStringLiteral("modeId"), modeId);
    p.insert(QStringLiteral("channelIds"), strings({ ids[0], ids[1] }));
    QJsonObject result = callOk(QStringLiteral("fixturedefs.mode.head.add"), p);
    QCOMPARE(result.value(QStringLiteral("headIndex")).toInt(), 0);
    p = params(sid, rev++);
    p.insert(QStringLiteral("modeId"), modeId);
    p.insert(QStringLiteral("channelIds"), strings({ ids[2] }));
    QCOMPARE(callOk(QStringLiteral("fixturedefs.mode.head.add"), p).value(QStringLiteral("headIndex")).toInt(), 1);

    // Reorder: C, A, B -> heads still name the same channels.
    setChannels({ ids[2], ids[0], ids[1] });
    QVERIFY(QTest::qWaitFor([&]() {
        return findMode(lastUpdatedDefinition(spy), modeId).value(QStringLiteral("channels")).toArray().first().toObject().value(QStringLiteral("channelId")).toString() == ids[2]; }, 3000));
    QJsonArray heads = findMode(lastUpdatedDefinition(spy), modeId).value(QStringLiteral("heads")).toArray();
    QCOMPARE(heads.count(), 2);
    QCOMPARE(heads.at(0).toObject().value(QStringLiteral("channelIds")).toArray(), strings({ ids[0], ids[1] }));
    QCOMPARE(heads.at(1).toObject().value(QStringLiteral("channelIds")).toArray(), strings({ ids[2] }));
    // Engine-side check: head 0 now holds slot indices 1 and 2.
    // (Removing C from the mode empties head 1 -> it is dropped.)
    setChannels({ ids[0], ids[1] });
    QVERIFY(QTest::qWaitFor([&]() { return findMode(lastUpdatedDefinition(spy), modeId).value(QStringLiteral("heads")).toArray().count() == 1; }, 3000));

    p = params(sid, rev);
    p.insert(QStringLiteral("modeId"), modeId);
    p.insert(QStringLiteral("headIndexes"), QJsonArray{ 5 });
    QCOMPARE(callError(QStringLiteral("fixturedefs.mode.head.remove"), p), QStringLiteral("NOT_FOUND"));
    p = params(sid, rev++);
    p.insert(QStringLiteral("modeId"), modeId);
    p.insert(QStringLiteral("headIndexes"), QJsonArray{ 0 });
    callOk(QStringLiteral("fixturedefs.mode.head.remove"), p);
    QVERIFY(QTest::qWaitFor([&]() { return findMode(lastUpdatedDefinition(spy), modeId).value(QStringLiteral("heads")).toArray().isEmpty(); }, 3000));
}

void ApiFixtureDefsDomain_Test::saveOnSystemSessionIsReadOnlyUntilForked()
{
    hello();
    QString sid = openSession(SysMan, SysModel).value(QStringLiteral("sessionId")).toString();
    QSignalSpy spy(m_client, &QWebSocket::textMessageReceived);

    QJsonObject save;
    save.insert(QStringLiteral("sessionId"), sid);
    save.insert(QStringLiteral("baseRevision"), 0);
    QCOMPARE(callError(QStringLiteral("fixturedefs.save"), save), QStringLiteral("FIXTUREDEFS_SYSTEM_READONLY"));

    QJsonObject result = callOk(QStringLiteral("fixturedefs.session.forkToUser"), params(sid, 0));
    QCOMPARE(result.value(QStringLiteral("sessionRevision")).toInt(), 1);
    QJsonObject event = waitForEvent(spy, QStringLiteral("fixturedefs.session.updated"));
    QCOMPARE(event.value(QStringLiteral("changeKind")).toString(), QStringLiteral("forkToUser"));
    QJsonObject def = event.value(QStringLiteral("definition")).toObject();
    QCOMPARE(def.value(QStringLiteral("isUser")).toBool(), true);
    QString target = def.value(QStringLiteral("sourceFile")).toString();
    QVERIFY(target.startsWith(QDir(m_userDir->path()).absolutePath()));
    QVERIFY(target.endsWith(QStringLiteral("SysMan-SysModel.qxf")));

    // Forking twice is a no-op ack.
    QCOMPARE(callOk(QStringLiteral("fixturedefs.session.forkToUser"), params(sid, 1)).value(QStringLiteral("sessionRevision")).toInt(), 1);

    result = callOk(QStringLiteral("fixturedefs.save"), save);
    QCOMPARE(result.value(QStringLiteral("defRevision")).toInt(), 1);
    QCOMPARE(result.value(QStringLiteral("manufacturer")).toString(), SysMan);
    QVERIFY(QFile::exists(target));
    QLCFixtureDef *cached = m_doc->fixtureDefCache()->fixtureDef(SysMan, SysModel);
    QVERIFY(cached != nullptr);
    QCOMPARE(cached->isUser(), true);
    QCOMPARE(cached->definitionSourceFile(), target);
    QJsonObject saved = waitForEvent(spy, QStringLiteral("fixturedefs.saved"));
    QCOMPARE(saved.value(QStringLiteral("defRevision")).toInt(), 1);
    QCOMPARE(saved.value(QStringLiteral("definition")).toObject().value(QStringLiteral("isUser")).toBool(), true);

    // Library revision moved on: an unrelated session opened earlier is stale.
    QCOMPARE(callError(QStringLiteral("fixturedefs.save"), save), QStringLiteral("CONFLICT"));
    QJsonArray entries = callOk(QStringLiteral("fixturedefs.list"), QJsonObject()).value(QStringLiteral("entries")).toArray();
    QCOMPARE(entries.first().toObject().value(QStringLiteral("defRevision")).toInt(), 1);
    QCOMPARE(entries.first().toObject().value(QStringLiteral("isUser")).toBool(), true);
}

void ApiFixtureDefsDomain_Test::deleteSystemIsReadOnly()
{
    hello();
    QJsonObject p;
    p.insert(QStringLiteral("manufacturer"), SysMan);
    p.insert(QStringLiteral("model"), SysModel);
    p.insert(QStringLiteral("baseRevision"), 0);
    QCOMPARE(callError(QStringLiteral("fixturedefs.delete"), p), QStringLiteral("FIXTUREDEFS_SYSTEM_READONLY"));
    QVERIFY(m_doc->fixtureDefCache()->fixtureDef(SysMan, SysModel) != nullptr);
    p.insert(QStringLiteral("baseRevision"), 3);
    QCOMPARE(callError(QStringLiteral("fixturedefs.delete"), p), QStringLiteral("CONFLICT"));
    p.insert(QStringLiteral("model"), QStringLiteral("Nope"));
    QCOMPARE(callError(QStringLiteral("fixturedefs.delete"), p), QStringLiteral("NOT_FOUND"));
}

void ApiFixtureDefsDomain_Test::deleteUserCopyRestoresBundledDefinition()
{
    // Stage a bundled definition the way an install ships one: a .qxf under the
    // system fixture directory plus its FixturesMap.xml entry (manufacturer
    // "Bundled Co" is stored as "Bundled_Co" in the map, like the real map does).
    // On Windows/macOS that directory is applicationDirPath-relative, i.e. in the
    // build tree; skip where it is an install prefix we must not write into.
    QDir sysDir = QLCFixtureDefCache::systemDefinitionDirectory();
    const QString mapPath = sysDir.absoluteFilePath(QStringLiteral("FixturesMap.xml"));
    if (QFile::exists(mapPath))
        QSKIP("the system fixture directory already has a FixturesMap.xml; not overwriting it");
    QDir().mkpath(sysDir.absoluteFilePath(QStringLiteral("Bundled_Co")));
    QFile map(mapPath);
    if (map.open(QIODevice::WriteOnly | QIODevice::Text) == false)
        QSKIP("system fixture directory is not writable here");
    map.write("<?xml version='1.0' encoding='UTF-8'?>\n<!DOCTYPE FixturesMap>\n"
              "<FixturesMap xmlns=\"http://www.qlcplus.org/FixturesMap\">\n"
              " <M n=\"Bundled_Co\">\n  <F n=\"Bundled-Co-Lamp\" m=\"Lamp\"/>\n </M>\n</FixturesMap>\n");
    map.close();
    struct Cleanup { QDir dir; ~Cleanup() { QFile::remove(dir.absoluteFilePath(QStringLiteral("FixturesMap.xml")));
                                            QDir(dir.absoluteFilePath(QStringLiteral("Bundled_Co"))).removeRecursively(); } } cleanup{ sysDir };

    QLCFixtureDef *bundled = makeSystemDef();
    bundled->setManufacturer(QStringLiteral("Bundled Co"));
    bundled->setModel(QStringLiteral("Lamp"));
    bundled->setAuthor(QStringLiteral("bundled author"));
    const QString bundledPath = sysDir.absoluteFilePath(QStringLiteral("Bundled_Co/Bundled-Co-Lamp.qxf"));
    QCOMPARE(bundled->saveXML(bundledPath), QFile::NoError);
    delete bundled;
    QVERIFY(m_doc->fixtureDefCache()->loadQXF(bundledPath, false));

    hello();
    // fork -> edit -> save: the user copy shadows the bundled definition
    QString sid = openSession(QStringLiteral("Bundled Co"), QStringLiteral("Lamp")).value(QStringLiteral("sessionId")).toString();
    callOk(QStringLiteral("fixturedefs.session.forkToUser"), params(sid, 0));
    QJsonObject upd = params(sid, 1);
    upd.insert(QStringLiteral("author"), QStringLiteral("user author"));
    callOk(QStringLiteral("fixturedefs.session.update"), upd);
    QJsonObject save;
    save.insert(QStringLiteral("sessionId"), sid);
    save.insert(QStringLiteral("baseRevision"), 0);
    QCOMPARE(callOk(QStringLiteral("fixturedefs.save"), save).value(QStringLiteral("defRevision")).toInt(), 1);
    QLCFixtureDef *cached = m_doc->fixtureDefCache()->fixtureDef(QStringLiteral("Bundled Co"), QStringLiteral("Lamp"));
    QVERIFY(cached != nullptr);
    QCOMPARE(cached->isUser(), true);
    QCOMPARE(cached->author(), QStringLiteral("user author"));

    // delete the user copy: the bundled definition is back in the library
    QJsonObject del;
    del.insert(QStringLiteral("manufacturer"), QStringLiteral("Bundled Co"));
    del.insert(QStringLiteral("model"), QStringLiteral("Lamp"));
    del.insert(QStringLiteral("baseRevision"), 1);
    callOk(QStringLiteral("fixturedefs.delete"), del);
    cached = m_doc->fixtureDefCache()->fixtureDef(QStringLiteral("Bundled Co"), QStringLiteral("Lamp"));
    QVERIFY(cached != nullptr);
    QCOMPARE(cached->isUser(), false);
    QCOMPARE(cached->author(), QStringLiteral("bundled author"));
    QVERIFY(QFile::exists(bundledPath));

    QJsonObject filter;
    filter.insert(QStringLiteral("manufacturer"), QStringLiteral("Bundled Co"));
    QJsonArray entries = callOk(QStringLiteral("fixturedefs.list"), filter).value(QStringLiteral("entries")).toArray();
    QCOMPARE(entries.count(), 1);
    QCOMPARE(entries.first().toObject().value(QStringLiteral("isUser")).toBool(), false);
    QCOMPARE(entries.first().toObject().value(QStringLiteral("defRevision")).toInt(), 2);

    // a bundled definition itself still cannot be deleted
    del.insert(QStringLiteral("baseRevision"), 2);
    QCOMPARE(callError(QStringLiteral("fixturedefs.delete"), del), QStringLiteral("FIXTUREDEFS_SYSTEM_READONLY"));
}

void ApiFixtureDefsDomain_Test::deleteInUseIsRejected()
{
    hello();
    // Make a user definition by forking + saving the seeded one.
    QString sid = openSession(SysMan, SysModel).value(QStringLiteral("sessionId")).toString();
    callOk(QStringLiteral("fixturedefs.session.forkToUser"), params(sid, 0));
    QJsonObject save;
    save.insert(QStringLiteral("sessionId"), sid);
    save.insert(QStringLiteral("baseRevision"), 0);
    QCOMPARE(callOk(QStringLiteral("fixturedefs.save"), save).value(QStringLiteral("defRevision")).toInt(), 1);

    QLCFixtureDef *def = m_doc->fixtureDefCache()->fixtureDef(SysMan, SysModel);
    QVERIFY(def != nullptr);
    Fixture *fixture = new Fixture(m_doc);
    fixture->setName(QStringLiteral("Test fixture"));
    fixture->setFixtureDefinition(def, def->modes().first());
    fixture->setAddress(0);
    fixture->setUniverse(0);
    QVERIFY(m_doc->addFixture(fixture));

    QJsonObject p;
    p.insert(QStringLiteral("manufacturer"), SysMan);
    p.insert(QStringLiteral("model"), SysModel);
    p.insert(QStringLiteral("baseRevision"), 1);
    QJsonObject details;
    QCOMPARE(callError(QStringLiteral("fixturedefs.delete"), p, &details), QStringLiteral("FIXTUREDEFS_IN_USE"));
    QCOMPARE(details.value(QStringLiteral("fixtureIds")).toArray().count(), 1);
    QVERIFY(m_doc->fixtureDefCache()->fixtureDef(SysMan, SysModel) != nullptr);

    // Saving again while the fixture is patched must keep it on a live mode.
    QJsonObject up = params(sid, 1);
    up.insert(QStringLiteral("author"), QStringLiteral("edited"));
    callOk(QStringLiteral("fixturedefs.session.update"), up);
    save.insert(QStringLiteral("baseRevision"), 1);
    QCOMPARE(callOk(QStringLiteral("fixturedefs.save"), save).value(QStringLiteral("defRevision")).toInt(), 2);
    QVERIFY(fixture->fixtureDef() != nullptr);
    QVERIFY(fixture->fixtureMode() != nullptr);
    QCOMPARE(fixture->fixtureMode()->name(), QStringLiteral("1 Channel"));
    QCOMPARE(fixture->fixtureDef()->author(), QStringLiteral("edited"));
    QCOMPARE(fixture->channels(), 1u);
}

void ApiFixtureDefsDomain_Test::importCreatesUserSession()
{
    hello();
    QSignalSpy spy(m_client, &QWebSocket::textMessageReceived);
    QJsonObject exportParams;
    exportParams.insert(QStringLiteral("manufacturer"), SysMan);
    exportParams.insert(QStringLiteral("model"), SysModel);
    QJsonObject exported = callOk(QStringLiteral("fixturedefs.export"), exportParams);
    QVERIFY(exported.value(QStringLiteral("fileName")).toString().endsWith(QStringLiteral(".qxf")));
    QByteArray xml = QByteArray::fromBase64(exported.value(QStringLiteral("qxfBase64")).toString().toLatin1());
    QVERIFY(xml.contains("<Model>SysModel</Model>"));

    // Import it back under a different name (path components are stripped).
    QJsonObject p;
    p.insert(QStringLiteral("fileName"), QStringLiteral("../evil/Imported-Thing.qxf"));
    p.insert(QStringLiteral("qxfBase64"), exported.value(QStringLiteral("qxfBase64")));
    QJsonObject result = callOk(QStringLiteral("fixturedefs.session.import"), p);
    QCOMPARE(result.value(QStringLiteral("isUser")).toBool(), true);
    // The library has SysMan/SysModel already, so the import carries its revision.
    QCOMPARE(result.value(QStringLiteral("baseRevision")).toInt(), 0);
    QJsonObject def = result.value(QStringLiteral("definition")).toObject();
    QCOMPARE(def.value(QStringLiteral("channels")).toArray().count(), 1);
    QString target = def.value(QStringLiteral("sourceFile")).toString();
    QCOMPARE(QFileInfo(target).fileName(), QStringLiteral("Imported-Thing.qxf"));
    QCOMPARE(QDir(QFileInfo(target).absolutePath()).absolutePath(), QDir(m_userDir->path()).absolutePath());
    QCOMPARE(waitForEvent(spy, QStringLiteral("fixturedefs.session.opened")).value(QStringLiteral("source")).toString(),
             QStringLiteral("imported"));

    p.insert(QStringLiteral("qxfBase64"), QString::fromLatin1(QByteArray("<nonsense/>").toBase64()));
    QCOMPARE(callError(QStringLiteral("fixturedefs.session.import"), p), QStringLiteral("INVALID_PARAMS"));
    p.insert(QStringLiteral("qxfBase64"), QString());
    QCOMPARE(callError(QStringLiteral("fixturedefs.session.import"), p), QStringLiteral("INVALID_PARAMS"));

    QJsonObject bogus;
    QCOMPARE(callError(QStringLiteral("fixturedefs.export"), bogus), QStringLiteral("NOT_FOUND"));
    bogus.insert(QStringLiteral("sessionId"), QStringLiteral("fxs-999"));
    QCOMPARE(callError(QStringLiteral("fixturedefs.export"), bogus), QStringLiteral("NOT_FOUND"));
}

void ApiFixtureDefsDomain_Test::fullRoundTrip()
{
    QString clientId = hello();
    QSignalSpy spy(m_client, &QWebSocket::textMessageReceived);
    QJsonObject created = createSession();
    QString sid = created.value(QStringLiteral("sessionId")).toString();
    int rev = 0;

    // Saving a nameless definition is refused up front.
    QJsonObject save;
    save.insert(QStringLiteral("sessionId"), sid);
    save.insert(QStringLiteral("baseRevision"), QJsonValue::Null);
    QCOMPARE(callError(QStringLiteral("fixturedefs.save"), save), QStringLiteral("INVALID_PARAMS"));

    QJsonObject p = params(sid, rev++);
    p.insert(QStringLiteral("manufacturer"), QStringLiteral("Test Co"));
    p.insert(QStringLiteral("model"), QStringLiteral("Beam 1"));
    p.insert(QStringLiteral("type"), QStringLiteral("Moving Head"));
    p.insert(QStringLiteral("author"), QStringLiteral("round trip"));
    callOk(QStringLiteral("fixturedefs.session.update"), p);

    // Channels: dimmer (preset), pan + pan fine, colour wheel with capabilities.
    p = params(sid, rev++);
    p.insert(QStringLiteral("name"), QStringLiteral("Dimmer"));
    p.insert(QStringLiteral("preset"), QStringLiteral("IntensityDimmer"));
    QString dimmer = callOk(QStringLiteral("fixturedefs.channel.add"), p).value(QStringLiteral("channelId")).toString();
    p = params(sid, rev++);
    p.insert(QStringLiteral("name"), QStringLiteral("Pan"));
    p.insert(QStringLiteral("preset"), QStringLiteral("PositionPan"));
    QString pan = callOk(QStringLiteral("fixturedefs.channel.add"), p).value(QStringLiteral("channelId")).toString();
    p = params(sid, rev++);
    p.insert(QStringLiteral("name"), QStringLiteral("Pan fine"));
    p.insert(QStringLiteral("preset"), QStringLiteral("PositionPanFine"));
    QString panFine = callOk(QStringLiteral("fixturedefs.channel.add"), p).value(QStringLiteral("channelId")).toString();
    p = params(sid, rev++);
    p.insert(QStringLiteral("name"), QStringLiteral("Color"));
    p.insert(QStringLiteral("group"), QStringLiteral("Colour"));
    QString color = callOk(QStringLiteral("fixturedefs.channel.add"), p).value(QStringLiteral("channelId")).toString();
    p = params(sid, rev++);
    p.insert(QStringLiteral("channelId"), color);
    p.insert(QStringLiteral("capabilityIndex"), 0);
    p.insert(QStringLiteral("max"), 9);
    p.insert(QStringLiteral("name"), QStringLiteral("White"));
    p.insert(QStringLiteral("preset"), QStringLiteral("ColorMacro"));
    p.insert(QStringLiteral("resources"), QJsonArray{ QStringLiteral("#ffffff") });
    callOk(QStringLiteral("fixturedefs.channel.capability.update"), p);
    p = params(sid, rev++);
    p.insert(QStringLiteral("channelId"), color);
    p.insert(QStringLiteral("min"), 10);
    p.insert(QStringLiteral("max"), 19);
    p.insert(QStringLiteral("name"), QStringLiteral("Speed alias"));
    QCOMPARE(callOk(QStringLiteral("fixturedefs.channel.capability.add"), p).value(QStringLiteral("capabilityIndex")).toInt(), 1);
    p = params(sid, rev++);
    p.insert(QStringLiteral("channelId"), color);
    p.insert(QStringLiteral("capabilityIndex"), 1);
    p.insert(QStringLiteral("preset"), QStringLiteral("Alias"));
    callOk(QStringLiteral("fixturedefs.channel.capability.update"), p);

    // Modes + heads + physical.
    p = params(sid, rev++);
    p.insert(QStringLiteral("name"), QStringLiteral("Standard"));
    QString modeId = callOk(QStringLiteral("fixturedefs.mode.add"), p).value(QStringLiteral("modeId")).toString();
    QJsonArray slotArray;
    for (const QString &chId : { dimmer, pan, panFine, color })
    {
        QJsonObject s;
        s.insert(QStringLiteral("channelId"), chId);
        if (chId == panFine)
            s.insert(QStringLiteral("actsOnChannelId"), pan);
        slotArray.append(s);
    }
    p = params(sid, rev++);
    p.insert(QStringLiteral("modeId"), modeId);
    p.insert(QStringLiteral("channels"), slotArray);
    callOk(QStringLiteral("fixturedefs.mode.setChannels"), p);
    p = params(sid, rev++);
    p.insert(QStringLiteral("modeId"), modeId);
    p.insert(QStringLiteral("channelIds"), strings({ dimmer, color }));
    callOk(QStringLiteral("fixturedefs.mode.head.add"), p);
    QJsonObject phy;
    phy.insert(QStringLiteral("bulbType"), QStringLiteral("LED 60W"));
    phy.insert(QStringLiteral("focusPanMax"), 540);
    phy.insert(QStringLiteral("dmxConnector"), QStringLiteral("3-pin"));
    p = params(sid, rev++);
    p.insert(QStringLiteral("physical"), phy);
    callOk(QStringLiteral("fixturedefs.session.setPhysical"), p);
    QJsonObject modePhy;
    modePhy.insert(QStringLiteral("weight"), 7.5);
    p = params(sid, rev++);
    p.insert(QStringLiteral("modeId"), modeId);
    p.insert(QStringLiteral("useGlobalPhysical"), false);
    p.insert(QStringLiteral("physical"), modePhy);
    callOk(QStringLiteral("fixturedefs.mode.setPhysical"), p);

    // Alias: while Color is 10-19 in "Standard", Color is replaced by Pan.
    p = params(sid, rev++);
    p.insert(QStringLiteral("channelId"), color);
    p.insert(QStringLiteral("capabilityIndex"), 1);
    p.insert(QStringLiteral("targetMode"), QStringLiteral("Standard"));
    p.insert(QStringLiteral("targetChannel"), QStringLiteral("Pan"));
    callOk(QStringLiteral("fixturedefs.channel.capability.alias.add"), p);

    QJsonObject validateParams;
    validateParams.insert(QStringLiteral("sessionId"), sid);
    QJsonArray warnings = callOk(QStringLiteral("fixturedefs.session.validate"), validateParams).value(QStringLiteral("warnings")).toArray();
    QCOMPARE(warnings.count(), 0);

    // Save (never saved before: baseRevision null).
    QJsonObject result = callOk(QStringLiteral("fixturedefs.save"), save);
    QCOMPARE(result.value(QStringLiteral("defRevision")).toInt(), 1);
    QCOMPARE(result.value(QStringLiteral("warnings")).toArray().count(), 0);
    QCOMPARE(result.value(QStringLiteral("model")).toString(), QStringLiteral("Beam 1"));
    QString file = QDir(m_userDir->path()).absoluteFilePath(QStringLiteral("Test-Co-Beam-1.qxf"));
    QVERIFY(QFile::exists(file));
    QJsonObject savedEvent = waitForEvent(spy, QStringLiteral("fixturedefs.saved"));
    QCOMPARE(savedEvent.value(QStringLiteral("__originClientId")).toString(), clientId);
    QCOMPARE(savedEvent.value(QStringLiteral("definition")).toObject().value(QStringLiteral("sourceFile")).toString(), file);
    QLCFixtureDef *cached = m_doc->fixtureDefCache()->fixtureDef(QStringLiteral("Test Co"), QStringLiteral("Beam 1"));
    QVERIFY(cached != nullptr);
    QCOMPARE(cached->isUser(), true);
    QCOMPARE(cached->channels().count(), 4);

    QJsonArray sessions = callOk(QStringLiteral("fixturedefs.session.list"), QJsonObject()).value(QStringLiteral("sessions")).toArray();
    QCOMPARE(sessions.first().toObject().value(QStringLiteral("isModified")).toBool(), false);
    QCOMPARE(sessions.first().toObject().value(QStringLiteral("baseRevision")).toInt(), 1);

    // A second save against the old (null) library revision conflicts; the
    // fresh one goes through and bumps defRevision.
    QCOMPARE(callError(QStringLiteral("fixturedefs.save"), save), QStringLiteral("CONFLICT"));
    save.insert(QStringLiteral("baseRevision"), 1);
    QCOMPARE(callOk(QStringLiteral("fixturedefs.save"), save).value(QStringLiteral("defRevision")).toInt(), 2);

    // Re-open from the library: everything round-tripped through the QXF.
    QJsonObject reopened = openSession(QStringLiteral("Test Co"), QStringLiteral("Beam 1"));
    QCOMPARE(reopened.value(QStringLiteral("isUser")).toBool(), true);
    QCOMPARE(reopened.value(QStringLiteral("baseRevision")).toInt(), 2);
    QJsonObject def = reopened.value(QStringLiteral("definition")).toObject();
    QCOMPARE(def.value(QStringLiteral("type")).toString(), QStringLiteral("Moving Head"));
    QCOMPARE(def.value(QStringLiteral("author")).toString(), QStringLiteral("round trip"));
    QCOMPARE(def.value(QStringLiteral("sourceFile")).toString(), file);
    QCOMPARE(def.value(QStringLiteral("physical")).toObject().value(QStringLiteral("bulbType")).toString(), QStringLiteral("LED 60W"));
    QCOMPARE(def.value(QStringLiteral("physical")).toObject().value(QStringLiteral("focusPanMax")).toInt(), 540);
    QJsonArray channels = def.value(QStringLiteral("channels")).toArray();
    QCOMPARE(channels.count(), 4);
    QJsonObject colorCh;
    QString panId, panFineId, colorId, dimmerId;
    for (const QJsonValue &v : channels)
    {
        QJsonObject c = v.toObject();
        const QString name = c.value(QStringLiteral("name")).toString();
        if (name == QStringLiteral("Color")) { colorCh = c; colorId = c.value(QStringLiteral("channelId")).toString(); }
        else if (name == QStringLiteral("Pan")) panId = c.value(QStringLiteral("channelId")).toString();
        else if (name == QStringLiteral("Pan fine")) panFineId = c.value(QStringLiteral("channelId")).toString();
        else if (name == QStringLiteral("Dimmer")) dimmerId = c.value(QStringLiteral("channelId")).toString();
    }
    QVERIFY(colorCh.isEmpty() == false);
    QCOMPARE(colorCh.value(QStringLiteral("group")).toString(), QStringLiteral("Colour"));
    QJsonArray colorCaps = colorCh.value(QStringLiteral("capabilities")).toArray();
    QCOMPARE(colorCaps.count(), 2);
    QCOMPARE(colorCaps.at(0).toObject().value(QStringLiteral("preset")).toString(), QStringLiteral("ColorMacro"));
    QCOMPARE(colorCaps.at(0).toObject().value(QStringLiteral("resources")).toArray(), QJsonArray{ QStringLiteral("#ffffff") });
    QCOMPARE(colorCaps.at(1).toObject().value(QStringLiteral("preset")).toString(), QStringLiteral("Alias"));
    QJsonArray aliases = colorCaps.at(1).toObject().value(QStringLiteral("aliases")).toArray();
    QCOMPARE(aliases.count(), 1);
    QCOMPARE(aliases.first().toObject().value(QStringLiteral("targetMode")).toString(), QStringLiteral("Standard"));
    QCOMPARE(aliases.first().toObject().value(QStringLiteral("targetChannel")).toString(), QStringLiteral("Pan"));
    QJsonArray modes = def.value(QStringLiteral("modes")).toArray();
    QCOMPARE(modes.count(), 1);
    QJsonObject mode = modes.first().toObject();
    QCOMPARE(mode.value(QStringLiteral("name")).toString(), QStringLiteral("Standard"));
    QJsonArray modeSlots = mode.value(QStringLiteral("channels")).toArray();
    QCOMPARE(modeSlots.count(), 4);
    QCOMPARE(modeSlots.at(0).toObject().value(QStringLiteral("channelId")).toString(), dimmerId);
    QCOMPARE(modeSlots.at(2).toObject().value(QStringLiteral("channelId")).toString(), panFineId);
    QCOMPARE(modeSlots.at(2).toObject().value(QStringLiteral("actsOnChannelId")).toString(), panId);
    QJsonArray heads = mode.value(QStringLiteral("heads")).toArray();
    QCOMPARE(heads.count(), 1);
    QCOMPARE(heads.first().toObject().value(QStringLiteral("channelIds")).toArray(), strings({ dimmerId, colorId }));
    QCOMPARE(mode.value(QStringLiteral("useGlobalPhysical")).toBool(), false);
    QCOMPARE(mode.value(QStringLiteral("physical")).toObject().value(QStringLiteral("weight")).toDouble(), 7.5);
    QCOMPARE(mode.value(QStringLiteral("physical")).toObject().value(QStringLiteral("bulbType")).toString(), QStringLiteral("LED 60W"));

    // Export from the session and from the library: both are the same QXF.
    QJsonObject exportSession;
    exportSession.insert(QStringLiteral("sessionId"), sid);
    QJsonObject exported = callOk(QStringLiteral("fixturedefs.export"), exportSession);
    QCOMPARE(exported.value(QStringLiteral("fileName")).toString(), QStringLiteral("Test-Co-Beam-1.qxf"));
    QByteArray xml = QByteArray::fromBase64(exported.value(QStringLiteral("qxfBase64")).toString().toLatin1());
    QVERIFY(xml.startsWith("<?xml"));
    QVERIFY(xml.contains("<Model>Beam 1</Model>"));
    QVERIFY(xml.contains("<Alias Mode=\"Standard\" Channel=\"Color\" With=\"Pan\"/>"));
    QJsonObject exportLibrary;
    exportLibrary.insert(QStringLiteral("manufacturer"), QStringLiteral("Test Co"));
    exportLibrary.insert(QStringLiteral("model"), QStringLiteral("Beam 1"));
    QByteArray libraryXml = QByteArray::fromBase64(callOk(QStringLiteral("fixturedefs.export"), exportLibrary).value(QStringLiteral("qxfBase64")).toString().toLatin1());
    QCOMPARE(libraryXml, xml);

    // Delete: stale revision conflicts, the right one removes file + cache.
    QJsonObject del;
    del.insert(QStringLiteral("manufacturer"), QStringLiteral("Test Co"));
    del.insert(QStringLiteral("model"), QStringLiteral("Beam 1"));
    del.insert(QStringLiteral("baseRevision"), 1);
    QCOMPARE(callError(QStringLiteral("fixturedefs.delete"), del), QStringLiteral("CONFLICT"));
    del.insert(QStringLiteral("baseRevision"), 2);
    result = callOk(QStringLiteral("fixturedefs.delete"), del);
    QCOMPARE(result.value(QStringLiteral("model")).toString(), QStringLiteral("Beam 1"));
    QJsonObject deleted = waitForEvent(spy, QStringLiteral("fixturedefs.deleted"));
    QCOMPARE(deleted.value(QStringLiteral("manufacturer")).toString(), QStringLiteral("Test Co"));
    QVERIFY(QFile::exists(file) == false);
    QVERIFY(m_doc->fixtureDefCache()->fixtureDef(QStringLiteral("Test Co"), QStringLiteral("Beam 1")) == nullptr);
    QJsonArray entries = callOk(QStringLiteral("fixturedefs.list"), QJsonObject()).value(QStringLiteral("entries")).toArray();
    QCOMPARE(entries.count(), 1); // only the seeded system definition remains

    // The still-open sessions are untouched, and saving one again re-creates
    // the library entry with the counter continuing past the deleted one
    // (delete itself bumped it to 3, so the re-created entry is 4).
    save.insert(QStringLiteral("baseRevision"), QJsonValue::Null);
    QCOMPARE(callOk(QStringLiteral("fixturedefs.save"), save).value(QStringLiteral("defRevision")).toInt(), 4);
    QVERIFY(QFile::exists(file));
}

QTEST_MAIN(ApiFixtureDefsDomain_Test)
