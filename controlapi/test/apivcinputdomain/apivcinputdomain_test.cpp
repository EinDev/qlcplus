/*
  Q Light Controller Plus - Control API unit test
  apivcinputdomain_test.cpp

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
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonValue>
#include <QSignalSpy>
#include <QWebSocket>
#include <QtTest>

#include "apivcinputdomain_test.h"
#include "apiserver.h"
#include "doc.h"
#include "fakevchost.h"
#include "inputoutputmap.h"
#include "ioplugincache.h"
#include "iopluginstub.h"
#include "mastertimer.h"
#include "qlcfile.h"

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

static QJsonObject sourceParams(const QString &widgetId, int controlId, int universe, int channel)
{
    QJsonObject p;
    p.insert(QStringLiteral("widgetId"), widgetId);
    p.insert(QStringLiteral("controlId"), controlId);
    p.insert(QStringLiteral("universe"), universe);
    p.insert(QStringLiteral("channel"), channel);
    return p;
}

static QJsonObject keyParams(const QString &widgetId, const QString &seq, int controlId = -1)
{
    QJsonObject p;
    p.insert(QStringLiteral("widgetId"), widgetId);
    p.insert(QStringLiteral("keySequence"), seq);
    if (controlId >= 0)
        p.insert(QStringLiteral("controlId"), controlId);
    return p;
}

void ApiVcInputDomain_Test::initTestCase()
{
    QCoreApplication::setOrganizationName(QStringLiteral("qlcplus-controlapi-test"));
    QCoreApplication::setApplicationName(QStringLiteral("apivcinputdomain_test"));
}

void ApiVcInputDomain_Test::init()
{
    m_doc = new Doc(nullptr);
    // The detect cases feed a signal through the stub plugin: InputPatch buffers it and the
    // universe's tick (Universe::tick -> flushInput) relays it to InputOutputMap::inputValueChanged.
    m_doc->masterTimer()->start();
    m_doc->inputOutputMap()->startUniverses();
    m_vcHost = new FakeVcHost();
    m_apiServer = new ApiServer(m_vcHost, m_doc);
    QVERIFY(m_apiServer->listen(0, QHostAddress::LocalHost));

    m_client = new QWebSocket();
    m_client->open(QUrl(QStringLiteral("ws://127.0.0.1:%1/qlcplusapi").arg(m_apiServer->serverPort())));
    QVERIFY(QTest::qWaitFor([this]() { return m_client->state() == QAbstractSocket::ConnectedState; }, 2000));
}

void ApiVcInputDomain_Test::cleanup()
{
    qDeleteAll(m_extraClients);
    m_extraClients.clear();
    delete m_client;
    m_client = nullptr;
    delete m_apiServer;
    m_apiServer = nullptr;
    delete m_vcHost;
    m_vcHost = nullptr;
    m_doc->masterTimer()->stop();
    delete m_doc;
    m_doc = nullptr;
}

QJsonObject ApiVcInputDomain_Test::sendAndWaitForReply(QWebSocket *client, const QString &method, const QJsonObject &params, const QString &requestId)
{
    QSignalSpy spy(client, &QWebSocket::textMessageReceived);
    client->sendTextMessage(buildRequest(method, params, requestId));

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
    }, 3000);
    return found;
}

QJsonObject ApiVcInputDomain_Test::sendAndWaitForReply(const QString &method, const QJsonObject &params, const QString &requestId)
{
    return sendAndWaitForReply(m_client, method, params, requestId);
}

QString ApiVcInputDomain_Test::hello(QWebSocket *client)
{
    QJsonObject reply = sendAndWaitForReply(client, QStringLiteral("hello"), QJsonObject(), QStringLiteral("t-hello"));
    return resultOf(reply).value(QStringLiteral("clientId")).toString();
}

QWebSocket *ApiVcInputDomain_Test::connectSecondClient()
{
    QWebSocket *client = new QWebSocket();
    m_extraClients.append(client);
    client->open(QUrl(QStringLiteral("ws://127.0.0.1:%1/qlcplusapi").arg(m_apiServer->serverPort())));
    (void)QTest::qWaitFor([client]() { return client->state() == QAbstractSocket::ConnectedState; }, 2000);
    return client;
}

int ApiVcInputDomain_Test::currentDocRevision()
{
    return int(m_doc->docRevision());
}

QString ApiVcInputDomain_Test::createWidget(const QString &widgetType, const QJsonObject &typeConfig)
{
    QJsonObject geometry;
    geometry.insert(QStringLiteral("x"), 0); geometry.insert(QStringLiteral("y"), 0);
    geometry.insert(QStringLiteral("width"), 50); geometry.insert(QStringLiteral("height"), 50);
    QJsonObject params;
    params.insert(QStringLiteral("widgetType"), widgetType);
    params.insert(QStringLiteral("page"), 0);
    params.insert(QStringLiteral("geometry"), geometry);
    if (typeConfig.isEmpty() == false)
        params.insert(QStringLiteral("typeConfig"), typeConfig);
    params.insert(QStringLiteral("baseRevision"), currentDocRevision());
    QJsonObject reply = sendAndWaitForReply(QStringLiteral("vc.widget.create"), params, QStringLiteral("t-create"));
    return resultOf(reply).value(QStringLiteral("widgetId")).toString();
}

QJsonObject ApiVcInputDomain_Test::widgetDetail(const QString &widgetId)
{
    QJsonObject params;
    params.insert(QStringLiteral("widgetId"), widgetId);
    return resultOf(sendAndWaitForReply(QStringLiteral("vc.widget.get"), params, QStringLiteral("t-get")));
}

IOPluginStub *ApiVcInputDomain_Test::loadStubPlugin()
{
    QDir dir(QCoreApplication::applicationDirPath() + QStringLiteral("/../../../engine/test/iopluginstub"));
    dir.setFilter(QDir::Files);
    dir.setNameFilters(QStringList() << QStringLiteral("*%1").arg(KExtPlugin));
    m_doc->ioPluginCache()->load(dir);
    QList<QLCIOPlugin *> plugins = m_doc->ioPluginCache()->plugins();
    if (plugins.isEmpty())
        return nullptr;
    // IOPluginStub is final: a direct stub->name() would be devirtualised into a symbol this binary
    // does not link (the code lives in the DLL) - see stubName().
    return static_cast<IOPluginStub *>(plugins.first());
}

QString ApiVcInputDomain_Test::stubName(IOPluginStub *stub) const
{
    return static_cast<QLCIOPlugin *>(stub)->name();
}

/*****************************************************************************
 * Snapshot
 *****************************************************************************/

void ApiVcInputDomain_Test::snapshotExposesExternalControls()
{
    hello(m_client);
    QString button = createWidget(QStringLiteral("Button"));
    QJsonObject detail = widgetDetail(button);
    QJsonArray controls = detail.value(QStringLiteral("externalControls")).toArray();
    QCOMPARE(controls.size(), 1);
    QCOMPARE(controls.at(0).toObject().value(QStringLiteral("controlId")).toInt(), 0);
    QCOMPARE(controls.at(0).toObject().value(QStringLiteral("name")).toString(), QStringLiteral("Pressure"));
    QCOMPARE(controls.at(0).toObject().value(QStringLiteral("allowKeyboard")).toBool(), true);
    QVERIFY(detail.value(QStringLiteral("inputSources")).toArray().isEmpty());
    QVERIFY(detail.value(QStringLiteral("keySequences")).toArray().isEmpty());

    QString slider = createWidget(QStringLiteral("Slider"));
    controls = widgetDetail(slider).value(QStringLiteral("externalControls")).toArray();
    QCOMPARE(controls.size(), 3);
    QCOMPARE(controls.at(0).toObject().value(QStringLiteral("allowKeyboard")).toBool(), false);
    QCOMPARE(controls.at(2).toObject().value(QStringLiteral("name")).toString(), QStringLiteral("Flash Control"));
}

/*****************************************************************************
 * vc.widget.inputSource.set / remove
 *****************************************************************************/

void ApiVcInputDomain_Test::inputSourceSetCreatesAndBroadcasts()
{
    QString clientId = hello(m_client);
    QString cueList = createWidget(QStringLiteral("CueList"));

    QSignalSpy spy(m_client, &QWebSocket::textMessageReceived);
    QJsonObject params = sourceParams(cueList, 0, 0, 4); // "Next Cue" <- universe 1, channel 5
    params.insert(QStringLiteral("lowerValue"), 10);
    params.insert(QStringLiteral("upperValue"), 200);
    params.insert(QStringLiteral("monitorValue"), 100);
    params.insert(QStringLiteral("baseRevision"), currentDocRevision());
    int before = currentDocRevision();
    QJsonObject reply = sendAndWaitForReply(QStringLiteral("vc.widget.inputSource.set"), params);
    QVERIFY2(isOk(reply), qPrintable(QJsonDocument(reply).toJson()));
    QCOMPARE(resultOf(reply).value(QStringLiteral("docRevision")).toInt(), before + 1);

    QList<QJsonObject> events = eventsWithTopic(spy, QStringLiteral("vc.widget.inputSourcesChanged"));
    QCOMPARE(events.size(), 1);
    QCOMPARE(events.at(0).value(QStringLiteral("widgetId")).toString(), cueList);
    QCOMPARE(events.at(0).value(QStringLiteral("_origin")).toString(), clientId);
    QCOMPARE(events.at(0).value(QStringLiteral("docRevision")).toInt(), before + 1);
    QJsonArray sources = events.at(0).value(QStringLiteral("inputSources")).toArray();
    QCOMPARE(sources.size(), 1);
    QJsonObject s = sources.at(0).toObject();
    QCOMPARE(s.value(QStringLiteral("controlId")).toInt(), 0);
    QCOMPARE(s.value(QStringLiteral("universe")).toInt(), 0);
    QCOMPARE(s.value(QStringLiteral("channel")).toInt(), 4);
    QCOMPARE(s.value(QStringLiteral("lowerValue")).toInt(), 10);
    QCOMPARE(s.value(QStringLiteral("upperValue")).toInt(), 200);
    QCOMPARE(s.value(QStringLiteral("monitorValue")).toInt(), 100);
    QVERIFY(s.contains(QStringLiteral("lowerChannel")) == false);

    // vc.widget.get shows the same list
    QCOMPARE(widgetDetail(cueList).value(QStringLiteral("inputSources")).toArray(), sources);

    // A second source on another control of the same widget
    params = sourceParams(cueList, 1, 0, 5);
    params.insert(QStringLiteral("baseRevision"), currentDocRevision());
    QVERIFY(isOk(sendAndWaitForReply(QStringLiteral("vc.widget.inputSource.set"), params)));
    sources = widgetDetail(cueList).value(QStringLiteral("inputSources")).toArray();
    QCOMPARE(sources.size(), 2);
    // Defaults of a fresh source (QLCInputSource): lower 0, upper 255, monitor 255
    QCOMPARE(sources.at(1).toObject().value(QStringLiteral("upperValue")).toInt(), 255);
}

void ApiVcInputDomain_Test::inputSourceSetUpdatesExistingUniverseChannel()
{
    hello(m_client);
    QString slider = createWidget(QStringLiteral("Slider"));

    QJsonObject params = sourceParams(slider, 0, 1, 7);
    params.insert(QStringLiteral("baseRevision"), currentDocRevision());
    QVERIFY(isOk(sendAndWaitForReply(QStringLiteral("vc.widget.inputSource.set"), params)));

    // Same universe/channel, other control + MIDI routing: re-targets in place, no second entry
    params = sourceParams(slider, 2, 1, 7);
    params.insert(QStringLiteral("lowerChannel"), 3);
    params.insert(QStringLiteral("upperChannel"), 0);
    params.insert(QStringLiteral("baseRevision"), currentDocRevision());
    QVERIFY(isOk(sendAndWaitForReply(QStringLiteral("vc.widget.inputSource.set"), params)));
    QJsonArray sources = widgetDetail(slider).value(QStringLiteral("inputSources")).toArray();
    QCOMPARE(sources.size(), 1);
    QCOMPARE(sources.at(0).toObject().value(QStringLiteral("controlId")).toInt(), 2);
    QCOMPARE(sources.at(0).toObject().value(QStringLiteral("lowerChannel")).toInt(), 3);
    QVERIFY(sources.at(0).toObject().contains(QStringLiteral("upperChannel")) == false); // 0 = profile routing -> absent
}

void ApiVcInputDomain_Test::inputSourceSetValidates()
{
    hello(m_client);
    QString button = createWidget(QStringLiteral("Button"));

    // Unknown control id for a Button (only 0 exists)
    QJsonObject params = sourceParams(button, 7, 0, 0);
    params.insert(QStringLiteral("baseRevision"), currentDocRevision());
    QJsonObject reply = sendAndWaitForReply(QStringLiteral("vc.widget.inputSource.set"), params);
    QCOMPARE(isOk(reply), false);
    QCOMPARE(errorCode(reply), QStringLiteral("INVALID_PARAMS"));
    QVERIFY(reply.value(QStringLiteral("error")).toObject().value(QStringLiteral("details")).toObject().contains(QStringLiteral("externalControls")));

    // Feedback value out of range
    params = sourceParams(button, 0, 0, 0);
    params.insert(QStringLiteral("lowerValue"), 300);
    params.insert(QStringLiteral("baseRevision"), currentDocRevision());
    reply = sendAndWaitForReply(QStringLiteral("vc.widget.inputSource.set"), params);
    QCOMPARE(errorCode(reply), QStringLiteral("INVALID_PARAMS"));

    // Non-integer channel
    params = sourceParams(button, 0, 0, 0);
    params.insert(QStringLiteral("channel"), QStringLiteral("five"));
    params.insert(QStringLiteral("baseRevision"), currentDocRevision());
    reply = sendAndWaitForReply(QStringLiteral("vc.widget.inputSource.set"), params);
    QCOMPARE(errorCode(reply), QStringLiteral("INVALID_PARAMS"));

    // Unknown widget
    params = sourceParams(QStringLiteral("9999"), 0, 0, 0);
    params.insert(QStringLiteral("baseRevision"), currentDocRevision());
    reply = sendAndWaitForReply(QStringLiteral("vc.widget.inputSource.set"), params);
    QCOMPARE(errorCode(reply), QStringLiteral("NOT_FOUND"));

    // Nothing was added by the refused requests
    QVERIFY(widgetDetail(button).value(QStringLiteral("inputSources")).toArray().isEmpty());
}

void ApiVcInputDomain_Test::inputSourceRemove()
{
    QString clientId = hello(m_client);
    QString button = createWidget(QStringLiteral("Button"));
    QJsonObject params = sourceParams(button, 0, 0, 4);
    params.insert(QStringLiteral("baseRevision"), currentDocRevision());
    QVERIFY(isOk(sendAndWaitForReply(QStringLiteral("vc.widget.inputSource.set"), params)));

    // Wrong channel -> NOT_FOUND, nothing removed
    params = sourceParams(button, 0, 0, 5);
    params.insert(QStringLiteral("baseRevision"), currentDocRevision());
    QJsonObject reply = sendAndWaitForReply(QStringLiteral("vc.widget.inputSource.remove"), params);
    QCOMPARE(errorCode(reply), QStringLiteral("NOT_FOUND"));
    QCOMPARE(widgetDetail(button).value(QStringLiteral("inputSources")).toArray().size(), 1);

    QSignalSpy spy(m_client, &QWebSocket::textMessageReceived);
    params = sourceParams(button, 0, 0, 4);
    params.insert(QStringLiteral("baseRevision"), currentDocRevision());
    reply = sendAndWaitForReply(QStringLiteral("vc.widget.inputSource.remove"), params);
    QVERIFY(isOk(reply));
    QList<QJsonObject> events = eventsWithTopic(spy, QStringLiteral("vc.widget.inputSourcesChanged"));
    QCOMPARE(events.size(), 1);
    QVERIFY(events.at(0).value(QStringLiteral("inputSources")).toArray().isEmpty());
    QCOMPARE(events.at(0).value(QStringLiteral("_origin")).toString(), clientId);
    QVERIFY(widgetDetail(button).value(QStringLiteral("inputSources")).toArray().isEmpty());
}

/*****************************************************************************
 * vc.widget.keySequence.set / remove
 *****************************************************************************/

void ApiVcInputDomain_Test::keySequenceSetAndRemove()
{
    QString clientId = hello(m_client);
    QString button = createWidget(QStringLiteral("Button"));

    QSignalSpy spy(m_client, &QWebSocket::textMessageReceived);
    // Lower-case / odd spacing is normalised to Qt's portable spelling
    QJsonObject params = keyParams(button, QStringLiteral("ctrl+shift+k"), 0);
    params.insert(QStringLiteral("baseRevision"), currentDocRevision());
    int before = currentDocRevision();
    QJsonObject reply = sendAndWaitForReply(QStringLiteral("vc.widget.keySequence.set"), params);
    QVERIFY2(isOk(reply), qPrintable(QJsonDocument(reply).toJson()));
    QCOMPARE(resultOf(reply).value(QStringLiteral("docRevision")).toInt(), before + 1);

    QList<QJsonObject> events = eventsWithTopic(spy, QStringLiteral("vc.widget.keySequencesChanged"));
    QCOMPARE(events.size(), 1);
    QCOMPARE(events.at(0).value(QStringLiteral("_origin")).toString(), clientId);
    QJsonArray keys = events.at(0).value(QStringLiteral("keySequences")).toArray();
    QCOMPARE(keys.size(), 1);
    QCOMPARE(keys.at(0).toObject().value(QStringLiteral("keySequence")).toString(), QStringLiteral("Ctrl+Shift+K"));
    QCOMPARE(keys.at(0).toObject().value(QStringLiteral("controlId")).toInt(), 0);
    QCOMPARE(widgetDetail(button).value(QStringLiteral("keySequences")).toArray(), keys);

    // A plain letter on a frame's "Next Page"
    QString frame = createWidget(QStringLiteral("Frame"));
    params = keyParams(frame, QStringLiteral("F"), 0);
    params.insert(QStringLiteral("baseRevision"), currentDocRevision());
    QVERIFY(isOk(sendAndWaitForReply(QStringLiteral("vc.widget.keySequence.set"), params)));
    QCOMPARE(widgetDetail(frame).value(QStringLiteral("keySequences")).toArray().at(0).toObject().value(QStringLiteral("keySequence")).toString(), QStringLiteral("F"));

    // Remove: unknown sequence -> NOT_FOUND; the bound one -> event with an empty list
    params = keyParams(button, QStringLiteral("Ctrl+G"));
    params.insert(QStringLiteral("baseRevision"), currentDocRevision());
    QCOMPARE(errorCode(sendAndWaitForReply(QStringLiteral("vc.widget.keySequence.remove"), params)), QStringLiteral("NOT_FOUND"));

    spy.clear();
    params = keyParams(button, QStringLiteral("Ctrl+Shift+K"));
    params.insert(QStringLiteral("baseRevision"), currentDocRevision());
    QVERIFY(isOk(sendAndWaitForReply(QStringLiteral("vc.widget.keySequence.remove"), params)));
    events = eventsWithTopic(spy, QStringLiteral("vc.widget.keySequencesChanged"));
    QCOMPARE(events.size(), 1);
    QVERIFY(events.at(0).value(QStringLiteral("keySequences")).toArray().isEmpty());
}

void ApiVcInputDomain_Test::keySequenceSetValidates()
{
    hello(m_client);
    QString slider = createWidget(QStringLiteral("Slider"));

    // "Slider Control" (0) is not a keyboard-capable control
    QJsonObject params = keyParams(slider, QStringLiteral("F"), 0);
    params.insert(QStringLiteral("baseRevision"), currentDocRevision());
    QJsonObject reply = sendAndWaitForReply(QStringLiteral("vc.widget.keySequence.set"), params);
    QCOMPARE(errorCode(reply), QStringLiteral("INVALID_PARAMS"));

    // Garbage text on the keyboard-capable "Flash Control" (2)
    params = keyParams(slider, QStringLiteral("NotAKey+++"), 2);
    params.insert(QStringLiteral("baseRevision"), currentDocRevision());
    reply = sendAndWaitForReply(QStringLiteral("vc.widget.keySequence.set"), params);
    QCOMPARE(errorCode(reply), QStringLiteral("INVALID_PARAMS"));

    // Empty text
    params = keyParams(slider, QString(), 2);
    params.insert(QStringLiteral("baseRevision"), currentDocRevision());
    reply = sendAndWaitForReply(QStringLiteral("vc.widget.keySequence.set"), params);
    QCOMPARE(errorCode(reply), QStringLiteral("INVALID_PARAMS"));

    // Multi-chord sequences are not something the VC can match
    params = keyParams(slider, QStringLiteral("Ctrl+X, Ctrl+C"), 2);
    params.insert(QStringLiteral("baseRevision"), currentDocRevision());
    reply = sendAndWaitForReply(QStringLiteral("vc.widget.keySequence.set"), params);
    QCOMPARE(errorCode(reply), QStringLiteral("INVALID_PARAMS"));

    QVERIFY(widgetDetail(slider).value(QStringLiteral("keySequences")).toArray().isEmpty());

    // Missing controlId
    params = keyParams(slider, QStringLiteral("F"));
    params.insert(QStringLiteral("baseRevision"), currentDocRevision());
    QCOMPARE(errorCode(sendAndWaitForReply(QStringLiteral("vc.widget.keySequence.set"), params)), QStringLiteral("INVALID_PARAMS"));
}

void ApiVcInputDomain_Test::keySequenceRebindLeavesOneEntry()
{
    hello(m_client);
    QString cueList = createWidget(QStringLiteral("CueList"));

    QJsonObject params = keyParams(cueList, QStringLiteral("Space"), 2); // Play/Stop/Pause
    params.insert(QStringLiteral("baseRevision"), currentDocRevision());
    QVERIFY(isOk(sendAndWaitForReply(QStringLiteral("vc.widget.keySequence.set"), params)));
    params = keyParams(cueList, QStringLiteral("Space"), 3); // -> Stop/Pause
    params.insert(QStringLiteral("baseRevision"), currentDocRevision());
    QVERIFY(isOk(sendAndWaitForReply(QStringLiteral("vc.widget.keySequence.set"), params)));

    QJsonArray keys = widgetDetail(cueList).value(QStringLiteral("keySequences")).toArray();
    QCOMPARE(keys.size(), 1);
    QCOMPARE(keys.at(0).toObject().value(QStringLiteral("controlId")).toInt(), 3);
}

void ApiVcInputDomain_Test::structuralMethodsConflictOnStaleRevision()
{
    hello(m_client);
    QString button = createWidget(QStringLiteral("Button"));
    int stale = currentDocRevision() - 1;

    QJsonObject params = sourceParams(button, 0, 0, 1);
    params.insert(QStringLiteral("baseRevision"), stale);
    QJsonObject reply = sendAndWaitForReply(QStringLiteral("vc.widget.inputSource.set"), params);
    QCOMPARE(errorCode(reply), QStringLiteral("CONFLICT"));
    QCOMPARE(reply.value(QStringLiteral("error")).toObject().value(QStringLiteral("details")).toObject().value(QStringLiteral("docRevision")).toInt(), currentDocRevision());

    reply = sendAndWaitForReply(QStringLiteral("vc.widget.inputSource.remove"), params);
    QCOMPARE(errorCode(reply), QStringLiteral("CONFLICT"));

    params = keyParams(button, QStringLiteral("F"), 0);
    params.insert(QStringLiteral("baseRevision"), stale);
    QCOMPARE(errorCode(sendAndWaitForReply(QStringLiteral("vc.widget.keySequence.set"), params)), QStringLiteral("CONFLICT"));
    QCOMPARE(errorCode(sendAndWaitForReply(QStringLiteral("vc.widget.keySequence.remove"), params)), QStringLiteral("CONFLICT"));
}

/*****************************************************************************
 * vc.widget.inputDetect.start / stop
 *****************************************************************************/

void ApiVcInputDomain_Test::inputDetectBindsNextSignal()
{
    IOPluginStub *stub = loadStubPlugin();
    QVERIFY2(stub != nullptr, "iopluginstub DLL not found - is engine/test built?");
    QVERIFY(m_doc->inputOutputMap()->setInputPatch(0, stubName(stub), QString(), QString(), 0, QString()));

    QString learnerId = hello(m_client);
    QWebSocket *other = connectSecondClient();
    hello(other);
    QString button = createWidget(QStringLiteral("Button"));

    QSignalSpy learnerSpy(m_client, &QWebSocket::textMessageReceived);
    QSignalSpy otherSpy(other, &QWebSocket::textMessageReceived);

    QJsonObject params;
    params.insert(QStringLiteral("widgetId"), button);
    params.insert(QStringLiteral("controlId"), 0);
    QJsonObject reply = sendAndWaitForReply(QStringLiteral("vc.widget.inputDetect.start"), params);
    QVERIFY2(isOk(reply), qPrintable(QJsonDocument(reply).toJson()));

    int before = currentDocRevision();
    // A control moves on the patched line: buffered by InputPatch, flushed on the universe's next
    // tick, relayed by InputOutputMap - and bound to the armed control.
    stub->emitValueChanged(0, 0, 5, 255);
    QVERIFY(QTest::qWaitFor([&]() { return eventsWithTopic(learnerSpy, QStringLiteral("vc.widget.inputSourcesChanged")).count() >= 1; }, 3000));
    QJsonObject event = eventsWithTopic(learnerSpy, QStringLiteral("vc.widget.inputSourcesChanged")).first();
    QCOMPARE(event.value(QStringLiteral("widgetId")).toString(), button);
    QCOMPARE(event.value(QStringLiteral("_origin")).toString(), learnerId);
    QCOMPARE(event.value(QStringLiteral("docRevision")).toInt(), before + 1);
    QJsonArray sources = event.value(QStringLiteral("inputSources")).toArray();
    QCOMPARE(sources.size(), 1);
    QCOMPARE(sources.at(0).toObject().value(QStringLiteral("controlId")).toInt(), 0);
    QCOMPARE(sources.at(0).toObject().value(QStringLiteral("universe")).toInt(), 0);
    QCOMPARE(sources.at(0).toObject().value(QStringLiteral("channel")).toInt(), 5);

    // A document change: the bystander sees it too (§4a broadcast), with the learner as origin.
    QVERIFY(QTest::qWaitFor([&]() { return eventsWithTopic(otherSpy, QStringLiteral("vc.widget.inputSourcesChanged")).count() >= 1; }, 2000));
    QCOMPARE(eventsWithTopic(otherSpy, QStringLiteral("vc.widget.inputSourcesChanged")).first().value(QStringLiteral("_origin")).toString(), learnerId);

    // The slot disarmed itself: a second signal binds nothing more.
    stub->emitValueChanged(0, 0, 6, 255);
    QTest::qWait(300);
    QCOMPARE(eventsWithTopic(learnerSpy, QStringLiteral("vc.widget.inputSourcesChanged")).count(), 1);
    QCOMPARE(widgetDetail(button).value(QStringLiteral("inputSources")).toArray().size(), 1);
}

void ApiVcInputDomain_Test::inputDetectSlotIsGlobal()
{
    IOPluginStub *stub = loadStubPlugin();
    QVERIFY(stub != nullptr);
    QVERIFY(m_doc->inputOutputMap()->setInputPatch(0, stubName(stub), QString(), QString(), 0, QString()));

    QString learnerId = hello(m_client);
    QWebSocket *other = connectSecondClient();
    hello(other);
    QString button = createWidget(QStringLiteral("Button"));
    QString slider = createWidget(QStringLiteral("Slider"));

    QJsonObject params;
    params.insert(QStringLiteral("widgetId"), button);
    params.insert(QStringLiteral("controlId"), 0);
    QVERIFY(isOk(sendAndWaitForReply(QStringLiteral("vc.widget.inputDetect.start"), params)));

    // Another client is refused while the slot is held, and told who holds it
    QJsonObject otherParams;
    otherParams.insert(QStringLiteral("widgetId"), slider);
    otherParams.insert(QStringLiteral("controlId"), 0);
    QJsonObject reply = sendAndWaitForReply(other, QStringLiteral("vc.widget.inputDetect.start"), otherParams);
    QCOMPARE(errorCode(reply), QStringLiteral("INVALID_STATE"));
    QCOMPARE(reply.value(QStringLiteral("error")).toObject().value(QStringLiteral("details")).toObject().value(QStringLiteral("clientId")).toString(), learnerId);

    // The holder may re-target its own detection (slider control 0 instead of the button)
    params.insert(QStringLiteral("widgetId"), slider);
    QVERIFY(isOk(sendAndWaitForReply(QStringLiteral("vc.widget.inputDetect.start"), params)));

    // Unknown control -> INVALID_PARAMS, the existing arm stays
    QJsonObject bad = params;
    bad.insert(QStringLiteral("controlId"), 42);
    QCOMPARE(errorCode(sendAndWaitForReply(QStringLiteral("vc.widget.inputDetect.start"), bad)), QStringLiteral("INVALID_PARAMS"));

    QSignalSpy spy(m_client, &QWebSocket::textMessageReceived);
    stub->emitValueChanged(0, 0, 9, 128);
    QVERIFY(QTest::qWaitFor([&]() { return eventsWithTopic(spy, QStringLiteral("vc.widget.inputSourcesChanged")).count() >= 1; }, 3000));
    QCOMPARE(eventsWithTopic(spy, QStringLiteral("vc.widget.inputSourcesChanged")).first().value(QStringLiteral("widgetId")).toString(), slider);
    QVERIFY(widgetDetail(button).value(QStringLiteral("inputSources")).toArray().isEmpty());
    QCOMPARE(widgetDetail(slider).value(QStringLiteral("inputSources")).toArray().at(0).toObject().value(QStringLiteral("channel")).toInt(), 9);

    // Slot released: the other client can now arm
    QVERIFY(isOk(sendAndWaitForReply(other, QStringLiteral("vc.widget.inputDetect.start"), otherParams)));
    QVERIFY(isOk(sendAndWaitForReply(other, QStringLiteral("vc.widget.inputDetect.stop"), QJsonObject())));
}

void ApiVcInputDomain_Test::inputDetectStopAndDisconnectRelease()
{
    IOPluginStub *stub = loadStubPlugin();
    QVERIFY(stub != nullptr);
    QVERIFY(m_doc->inputOutputMap()->setInputPatch(0, stubName(stub), QString(), QString(), 0, QString()));

    hello(m_client);
    QWebSocket *other = connectSecondClient();
    hello(other);
    QString button = createWidget(QStringLiteral("Button"));

    QJsonObject params;
    params.insert(QStringLiteral("widgetId"), button);
    params.insert(QStringLiteral("controlId"), 0);
    QVERIFY(isOk(sendAndWaitForReply(QStringLiteral("vc.widget.inputDetect.start"), params)));

    // stop from ANY client cancels (spec: the engine slot is global)
    QVERIFY(isOk(sendAndWaitForReply(other, QStringLiteral("vc.widget.inputDetect.stop"), QJsonObject())));
    QSignalSpy spy(m_client, &QWebSocket::textMessageReceived);
    stub->emitValueChanged(0, 0, 5, 255);
    QTest::qWait(300);
    QCOMPARE(eventsWithTopic(spy, QStringLiteral("vc.widget.inputSourcesChanged")).count(), 0);
    QVERIFY(widgetDetail(button).value(QStringLiteral("inputSources")).toArray().isEmpty());

    // The other client arms and then drops its connection: the slot is released, nothing binds
    QJsonObject otherParams;
    otherParams.insert(QStringLiteral("widgetId"), button);
    otherParams.insert(QStringLiteral("controlId"), 0);
    QVERIFY(isOk(sendAndWaitForReply(other, QStringLiteral("vc.widget.inputDetect.start"), otherParams)));
    other->close();
    QVERIFY(QTest::qWaitFor([other]() { return other->state() == QAbstractSocket::UnconnectedState; }, 2000));
    QTest::qWait(100);
    stub->emitValueChanged(0, 0, 5, 255);
    QTest::qWait(300);
    QVERIFY(widgetDetail(button).value(QStringLiteral("inputSources")).toArray().isEmpty());

    // ...and the first client can arm again
    QVERIFY(isOk(sendAndWaitForReply(QStringLiteral("vc.widget.inputDetect.start"), params)));
    QVERIFY(isOk(sendAndWaitForReply(QStringLiteral("vc.widget.inputDetect.stop"), QJsonObject())));
}

QTEST_MAIN(ApiVcInputDomain_Test)
