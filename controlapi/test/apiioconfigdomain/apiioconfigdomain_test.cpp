/*
  Q Light Controller Plus - Control API unit test
  apiioconfigdomain_test.cpp

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
#include <QFile>
#include <QSettings>
#include <QTemporaryDir>
#include <QCoreApplication>

#include "apiioconfigdomain_test.h"
#include "apiserver.h"
#include "mastertimer.h"
#include "inputoutputmap.h"
#include "ioplugincache.h"
#include "iopluginstub.h"
#include "qlcinputprofile.h"
#include "qlcinputchannel.h"
#include "qlcfile.h"
#include "outputpatch.h"
#include "inputpatch.h"
#include "grandmaster.h"
#include "universe.h"
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

static QJsonObject parseFrame(const QList<QVariant> &frame)
{
    return QJsonDocument::fromJson(frame.at(0).toString().toUtf8()).object();
}

void ApiIoConfigDomain_Test::initTestCase()
{
    // io.audio.setDevice writes QSettings: keep them out of the real
    // "qlcplus" organization the desktop app uses.
    QCoreApplication::setOrganizationName(QStringLiteral("qlcplus-controlapi-test"));
    QCoreApplication::setApplicationName(QStringLiteral("apiioconfigdomain_test"));
}

void ApiIoConfigDomain_Test::init()
{
    m_profileDir = new QTemporaryDir();
    QVERIFY(m_profileDir->isValid());
    qputenv(USER_INPUTPROFILE_DIR_ENV, m_profileDir->path().toUtf8());

    m_doc = new Doc(nullptr);
    // MasterTimer + universe threads: the keypad case checks the value
    // reaches io.dmx.universe.get, the learn case needs the input buffer
    // flushed (Universe::tick -> flushInput) - see apiiodomain_test.cpp.
    m_doc->masterTimer()->start();
    m_doc->inputOutputMap()->startUniverses();
    m_apiServer = new ApiServer(nullptr, m_doc);
    QVERIFY(m_apiServer->listen(0, QHostAddress::LocalHost));

    m_client = new QWebSocket();
    m_client->open(QUrl(QStringLiteral("ws://127.0.0.1:%1/qlcplusapi").arg(m_apiServer->serverPort())));
    QVERIFY(QTest::qWaitFor([this]() { return m_client->state() == QAbstractSocket::ConnectedState; }, 2000));
}

void ApiIoConfigDomain_Test::cleanup()
{
    qDeleteAll(m_extraClients);
    m_extraClients.clear();
    delete m_client;
    m_client = nullptr;
    delete m_apiServer;
    m_apiServer = nullptr;
    m_doc->masterTimer()->stop();
    delete m_doc;
    m_doc = nullptr;
    delete m_profileDir;
    m_profileDir = nullptr;
    qunsetenv(USER_INPUTPROFILE_DIR_ENV);

    QSettings settings;
    settings.remove(QStringLiteral("audio"));
}

QJsonObject ApiIoConfigDomain_Test::sendAndWaitForReply(QWebSocket *client, const QString &method, const QJsonObject &params)
{
    const QString requestId = QStringLiteral("t-1");
    QSignalSpy spy(client, &QWebSocket::textMessageReceived);
    client->sendTextMessage(buildRequest(method, params, requestId));

    QJsonObject found;
    (void)QTest::qWaitFor([&]()
    {
        for (const QList<QVariant> &frame : spy)
        {
            QJsonObject obj = parseFrame(frame);
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

QJsonObject ApiIoConfigDomain_Test::sendAndWaitForReply(const QString &method, const QJsonObject &params)
{
    return sendAndWaitForReply(m_client, method, params);
}

QString ApiIoConfigDomain_Test::hello(QWebSocket *client)
{
    QJsonObject reply = sendAndWaitForReply(client, QStringLiteral("hello"), QJsonObject());
    return reply.value(QStringLiteral("result")).toObject().value(QStringLiteral("clientId")).toString();
}

QWebSocket *ApiIoConfigDomain_Test::connectSecondClient()
{
    QWebSocket *client = new QWebSocket();
    m_extraClients.append(client);
    client->open(QUrl(QStringLiteral("ws://127.0.0.1:%1/qlcplusapi").arg(m_apiServer->serverPort())));
    (void)QTest::qWaitFor([client]() { return client->state() == QAbstractSocket::ConnectedState; }, 2000);
    return client;
}

IOPluginStub *ApiIoConfigDomain_Test::loadStubPlugin()
{
    QDir dir(QCoreApplication::applicationDirPath() + QStringLiteral("/../../../engine/test/iopluginstub"));
    dir.setFilter(QDir::Files);
    dir.setNameFilters(QStringList() << QStringLiteral("*%1").arg(KExtPlugin));
    m_doc->ioPluginCache()->load(dir);
    QList<QLCIOPlugin *> plugins = m_doc->ioPluginCache()->plugins();
    if (plugins.isEmpty())
        return nullptr;
    // Same header, same build: engine/test/inputpatch does this too. Virtual
    // calls must still go through the QLCIOPlugin vtable (the DLL's code) -
    // IOPluginStub is final, so stub->name() would be devirtualised into a
    // symbol this binary does not link. Hence stubName() below.
    return static_cast<IOPluginStub *>(plugins.first());
}

QString ApiIoConfigDomain_Test::stubName(IOPluginStub *stub) const
{
    return static_cast<QLCIOPlugin *>(stub)->name();
}

void ApiIoConfigDomain_Test::patchStubOutput(IOPluginStub *stub, quint32 universeId, quint32 line)
{
    QVERIFY(m_doc->inputOutputMap()->setOutputPatch(universeId, stubName(stub), QString(), QString(), line, false, 0));
}

void ApiIoConfigDomain_Test::patchStubInput(IOPluginStub *stub, quint32 universeId, quint32 line, const QString &profile)
{
    QVERIFY(m_doc->inputOutputMap()->setInputPatch(universeId, stubName(stub), QString(), QString(), line, profile));
}

QList<QJsonObject> ApiIoConfigDomain_Test::eventsWithTopic(QSignalSpy &spy, const QString &topic)
{
    QList<QJsonObject> events;
    for (const QList<QVariant> &frame : spy)
    {
        QJsonObject obj = parseFrame(frame);
        if (obj.value(QStringLiteral("type")).toString() == QStringLiteral("event") &&
            obj.value(QStringLiteral("topic")).toString() == topic)
            events.append(obj);
    }
    return events;
}

QJsonObject ApiIoConfigDomain_Test::sampleProfile(const QString &model, const QString &channelName)
{
    QJsonObject channel;
    channel.insert(QStringLiteral("number"), 5);
    channel.insert(QStringLiteral("name"), channelName);
    channel.insert(QStringLiteral("type"), QStringLiteral("Slider"));
    channel.insert(QStringLiteral("movementType"), QStringLiteral("Relative"));
    channel.insert(QStringLiteral("movementSensitivity"), 33);
    channel.insert(QStringLiteral("sendExtraPress"), false);
    channel.insert(QStringLiteral("lowerValue"), 10);
    channel.insert(QStringLiteral("upperValue"), 200);

    // Custom feedback (lower/upper value, MIDI channel) is a Button-only
    // thing in the .qxi format (QLCInputChannel::saveXML) - a second channel
    // carries it.
    QJsonObject button;
    button.insert(QStringLiteral("number"), 9);
    button.insert(QStringLiteral("name"), QStringLiteral("Go"));
    button.insert(QStringLiteral("type"), QStringLiteral("Button"));
    button.insert(QStringLiteral("sendExtraPress"), true);
    button.insert(QStringLiteral("lowerValue"), 10);
    button.insert(QStringLiteral("upperValue"), 200);
    button.insert(QStringLiteral("lowerChannel"), 3);

    QJsonObject color;
    color.insert(QStringLiteral("value"), 1);
    color.insert(QStringLiteral("label"), QStringLiteral("Red"));
    color.insert(QStringLiteral("color"), QStringLiteral("#ff0000"));

    QJsonObject midiChannel;
    midiChannel.insert(QStringLiteral("channel"), 0);
    midiChannel.insert(QStringLiteral("label"), QStringLiteral("Main"));

    QJsonObject profile;
    profile.insert(QStringLiteral("manufacturer"), QStringLiteral("Acme"));
    profile.insert(QStringLiteral("model"), model);
    profile.insert(QStringLiteral("type"), QStringLiteral("MIDI"));
    profile.insert(QStringLiteral("midiSendNoteOff"), false);
    profile.insert(QStringLiteral("channels"), QJsonArray() << channel << button);
    profile.insert(QStringLiteral("colorTable"), QJsonArray() << color);
    profile.insert(QStringLiteral("midiChannelTable"), QJsonArray() << midiChannel);
    return profile;
}

/*********************************************************************
 * io.plugin.*
 *********************************************************************/

void ApiIoConfigDomain_Test::pluginGetLinesDescribesStubLines()
{
    IOPluginStub *stub = loadStubPlugin();
    QVERIFY2(stub != nullptr, "iopluginstub DLL not found - is engine/test built?");
    hello(m_client);

    QJsonObject params;
    params.insert(QStringLiteral("pluginName"), stubName(stub));
    QJsonObject reply = sendAndWaitForReply(QStringLiteral("io.plugin.getLines"), params);
    QCOMPARE(reply.value(QStringLiteral("ok")).toBool(), true);
    QJsonObject result = reply.value(QStringLiteral("result")).toObject();
    QCOMPARE(result.value(QStringLiteral("inputs")).toArray().count(), 4);
    QCOMPARE(result.value(QStringLiteral("outputs")).toArray().count(), 4);
    QJsonObject line = result.value(QStringLiteral("outputs")).toArray().at(2).toObject();
    QCOMPARE(line.value(QStringLiteral("index")).toInt(), 2);
    QCOMPARE(line.value(QStringLiteral("line")).toInt(), 2);
    QCOMPARE(line.value(QStringLiteral("name")).toString(), QStringLiteral("3: Stub 3"));
}

void ApiIoConfigDomain_Test::pluginGetLinesOnUnknownPluginIsNotFound()
{
    hello(m_client);
    QJsonObject params;
    params.insert(QStringLiteral("pluginName"), QStringLiteral("Nope"));
    QJsonObject reply = sendAndWaitForReply(QStringLiteral("io.plugin.getLines"), params);
    QCOMPARE(reply.value(QStringLiteral("ok")).toBool(), false);
    QCOMPARE(reply.value(QStringLiteral("error")).toObject().value(QStringLiteral("code")).toString(), QStringLiteral("NOT_FOUND"));
}

void ApiIoConfigDomain_Test::pluginRescanInvokesStubAndBroadcastsLinesChanged()
{
    IOPluginStub *stub = loadStubPlugin();
    QVERIFY(stub != nullptr);
    hello(m_client);
    QSignalSpy spy(m_client, &QWebSocket::textMessageReceived);

    QJsonObject params;
    params.insert(QStringLiteral("pluginName"), stubName(stub));
    QJsonObject reply = sendAndWaitForReply(QStringLiteral("io.plugin.rescan"), params);
    QCOMPARE(reply.value(QStringLiteral("ok")).toBool(), true);
    QCOMPARE(stub->m_rescanCalled, 1);

    QVERIFY(QTest::qWaitFor([&]() { return eventsWithTopic(spy, QStringLiteral("io.plugin.linesChanged")).count() >= 1; }, 2000));
    QJsonObject data = eventsWithTopic(spy, QStringLiteral("io.plugin.linesChanged")).first().value(QStringLiteral("data")).toObject();
    QCOMPARE(data.value(QStringLiteral("pluginName")).toString(), stubName(stub));
    QCOMPARE(data.value(QStringLiteral("inputs")).toArray().count(), 4);
    QCOMPARE(data.value(QStringLiteral("outputs")).toArray().count(), 4);
}

void ApiIoConfigDomain_Test::pluginRescanOnUnknownPluginIsNotFound()
{
    hello(m_client);
    QJsonObject params;
    params.insert(QStringLiteral("pluginName"), QStringLiteral("Nope"));
    QJsonObject reply = sendAndWaitForReply(QStringLiteral("io.plugin.rescan"), params);
    QCOMPARE(reply.value(QStringLiteral("ok")).toBool(), false);
    QCOMPARE(reply.value(QStringLiteral("error")).toObject().value(QStringLiteral("code")).toString(), QStringLiteral("NOT_FOUND"));
}

void ApiIoConfigDomain_Test::pluginConfigureWithoutDialogIsUnsupported()
{
    IOPluginStub *stub = loadStubPlugin();
    QVERIFY(stub != nullptr);
    stub->m_canConfigure = false;
    hello(m_client);

    QJsonObject params;
    params.insert(QStringLiteral("pluginName"), stubName(stub));
    QJsonObject reply = sendAndWaitForReply(QStringLiteral("io.plugin.configure"), params);
    QCOMPARE(reply.value(QStringLiteral("ok")).toBool(), false);
    QJsonObject error = reply.value(QStringLiteral("error")).toObject();
    QCOMPARE(error.value(QStringLiteral("code")).toString(), QStringLiteral("UNSUPPORTED"));
    QVERIFY(error.value(QStringLiteral("message")).toString().contains(QStringLiteral("no configuration dialog")));
    QCOMPARE(stub->m_configureCalled, 0);
}

void ApiIoConfigDomain_Test::pluginConfigureCallsThroughOnGuiHost()
{
    // This test binary is a QApplication (QT_GUI_LIB comes in through the
    // engine's public Qt6::Gui link, so QTEST_MAIN builds one): a plugin
    // that has a dialog gets configure() called and the caller is told the
    // dialog opened on the host. The stub's configure() also emits
    // configurationChanged(), which must surface as io.plugin.linesChanged.
    // (A QCoreApplication-only host answers UNSUPPORTED instead - see the
    // handler; not reachable from this binary.)
    IOPluginStub *stub = loadStubPlugin();
    QVERIFY(stub != nullptr);
    QVERIFY2(QCoreApplication::instance()->inherits("QGuiApplication"), "expected a GUI test application");
    stub->m_canConfigure = true;
    hello(m_client);
    QSignalSpy spy(m_client, &QWebSocket::textMessageReceived);

    QJsonObject params;
    params.insert(QStringLiteral("pluginName"), stubName(stub));
    QJsonObject reply = sendAndWaitForReply(QStringLiteral("io.plugin.configure"), params);
    QCOMPARE(reply.value(QStringLiteral("ok")).toBool(), true);
    QCOMPARE(reply.value(QStringLiteral("result")).toObject().value(QStringLiteral("openedOnHost")).toBool(), true);
    QCOMPARE(stub->m_configureCalled, 1);
    QVERIFY(QTest::qWaitFor([&]() { return eventsWithTopic(spy, QStringLiteral("io.plugin.linesChanged")).count() >= 1; }, 2000));
}

/*********************************************************************
 * io.patch.setParameters / io.patch.output.setState
 *********************************************************************/

void ApiIoConfigDomain_Test::patchSetParametersStoresAndBroadcastsUniverseUpdated()
{
    IOPluginStub *stub = loadStubPlugin();
    QVERIFY(stub != nullptr);
    patchStubOutput(stub);
    hello(m_client);
    quint32 before = m_doc->docRevision();
    QSignalSpy spy(m_client, &QWebSocket::textMessageReceived);

    QJsonObject parameters;
    parameters.insert(QStringLiteral("outputIP"), QStringLiteral("10.0.0.1"));
    parameters.insert(QStringLiteral("port"), 6454);
    parameters.insert(QStringLiteral("enabled"), true);
    QJsonObject params;
    params.insert(QStringLiteral("universeId"), 0);
    params.insert(QStringLiteral("patchType"), QStringLiteral("output"));
    params.insert(QStringLiteral("index"), 0);
    params.insert(QStringLiteral("parameters"), parameters);
    params.insert(QStringLiteral("baseRevision"), int(before));
    QJsonObject reply = sendAndWaitForReply(QStringLiteral("io.patch.setParameters"), params);
    QCOMPARE(reply.value(QStringLiteral("ok")).toBool(), true);
    QJsonObject result = reply.value(QStringLiteral("result")).toObject();
    QVERIFY(quint32(result.value(QStringLiteral("docRevision")).toInt()) > before);
    QCOMPARE(result.value(QStringLiteral("parameters")).toObject().value(QStringLiteral("port")).toInt(), 6454);

    // Engine state: the plugin holds the parameters for that line.
    QMap<QString, QVariant> stored = m_doc->inputOutputMap()->universe(0)->outputPatch(0)->getPluginParameters();
    QCOMPARE(stored.value(QStringLiteral("outputIP")).toString(), QStringLiteral("10.0.0.1"));
    QCOMPARE(stored.value(QStringLiteral("port")).toInt(), 6454);
    QCOMPARE(stored.value(QStringLiteral("enabled")).toBool(), true);

    // io.universe.updated carries the patch with its parameters.
    QVERIFY(QTest::qWaitFor([&]() { return eventsWithTopic(spy, QStringLiteral("io.universe.updated")).count() >= 1; }, 2000));
    QJsonObject data = eventsWithTopic(spy, QStringLiteral("io.universe.updated")).first().value(QStringLiteral("data")).toObject();
    QJsonObject patch = data.value(QStringLiteral("universe")).toObject().value(QStringLiteral("outputPatches")).toArray().at(0).toObject();
    QCOMPARE(patch.value(QStringLiteral("parameters")).toObject().value(QStringLiteral("outputIP")).toString(), QStringLiteral("10.0.0.1"));

    // And io.universe.get agrees.
    QJsonObject getParams;
    getParams.insert(QStringLiteral("universeId"), 0);
    QJsonObject get = sendAndWaitForReply(QStringLiteral("io.universe.get"), getParams);
    QJsonObject gotPatch = get.value(QStringLiteral("result")).toObject().value(QStringLiteral("outputPatches")).toArray().at(0).toObject();
    QCOMPARE(gotPatch.value(QStringLiteral("parameters")).toObject().value(QStringLiteral("port")).toInt(), 6454);
}

void ApiIoConfigDomain_Test::patchSetParametersNullUnsetsKey()
{
    IOPluginStub *stub = loadStubPlugin();
    QVERIFY(stub != nullptr);
    patchStubOutput(stub);
    hello(m_client);

    QJsonObject parameters;
    parameters.insert(QStringLiteral("port"), 6454);
    QJsonObject params;
    params.insert(QStringLiteral("universeId"), 0);
    params.insert(QStringLiteral("patchType"), QStringLiteral("output"));
    params.insert(QStringLiteral("parameters"), parameters);
    QCOMPARE(sendAndWaitForReply(QStringLiteral("io.patch.setParameters"), params).value(QStringLiteral("ok")).toBool(), true);
    QVERIFY(m_doc->inputOutputMap()->universe(0)->outputPatch(0)->getPluginParameters().contains(QStringLiteral("port")));

    parameters.insert(QStringLiteral("port"), QJsonValue::Null);
    params.insert(QStringLiteral("parameters"), parameters);
    QJsonObject reply = sendAndWaitForReply(QStringLiteral("io.patch.setParameters"), params);
    QCOMPARE(reply.value(QStringLiteral("ok")).toBool(), true);
    QVERIFY(m_doc->inputOutputMap()->universe(0)->outputPatch(0)->getPluginParameters().contains(QStringLiteral("port")) == false);
    QVERIFY(reply.value(QStringLiteral("result")).toObject().value(QStringLiteral("parameters")).toObject().contains(QStringLiteral("port")) == false);
}

void ApiIoConfigDomain_Test::patchSetParametersOnUnpatchedUniverseIsNotFound()
{
    hello(m_client);
    QJsonObject params;
    params.insert(QStringLiteral("universeId"), 0);
    params.insert(QStringLiteral("patchType"), QStringLiteral("input"));
    params.insert(QStringLiteral("parameters"), QJsonObject());
    QJsonObject reply = sendAndWaitForReply(QStringLiteral("io.patch.setParameters"), params);
    QCOMPARE(reply.value(QStringLiteral("ok")).toBool(), false);
    QCOMPARE(reply.value(QStringLiteral("error")).toObject().value(QStringLiteral("code")).toString(), QStringLiteral("NOT_FOUND"));
}

void ApiIoConfigDomain_Test::patchSetParametersWithStaleRevisionConflicts()
{
    IOPluginStub *stub = loadStubPlugin();
    QVERIFY(stub != nullptr);
    patchStubOutput(stub);
    hello(m_client);
    QJsonObject params;
    params.insert(QStringLiteral("universeId"), 0);
    params.insert(QStringLiteral("patchType"), QStringLiteral("output"));
    params.insert(QStringLiteral("parameters"), QJsonObject());
    params.insert(QStringLiteral("baseRevision"), int(m_doc->docRevision()) + 7);
    QJsonObject reply = sendAndWaitForReply(QStringLiteral("io.patch.setParameters"), params);
    QCOMPARE(reply.value(QStringLiteral("ok")).toBool(), false);
    QJsonObject error = reply.value(QStringLiteral("error")).toObject();
    QCOMPARE(error.value(QStringLiteral("code")).toString(), QStringLiteral("CONFLICT"));
    QCOMPARE(error.value(QStringLiteral("details")).toObject().value(QStringLiteral("docRevision")).toInt(), int(m_doc->docRevision()));
}

void ApiIoConfigDomain_Test::patchOutputSetStatePausesAndBroadcasts()
{
    IOPluginStub *stub = loadStubPlugin();
    QVERIFY(stub != nullptr);
    patchStubOutput(stub);
    hello(m_client);
    quint32 before = m_doc->docRevision();
    QSignalSpy spy(m_client, &QWebSocket::textMessageReceived);

    QJsonObject params;
    params.insert(QStringLiteral("universeId"), 0);
    params.insert(QStringLiteral("index"), 0);
    params.insert(QStringLiteral("paused"), true);
    QJsonObject reply = sendAndWaitForReply(QStringLiteral("io.patch.output.setState"), params);
    QCOMPARE(reply.value(QStringLiteral("ok")).toBool(), true);
    QCOMPARE(m_doc->inputOutputMap()->universe(0)->outputPatch(0)->paused(), true);
    QCOMPARE(m_doc->inputOutputMap()->universe(0)->outputPatch(0)->blackout(), false);
    QCOMPARE(m_doc->docRevision(), before); // live only, never structural

    QVERIFY(QTest::qWaitFor([&]() { return eventsWithTopic(spy, QStringLiteral("io.patch.output.stateChanged")).count() >= 1; }, 2000));
    QJsonObject data = eventsWithTopic(spy, QStringLiteral("io.patch.output.stateChanged")).first().value(QStringLiteral("data")).toObject();
    QCOMPARE(data.value(QStringLiteral("universeId")).toInt(), 0);
    QCOMPARE(data.value(QStringLiteral("index")).toInt(), 0);
    QCOMPARE(data.value(QStringLiteral("paused")).toBool(), true);
    QCOMPARE(data.value(QStringLiteral("blackout")).toBool(), false);

    params.remove(QStringLiteral("paused"));
    params.insert(QStringLiteral("blackout"), true);
    QCOMPARE(sendAndWaitForReply(QStringLiteral("io.patch.output.setState"), params).value(QStringLiteral("ok")).toBool(), true);
    QCOMPARE(m_doc->inputOutputMap()->universe(0)->outputPatch(0)->paused(), true);   // untouched
    QCOMPARE(m_doc->inputOutputMap()->universe(0)->outputPatch(0)->blackout(), true);
}

void ApiIoConfigDomain_Test::patchOutputSetStateWithoutFieldsIsInvalidParams()
{
    IOPluginStub *stub = loadStubPlugin();
    QVERIFY(stub != nullptr);
    patchStubOutput(stub);
    hello(m_client);
    QJsonObject params;
    params.insert(QStringLiteral("universeId"), 0);
    params.insert(QStringLiteral("index"), 0);
    QJsonObject reply = sendAndWaitForReply(QStringLiteral("io.patch.output.setState"), params);
    QCOMPARE(reply.value(QStringLiteral("ok")).toBool(), false);
    QCOMPARE(reply.value(QStringLiteral("error")).toObject().value(QStringLiteral("code")).toString(), QStringLiteral("INVALID_PARAMS"));
}

/*********************************************************************
 * io.inputProfile.get / save / delete
 *********************************************************************/

void ApiIoConfigDomain_Test::inputProfileSaveWritesFileAndBumpsRevision()
{
    hello(m_client);
    QSignalSpy spy(m_client, &QWebSocket::textMessageReceived);

    QJsonObject params;
    params.insert(QStringLiteral("profile"), sampleProfile(QStringLiteral("Faderbox"), QStringLiteral("Fader A")));
    params.insert(QStringLiteral("baseRevision"), 0);
    QJsonObject reply = sendAndWaitForReply(QStringLiteral("io.inputProfile.save"), params);
    QCOMPARE(reply.value(QStringLiteral("ok")).toBool(), true);
    QJsonObject result = reply.value(QStringLiteral("result")).toObject();
    QCOMPARE(result.value(QStringLiteral("profilesRevision")).toInt(), 1);
    QCOMPARE(result.value(QStringLiteral("name")).toString(), QStringLiteral("Acme Faderbox"));

    // The file landed in the overridden (temporary) user directory, nowhere else.
    QString path = result.value(QStringLiteral("path")).toString();
    QVERIFY2(QDir::cleanPath(path).startsWith(QDir::cleanPath(m_profileDir->path())), qPrintable(path));
    QVERIFY(QFile::exists(path));
    QCOMPARE(QFileInfo(path).fileName(), QStringLiteral("Acme-Faderbox.qxi"));

    // Loadable back by the engine with everything intact.
    QLCInputProfile *reloaded = QLCInputProfile::loader(path);
    QVERIFY(reloaded != nullptr);
    QCOMPARE(reloaded->type(), QLCInputProfile::MIDI);
    QCOMPARE(reloaded->midiSendNoteOff(), false);
    QVERIFY(reloaded->channel(5) != nullptr);
    QCOMPARE(reloaded->channel(5)->name(), QStringLiteral("Fader A"));
    QCOMPARE(reloaded->channel(5)->type(), QLCInputChannel::Slider);
    QCOMPARE(reloaded->channel(5)->movementType(), QLCInputChannel::Relative);
    QCOMPARE(reloaded->channel(5)->movementSensitivity(), 33);
    QVERIFY(reloaded->channel(9) != nullptr);
    QCOMPARE(reloaded->channel(9)->type(), QLCInputChannel::Button);
    QCOMPARE(reloaded->channel(9)->sendExtraPress(), true);
    QCOMPARE(int(reloaded->channel(9)->lowerValue()), 10);
    QCOMPARE(int(reloaded->channel(9)->upperValue()), 200);
    QCOMPARE(reloaded->channel(9)->lowerChannel(), 3);
    QCOMPARE(reloaded->colorTable().count(), 1);
    QCOMPARE(reloaded->midiChannelTable().value(0), QStringLiteral("Main"));
    delete reloaded;

    // list / get see it with the bumped counter.
    QJsonObject list = sendAndWaitForReply(QStringLiteral("io.inputProfile.list"), QJsonObject()).value(QStringLiteral("result")).toObject();
    QCOMPARE(list.value(QStringLiteral("profiles")).toArray().count(), 1);
    QCOMPARE(list.value(QStringLiteral("profilesRevision")).toInt(), 1);
    QJsonObject getParams;
    getParams.insert(QStringLiteral("name"), QStringLiteral("Acme Faderbox"));
    QJsonObject get = sendAndWaitForReply(QStringLiteral("io.inputProfile.get"), getParams).value(QStringLiteral("result")).toObject();
    QJsonObject profile = get.value(QStringLiteral("profile")).toObject();
    QCOMPARE(profile.value(QStringLiteral("channels")).toArray().count(), 2);
    QCOMPARE(profile.value(QStringLiteral("channels")).toArray().at(0).toObject().value(QStringLiteral("number")).toInt(), 5);
    QCOMPARE(profile.value(QStringLiteral("channels")).toArray().at(1).toObject().value(QStringLiteral("lowerChannel")).toInt(), 3);
    QCOMPARE(profile.value(QStringLiteral("channels")).toArray().at(0).toObject().value(QStringLiteral("type")).toString(), QStringLiteral("Slider"));
    QCOMPARE(profile.value(QStringLiteral("isUser")).toBool(), true);
    QCOMPARE(profile.value(QStringLiteral("colorTable")).toArray().at(0).toObject().value(QStringLiteral("color")).toString(), QStringLiteral("#ff0000"));

    QVERIFY(QTest::qWaitFor([&]() { return eventsWithTopic(spy, QStringLiteral("io.inputProfile.changed")).count() >= 1; }, 2000));
    QJsonObject data = eventsWithTopic(spy, QStringLiteral("io.inputProfile.changed")).first().value(QStringLiteral("data")).toObject();
    QCOMPARE(data.value(QStringLiteral("profile")).toObject().value(QStringLiteral("name")).toString(), QStringLiteral("Acme Faderbox"));
    QCOMPARE(data.value(QStringLiteral("profilesRevision")).toInt(), 1);
}

void ApiIoConfigDomain_Test::inputProfileSaveOverExistingUpdatesInPlace()
{
    IOPluginStub *stub = loadStubPlugin();
    QVERIFY(stub != nullptr);
    hello(m_client);

    QJsonObject params;
    params.insert(QStringLiteral("profile"), sampleProfile(QStringLiteral("Faderbox"), QStringLiteral("Fader A")));
    QCOMPARE(sendAndWaitForReply(QStringLiteral("io.inputProfile.save"), params).value(QStringLiteral("ok")).toBool(), true);

    // A universe uses the profile; its InputPatch holds the pointer.
    patchStubInput(stub, 0, 0, QStringLiteral("Acme Faderbox"));
    QLCInputProfile *held = m_doc->inputOutputMap()->universe(0)->inputPatch()->profile();
    QVERIFY(held != nullptr);

    params.insert(QStringLiteral("profile"), sampleProfile(QStringLiteral("Faderbox"), QStringLiteral("Fader A2")));
    params.insert(QStringLiteral("baseRevision"), 1);
    QJsonObject reply = sendAndWaitForReply(QStringLiteral("io.inputProfile.save"), params);
    QCOMPARE(reply.value(QStringLiteral("ok")).toBool(), true);
    QCOMPARE(reply.value(QStringLiteral("result")).toObject().value(QStringLiteral("profilesRevision")).toInt(), 2);

    // Same object, new content - no dangling patch, no duplicate entry.
    QCOMPARE(m_doc->inputOutputMap()->universe(0)->inputPatch()->profile(), held);
    QCOMPARE(held->channel(5)->name(), QStringLiteral("Fader A2"));
    QCOMPARE(m_doc->inputOutputMap()->profileNames().count(), 1);
}

void ApiIoConfigDomain_Test::inputProfileSaveWithStaleRevisionConflicts()
{
    hello(m_client);
    QJsonObject params;
    params.insert(QStringLiteral("profile"), sampleProfile(QStringLiteral("Faderbox"), QStringLiteral("Fader A")));
    params.insert(QStringLiteral("baseRevision"), 41);
    QJsonObject reply = sendAndWaitForReply(QStringLiteral("io.inputProfile.save"), params);
    QCOMPARE(reply.value(QStringLiteral("ok")).toBool(), false);
    QJsonObject error = reply.value(QStringLiteral("error")).toObject();
    QCOMPARE(error.value(QStringLiteral("code")).toString(), QStringLiteral("CONFLICT"));
    QCOMPARE(error.value(QStringLiteral("details")).toObject().value(QStringLiteral("profilesRevision")).toInt(), 0);
    QCOMPARE(QDir(m_profileDir->path()).entryList(QDir::Files).count(), 0);
}

void ApiIoConfigDomain_Test::inputProfileSaveWithoutModelIsInvalidParams()
{
    hello(m_client);
    QJsonObject profile = sampleProfile(QString(), QStringLiteral("Fader A"));
    QJsonObject params;
    params.insert(QStringLiteral("profile"), profile);
    QJsonObject reply = sendAndWaitForReply(QStringLiteral("io.inputProfile.save"), params);
    QCOMPARE(reply.value(QStringLiteral("ok")).toBool(), false);
    QCOMPARE(reply.value(QStringLiteral("error")).toObject().value(QStringLiteral("code")).toString(), QStringLiteral("INVALID_PARAMS"));
    QCOMPARE(m_doc->inputOutputMap()->profileNames().count(), 0);
}

void ApiIoConfigDomain_Test::inputProfileGetOnUnknownIsNotFound()
{
    hello(m_client);
    QJsonObject params;
    params.insert(QStringLiteral("name"), QStringLiteral("Nobody Nothing"));
    QJsonObject reply = sendAndWaitForReply(QStringLiteral("io.inputProfile.get"), params);
    QCOMPARE(reply.value(QStringLiteral("ok")).toBool(), false);
    QCOMPARE(reply.value(QStringLiteral("error")).toObject().value(QStringLiteral("code")).toString(), QStringLiteral("NOT_FOUND"));
}

void ApiIoConfigDomain_Test::inputProfileDeleteRemovesFileAndClearsPatches()
{
    IOPluginStub *stub = loadStubPlugin();
    QVERIFY(stub != nullptr);
    hello(m_client);

    QJsonObject saveParams;
    saveParams.insert(QStringLiteral("profile"), sampleProfile(QStringLiteral("Faderbox"), QStringLiteral("Fader A")));
    QJsonObject saved = sendAndWaitForReply(QStringLiteral("io.inputProfile.save"), saveParams);
    QString path = saved.value(QStringLiteral("result")).toObject().value(QStringLiteral("path")).toString();
    QVERIFY(QFile::exists(path));
    patchStubInput(stub, 0, 0, QStringLiteral("Acme Faderbox"));
    QVERIFY(m_doc->inputOutputMap()->universe(0)->inputPatch()->profile() != nullptr);

    QSignalSpy spy(m_client, &QWebSocket::textMessageReceived);
    QJsonObject params;
    params.insert(QStringLiteral("name"), QStringLiteral("Acme Faderbox"));
    params.insert(QStringLiteral("baseRevision"), 1);
    QJsonObject reply = sendAndWaitForReply(QStringLiteral("io.inputProfile.delete"), params);
    QCOMPARE(reply.value(QStringLiteral("ok")).toBool(), true);
    QCOMPARE(reply.value(QStringLiteral("result")).toObject().value(QStringLiteral("profilesRevision")).toInt(), 2);

    QVERIFY(QFile::exists(path) == false);
    QVERIFY(m_doc->inputOutputMap()->profile(QStringLiteral("Acme Faderbox")) == nullptr);
    // The patch was detached from the (now deleted) profile instead of dangling.
    QVERIFY(m_doc->inputOutputMap()->universe(0)->inputPatch() != nullptr);
    QVERIFY(m_doc->inputOutputMap()->universe(0)->inputPatch()->profile() == nullptr);

    QVERIFY(QTest::qWaitFor([&]() { return eventsWithTopic(spy, QStringLiteral("io.inputProfile.deleted")).count() >= 1
                                          && eventsWithTopic(spy, QStringLiteral("io.universe.updated")).count() >= 1; }, 2000));
    QJsonObject data = eventsWithTopic(spy, QStringLiteral("io.inputProfile.deleted")).first().value(QStringLiteral("data")).toObject();
    QCOMPARE(data.value(QStringLiteral("name")).toString(), QStringLiteral("Acme Faderbox"));
    QJsonObject universe = eventsWithTopic(spy, QStringLiteral("io.universe.updated")).first().value(QStringLiteral("data")).toObject().value(QStringLiteral("universe")).toObject();
    QVERIFY(universe.value(QStringLiteral("inputPatch")).toObject().value(QStringLiteral("profileName")).isNull());
}

void ApiIoConfigDomain_Test::inputProfileDeleteOnUnknownIsNotFound()
{
    hello(m_client);
    QJsonObject params;
    params.insert(QStringLiteral("name"), QStringLiteral("Nobody Nothing"));
    QJsonObject reply = sendAndWaitForReply(QStringLiteral("io.inputProfile.delete"), params);
    QCOMPARE(reply.value(QStringLiteral("ok")).toBool(), false);
    QCOMPARE(reply.value(QStringLiteral("error")).toObject().value(QStringLiteral("code")).toString(), QStringLiteral("NOT_FOUND"));
}

/*********************************************************************
 * io.inputProfile.learn.*
 *********************************************************************/

void ApiIoConfigDomain_Test::learnSignalGoesOnlyToRequester()
{
    IOPluginStub *stub = loadStubPlugin();
    QVERIFY(stub != nullptr);
    patchStubInput(stub, 0, 0);
    QString learnerId = hello(m_client);
    QWebSocket *other = connectSecondClient();
    hello(other);

    QSignalSpy learnerSpy(m_client, &QWebSocket::textMessageReceived);
    QSignalSpy otherSpy(other, &QWebSocket::textMessageReceived);

    QJsonObject params;
    params.insert(QStringLiteral("universeId"), 0);
    QCOMPARE(sendAndWaitForReply(QStringLiteral("io.inputProfile.learn.start"), params).value(QStringLiteral("ok")).toBool(), true);

    // A control moves on the patched line: buffered by InputPatch, flushed
    // on the universe's next tick, relayed by InputOutputMap.
    stub->emitValueChanged(0, 0, 7, 200, QStringLiteral("/fader/7"));
    QVERIFY(QTest::qWaitFor([&]() { return eventsWithTopic(learnerSpy, QStringLiteral("io.inputProfile.learn.signal")).count() >= 1; }, 3000));
    QJsonObject event = eventsWithTopic(learnerSpy, QStringLiteral("io.inputProfile.learn.signal")).first();
    QJsonObject data = event.value(QStringLiteral("data")).toObject();
    QCOMPARE(data.value(QStringLiteral("universeId")).toInt(), 0);
    QCOMPARE(data.value(QStringLiteral("channelNumber")).toInt(), 7);
    QCOMPARE(data.value(QStringLiteral("value")).toInt(), 200);
    QCOMPARE(data.value(QStringLiteral("key")).toString(), QStringLiteral("/fader/7"));
    QCOMPARE(data.value(QStringLiteral("alreadyMapped")).toBool(), false);
    QCOMPARE(event.value(QStringLiteral("originClientId")).toString(), learnerId);

    // The bystander saw nothing.
    QTest::qWait(200);
    QCOMPARE(eventsWithTopic(otherSpy, QStringLiteral("io.inputProfile.learn.signal")).count(), 0);

    // After stop, no more signals reach the learner either.
    QCOMPARE(sendAndWaitForReply(QStringLiteral("io.inputProfile.learn.stop"), QJsonObject()).value(QStringLiteral("ok")).toBool(), true);
    int before = eventsWithTopic(learnerSpy, QStringLiteral("io.inputProfile.learn.signal")).count();
    stub->emitValueChanged(0, 0, 8, 100);
    QTest::qWait(300);
    QCOMPARE(eventsWithTopic(learnerSpy, QStringLiteral("io.inputProfile.learn.signal")).count(), before);
}

void ApiIoConfigDomain_Test::learnStartWithoutInputPatchIsInvalidState()
{
    hello(m_client);
    QJsonObject params;
    params.insert(QStringLiteral("universeId"), 0);
    QJsonObject reply = sendAndWaitForReply(QStringLiteral("io.inputProfile.learn.start"), params);
    QCOMPARE(reply.value(QStringLiteral("ok")).toBool(), false);
    QCOMPARE(reply.value(QStringLiteral("error")).toObject().value(QStringLiteral("code")).toString(), QStringLiteral("INVALID_STATE"));
}

void ApiIoConfigDomain_Test::learnStopByOtherClientIsInvalidState()
{
    IOPluginStub *stub = loadStubPlugin();
    QVERIFY(stub != nullptr);
    patchStubInput(stub, 0, 0);
    hello(m_client);
    QWebSocket *other = connectSecondClient();
    hello(other);

    QJsonObject params;
    params.insert(QStringLiteral("universeId"), 0);
    QCOMPARE(sendAndWaitForReply(QStringLiteral("io.inputProfile.learn.start"), params).value(QStringLiteral("ok")).toBool(), true);

    QJsonObject reply = sendAndWaitForReply(other, QStringLiteral("io.inputProfile.learn.stop"), QJsonObject());
    QCOMPARE(reply.value(QStringLiteral("ok")).toBool(), false);
    QCOMPARE(reply.value(QStringLiteral("error")).toObject().value(QStringLiteral("code")).toString(), QStringLiteral("INVALID_STATE"));

    // A second learner is refused too, with the holder's id.
    reply = sendAndWaitForReply(other, QStringLiteral("io.inputProfile.learn.start"), params);
    QCOMPARE(reply.value(QStringLiteral("ok")).toBool(), false);
    QCOMPARE(reply.value(QStringLiteral("error")).toObject().value(QStringLiteral("code")).toString(), QStringLiteral("INVALID_STATE"));
    QVERIFY(reply.value(QStringLiteral("error")).toObject().value(QStringLiteral("details")).toObject().contains(QStringLiteral("clientId")));
}

/*********************************************************************
 * io.grandMaster.setMode / io.universe.setMonitor
 *********************************************************************/

void ApiIoConfigDomain_Test::grandMasterSetModeBroadcastsChanged()
{
    hello(m_client);
    QCOMPARE(m_doc->inputOutputMap()->grandMasterChannelMode(), GrandMaster::Intensity);
    QCOMPARE(m_doc->inputOutputMap()->grandMasterValueMode(), GrandMaster::Reduce);
    QSignalSpy spy(m_client, &QWebSocket::textMessageReceived);

    QJsonObject params;
    params.insert(QStringLiteral("channelMode"), QStringLiteral("AllChannels"));
    params.insert(QStringLiteral("valueMode"), QStringLiteral("Limit"));
    QCOMPARE(sendAndWaitForReply(QStringLiteral("io.grandMaster.setMode"), params).value(QStringLiteral("ok")).toBool(), true);
    QCOMPARE(m_doc->inputOutputMap()->grandMasterChannelMode(), GrandMaster::AllChannels);
    QCOMPARE(m_doc->inputOutputMap()->grandMasterValueMode(), GrandMaster::Limit);

    QVERIFY(QTest::qWaitFor([&]() { return eventsWithTopic(spy, QStringLiteral("io.grandMaster.changed")).count() >= 1; }, 2000));
    QJsonObject data = eventsWithTopic(spy, QStringLiteral("io.grandMaster.changed")).first().value(QStringLiteral("data")).toObject();
    QCOMPARE(data.value(QStringLiteral("channelMode")).toString(), QStringLiteral("AllChannels"));
    QCOMPARE(data.value(QStringLiteral("valueMode")).toString(), QStringLiteral("Limit"));

    QJsonObject get = sendAndWaitForReply(QStringLiteral("io.grandMaster.get"), QJsonObject()).value(QStringLiteral("result")).toObject();
    QCOMPARE(get.value(QStringLiteral("channelMode")).toString(), QStringLiteral("AllChannels"));
    QCOMPARE(get.value(QStringLiteral("valueMode")).toString(), QStringLiteral("Limit"));

    // Omitted field stays put.
    QJsonObject onlyValue;
    onlyValue.insert(QStringLiteral("valueMode"), QStringLiteral("Reduce"));
    QCOMPARE(sendAndWaitForReply(QStringLiteral("io.grandMaster.setMode"), onlyValue).value(QStringLiteral("ok")).toBool(), true);
    QCOMPARE(m_doc->inputOutputMap()->grandMasterChannelMode(), GrandMaster::AllChannels);
    QCOMPARE(m_doc->inputOutputMap()->grandMasterValueMode(), GrandMaster::Reduce);
}

void ApiIoConfigDomain_Test::grandMasterSetModeWithBadValueIsInvalidParams()
{
    hello(m_client);
    QJsonObject params;
    params.insert(QStringLiteral("channelMode"), QStringLiteral("Everything"));
    QJsonObject reply = sendAndWaitForReply(QStringLiteral("io.grandMaster.setMode"), params);
    QCOMPARE(reply.value(QStringLiteral("ok")).toBool(), false);
    QCOMPARE(reply.value(QStringLiteral("error")).toObject().value(QStringLiteral("code")).toString(), QStringLiteral("INVALID_PARAMS"));
    QCOMPARE(sendAndWaitForReply(QStringLiteral("io.grandMaster.setMode"), QJsonObject()).value(QStringLiteral("ok")).toBool(), false);
}

void ApiIoConfigDomain_Test::universeSetMonitorBroadcasts()
{
    hello(m_client);
    QSignalSpy spy(m_client, &QWebSocket::textMessageReceived);
    quint32 before = m_doc->docRevision();

    QJsonObject params;
    params.insert(QStringLiteral("universeId"), 1);
    params.insert(QStringLiteral("monitor"), true);
    QCOMPARE(sendAndWaitForReply(QStringLiteral("io.universe.setMonitor"), params).value(QStringLiteral("ok")).toBool(), true);
    QCOMPARE(m_doc->inputOutputMap()->universe(1)->monitor(), true);
    QCOMPARE(m_doc->inputOutputMap()->universe(0)->monitor(), false);
    QCOMPARE(m_doc->docRevision(), before); // live only

    QVERIFY(QTest::qWaitFor([&]() { return eventsWithTopic(spy, QStringLiteral("io.universe.monitorChanged")).count() >= 1; }, 2000));
    QJsonObject data = eventsWithTopic(spy, QStringLiteral("io.universe.monitorChanged")).first().value(QStringLiteral("data")).toObject();
    QCOMPARE(data.value(QStringLiteral("universeId")).toInt(), 1);
    QCOMPARE(data.value(QStringLiteral("monitor")).toBool(), true);

    QJsonObject getParams;
    getParams.insert(QStringLiteral("universeId"), 1);
    QJsonObject get = sendAndWaitForReply(QStringLiteral("io.universe.get"), getParams).value(QStringLiteral("result")).toObject();
    QCOMPARE(get.value(QStringLiteral("monitor")).toBool(), true);

    params.insert(QStringLiteral("monitor"), QStringLiteral("yes"));
    QCOMPARE(sendAndWaitForReply(QStringLiteral("io.universe.setMonitor"), params).value(QStringLiteral("ok")).toBool(), false);
}

/*********************************************************************
 * io.audio.*
 *********************************************************************/

void ApiIoConfigDomain_Test::audioListDevicesStartsWithDefault()
{
    hello(m_client);
    QJsonObject reply = sendAndWaitForReply(QStringLiteral("io.audio.listDevices"), QJsonObject());
    QCOMPARE(reply.value(QStringLiteral("ok")).toBool(), true);
    QJsonObject result = reply.value(QStringLiteral("result")).toObject();
    QJsonArray inputs = result.value(QStringLiteral("inputs")).toArray();
    QJsonArray outputs = result.value(QStringLiteral("outputs")).toArray();
    QVERIFY(inputs.count() >= 1);
    QVERIFY(outputs.count() >= 1);
    QCOMPARE(inputs.at(0).toObject().value(QStringLiteral("privateName")).toString(), QStringLiteral("__qlcplusdefault__"));
    QCOMPARE(outputs.at(0).toObject().value(QStringLiteral("privateName")).toString(), QStringLiteral("__qlcplusdefault__"));
    QCOMPARE(result.value(QStringLiteral("inputDevice")).toString(), QStringLiteral("__qlcplusdefault__"));
    QCOMPARE(result.value(QStringLiteral("outputDevice")).toString(), QStringLiteral("__qlcplusdefault__"));
    for (const QJsonValue &v : inputs)
        QVERIFY(v.toObject().value(QStringLiteral("name")).toString().isEmpty() == false);
}

void ApiIoConfigDomain_Test::audioSetDeviceUnknownIsNotFound()
{
    hello(m_client);
    QJsonObject params;
    params.insert(QStringLiteral("direction"), QStringLiteral("input"));
    params.insert(QStringLiteral("privateName"), QStringLiteral("no-such-device-9f3a"));
    QJsonObject reply = sendAndWaitForReply(QStringLiteral("io.audio.setDevice"), params);
    QCOMPARE(reply.value(QStringLiteral("ok")).toBool(), false);
    QCOMPARE(reply.value(QStringLiteral("error")).toObject().value(QStringLiteral("code")).toString(), QStringLiteral("NOT_FOUND"));

    params.insert(QStringLiteral("direction"), QStringLiteral("sideways"));
    reply = sendAndWaitForReply(QStringLiteral("io.audio.setDevice"), params);
    QCOMPARE(reply.value(QStringLiteral("error")).toObject().value(QStringLiteral("code")).toString(), QStringLiteral("INVALID_PARAMS"));
}

void ApiIoConfigDomain_Test::audioSetDefaultDeviceRoundTrips()
{
    hello(m_client);
    QSignalSpy spy(m_client, &QWebSocket::textMessageReceived);
    QJsonObject params;
    params.insert(QStringLiteral("direction"), QStringLiteral("output"));
    params.insert(QStringLiteral("privateName"), QStringLiteral("__qlcplusdefault__"));
    QCOMPARE(sendAndWaitForReply(QStringLiteral("io.audio.setDevice"), params).value(QStringLiteral("ok")).toBool(), true);
    QVERIFY(QTest::qWaitFor([&]() { return eventsWithTopic(spy, QStringLiteral("io.audio.deviceChanged")).count() >= 1; }, 2000));
    QJsonObject data = eventsWithTopic(spy, QStringLiteral("io.audio.deviceChanged")).first().value(QStringLiteral("data")).toObject();
    QCOMPARE(data.value(QStringLiteral("direction")).toString(), QStringLiteral("output"));
    QCOMPARE(data.value(QStringLiteral("privateName")).toString(), QStringLiteral("__qlcplusdefault__"));

    QJsonObject result = sendAndWaitForReply(QStringLiteral("io.audio.listDevices"), QJsonObject()).value(QStringLiteral("result")).toObject();
    QCOMPARE(result.value(QStringLiteral("outputDevice")).toString(), QStringLiteral("__qlcplusdefault__"));
    QSettings settings;
    QVERIFY(settings.contains(QStringLiteral("audio/output")) == false);
}

/*********************************************************************
 * io.simpleDesk.sendKeypadCommand (ApiIoDomain)
 *********************************************************************/

void ApiIoConfigDomain_Test::keypadCommandSetsChannelsAndHistory()
{
    hello(m_client);
    QSignalSpy spy(m_client, &QWebSocket::textMessageReceived);

    QJsonObject params;
    params.insert(QStringLiteral("command"), QStringLiteral("1 thru 4 @ 50"));
    params.insert(QStringLiteral("universeId"), 0);
    QJsonObject reply = sendAndWaitForReply(QStringLiteral("io.simpleDesk.sendKeypadCommand"), params);
    QCOMPARE(reply.value(QStringLiteral("ok")).toBool(), true);
    QJsonObject result = reply.value(QStringLiteral("result")).toObject();
    QCOMPARE(result.value(QStringLiteral("accepted")).toBool(), true);
    QCOMPARE(result.value(QStringLiteral("channelsChanged")).toInt(), 4);
    QJsonArray history = result.value(QStringLiteral("history")).toArray();
    QCOMPARE(history.count(), 1);
    QCOMPARE(history.at(0).toString(), QStringLiteral("1 THRU 4 AT 50"));

    // Held values are visible right away, the DMX output after a tick.
    QJsonObject getParams;
    getParams.insert(QStringLiteral("universeId"), 0);
    QJsonObject desk = sendAndWaitForReply(QStringLiteral("io.simpleDesk.get"), getParams).value(QStringLiteral("result")).toObject();
    QCOMPARE(desk.value(QStringLiteral("channels")).toArray().count(), 4);
    QCOMPARE(desk.value(QStringLiteral("commandHistory")).toArray().count(), 1);
    QCOMPARE(desk.value(QStringLiteral("commandHistory")).toArray().at(0).toString(), QStringLiteral("1 THRU 4 AT 50"));

    QVERIFY(QTest::qWaitFor([&]()
    {
        QJsonObject dmx = sendAndWaitForReply(QStringLiteral("io.dmx.universe.get"), getParams).value(QStringLiteral("result")).toObject();
        QJsonArray values = dmx.value(QStringLiteral("values")).toArray();
        return values.count() == 512 && values.at(0).toInt() == 50 && values.at(3).toInt() == 50 && values.at(4).toInt() == 0;
    }, 3000));

    // One channelChanged per channel plus the history event, to every client.
    QVERIFY(QTest::qWaitFor([&]() { return eventsWithTopic(spy, QStringLiteral("io.simpleDesk.channelChanged")).count() >= 4
                                          && eventsWithTopic(spy, QStringLiteral("io.simpleDesk.commandHistoryChanged")).count() >= 1; }, 2000));
    QJsonObject histData = eventsWithTopic(spy, QStringLiteral("io.simpleDesk.commandHistoryChanged")).first().value(QStringLiteral("data")).toObject();
    QCOMPARE(histData.value(QStringLiteral("history")).toArray().at(0).toString(), QStringLiteral("1 THRU 4 AT 50"));

    // A relative command on the remembered selection, and the history cap.
    for (int i = 0; i < 11; i++)
    {
        QJsonObject next;
        next.insert(QStringLiteral("command"), QStringLiteral("+ %1").arg(i + 1));
        next.insert(QStringLiteral("universeId"), 0);
        QCOMPARE(sendAndWaitForReply(QStringLiteral("io.simpleDesk.sendKeypadCommand"), next).value(QStringLiteral("ok")).toBool(), true);
    }
    desk = sendAndWaitForReply(QStringLiteral("io.simpleDesk.get"), getParams).value(QStringLiteral("result")).toObject();
    QCOMPARE(desk.value(QStringLiteral("commandHistory")).toArray().count(), 10);
    QCOMPARE(desk.value(QStringLiteral("commandHistory")).toArray().at(0).toString(), QStringLiteral("+ 11"));
}

void ApiIoConfigDomain_Test::keypadCommandEmptyIsInvalidParams()
{
    hello(m_client);
    QJsonObject params;
    params.insert(QStringLiteral("command"), QStringLiteral("  ENTER "));
    QJsonObject reply = sendAndWaitForReply(QStringLiteral("io.simpleDesk.sendKeypadCommand"), params);
    QCOMPARE(reply.value(QStringLiteral("ok")).toBool(), false);
    QCOMPARE(reply.value(QStringLiteral("error")).toObject().value(QStringLiteral("code")).toString(), QStringLiteral("INVALID_PARAMS"));

    params.insert(QStringLiteral("command"), QStringLiteral("1 AT 10"));
    params.insert(QStringLiteral("universeId"), 99);
    reply = sendAndWaitForReply(QStringLiteral("io.simpleDesk.sendKeypadCommand"), params);
    QCOMPARE(reply.value(QStringLiteral("error")).toObject().value(QStringLiteral("code")).toString(), QStringLiteral("NOT_FOUND"));
}

QTEST_MAIN(ApiIoConfigDomain_Test)
