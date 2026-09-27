/*
  Q Light Controller Plus - Control API unit test
  apifunctionsmiscdomain_test.cpp

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
#include <QtEndian>
#include <QtMath>
#include <QtTest>
#include <QFile>
#include <QDir>

#include "apifunctionsmiscdomain_test.h"
#include "fakevchost.h"
#include "apiserver.h"
#include "inputoutputmap.h"
#include "audioplugincache.h"
#include "rgbscriptscache.h"
#include "rgbalgorithm.h"
#include "mediaassets.h"
#include "fixturegroup.h"
#include "qlcpalette.h"
#include "collection.h"
#include "chaserstep.h"
#include "rgbmatrix.h"
#include "sequence.h"
#include "universe.h"
#include "fixture.h"
#include "chaser.h"
#include "scene.h"
#include "audio.h"
#include "video.h"
#include "doc.h"

namespace
{

QJsonObject resultOf(const QJsonObject &reply) { return reply.value(QStringLiteral("result")).toObject(); }
bool isOk(const QJsonObject &reply) { return reply.value(QStringLiteral("ok")).toBool(); }
QString errorCode(const QJsonObject &reply)
{
    return reply.value(QStringLiteral("error")).toObject().value(QStringLiteral("code")).toString();
}

/** A 16-bit mono PCM WAV: @seconds of silence with a 20 ms 1 kHz click
 *  every 60/@bpm seconds - a trivially detectable beat */
bool writeClickTrack(const QString &path, double bpm, int seconds)
{
    const int rate = 44100;
    const int samples = rate * seconds;
    QByteArray pcm(samples * 2, 0);
    const int period = int(rate * 60.0 / bpm);
    const int clickLen = rate / 50;
    for (int i = 0; i < samples; i++)
    {
        int inBeat = i % period;
        qint16 v = 0;
        if (inBeat < clickLen)
            v = qint16(26000 * qSin(2 * M_PI * 1000.0 * inBeat / rate) * (1.0 - double(inBeat) / clickLen));
        qToLittleEndian<qint16>(v, pcm.data() + i * 2);
    }
    QByteArray hdr;
    auto u32 = [&hdr](quint32 x) { char b[4]; qToLittleEndian<quint32>(x, b); hdr.append(b, 4); };
    auto u16 = [&hdr](quint16 x) { char b[2]; qToLittleEndian<quint16>(x, b); hdr.append(b, 2); };
    hdr.append("RIFF"); u32(36 + pcm.size()); hdr.append("WAVE");
    hdr.append("fmt "); u32(16); u16(1); u16(1); u32(rate); u32(rate * 2); u16(2); u16(16);
    hdr.append("data"); u32(pcm.size());
    QFile f(path);
    if (f.open(QIODevice::WriteOnly) == false)
        return false;
    f.write(hdr);
    f.write(pcm);
    return true;
}

} // namespace

void ApiFunctionsMiscDomain_Test::init()
{
    m_seq = 0;
    m_doc = new Doc(nullptr);
    QVERIFY(m_doc->rgbScriptsCache()->load(QDir(QString::fromUtf8(RGBSCRIPTS_DIR))));
    m_host = new FakeVcHost();
    m_apiServer = new ApiServer(m_host, m_doc);
    QVERIFY(m_apiServer->listen(0, QHostAddress::LocalHost));

    m_client = new QWebSocket();
    m_client->open(QUrl(QStringLiteral("ws://127.0.0.1:%1/qlcplusapi").arg(m_apiServer->serverPort())));
    QVERIFY(QTest::qWaitFor([this]() { return m_client->state() == QAbstractSocket::ConnectedState; }, 2000));
    QVERIFY(isOk(call(QStringLiteral("hello"), QJsonObject())));
}

void ApiFunctionsMiscDomain_Test::cleanup()
{
    delete m_client;
    m_client = nullptr;
    delete m_apiServer;
    m_apiServer = nullptr;
    delete m_host;
    m_host = nullptr;
    delete m_doc;
    m_doc = nullptr;
}

QJsonObject ApiFunctionsMiscDomain_Test::call(const QString &method, const QJsonObject &params)
{
    const QString requestId = QStringLiteral("t-%1").arg(++m_seq);
    QJsonObject req;
    req.insert(QStringLiteral("type"), QStringLiteral("request"));
    req.insert(QStringLiteral("id"), requestId);
    req.insert(QStringLiteral("method"), method);
    req.insert(QStringLiteral("params"), params);

    QSignalSpy spy(m_client, &QWebSocket::textMessageReceived);
    m_client->sendTextMessage(QString::fromUtf8(QJsonDocument(req).toJson(QJsonDocument::Compact)));
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
    }, 5000);
    return found;
}

QJsonObject ApiFunctionsMiscDomain_Test::callRev(const QString &method, QJsonObject params)
{
    params.insert(QStringLiteral("baseRevision"), revision());
    return call(method, params);
}

int ApiFunctionsMiscDomain_Test::revision()
{
    return int(m_doc->docRevision());
}

QJsonObject ApiFunctionsMiscDomain_Test::waitForEvent(QSignalSpy &spy, const QString &topic,
                                                      const std::function<bool(const QJsonObject &)> &accept, int timeoutMs)
{
    QJsonObject found;
    (void)QTest::qWaitFor([&]()
    {
        for (const QList<QVariant> &frame : spy)
        {
            QJsonObject obj = QJsonDocument::fromJson(frame.at(0).toString().toUtf8()).object();
            if (obj.value(QStringLiteral("type")).toString() == QStringLiteral("event") &&
                obj.value(QStringLiteral("topic")).toString() == topic &&
                (accept == nullptr || accept(obj.value(QStringLiteral("data")).toObject())))
            {
                found = obj.value(QStringLiteral("data")).toObject();
                return true;
            }
        }
        return false;
    }, timeoutMs);
    return found;
}

Scene *ApiFunctionsMiscDomain_Test::addScene(const QString &name)
{
    Scene *scene = new Scene(m_doc);
    scene->setName(name);
    if (m_doc->addFunction(scene) == false)
    {
        delete scene;
        return nullptr;
    }
    return scene;
}

/*****************************************************************************
 * Chaser / Sequence
 *****************************************************************************/

void ApiFunctionsMiscDomain_Test::chaserSetSpeedModesChangesAndBroadcasts()
{
    Chaser *chaser = new Chaser(m_doc);
    QVERIFY(m_doc->addFunction(chaser));
    QSignalSpy spy(m_client, &QWebSocket::textMessageReceived);

    QJsonObject p;
    p.insert(QStringLiteral("functionId"), sid(chaser->id()));
    p.insert(QStringLiteral("fadeInMode"), QStringLiteral("PerStep"));
    p.insert(QStringLiteral("durationMode"), QStringLiteral("Common"));
    QJsonObject reply = callRev(QStringLiteral("functions.chaser.setSpeedModes"), p);
    QVERIFY2(isOk(reply), qPrintable(QJsonDocument(reply).toJson()));
    QCOMPARE(chaser->fadeInMode(), Chaser::PerStep);
    QCOMPARE(chaser->durationMode(), Chaser::Common);
    QCOMPARE(resultOf(reply).value(QStringLiteral("docRevision")).toInt(), revision());

    QJsonObject ev = waitForEvent(spy, QStringLiteral("functions.chaser.changed"));
    QCOMPARE(ev.value(QStringLiteral("fadeInMode")).toString(), QStringLiteral("PerStep"));
    QCOMPARE(ev.value(QStringLiteral("durationMode")).toString(), QStringLiteral("Common"));
    QCOMPARE(ev.value(QStringLiteral("docRevision")).toInt(), revision());

    // functions.get reads the modes back
    QJsonObject get;
    get.insert(QStringLiteral("functionId"), sid(chaser->id()));
    QJsonObject td = resultOf(call(QStringLiteral("functions.get"), get)).value(QStringLiteral("typeDetail")).toObject();
    QCOMPARE(td.value(QStringLiteral("fadeInMode")).toString(), QStringLiteral("PerStep"));
}

void ApiFunctionsMiscDomain_Test::chaserSetSpeedModesRejectsBadInput()
{
    Chaser *chaser = new Chaser(m_doc);
    QVERIFY(m_doc->addFunction(chaser));
    Scene *scene = addScene(QStringLiteral("S"));

    QJsonObject p;
    p.insert(QStringLiteral("functionId"), sid(chaser->id()));
    p.insert(QStringLiteral("fadeOutMode"), QStringLiteral("Sometimes"));
    QCOMPARE(errorCode(callRev(QStringLiteral("functions.chaser.setSpeedModes"), p)), QStringLiteral("INVALID_PARAMS"));

    p.insert(QStringLiteral("fadeOutMode"), QStringLiteral("Common"));
    p.insert(QStringLiteral("baseRevision"), revision() + 5);
    QCOMPARE(errorCode(call(QStringLiteral("functions.chaser.setSpeedModes"), p)), QStringLiteral("CONFLICT"));

    p.insert(QStringLiteral("functionId"), sid(scene->id()));
    QCOMPARE(errorCode(callRev(QStringLiteral("functions.chaser.setSpeedModes"), p)), QStringLiteral("NOT_FOUND"));
}

void ApiFunctionsMiscDomain_Test::chaserSetActionValidates()
{
    Scene *a = addScene(QStringLiteral("A"));
    Scene *b = addScene(QStringLiteral("B"));
    Chaser *chaser = new Chaser(m_doc);
    QVERIFY(m_doc->addFunction(chaser));
    chaser->addStep(ChaserStep(a->id()));
    chaser->addStep(ChaserStep(b->id()));

    QJsonObject p;
    p.insert(QStringLiteral("functionId"), sid(chaser->id()));
    p.insert(QStringLiteral("action"), QStringLiteral("nextStep"));
    QVERIFY(isOk(call(QStringLiteral("functions.chaser.setAction"), p)));
    p.insert(QStringLiteral("action"), QStringLiteral("previousStep"));
    QVERIFY(isOk(call(QStringLiteral("functions.chaser.setAction"), p)));

    p.insert(QStringLiteral("action"), QStringLiteral("setStepIndex"));
    QCOMPARE(errorCode(call(QStringLiteral("functions.chaser.setAction"), p)), QStringLiteral("INVALID_PARAMS"));
    p.insert(QStringLiteral("stepIndex"), 5);
    QCOMPARE(errorCode(call(QStringLiteral("functions.chaser.setAction"), p)), QStringLiteral("INVALID_PARAMS"));
    p.insert(QStringLiteral("stepIndex"), 1);
    QVERIFY(isOk(call(QStringLiteral("functions.chaser.setAction"), p)));

    // the runner's currentStepChanged is relayed as functions.chaser.currentStepChanged
    QSignalSpy spy(m_client, &QWebSocket::textMessageReceived);
    emit chaser->currentStepChanged(1);
    QJsonObject ev = waitForEvent(spy, QStringLiteral("functions.chaser.currentStepChanged"));
    QCOMPARE(ev.value(QStringLiteral("functionId")).toString(), sid(chaser->id()));
    QCOMPARE(ev.value(QStringLiteral("stepIndex")).toInt(), 1);

    p.insert(QStringLiteral("action"), QStringLiteral("jump"));
    QCOMPARE(errorCode(call(QStringLiteral("functions.chaser.setAction"), p)), QStringLiteral("INVALID_PARAMS"));
    p.insert(QStringLiteral("functionId"), sid(a->id()));
    p.insert(QStringLiteral("action"), QStringLiteral("nextStep"));
    QCOMPARE(errorCode(call(QStringLiteral("functions.chaser.setAction"), p)), QStringLiteral("NOT_FOUND"));
}

void ApiFunctionsMiscDomain_Test::sequenceSetBoundSceneRebindsSteps()
{
    Scene *oldScene = addScene(QStringLiteral("Old"));
    Scene *newScene = addScene(QStringLiteral("New"));
    Sequence *sequence = new Sequence(m_doc);
    QVERIFY(m_doc->addFunction(sequence));
    sequence->setBoundSceneID(oldScene->id());
    sequence->addStep(ChaserStep(oldScene->id()));
    sequence->addStep(ChaserStep(oldScene->id()));
    QSignalSpy spy(m_client, &QWebSocket::textMessageReceived);

    QJsonObject p;
    p.insert(QStringLiteral("functionId"), sid(sequence->id()));
    p.insert(QStringLiteral("sceneId"), sid(newScene->id()));
    QVERIFY(isOk(callRev(QStringLiteral("functions.sequence.setBoundScene"), p)));
    QCOMPARE(sequence->boundSceneID(), newScene->id());
    QCOMPARE(sequence->stepAt(0)->fid, newScene->id());
    QCOMPARE(sequence->stepAt(1)->fid, newScene->id());
    QJsonObject ev = waitForEvent(spy, QStringLiteral("functions.sequence.changed"));
    QCOMPARE(ev.value(QStringLiteral("boundSceneId")).toString(), sid(newScene->id()));

    // a non-Scene target is refused
    p.insert(QStringLiteral("sceneId"), sid(sequence->id()));
    QCOMPARE(errorCode(callRev(QStringLiteral("functions.sequence.setBoundScene"), p)), QStringLiteral("NOT_FOUND"));
}

void ApiFunctionsMiscDomain_Test::sequenceApplyDumpValuesExplicit()
{
    Scene *scene = addScene(QStringLiteral("Bound"));
    scene->setValue(SceneValue(0, 0, 0));
    scene->setValue(SceneValue(0, 1, 0));
    Sequence *sequence = new Sequence(m_doc);
    QVERIFY(m_doc->addFunction(sequence));
    sequence->setBoundSceneID(scene->id());
    ChaserStep step(scene->id());
    step.values << SceneValue(0, 0, 10) << SceneValue(0, 1, 20);
    sequence->addStep(step);
    QSignalSpy spy(m_client, &QWebSocket::textMessageReceived);

    // into step 0
    QJsonObject values;
    values.insert(QStringLiteral("0.1"), 99);
    QJsonObject p;
    p.insert(QStringLiteral("functionId"), sid(sequence->id()));
    p.insert(QStringLiteral("values"), values);
    p.insert(QStringLiteral("targetStepIndex"), 0);
    QJsonObject reply = callRev(QStringLiteral("functions.sequence.applyDumpValues"), p);
    QVERIFY2(isOk(reply), qPrintable(QJsonDocument(reply).toJson()));
    QCOMPARE(resultOf(reply).value(QStringLiteral("stepIndex")).toInt(), 0);
    QCOMPARE(sequence->stepsCount(), 1);
    QCOMPARE(int(sequence->stepAt(0)->values.at(0).value), 10);
    QCOMPARE(int(sequence->stepAt(0)->values.at(1).value), 99);
    QJsonObject ev = waitForEvent(spy, QStringLiteral("functions.sequence.stepsChanged"));
    QJsonObject op = ev.value(QStringLiteral("patch")).toArray().at(0).toObject();
    QCOMPARE(op.value(QStringLiteral("path")).toString(), QStringLiteral("/steps"));
    QCOMPARE(op.value(QStringLiteral("value")).toArray().at(0).toObject().value(QStringLiteral("values")).toObject()
             .value(QStringLiteral("0.1")).toInt(), 99);

    // omitted index appends a step
    p.remove(QStringLiteral("targetStepIndex"));
    reply = callRev(QStringLiteral("functions.sequence.applyDumpValues"), p);
    QVERIFY(isOk(reply));
    QCOMPARE(sequence->stepsCount(), 2);
    QCOMPARE(resultOf(reply).value(QStringLiteral("stepIndex")).toInt(), 1);

    // neither values nor captureLive
    p.remove(QStringLiteral("values"));
    QCOMPARE(errorCode(callRev(QStringLiteral("functions.sequence.applyDumpValues"), p)), QStringLiteral("INVALID_PARAMS"));
}

void ApiFunctionsMiscDomain_Test::sequenceApplyDumpValuesCaptureLive()
{
    Fixture *fxi = new Fixture(m_doc);
    fxi->setName(QStringLiteral("Dim"));
    fxi->setUniverse(0);
    fxi->setAddress(10);
    fxi->setChannels(3);
    QVERIFY(m_doc->addFixture(fxi));

    Scene *scene = addScene(QStringLiteral("Bound"));
    scene->setValue(SceneValue(fxi->id(), 0, 0));
    scene->setValue(SceneValue(fxi->id(), 2, 0));
    Sequence *sequence = new Sequence(m_doc);
    QVERIFY(m_doc->addFunction(sequence));
    sequence->setBoundSceneID(scene->id());

    // live output: channel 0 = 77, channel 2 = 150 (absolute 10 and 12)
    QList<Universe *> ua = m_doc->inputOutputMap()->claimUniverses();
    ua.at(0)->write(10, 77);
    ua.at(0)->write(12, 150);
    m_doc->inputOutputMap()->releaseUniverses(false);

    QJsonObject p;
    p.insert(QStringLiteral("functionId"), sid(sequence->id()));
    p.insert(QStringLiteral("captureLive"), true);
    QJsonObject reply = callRev(QStringLiteral("functions.sequence.applyDumpValues"), p);
    QVERIFY2(isOk(reply), qPrintable(QJsonDocument(reply).toJson()));
    QCOMPARE(resultOf(reply).value(QStringLiteral("capturedChannels")).toInt(), 2);
    QCOMPARE(sequence->stepsCount(), 1);
    QList<SceneValue> vals = sequence->stepAt(0)->values;
    QCOMPARE(vals.count(), 2);
    QCOMPARE(int(vals.at(0).value), 77);
    QCOMPARE(int(vals.at(1).value), 150);
}

/*****************************************************************************
 * Generic
 *****************************************************************************/

void ApiFunctionsMiscDomain_Test::adjustAttributeClampsAndBroadcasts()
{
    Scene *scene = addScene(QStringLiteral("S"));
    QSignalSpy spy(m_client, &QWebSocket::textMessageReceived);

    QJsonObject p;
    p.insert(QStringLiteral("functionId"), sid(scene->id()));
    p.insert(QStringLiteral("attributeName"), QStringLiteral("Intensity"));
    p.insert(QStringLiteral("value"), 0.4);
    QVERIFY(isOk(call(QStringLiteral("functions.adjustAttribute"), p)));
    QCOMPARE(scene->getAttributeValue(Function::Intensity), 0.4);
    QJsonObject ev = waitForEvent(spy, QStringLiteral("functions.attributeChanged"));
    QCOMPARE(ev.value(QStringLiteral("attributeName")).toString(), QStringLiteral("Intensity"));
    QCOMPARE(ev.value(QStringLiteral("value")).toDouble(), 0.4);

    // clamped to the attribute's max
    p.remove(QStringLiteral("attributeName"));
    p.insert(QStringLiteral("attributeIndex"), 0);
    p.insert(QStringLiteral("value"), 7.0);
    QVERIFY(isOk(call(QStringLiteral("functions.adjustAttribute"), p)));
    QCOMPARE(scene->getAttributeValue(Function::Intensity), 1.0);

    p.insert(QStringLiteral("attributeIndex"), 42);
    QCOMPARE(errorCode(call(QStringLiteral("functions.adjustAttribute"), p)), QStringLiteral("NOT_FOUND"));
    p.remove(QStringLiteral("attributeIndex"));
    QCOMPARE(errorCode(call(QStringLiteral("functions.adjustAttribute"), p)), QStringLiteral("INVALID_PARAMS"));
}

void ApiFunctionsMiscDomain_Test::tapKnownAndUnknown()
{
    Chaser *chaser = new Chaser(m_doc);
    QVERIFY(m_doc->addFunction(chaser));
    QJsonObject p;
    p.insert(QStringLiteral("functionId"), sid(chaser->id()));
    QVERIFY(isOk(call(QStringLiteral("functions.tap"), p)));
    p.insert(QStringLiteral("functionId"), QStringLiteral("9999"));
    QCOMPARE(errorCode(call(QStringLiteral("functions.tap"), p)), QStringLiteral("NOT_FOUND"));
}

void ApiFunctionsMiscDomain_Test::cloneSceneAndSequence()
{
    Scene *scene = addScene(QStringLiteral("Warm"));
    scene->setValue(SceneValue(0, 3, 128));
    scene->setPath(QStringLiteral("Looks"));
    Scene *bound = addScene(QStringLiteral("Bound"));
    Sequence *sequence = new Sequence(m_doc);
    sequence->setName(QStringLiteral("Seq"));
    QVERIFY(m_doc->addFunction(sequence));
    sequence->setBoundSceneID(bound->id());
    sequence->addStep(ChaserStep(bound->id()));
    int before = m_doc->functions().count();
    QSignalSpy spy(m_client, &QWebSocket::textMessageReceived);

    QJsonObject p;
    p.insert(QStringLiteral("functionIds"), QJsonArray{ sid(scene->id()), sid(sequence->id()) });
    QJsonObject reply = callRev(QStringLiteral("functions.clone"), p);
    QVERIFY2(isOk(reply), qPrintable(QJsonDocument(reply).toJson()));
    QJsonArray ids = resultOf(reply).value(QStringLiteral("functionIds")).toArray();
    QCOMPARE(ids.count(), 2);
    QCOMPARE(m_doc->functions().count(), before + 3); // + the Sequence's own Scene copy

    Scene *sceneCopy = qobject_cast<Scene *>(m_doc->function(ids.at(0).toString().toUInt()));
    QVERIFY(sceneCopy != nullptr);
    QCOMPARE(sceneCopy->name(), QStringLiteral("Warm (Copy)"));
    QCOMPARE(sceneCopy->path(true), QStringLiteral("Looks"));
    QCOMPARE(int(sceneCopy->value(0, 3)), 128);

    Sequence *seqCopy = qobject_cast<Sequence *>(m_doc->function(ids.at(1).toString().toUInt()));
    QVERIFY(seqCopy != nullptr);
    QVERIFY(seqCopy->boundSceneID() != bound->id());
    QVERIFY(qobject_cast<Scene *>(m_doc->function(seqCopy->boundSceneID())) != nullptr);
    QCOMPARE(seqCopy->stepAt(0)->fid, seqCopy->boundSceneID());

    QJsonObject ev = waitForEvent(spy, QStringLiteral("functions.created"), [&](const QJsonObject &d)
    {
        return d.value(QStringLiteral("function")).toObject().value(QStringLiteral("id")).toString() == ids.at(1).toString();
    });
    QVERIFY2(ev.isEmpty() == false, "missing functions.created for the Sequence copy");

    p.insert(QStringLiteral("functionIds"), QJsonArray{ QStringLiteral("9999") });
    QCOMPARE(errorCode(callRev(QStringLiteral("functions.clone"), p)), QStringLiteral("NOT_FOUND"));
}

void ApiFunctionsMiscDomain_Test::usageListsFunctionsAndWidgets()
{
    Scene *scene = addScene(QStringLiteral("Used"));
    Scene *other = addScene(QStringLiteral("Other"));
    Collection *collection = new Collection(m_doc);
    collection->setName(QStringLiteral("Coll"));
    QVERIFY(m_doc->addFunction(collection));
    collection->addFunction(other->id());
    collection->addFunction(scene->id());
    Chaser *chaser = new Chaser(m_doc);
    chaser->setName(QStringLiteral("Ch"));
    QVERIFY(m_doc->addFunction(chaser));
    chaser->addStep(ChaserStep(scene->id()));

    // a VC button referencing the scene
    QJsonObject cfg;
    cfg.insert(QStringLiteral("functionID"), sid(scene->id()));
    QJsonObject geom;
    geom.insert(QStringLiteral("x"), 0); geom.insert(QStringLiteral("y"), 0);
    geom.insert(QStringLiteral("width"), 10); geom.insert(QStringLiteral("height"), 10);
    QJsonObject create;
    create.insert(QStringLiteral("widgetType"), QStringLiteral("Button"));
    create.insert(QStringLiteral("page"), 0);
    create.insert(QStringLiteral("geometry"), geom);
    create.insert(QStringLiteral("typeConfig"), cfg);
    QJsonObject created = callRev(QStringLiteral("vc.widget.create"), create);
    QVERIFY2(isOk(created), qPrintable(QJsonDocument(created).toJson()));
    QString widgetId = resultOf(created).value(QStringLiteral("widgetId")).toString();

    QJsonObject p;
    p.insert(QStringLiteral("functionId"), sid(scene->id()));
    QJsonObject result = resultOf(call(QStringLiteral("functions.usage"), p));
    QJsonArray functions = result.value(QStringLiteral("functions")).toArray();
    QCOMPARE(functions.count(), 2);
    QHash<QString, int> positions;
    for (const QJsonValue &v : functions)
        positions.insert(v.toObject().value(QStringLiteral("name")).toString(), v.toObject().value(QStringLiteral("position")).toInt());
    QCOMPARE(positions.value(QStringLiteral("Coll"), -1), 1);
    QCOMPARE(positions.value(QStringLiteral("Ch"), -1), 0);
    QJsonArray widgets = result.value(QStringLiteral("widgets")).toArray();
    QCOMPARE(widgets.count(), 1);
    QCOMPARE(widgets.at(0).toObject().value(QStringLiteral("id")).toString(), widgetId);
    QCOMPARE(result.value(QStringLiteral("vcAvailable")).toBool(), true);

    // unused function: empty lists, not an error
    Scene *lonely = addScene(QStringLiteral("Lonely"));
    p.insert(QStringLiteral("functionId"), sid(lonely->id()));
    result = resultOf(call(QStringLiteral("functions.usage"), p));
    QCOMPARE(result.value(QStringLiteral("functions")).toArray().count(), 0);
    QCOMPARE(result.value(QStringLiteral("widgets")).toArray().count(), 0);

    p.insert(QStringLiteral("functionId"), QStringLiteral("abc"));
    QCOMPARE(errorCode(call(QStringLiteral("functions.usage"), p)), QStringLiteral("INVALID_PARAMS"));
}

void ApiFunctionsMiscDomain_Test::startupFunctionSetGetUnset()
{
    Scene *scene = addScene(QStringLiteral("Boot"));
    QCOMPARE(resultOf(call(QStringLiteral("core.project.get"), QJsonObject())).value(QStringLiteral("startupFunctionId")).isNull(), true);
    QSignalSpy spy(m_client, &QWebSocket::textMessageReceived);

    QJsonObject p;
    p.insert(QStringLiteral("functionId"), sid(scene->id()));
    int rev = revision();
    QVERIFY(isOk(callRev(QStringLiteral("core.project.setStartupFunction"), p)));
    QCOMPARE(m_doc->startupFunction(), scene->id());
    QVERIFY(revision() > rev);
    QCOMPARE(resultOf(call(QStringLiteral("core.project.get"), QJsonObject())).value(QStringLiteral("startupFunctionId")).toString(), sid(scene->id()));
    QJsonObject ev = waitForEvent(spy, QStringLiteral("core.project.startupFunctionChanged"));
    QCOMPARE(ev.value(QStringLiteral("startupFunctionId")).toString(), sid(scene->id()));

    p.insert(QStringLiteral("functionId"), QJsonValue());
    QVERIFY(isOk(callRev(QStringLiteral("core.project.setStartupFunction"), p)));
    QCOMPARE(m_doc->startupFunction(), Function::invalidId());

    p.insert(QStringLiteral("functionId"), QStringLiteral("9999"));
    QCOMPARE(errorCode(callRev(QStringLiteral("core.project.setStartupFunction"), p)), QStringLiteral("NOT_FOUND"));
    p.insert(QStringLiteral("functionId"), sid(scene->id()));
    p.insert(QStringLiteral("baseRevision"), revision() + 3);
    QCOMPARE(errorCode(call(QStringLiteral("core.project.setStartupFunction"), p)), QStringLiteral("CONFLICT"));
}

/*****************************************************************************
 * Media store, Audio / Video setters
 *****************************************************************************/

void ApiFunctionsMiscDomain_Test::mediaStatusCollectAndRemoveUnused()
{
    QTemporaryDir tmp;
    QVERIFY(tmp.isValid());
    // an external source and a saved project location for the store
    QString external = tmp.filePath(QStringLiteral("outside/clip.wav"));
    QVERIFY(QDir().mkpath(QFileInfo(external).absolutePath()));
    QVERIFY(writeClickTrack(external, 120, 1));
    m_doc->assets()->setProjectFile(tmp.filePath(QStringLiteral("show/show.qxw")));
    QVERIFY(QDir().mkpath(tmp.filePath(QStringLiteral("show"))));

    Audio *audio = new Audio(m_doc);
    QVERIFY(m_doc->addFunction(audio));
    audio->setSourceFileName(external);

    QJsonObject status = resultOf(call(QStringLiteral("functions.media.status"), QJsonObject()));
    QCOMPARE(status.value(QStringLiteral("external")).toArray().count(), 1);
    QCOMPARE(status.value(QStringLiteral("unused")).toArray().count(), 0);

    QSignalSpy spy(m_client, &QWebSocket::textMessageReceived);
    QJsonObject reply = callRev(QStringLiteral("functions.media.collect"), QJsonObject());
    QVERIFY2(isOk(reply), qPrintable(QJsonDocument(reply).toJson()));
    QCOMPARE(resultOf(reply).value(QStringLiteral("copied")).toInt() + resultOf(reply).value(QStringLiteral("queued")).toInt(), 1);
    QTRY_VERIFY_WITH_TIMEOUT(m_doc->assets()->isManaged(audio->getSourceFileName()), 10000);
    QVERIFY(waitForEvent(spy, QStringLiteral("functions.media.collected")).isEmpty() == false);
    status = resultOf(call(QStringLiteral("functions.media.status"), QJsonObject()));
    QCOMPARE(status.value(QStringLiteral("external")).toArray().count(), 0);

    // an orphan copy in the store's <sha12>/ layout is "unused" and removable
    QString storeDir = m_doc->assets()->assetsDir();
    QString orphan = storeDir + QStringLiteral("/0123456789ab/orphan.wav");
    QVERIFY(QDir().mkpath(QFileInfo(orphan).absolutePath()));
    QVERIFY(writeClickTrack(orphan, 100, 1));
    status = resultOf(call(QStringLiteral("functions.media.status"), QJsonObject()));
    QCOMPARE(status.value(QStringLiteral("unused")).toArray().count(), 1);

    reply = call(QStringLiteral("functions.media.removeUnused"), QJsonObject());
    QVERIFY(isOk(reply));
    QCOMPARE(resultOf(reply).value(QStringLiteral("removed")).toInt(), 1);
    QCOMPARE(QFileInfo::exists(orphan), false);
    QCOMPARE(QFileInfo::exists(audio->getSourceFileName()), true); // the referenced copy stays
    QCOMPARE(resultOf(reply).value(QStringLiteral("unused")).toArray().count(), 0);
}

void ApiFunctionsMiscDomain_Test::audioSetMutedAndDetectBpm()
{
    QTemporaryDir tmp;
    QVERIFY(tmp.isValid());
    QString wav = tmp.filePath(QStringLiteral("beat.wav"));
    QVERIFY(writeClickTrack(wav, 120, 12));
#ifdef SNDFILE_PLUGIN_DIR
    m_doc->audioPluginCache()->load(QDir(QString::fromUtf8(SNDFILE_PLUGIN_DIR)));
#endif
    Audio *audio = new Audio(m_doc);
    QVERIFY(m_doc->addFunction(audio));
    audio->setSourceFileName(wav);
    QTRY_VERIFY_WITH_TIMEOUT(audio->bpmAnalysisState() != Audio::Analyzing, 20000);
    QSignalSpy spy(m_client, &QWebSocket::textMessageReceived);

    QJsonObject p;
    p.insert(QStringLiteral("functionId"), sid(audio->id()));
    p.insert(QStringLiteral("muted"), true);
    QVERIFY(isOk(callRev(QStringLiteral("functions.audio.setMuted"), p)));
    QCOMPARE(audio->muted(), true);
    QCOMPARE(waitForEvent(spy, QStringLiteral("functions.audio.mutedChanged")).value(QStringLiteral("muted")).toBool(), true);
    p.insert(QStringLiteral("muted"), QStringLiteral("yes"));
    QCOMPARE(errorCode(callRev(QStringLiteral("functions.audio.setMuted"), p)), QStringLiteral("INVALID_PARAMS"));

    // detectBpm restarts the analysis and reports its end
    p.remove(QStringLiteral("muted"));
    QJsonObject reply = call(QStringLiteral("functions.audio.detectBpm"), p);
    QVERIFY2(isOk(reply), qPrintable(QJsonDocument(reply).toJson()));
    QJsonObject ev = waitForEvent(spy, QStringLiteral("functions.audio.bpmChanged"), [](const QJsonObject &d)
    {
        QString state = d.value(QStringLiteral("bpm")).toObject().value(QStringLiteral("state")).toString();
        return state == QStringLiteral("done") || state == QStringLiteral("failed");
    }, 30000);
    QVERIFY2(ev.isEmpty() == false, "missing the final functions.audio.bpmChanged");
#ifdef SNDFILE_PLUGIN_DIR
    // with a decoder the click track is detected at ~120 BPM (or a
    // half/double-time octave of it)
    QJsonObject bpm = ev.value(QStringLiteral("bpm")).toObject();
    QCOMPARE(bpm.value(QStringLiteral("state")).toString(), QStringLiteral("done"));
    double value = bpm.value(QStringLiteral("value")).toDouble();
    QVERIFY2(qAbs(value - 120) < 3 || qAbs(value - 60) < 2 || qAbs(value - 240) < 5, qPrintable(QString::number(value)));
#endif

    Audio *missing = new Audio(m_doc);
    QVERIFY(m_doc->addFunction(missing));
    p.insert(QStringLiteral("functionId"), sid(missing->id()));
    QCOMPARE(errorCode(call(QStringLiteral("functions.audio.detectBpm"), p)), QStringLiteral("INVALID_STATE"));
}

void ApiFunctionsMiscDomain_Test::videoVolumeMuteSpoutSize()
{
    Video *video = new Video(m_doc);
    QVERIFY(m_doc->addFunction(video));
    QSignalSpy spy(m_client, &QWebSocket::textMessageReceived);

    QJsonObject p;
    p.insert(QStringLiteral("functionId"), sid(video->id()));
    // 0-100, the Video Volume attribute's own range
    p.insert(QStringLiteral("volume"), 30);
    QVERIFY(isOk(callRev(QStringLiteral("functions.video.setVolume"), p)));
    QCOMPARE(video->volume(), 30.0);
    QCOMPARE(waitForEvent(spy, QStringLiteral("functions.video.volumeChanged")).value(QStringLiteral("volume")).toDouble(), 30.0);
    p.insert(QStringLiteral("volume"), 150);
    QCOMPARE(errorCode(callRev(QStringLiteral("functions.video.setVolume"), p)), QStringLiteral("INVALID_PARAMS"));

    p.remove(QStringLiteral("volume"));
    p.insert(QStringLiteral("muted"), true);
    QVERIFY(isOk(callRev(QStringLiteral("functions.video.setMuted"), p)));
    QCOMPARE(video->muted(), true);

    p.remove(QStringLiteral("muted"));
    p.insert(QStringLiteral("width"), 1280);
    p.insert(QStringLiteral("height"), 720);
    QVERIFY(isOk(callRev(QStringLiteral("functions.video.setSpoutSize"), p)));
    QCOMPARE(video->spoutSize(), QSize(1280, 720));
    QJsonObject ev = waitForEvent(spy, QStringLiteral("functions.video.spoutSizeChanged"));
    QCOMPARE(ev.value(QStringLiteral("spoutSize")).toObject().value(QStringLiteral("width")).toInt(), 1280);
    p.insert(QStringLiteral("width"), 0);
    QVERIFY(isOk(callRev(QStringLiteral("functions.video.setSpoutSize"), p)));
    QCOMPARE(video->spoutSize(), QSize(0, 0));

    // spoutSenderName is part of functions.get's config
    QJsonObject get;
    get.insert(QStringLiteral("functionId"), sid(video->id()));
    QJsonObject cfg = resultOf(call(QStringLiteral("functions.get"), get)).value(QStringLiteral("typeDetail")).toObject()
            .value(QStringLiteral("config")).toObject();
    QCOMPARE(cfg.value(QStringLiteral("spoutSenderName")).toString(), video->defaultSpoutSenderName());
    QCOMPARE(cfg.value(QStringLiteral("muted")).toBool(), true);
}

/*****************************************************************************
 * RGB Matrix -> Sequence, palette fanning
 *****************************************************************************/

void ApiFunctionsMiscDomain_Test::rgbMatrixSaveToSequence()
{
    // 3 single-channel dimmers in a 3x1 group
    FixtureGroup *grp = new FixtureGroup(m_doc);
    grp->setName(QStringLiteral("Row"));
    grp->setSize(QSize(3, 1));
    QVERIFY(m_doc->addFixtureGroup(grp));
    QList<quint32> fixtures;
    for (int i = 0; i < 3; i++)
    {
        Fixture *fxi = new Fixture(m_doc);
        fxi->setName(QStringLiteral("D%1").arg(i));
        fxi->setUniverse(0);
        fxi->setAddress(quint32(i));
        fxi->setChannels(1);
        QVERIFY(m_doc->addFixture(fxi));
        QVERIFY(grp->assignFixture(fxi->id(), QLCPoint(i, 0)));
        fixtures << fxi->id();
    }

    RGBMatrix *matrix = new RGBMatrix(m_doc);
    matrix->setName(QStringLiteral("Chase"));
    matrix->setFixtureGroup(grp->id());
    matrix->setControlMode(RGBMatrix::ControlModeDimmer);
    matrix->setColor(0, QColor(Qt::white));
    matrix->setColor(1, QColor());
    matrix->setDuration(500);
    QVERIFY(m_doc->addFunction(matrix));
    int matrixSteps = matrix->algorithm()->rgbMapStepCount(grp->size());
    QVERIFY(matrixSteps > 0);
    QSignalSpy spy(m_client, &QWebSocket::textMessageReceived);

    QJsonObject p;
    p.insert(QStringLiteral("functionId"), sid(matrix->id()));
    QJsonObject reply = callRev(QStringLiteral("functions.rgbmatrix.saveToSequence"), p);
    QVERIFY2(isOk(reply), qPrintable(QJsonDocument(reply).toJson()));
    QJsonObject result = resultOf(reply);
    Sequence *sequence = qobject_cast<Sequence *>(m_doc->function(result.value(QStringLiteral("sequenceId")).toString().toUInt()));
    Scene *scene = qobject_cast<Scene *>(m_doc->function(result.value(QStringLiteral("sceneId")).toString().toUInt()));
    QVERIFY(sequence != nullptr);
    QVERIFY(scene != nullptr);
    QCOMPARE(sequence->name(), QStringLiteral("Chase Sequence"));
    QCOMPARE(scene->isVisible(), false);
    QCOMPARE(sequence->boundSceneID(), scene->id());
    QCOMPARE(scene->values().count(), 3);
    QCOMPARE(sequence->stepsCount(), matrixSteps);
    QCOMPARE(sequence->durationMode(), Chaser::PerStep);
    for (int i = 0; i < sequence->stepsCount(); i++)
    {
        QCOMPARE(sequence->stepAt(i)->fid, scene->id());
        QCOMPARE(sequence->stepAt(i)->values.count(), 3);
        QCOMPARE(sequence->stepAt(i)->duration, quint32(500));
    }
    QVERIFY(waitForEvent(spy, QStringLiteral("functions.created")).isEmpty() == false);

    // Ping Pong bounces: 2n-2 steps, step n mirrors step n-2
    int before = sequence->stepsCount();
    matrix->setRunOrder(Function::PingPong);
    reply = callRev(QStringLiteral("functions.rgbmatrix.saveToSequence"), p);
    QVERIFY(isOk(reply));
    Sequence *pp = qobject_cast<Sequence *>(m_doc->function(resultOf(reply).value(QStringLiteral("sequenceId")).toString().toUInt()));
    QVERIFY(pp != nullptr);
    if (before > 1)
    {
        QCOMPARE(pp->stepsCount(), before * 2 - 2);
        QCOMPARE(pp->stepAt(before)->values, pp->stepAt(before - 2)->values);
    }

    // no group -> INVALID_STATE
    RGBMatrix *lonely = new RGBMatrix(m_doc);
    QVERIFY(m_doc->addFunction(lonely));
    p.insert(QStringLiteral("functionId"), sid(lonely->id()));
    QCOMPARE(errorCode(callRev(QStringLiteral("functions.rgbmatrix.saveToSequence"), p)), QStringLiteral("INVALID_STATE"));
}

void ApiFunctionsMiscDomain_Test::paletteFanningRoundTrip()
{
    QJsonObject fan;
    fan.insert(QStringLiteral("type"), QStringLiteral("Linear"));
    fan.insert(QStringLiteral("layout"), QStringLiteral("XCentered"));
    fan.insert(QStringLiteral("amount"), 75);
    fan.insert(QStringLiteral("value"), QStringLiteral("#0000ff"));
    QJsonObject p;
    p.insert(QStringLiteral("type"), QStringLiteral("Color"));
    p.insert(QStringLiteral("name"), QStringLiteral("Red fan"));
    p.insert(QStringLiteral("values"), QJsonArray{ QStringLiteral("#ff0000") });
    p.insert(QStringLiteral("fanning"), fan);
    QJsonObject reply = callRev(QStringLiteral("palette.create"), p);
    QVERIFY2(isOk(reply), qPrintable(QJsonDocument(reply).toJson()));
    int pid = resultOf(reply).value(QStringLiteral("paletteId")).toInt();
    QLCPalette *palette = m_doc->palette(quint32(pid));
    QVERIFY(palette != nullptr);
    QCOMPARE(palette->fanningType(), QLCPalette::Linear);
    QCOMPARE(palette->fanningLayout(), QLCPalette::XCentered);
    QCOMPARE(palette->fanningAmount(), 75);

    QJsonObject get;
    get.insert(QStringLiteral("paletteId"), pid);
    QJsonObject gotFan = resultOf(call(QStringLiteral("palette.get"), get)).value(QStringLiteral("fanning")).toObject();
    QCOMPARE(gotFan.value(QStringLiteral("type")).toString(), QStringLiteral("Linear"));
    QCOMPARE(gotFan.value(QStringLiteral("layout")).toString(), QStringLiteral("XCentered"));
    QCOMPARE(gotFan.value(QStringLiteral("value")).toString(), QStringLiteral("#0000ff"));

    // update only the layout
    QSignalSpy spy(m_client, &QWebSocket::textMessageReceived);
    QJsonObject up;
    up.insert(QStringLiteral("paletteId"), pid);
    QJsonObject fan2;
    fan2.insert(QStringLiteral("layout"), QStringLiteral("YDescending"));
    up.insert(QStringLiteral("fanning"), fan2);
    QVERIFY(isOk(callRev(QStringLiteral("palette.update"), up)));
    QCOMPARE(palette->fanningLayout(), QLCPalette::YDescending);
    QCOMPARE(palette->fanningType(), QLCPalette::Linear);
    QJsonObject ev = waitForEvent(spy, QStringLiteral("palette.updated"));
    QCOMPARE(ev.value(QStringLiteral("palette")).toObject().value(QStringLiteral("fanning")).toObject()
             .value(QStringLiteral("layout")).toString(), QStringLiteral("YDescending"));

    fan2.insert(QStringLiteral("type"), QStringLiteral("Wobbly"));
    up.insert(QStringLiteral("fanning"), fan2);
    QCOMPARE(errorCode(callRev(QStringLiteral("palette.update"), up)), QStringLiteral("INVALID_PARAMS"));
}

QTEST_MAIN(ApiFunctionsMiscDomain_Test)
