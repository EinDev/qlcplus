/*
  Q Light Controller Plus - Control API unit test
  apiefxcollectiondomain_test.cpp

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

#include "apiefxcollectiondomain_test.h"
#include "apiserver.h"
#include "collection.h"
#include "mastertimer.h"
#include "efx.h"
#include "efxfixture.h"
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

static QStringList toStringList(const QJsonArray &arr)
{
    QStringList out;
    for (const QJsonValue &v : arr)
        out << v.toString();
    return out;
}

void ApiEfxCollectionDomain_Test::init()
{
    m_doc = new Doc(nullptr);

    m_sceneA = new Scene(m_doc);
    m_sceneA->setName(QStringLiteral("Scene A"));
    QVERIFY(m_doc->addFunction(m_sceneA));
    m_sceneB = new Scene(m_doc);
    m_sceneB->setName(QStringLiteral("Scene B"));
    QVERIFY(m_doc->addFunction(m_sceneB));

    m_collection = new Collection(m_doc);
    m_collection->setName(QStringLiteral("Test Collection"));
    QVERIFY(m_doc->addFunction(m_collection));

    m_efx = new EFX(m_doc);
    m_efx->setName(QStringLiteral("Test EFX"));
    QVERIFY(m_doc->addFunction(m_efx));

    m_apiServer = new ApiServer(nullptr, m_doc);
    QVERIFY(m_apiServer->listen(0, QHostAddress::LocalHost));

    m_client = new QWebSocket();
    m_client->open(QUrl(QStringLiteral("ws://127.0.0.1:%1/qlcplusapi").arg(m_apiServer->serverPort())));
    QVERIFY(QTest::qWaitFor([this]() { return m_client->state() == QAbstractSocket::ConnectedState; }, 2000));
}

void ApiEfxCollectionDomain_Test::cleanup()
{
    delete m_client;
    m_client = nullptr;
    delete m_apiServer;
    m_apiServer = nullptr;
    delete m_doc; // owns every function and fixture added above
    m_doc = nullptr;
    m_sceneA = nullptr;
    m_sceneB = nullptr;
    m_collection = nullptr;
    m_efx = nullptr;
}

QJsonObject ApiEfxCollectionDomain_Test::sendAndWaitForReply(const QString &method, const QJsonObject &params)
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

QString ApiEfxCollectionDomain_Test::helloAndGetClientId()
{
    QJsonObject reply = sendAndWaitForReply(QStringLiteral("hello"), QJsonObject());
    return reply.value(QStringLiteral("result")).toObject().value(QStringLiteral("clientId")).toString();
}

QJsonObject ApiEfxCollectionDomain_Test::waitForEvent(QSignalSpy &spy, const QString &topic,
                                                     const std::function<bool(const QJsonObject &)> &accept, int timeoutMs)
{
    QJsonObject found;
    (void)QTest::qWaitFor([&]()
    {
        for (const QList<QVariant> &frame : spy)
        {
            QJsonObject obj = QJsonDocument::fromJson(frame.at(0).toString().toUtf8()).object();
            if (obj.value(QStringLiteral("type")).toString() != QStringLiteral("event") ||
                obj.value(QStringLiteral("topic")).toString() != topic)
                continue;
            if (accept(obj.value(QStringLiteral("data")).toObject()))
            {
                found = obj;
                return true;
            }
        }
        return false;
    }, timeoutMs);
    return found;
}

QJsonObject ApiEfxCollectionDomain_Test::withRevision(QJsonObject params) const
{
    params.insert(QStringLiteral("baseRevision"), int(m_doc->docRevision()));
    return params;
}

QString ApiEfxCollectionDomain_Test::errorCode(const QJsonObject &reply) const
{
    return reply.value(QStringLiteral("error")).toObject().value(QStringLiteral("code")).toString();
}

Fixture *ApiEfxCollectionDomain_Test::addDimmer(const QString &name, quint32 channels)
{
    Fixture *fixture = new Fixture(m_doc);
    fixture->setName(name);
    fixture->setUniverse(0);
    fixture->setAddress(m_doc->fixtures().count() * 16);
    fixture->setChannels(channels); // generic dimmer: one head per channel
    if (m_doc->addFixture(fixture) == false)
    {
        delete fixture;
        return nullptr;
    }
    return fixture;
}

/*****************************************************************************
 * Collection
 *****************************************************************************/

void ApiEfxCollectionDomain_Test::collectionTypeDetailListsMembers()
{
    helloAndGetClientId();
    m_collection->addFunction(m_sceneB->id());
    m_collection->addFunction(m_sceneA->id());

    QJsonObject params;
    params.insert(QStringLiteral("functionId"), QString::number(m_collection->id()));
    QJsonObject reply = sendAndWaitForReply(QStringLiteral("functions.get"), params);
    QCOMPARE(reply.value(QStringLiteral("ok")).toBool(), true);

    QJsonObject detail = reply.value(QStringLiteral("result")).toObject().value(QStringLiteral("typeDetail")).toObject();
    QCOMPARE(detail.value(QStringLiteral("functionId")).toString(), QString::number(m_collection->id()));
    QCOMPARE(toStringList(detail.value(QStringLiteral("functions")).toArray()),
             QStringList({ QString::number(m_sceneB->id()), QString::number(m_sceneA->id()) }));
    QCOMPARE(detail.value(QStringLiteral("docRevision")).toInt(), int(m_doc->docRevision()));
}

void ApiEfxCollectionDomain_Test::collectionAddFunctionAppendsInsertsAndBroadcasts()
{
    QString clientId = helloAndGetClientId();
    QSignalSpy spy(m_client, &QWebSocket::textMessageReceived);
    QString collectionId = QString::number(m_collection->id());

    QJsonObject params;
    params.insert(QStringLiteral("functionId"), collectionId);
    params.insert(QStringLiteral("memberFunctionId"), QString::number(m_sceneA->id()));
    quint32 before = m_doc->docRevision();
    QJsonObject reply = sendAndWaitForReply(QStringLiteral("functions.collection.addFunction"), withRevision(params));
    QCOMPARE(reply.value(QStringLiteral("ok")).toBool(), true);
    QVERIFY(m_doc->docRevision() > before);
    QCOMPARE(reply.value(QStringLiteral("result")).toObject().value(QStringLiteral("docRevision")).toInt(), int(m_doc->docRevision()));
    QCOMPARE(m_collection->functions(), QList<quint32>({ m_sceneA->id() }));

    QJsonObject event = waitForEvent(spy, QStringLiteral("functions.collection.membersChanged"), [&](const QJsonObject &d)
    {
        return d.value(QStringLiteral("functionId")).toString() == collectionId;
    });
    QVERIFY2(event.isEmpty() == false, "missing functions.collection.membersChanged");
    QCOMPARE(event.value(QStringLiteral("originClientId")).toString(), clientId);
    QJsonObject data = event.value(QStringLiteral("data")).toObject();
    QCOMPARE(toStringList(data.value(QStringLiteral("functions")).toArray()), QStringList({ QString::number(m_sceneA->id()) }));
    QCOMPARE(data.value(QStringLiteral("docRevision")).toInt(), int(m_doc->docRevision()));

    // insert at index 0 -> B before A
    params.insert(QStringLiteral("memberFunctionId"), QString::number(m_sceneB->id()));
    params.insert(QStringLiteral("index"), 0);
    reply = sendAndWaitForReply(QStringLiteral("functions.collection.addFunction"), withRevision(params));
    QCOMPARE(reply.value(QStringLiteral("ok")).toBool(), true);
    QCOMPARE(m_collection->functions(), QList<quint32>({ m_sceneB->id(), m_sceneA->id() }));
}

void ApiEfxCollectionDomain_Test::collectionAddRejectsSelfDuplicateLoopAndStaleRevision()
{
    helloAndGetClientId();
    QString collectionId = QString::number(m_collection->id());
    m_collection->addFunction(m_sceneA->id());

    // self
    QJsonObject params;
    params.insert(QStringLiteral("functionId"), collectionId);
    params.insert(QStringLiteral("memberFunctionId"), collectionId);
    QJsonObject reply = sendAndWaitForReply(QStringLiteral("functions.collection.addFunction"), withRevision(params));
    QCOMPARE(reply.value(QStringLiteral("ok")).toBool(), false);
    QCOMPARE(errorCode(reply), QStringLiteral("INVALID_PARAMS"));

    // duplicate
    params.insert(QStringLiteral("memberFunctionId"), QString::number(m_sceneA->id()));
    reply = sendAndWaitForReply(QStringLiteral("functions.collection.addFunction"), withRevision(params));
    QCOMPARE(reply.value(QStringLiteral("ok")).toBool(), false);
    QCOMPARE(errorCode(reply), QStringLiteral("INVALID_PARAMS"));

    // loop: outer contains m_collection; adding outer to m_collection must fail
    Collection *outer = new Collection(m_doc);
    outer->setName(QStringLiteral("Outer"));
    QVERIFY(m_doc->addFunction(outer));
    QVERIFY(outer->addFunction(m_collection->id()));
    params.insert(QStringLiteral("memberFunctionId"), QString::number(outer->id()));
    reply = sendAndWaitForReply(QStringLiteral("functions.collection.addFunction"), withRevision(params));
    QCOMPARE(reply.value(QStringLiteral("ok")).toBool(), false);
    QCOMPARE(errorCode(reply), QStringLiteral("INVALID_PARAMS"));
    QVERIFY(m_collection->functions().contains(outer->id()) == false);

    // unknown member
    params.insert(QStringLiteral("memberFunctionId"), QStringLiteral("999999"));
    reply = sendAndWaitForReply(QStringLiteral("functions.collection.addFunction"), withRevision(params));
    QCOMPARE(reply.value(QStringLiteral("ok")).toBool(), false);
    QCOMPARE(errorCode(reply), QStringLiteral("NOT_FOUND"));

    // unknown collection
    QJsonObject bad;
    bad.insert(QStringLiteral("functionId"), QStringLiteral("999999"));
    bad.insert(QStringLiteral("memberFunctionId"), QString::number(m_sceneB->id()));
    reply = sendAndWaitForReply(QStringLiteral("functions.collection.addFunction"), withRevision(bad));
    QCOMPARE(errorCode(reply), QStringLiteral("NOT_FOUND"));

    // stale revision
    params.insert(QStringLiteral("memberFunctionId"), QString::number(m_sceneB->id()));
    params.insert(QStringLiteral("baseRevision"), int(m_doc->docRevision()) + 5);
    reply = sendAndWaitForReply(QStringLiteral("functions.collection.addFunction"), params);
    QCOMPARE(reply.value(QStringLiteral("ok")).toBool(), false);
    QCOMPARE(errorCode(reply), QStringLiteral("CONFLICT"));
    QCOMPARE(reply.value(QStringLiteral("error")).toObject().value(QStringLiteral("details")).toObject()
             .value(QStringLiteral("docRevision")).toInt(), int(m_doc->docRevision()));
    QCOMPARE(m_collection->functions(), QList<quint32>({ m_sceneA->id() }));
}

void ApiEfxCollectionDomain_Test::collectionAddWhileRunningThenAdjustIntensity()
{
    // Crash audit: Collection::preRun() records one intensity override id per
    // member; a member added while the Collection runs had none, and the next
    // intensity change (a VC slider on the Collection, or a parent starting
    // it again) aborted on m_intensityOverrideIds.at(i)'s bounds assert.
    helloAndGetClientId();
    // raw non-fixture values keep the scenes (and so the Collection) running
    m_sceneA->setValue(Fixture::invalidId(), 0, 255);
    m_sceneB->setValue(Fixture::invalidId(), 1, 255);
    QJsonObject params;
    params.insert(QStringLiteral("functionId"), QString::number(m_collection->id()));
    params.insert(QStringLiteral("memberFunctionId"), QString::number(m_sceneA->id()));
    QCOMPARE(sendAndWaitForReply(QStringLiteral("functions.collection.addFunction"), withRevision(params)).value(QStringLiteral("ok")).toBool(), true);

    m_doc->masterTimer()->start();
    m_collection->start(m_doc->masterTimer(), FunctionParent::master());
    QVERIFY(QTest::qWaitFor([this]() { return m_collection->isRunning(); }, 2000));

    params.insert(QStringLiteral("memberFunctionId"), QString::number(m_sceneB->id()));
    QCOMPARE(sendAndWaitForReply(QStringLiteral("functions.collection.addFunction"), withRevision(params)).value(QStringLiteral("ok")).toBool(), true);
    QCOMPARE(m_collection->functions().count(), 2);

    m_collection->adjustAttribute(0.5, Function::Intensity);

    m_collection->stopAndWait(FunctionParent::master());
    m_doc->masterTimer()->stop();
}

void ApiEfxCollectionDomain_Test::collectionRemoveFunction()
{
    helloAndGetClientId();
    QSignalSpy spy(m_client, &QWebSocket::textMessageReceived);
    QString collectionId = QString::number(m_collection->id());
    m_collection->addFunction(m_sceneA->id());
    m_collection->addFunction(m_sceneB->id());

    QJsonObject params;
    params.insert(QStringLiteral("functionId"), collectionId);
    params.insert(QStringLiteral("memberFunctionId"), QString::number(m_sceneA->id()));
    quint32 before = m_doc->docRevision();
    QJsonObject reply = sendAndWaitForReply(QStringLiteral("functions.collection.removeFunction"), withRevision(params));
    QCOMPARE(reply.value(QStringLiteral("ok")).toBool(), true);
    QVERIFY(m_doc->docRevision() > before);
    QCOMPARE(m_collection->functions(), QList<quint32>({ m_sceneB->id() }));

    QJsonObject event = waitForEvent(spy, QStringLiteral("functions.collection.membersChanged"), [&](const QJsonObject &d)
    {
        return d.value(QStringLiteral("functionId")).toString() == collectionId;
    });
    QVERIFY(event.isEmpty() == false);
    QCOMPARE(toStringList(event.value(QStringLiteral("data")).toObject().value(QStringLiteral("functions")).toArray()),
             QStringList({ QString::number(m_sceneB->id()) }));

    // not a member any more
    reply = sendAndWaitForReply(QStringLiteral("functions.collection.removeFunction"), withRevision(params));
    QCOMPARE(reply.value(QStringLiteral("ok")).toBool(), false);
    QCOMPARE(errorCode(reply), QStringLiteral("NOT_FOUND"));
}

void ApiEfxCollectionDomain_Test::collectionSetMembersReplacesInOrder()
{
    helloAndGetClientId();
    QString collectionId = QString::number(m_collection->id());
    m_collection->addFunction(m_sceneA->id());

    QJsonObject params;
    params.insert(QStringLiteral("functionId"), collectionId);
    params.insert(QStringLiteral("functions"), QJsonArray{ QString::number(m_sceneB->id()), QString::number(m_sceneA->id()) });
    quint32 before = m_doc->docRevision();
    QJsonObject reply = sendAndWaitForReply(QStringLiteral("functions.collection.setMembers"), withRevision(params));
    QCOMPARE(reply.value(QStringLiteral("ok")).toBool(), true);
    QVERIFY(m_doc->docRevision() > before);
    QCOMPARE(m_collection->functions(), QList<quint32>({ m_sceneB->id(), m_sceneA->id() }));

    // a loop anywhere in the list rejects the whole request, leaving the list as it was
    Collection *outer = new Collection(m_doc);
    QVERIFY(m_doc->addFunction(outer));
    QVERIFY(outer->addFunction(m_collection->id()));
    params.insert(QStringLiteral("functions"), QJsonArray{ QString::number(m_sceneA->id()), QString::number(outer->id()) });
    reply = sendAndWaitForReply(QStringLiteral("functions.collection.setMembers"), withRevision(params));
    QCOMPARE(reply.value(QStringLiteral("ok")).toBool(), false);
    QCOMPARE(errorCode(reply), QStringLiteral("INVALID_PARAMS"));
    QCOMPARE(m_collection->functions(), QList<quint32>({ m_sceneB->id(), m_sceneA->id() }));

    // duplicates rejected too
    params.insert(QStringLiteral("functions"), QJsonArray{ QString::number(m_sceneA->id()), QString::number(m_sceneA->id()) });
    reply = sendAndWaitForReply(QStringLiteral("functions.collection.setMembers"), withRevision(params));
    QCOMPARE(errorCode(reply), QStringLiteral("INVALID_PARAMS"));

    // empty list clears
    params.insert(QStringLiteral("functions"), QJsonArray());
    reply = sendAndWaitForReply(QStringLiteral("functions.collection.setMembers"), withRevision(params));
    QCOMPARE(reply.value(QStringLiteral("ok")).toBool(), true);
    QVERIFY(m_collection->functions().isEmpty());
}

/*****************************************************************************
 * EFX
 *****************************************************************************/

void ApiEfxCollectionDomain_Test::efxTypeDetailCarriesParametersAlgorithmsAndFixtures()
{
    helloAndGetClientId();
    Fixture *dimmer = addDimmer(QStringLiteral("Dimmer"), 2);
    QVERIFY(dimmer != nullptr);
    m_efx->addFixture(dimmer->id(), 1);

    QJsonObject params;
    params.insert(QStringLiteral("functionId"), QString::number(m_efx->id()));
    QJsonObject reply = sendAndWaitForReply(QStringLiteral("functions.get"), params);
    QCOMPARE(reply.value(QStringLiteral("ok")).toBool(), true);

    QJsonObject detail = reply.value(QStringLiteral("result")).toObject().value(QStringLiteral("typeDetail")).toObject();
    QCOMPARE(detail.value(QStringLiteral("functionId")).toString(), QString::number(m_efx->id()));
    QCOMPARE(detail.value(QStringLiteral("algorithm")).toString(), QStringLiteral("Circle"));
    QCOMPARE(detail.value(QStringLiteral("propagationMode")).toString(), QStringLiteral("Parallel"));
    QCOMPARE(detail.value(QStringLiteral("width")).toInt(), 127);
    QCOMPARE(detail.value(QStringLiteral("height")).toInt(), 127);
    QCOMPARE(detail.value(QStringLiteral("rotation")).toInt(), 0);
    QCOMPARE(detail.value(QStringLiteral("startOffset")).toInt(), 0);
    QCOMPARE(detail.value(QStringLiteral("isRelative")).toBool(), false);
    QCOMPARE(detail.value(QStringLiteral("xOffset")).toInt(), 127);
    QCOMPARE(detail.value(QStringLiteral("yOffset")).toInt(), 127);
    QCOMPARE(detail.value(QStringLiteral("xFrequency")).toInt(), 2);
    QCOMPARE(detail.value(QStringLiteral("yFrequency")).toInt(), 3);
    QCOMPARE(detail.value(QStringLiteral("xPhase")).toInt(), 90);
    QCOMPARE(detail.value(QStringLiteral("yPhase")).toInt(), 0);
    QCOMPARE(detail.value(QStringLiteral("dimmerControlEnabled")).toBool(), false);
    QCOMPARE(detail.value(QStringLiteral("docRevision")).toInt(), int(m_doc->docRevision()));

    QStringList algorithms = toStringList(detail.value(QStringLiteral("algorithms")).toArray());
    QCOMPARE(algorithms, EFX::algorithmList());
    QCOMPARE(algorithms.first(), QStringLiteral("Circle"));
    QCOMPARE(algorithms.last(), QStringLiteral("Lissajous"));

    QJsonArray fixtures = detail.value(QStringLiteral("fixtures")).toArray();
    QCOMPARE(fixtures.count(), 1);
    QJsonObject ef = fixtures.at(0).toObject();
    QCOMPARE(ef.value(QStringLiteral("fixture")).toString(), QString::number(dimmer->id()));
    QCOMPARE(ef.value(QStringLiteral("head")).toInt(), 1);
    QCOMPARE(ef.value(QStringLiteral("direction")).toString(), QStringLiteral("Forward"));
    QCOMPARE(ef.value(QStringLiteral("startOffset")).toInt(), 0);
    // a generic dimmer head has an intensity channel and nothing else
    QCOMPARE(ef.value(QStringLiteral("mode")).toString(), QStringLiteral("Dimmer"));
    QCOMPARE(toStringList(ef.value(QStringLiteral("availableModes")).toArray()), QStringList({ QStringLiteral("Dimmer") }));
}

void ApiEfxCollectionDomain_Test::efxSetParametersAppliesClampsAndBroadcasts()
{
    QString clientId = helloAndGetClientId();
    QSignalSpy spy(m_client, &QWebSocket::textMessageReceived);
    QString efxId = QString::number(m_efx->id());

    QJsonObject params;
    params.insert(QStringLiteral("functionId"), efxId);
    params.insert(QStringLiteral("algorithm"), QStringLiteral("Lissajous"));
    params.insert(QStringLiteral("propagationMode"), QStringLiteral("Serial"));
    params.insert(QStringLiteral("width"), 50);
    params.insert(QStringLiteral("height"), 300);      // clamped to 127
    params.insert(QStringLiteral("rotation"), 45);
    params.insert(QStringLiteral("startOffset"), 10);
    params.insert(QStringLiteral("isRelative"), true);
    params.insert(QStringLiteral("xOffset"), 10);
    params.insert(QStringLiteral("yOffset"), 20);
    params.insert(QStringLiteral("xFrequency"), 3);
    params.insert(QStringLiteral("yFrequency"), 40);   // clamped to 32
    params.insert(QStringLiteral("xPhase"), 90);
    params.insert(QStringLiteral("yPhase"), 400);      // clamped to 359
    params.insert(QStringLiteral("dimmerControlEnabled"), true);

    quint32 before = m_doc->docRevision();
    QJsonObject reply = sendAndWaitForReply(QStringLiteral("functions.efx.setParameters"), withRevision(params));
    QCOMPARE(reply.value(QStringLiteral("ok")).toBool(), true);
    QVERIFY(m_doc->docRevision() > before);
    QCOMPARE(reply.value(QStringLiteral("result")).toObject().value(QStringLiteral("docRevision")).toInt(), int(m_doc->docRevision()));

    QCOMPARE(m_efx->algorithm(), EFX::Lissajous);
    QCOMPARE(m_efx->propagationMode(), EFX::Serial);
    QCOMPARE(m_efx->width(), 50);
    QCOMPARE(m_efx->height(), 127);
    QCOMPARE(m_efx->rotation(), 45);
    QCOMPARE(m_efx->startOffset(), 10);
    QCOMPARE(m_efx->isRelative(), true);
    QCOMPARE(m_efx->xOffset(), 10);
    QCOMPARE(m_efx->yOffset(), 20);
    QCOMPARE(m_efx->xFrequency(), 3);
    QCOMPARE(m_efx->yFrequency(), 32);
    QCOMPARE(m_efx->xPhase(), 90);
    QCOMPARE(m_efx->yPhase(), 359);
    QCOMPARE(m_efx->dimmerControlEnabled(), true);

    QJsonObject event = waitForEvent(spy, QStringLiteral("functions.efx.changed"), [&](const QJsonObject &d)
    {
        return d.value(QStringLiteral("functionId")).toString() == efxId;
    });
    QVERIFY2(event.isEmpty() == false, "missing functions.efx.changed");
    QCOMPARE(event.value(QStringLiteral("originClientId")).toString(), clientId);
    QJsonObject data = event.value(QStringLiteral("data")).toObject();
    QCOMPARE(data.value(QStringLiteral("algorithm")).toString(), QStringLiteral("Lissajous"));
    QCOMPARE(data.value(QStringLiteral("propagationMode")).toString(), QStringLiteral("Serial"));
    QCOMPARE(data.value(QStringLiteral("height")).toInt(), 127);
    QCOMPARE(data.value(QStringLiteral("yFrequency")).toInt(), 32);
    QCOMPARE(data.value(QStringLiteral("dimmerControlEnabled")).toBool(), true);
    QCOMPARE(data.value(QStringLiteral("docRevision")).toInt(), int(m_doc->docRevision()));

    // A partial update leaves everything else alone and still bumps the
    // revision (setDimmerControlEnabled emits nothing on its own).
    QJsonObject partial;
    partial.insert(QStringLiteral("functionId"), efxId);
    partial.insert(QStringLiteral("dimmerControlEnabled"), false);
    before = m_doc->docRevision();
    reply = sendAndWaitForReply(QStringLiteral("functions.efx.setParameters"), withRevision(partial));
    QCOMPARE(reply.value(QStringLiteral("ok")).toBool(), true);
    QVERIFY(m_doc->docRevision() > before);
    QCOMPARE(m_efx->dimmerControlEnabled(), false);
    QCOMPARE(m_efx->width(), 50);
    QCOMPARE(m_efx->algorithm(), EFX::Lissajous);
}

void ApiEfxCollectionDomain_Test::efxSetParametersRejectsBadEnumWithoutApplying()
{
    helloAndGetClientId();
    QJsonObject params;
    params.insert(QStringLiteral("functionId"), QString::number(m_efx->id()));
    params.insert(QStringLiteral("algorithm"), QStringLiteral("Spiral"));
    params.insert(QStringLiteral("width"), 10);
    quint32 before = m_doc->docRevision();
    QJsonObject reply = sendAndWaitForReply(QStringLiteral("functions.efx.setParameters"), withRevision(params));
    QCOMPARE(reply.value(QStringLiteral("ok")).toBool(), false);
    QCOMPARE(errorCode(reply), QStringLiteral("INVALID_PARAMS"));
    QCOMPARE(m_efx->width(), 127);
    QCOMPARE(m_doc->docRevision(), before);

    params.remove(QStringLiteral("algorithm"));
    params.insert(QStringLiteral("propagationMode"), QStringLiteral("Sideways"));
    reply = sendAndWaitForReply(QStringLiteral("functions.efx.setParameters"), withRevision(params));
    QCOMPARE(errorCode(reply), QStringLiteral("INVALID_PARAMS"));
    QCOMPARE(m_efx->width(), 127);

    // stale revision
    params.remove(QStringLiteral("propagationMode"));
    params.insert(QStringLiteral("baseRevision"), int(m_doc->docRevision()) + 1);
    reply = sendAndWaitForReply(QStringLiteral("functions.efx.setParameters"), params);
    QCOMPARE(errorCode(reply), QStringLiteral("CONFLICT"));
    QCOMPARE(m_efx->width(), 127);

    // not an EFX
    QJsonObject wrongType;
    wrongType.insert(QStringLiteral("functionId"), QString::number(m_sceneA->id()));
    wrongType.insert(QStringLiteral("width"), 10);
    reply = sendAndWaitForReply(QStringLiteral("functions.efx.setParameters"), withRevision(wrongType));
    QCOMPARE(errorCode(reply), QStringLiteral("NOT_FOUND"));
}

void ApiEfxCollectionDomain_Test::efxFixturesAddRemoveReorderAndParameters()
{
    QString clientId = helloAndGetClientId();
    QSignalSpy spy(m_client, &QWebSocket::textMessageReceived);
    QString efxId = QString::number(m_efx->id());
    Fixture *first = addDimmer(QStringLiteral("First"), 2);
    Fixture *second = addDimmer(QStringLiteral("Second"), 1);
    QVERIFY(first != nullptr && second != nullptr);
    QString firstId = QString::number(first->id());
    QString secondId = QString::number(second->id());

    // add head 1 of the first fixture
    QJsonObject add;
    add.insert(QStringLiteral("functionId"), efxId);
    add.insert(QStringLiteral("fixture"), firstId);
    add.insert(QStringLiteral("head"), 1);
    quint32 before = m_doc->docRevision();
    QJsonObject reply = sendAndWaitForReply(QStringLiteral("functions.efx.addFixture"), withRevision(add));
    QCOMPARE(reply.value(QStringLiteral("ok")).toBool(), true);
    QVERIFY(m_doc->docRevision() > before);
    QCOMPARE(m_efx->fixtures().count(), 1);
    QVERIFY(m_efx->fixture(first->id(), 1) != nullptr);

    QJsonObject event = waitForEvent(spy, QStringLiteral("functions.efx.fixturesChanged"), [&](const QJsonObject &d)
    {
        return d.value(QStringLiteral("functionId")).toString() == efxId;
    });
    QVERIFY2(event.isEmpty() == false, "missing functions.efx.fixturesChanged");
    QCOMPARE(event.value(QStringLiteral("originClientId")).toString(), clientId);
    QJsonArray listed = event.value(QStringLiteral("data")).toObject().value(QStringLiteral("fixtures")).toArray();
    QCOMPARE(listed.count(), 1);
    QCOMPARE(listed.at(0).toObject().value(QStringLiteral("fixture")).toString(), firstId);
    QCOMPARE(listed.at(0).toObject().value(QStringLiteral("head")).toInt(), 1);

    // the same head again is rejected
    reply = sendAndWaitForReply(QStringLiteral("functions.efx.addFixture"), withRevision(add));
    QCOMPARE(reply.value(QStringLiteral("ok")).toBool(), false);
    QCOMPARE(errorCode(reply), QStringLiteral("INVALID_PARAMS"));
    QCOMPARE(m_efx->fixtures().count(), 1);

    // out-of-range head, unknown fixture
    add.insert(QStringLiteral("head"), 7);
    reply = sendAndWaitForReply(QStringLiteral("functions.efx.addFixture"), withRevision(add));
    QCOMPARE(errorCode(reply), QStringLiteral("INVALID_PARAMS"));
    add.insert(QStringLiteral("head"), 0);
    add.insert(QStringLiteral("fixture"), QStringLiteral("999999"));
    reply = sendAndWaitForReply(QStringLiteral("functions.efx.addFixture"), withRevision(add));
    QCOMPARE(errorCode(reply), QStringLiteral("NOT_FOUND"));

    // second fixture (default head 0) appends
    add.insert(QStringLiteral("fixture"), secondId);
    add.remove(QStringLiteral("head"));
    reply = sendAndWaitForReply(QStringLiteral("functions.efx.addFixture"), withRevision(add));
    QCOMPARE(reply.value(QStringLiteral("ok")).toBool(), true);
    QCOMPARE(m_efx->fixtures().count(), 2);
    QCOMPARE(m_efx->fixtures().at(1)->head().fxi, second->id());

    // reorder: lower the first -> second becomes first
    QJsonObject reorder;
    reorder.insert(QStringLiteral("functionId"), efxId);
    reorder.insert(QStringLiteral("fixture"), firstId);
    reorder.insert(QStringLiteral("head"), 1);
    reorder.insert(QStringLiteral("move"), QStringLiteral("lower"));
    reply = sendAndWaitForReply(QStringLiteral("functions.efx.reorderFixture"), withRevision(reorder));
    QCOMPARE(reply.value(QStringLiteral("ok")).toBool(), true);
    QCOMPARE(m_efx->fixtures().at(0)->head().fxi, second->id());
    QCOMPARE(m_efx->fixtures().at(1)->head().fxi, first->id());
    // already last now
    reply = sendAndWaitForReply(QStringLiteral("functions.efx.reorderFixture"), withRevision(reorder));
    QCOMPARE(reply.value(QStringLiteral("ok")).toBool(), false);
    QCOMPARE(errorCode(reply), QStringLiteral("INVALID_PARAMS"));
    reorder.insert(QStringLiteral("move"), QStringLiteral("sideways"));
    reply = sendAndWaitForReply(QStringLiteral("functions.efx.reorderFixture"), withRevision(reorder));
    QCOMPARE(errorCode(reply), QStringLiteral("INVALID_PARAMS"));

    // per-fixture parameters
    QJsonObject fxParams;
    fxParams.insert(QStringLiteral("functionId"), efxId);
    fxParams.insert(QStringLiteral("fixture"), firstId);
    fxParams.insert(QStringLiteral("head"), 1);
    fxParams.insert(QStringLiteral("direction"), QStringLiteral("Backward"));
    fxParams.insert(QStringLiteral("startOffset"), 400); // clamped to 359
    fxParams.insert(QStringLiteral("mode"), QStringLiteral("PanTilt"));
    before = m_doc->docRevision();
    spy.clear();
    reply = sendAndWaitForReply(QStringLiteral("functions.efx.setFixtureParameters"), withRevision(fxParams));
    QCOMPARE(reply.value(QStringLiteral("ok")).toBool(), true);
    QVERIFY(m_doc->docRevision() > before);
    EFXFixture *ef = m_efx->fixture(first->id(), 1);
    QVERIFY(ef != nullptr);
    QCOMPARE(ef->direction(), Function::Backward);
    QCOMPARE(ef->startOffset(), 359);
    QCOMPARE(ef->mode(), EFXFixture::PanTilt);

    event = waitForEvent(spy, QStringLiteral("functions.efx.fixturesChanged"), [&](const QJsonObject &d)
    {
        return d.value(QStringLiteral("functionId")).toString() == efxId;
    });
    QVERIFY(event.isEmpty() == false);
    listed = event.value(QStringLiteral("data")).toObject().value(QStringLiteral("fixtures")).toArray();
    QCOMPARE(listed.count(), 2);
    QJsonObject firstJson = listed.at(1).toObject();
    QCOMPARE(firstJson.value(QStringLiteral("fixture")).toString(), firstId);
    QCOMPARE(firstJson.value(QStringLiteral("direction")).toString(), QStringLiteral("Backward"));
    QCOMPARE(firstJson.value(QStringLiteral("startOffset")).toInt(), 359);
    QCOMPARE(firstJson.value(QStringLiteral("mode")).toString(), QStringLiteral("PanTilt"));

    fxParams.insert(QStringLiteral("mode"), QStringLiteral("Sideways"));
    reply = sendAndWaitForReply(QStringLiteral("functions.efx.setFixtureParameters"), withRevision(fxParams));
    QCOMPARE(errorCode(reply), QStringLiteral("INVALID_PARAMS"));
    fxParams.insert(QStringLiteral("mode"), QStringLiteral("Dimmer"));
    fxParams.insert(QStringLiteral("direction"), QStringLiteral("Up"));
    reply = sendAndWaitForReply(QStringLiteral("functions.efx.setFixtureParameters"), withRevision(fxParams));
    QCOMPARE(errorCode(reply), QStringLiteral("INVALID_PARAMS"));
    QCOMPARE(ef->mode(), EFXFixture::PanTilt);

    // remove
    QJsonObject remove;
    remove.insert(QStringLiteral("functionId"), efxId);
    remove.insert(QStringLiteral("fixture"), firstId);
    remove.insert(QStringLiteral("head"), 1);
    before = m_doc->docRevision();
    reply = sendAndWaitForReply(QStringLiteral("functions.efx.removeFixture"), withRevision(remove));
    QCOMPARE(reply.value(QStringLiteral("ok")).toBool(), true);
    QVERIFY(m_doc->docRevision() > before);
    QCOMPARE(m_efx->fixtures().count(), 1);
    QVERIFY(m_efx->fixture(first->id(), 1) == nullptr);
    reply = sendAndWaitForReply(QStringLiteral("functions.efx.removeFixture"), withRevision(remove));
    QCOMPARE(errorCode(reply), QStringLiteral("NOT_FOUND"));
}

void ApiEfxCollectionDomain_Test::efxAddFixtureAllHeads()
{
    helloAndGetClientId();
    Fixture *bar = addDimmer(QStringLiteral("Bar"), 4);
    QVERIFY(bar != nullptr);
    QCOMPARE(bar->heads(), 4);
    m_efx->addFixture(bar->id(), 2); // one head already in

    QJsonObject add;
    add.insert(QStringLiteral("functionId"), QString::number(m_efx->id()));
    add.insert(QStringLiteral("fixture"), QString::number(bar->id()));
    add.insert(QStringLiteral("allHeads"), true);
    QJsonObject reply = sendAndWaitForReply(QStringLiteral("functions.efx.addFixture"), withRevision(add));
    QCOMPARE(reply.value(QStringLiteral("ok")).toBool(), true);
    QCOMPARE(m_efx->fixtures().count(), 4);
    for (int h = 0; h < 4; h++)
        QVERIFY(m_efx->fixture(bar->id(), h) != nullptr);

    // nothing left to add
    reply = sendAndWaitForReply(QStringLiteral("functions.efx.addFixture"), withRevision(add));
    QCOMPARE(reply.value(QStringLiteral("ok")).toBool(), false);
    QCOMPARE(errorCode(reply), QStringLiteral("INVALID_PARAMS"));
    QCOMPARE(m_efx->fixtures().count(), 4);
}

void ApiEfxCollectionDomain_Test::efxSetFixturesOffset()
{
    helloAndGetClientId();
    Fixture *a = addDimmer(QStringLiteral("A"), 1);
    Fixture *b = addDimmer(QStringLiteral("B"), 1);
    Fixture *c = addDimmer(QStringLiteral("C"), 1);
    QVERIFY(a != nullptr && b != nullptr && c != nullptr);
    m_efx->addFixture(a->id(), 0);
    m_efx->addFixture(b->id(), 0);
    m_efx->addFixture(c->id(), 0);

    QJsonObject params;
    params.insert(QStringLiteral("functionId"), QString::number(m_efx->id()));
    params.insert(QStringLiteral("offset"), 150);
    params.insert(QStringLiteral("mode"), QStringLiteral("Increasing"));
    quint32 before = m_doc->docRevision();
    QJsonObject reply = sendAndWaitForReply(QStringLiteral("functions.efx.setFixturesOffset"), withRevision(params));
    QCOMPARE(reply.value(QStringLiteral("ok")).toBool(), true);
    QVERIFY(m_doc->docRevision() > before);
    QCOMPARE(m_efx->fixtures().at(0)->startOffset(), 0);
    QCOMPARE(m_efx->fixtures().at(1)->startOffset(), 150);
    QCOMPARE(m_efx->fixtures().at(2)->startOffset(), 300);

    params.insert(QStringLiteral("mode"), QStringLiteral("Absolute"));
    params.insert(QStringLiteral("offset"), 45);
    reply = sendAndWaitForReply(QStringLiteral("functions.efx.setFixturesOffset"), withRevision(params));
    QCOMPARE(reply.value(QStringLiteral("ok")).toBool(), true);
    for (EFXFixture *ef : m_efx->fixtures())
        QCOMPARE(ef->startOffset(), 45);

    // Random: the Increasing set (0, 90, 180) in some order
    params.insert(QStringLiteral("mode"), QStringLiteral("Random"));
    params.insert(QStringLiteral("offset"), 90);
    reply = sendAndWaitForReply(QStringLiteral("functions.efx.setFixturesOffset"), withRevision(params));
    QCOMPARE(reply.value(QStringLiteral("ok")).toBool(), true);
    QList<int> offsets;
    for (EFXFixture *ef : m_efx->fixtures())
        offsets << ef->startOffset();
    std::sort(offsets.begin(), offsets.end());
    QCOMPARE(offsets, QList<int>({ 0, 90, 180 }));

    params.insert(QStringLiteral("mode"), QStringLiteral("Sideways"));
    reply = sendAndWaitForReply(QStringLiteral("functions.efx.setFixturesOffset"), withRevision(params));
    QCOMPARE(errorCode(reply), QStringLiteral("INVALID_PARAMS"));
    params.remove(QStringLiteral("mode"));
    params.remove(QStringLiteral("offset"));
    reply = sendAndWaitForReply(QStringLiteral("functions.efx.setFixturesOffset"), withRevision(params));
    QCOMPARE(errorCode(reply), QStringLiteral("INVALID_PARAMS"));
}

void ApiEfxCollectionDomain_Test::efxGetPreviewMatchesEditorMath()
{
    helloAndGetClientId();
    Fixture *a = addDimmer(QStringLiteral("A"), 1);
    Fixture *b = addDimmer(QStringLiteral("B"), 1);
    Fixture *c = addDimmer(QStringLiteral("C"), 1);
    QVERIFY(a != nullptr && b != nullptr && c != nullptr);
    m_efx->addFixture(a->id(), 0);
    m_efx->addFixture(b->id(), 0);
    m_efx->addFixture(c->id(), 0);
    m_efx->fixture(b->id(), 0)->setDirection(Function::Backward);
    m_efx->fixture(c->id(), 0)->setStartOffset(90);

    QJsonObject params;
    params.insert(QStringLiteral("functionId"), QString::number(m_efx->id()));
    QJsonObject reply = sendAndWaitForReply(QStringLiteral("functions.efx.getPreview"), params);
    QCOMPARE(reply.value(QStringLiteral("ok")).toBool(), true);
    QJsonObject result = reply.value(QStringLiteral("result")).toObject();
    QCOMPARE(result.value(QStringLiteral("functionId")).toString(), QString::number(m_efx->id()));

    // EFX::preview: 512 points in the 0-255 pan/tilt space
    QJsonArray pattern = result.value(QStringLiteral("pattern")).toArray();
    QCOMPARE(pattern.count(), 512);
    QPolygonF expected;
    m_efx->preview(expected);
    for (int i = 0; i < pattern.count(); i++)
    {
        QJsonArray pt = pattern.at(i).toArray();
        QCOMPARE(pt.count(), 2);
        QVERIFY(pt.at(0).toDouble() >= 0.0 && pt.at(0).toDouble() <= 255.0);
        QVERIFY(pt.at(1).toDouble() >= 0.0 && pt.at(1).toDouble() <= 255.0);
        QVERIFY(qAbs(pt.at(0).toDouble() - expected.at(i).x()) < 0.001);
        QVERIFY(qAbs(pt.at(1).toDouble() - expected.at(i).y()) < 0.001);
    }

    QJsonArray fixtures = result.value(QStringLiteral("fixtures")).toArray();
    QCOMPARE(fixtures.count(), 3);
    // forward, offset 0 -> index 0, step +1
    QCOMPARE(fixtures.at(0).toObject().value(QStringLiteral("fixture")).toString(), QString::number(a->id()));
    QCOMPARE(fixtures.at(0).toObject().value(QStringLiteral("startIndex")).toInt(), 0);
    QCOMPARE(fixtures.at(0).toObject().value(QStringLiteral("step")).toInt(), 1);
    // backward, offset 0 -> last index, step -1
    QCOMPARE(fixtures.at(1).toObject().value(QStringLiteral("startIndex")).toInt(), 511);
    QCOMPARE(fixtures.at(1).toObject().value(QStringLiteral("step")).toInt(), -1);
    // forward, offset 90 degrees on a Circle -> a quarter of the way round
    int quarter = fixtures.at(2).toObject().value(QStringLiteral("startIndex")).toInt();
    QVERIFY2(qAbs(quarter - 128) <= 2, qPrintable(QStringLiteral("startIndex %1, expected ~128").arg(quarter)));
    QVERIFY(fixtures.at(2).toObject().contains(QStringLiteral("path")) == false);

    // per-fixture paths on request
    params.insert(QStringLiteral("includeFixturePaths"), true);
    reply = sendAndWaitForReply(QStringLiteral("functions.efx.getPreview"), params);
    QCOMPARE(reply.value(QStringLiteral("ok")).toBool(), true);
    fixtures = reply.value(QStringLiteral("result")).toObject().value(QStringLiteral("fixtures")).toArray();
    QCOMPARE(fixtures.at(2).toObject().value(QStringLiteral("path")).toArray().count(), 512);

    // not an EFX
    QJsonObject wrong;
    wrong.insert(QStringLiteral("functionId"), QString::number(m_collection->id()));
    reply = sendAndWaitForReply(QStringLiteral("functions.efx.getPreview"), wrong);
    QCOMPARE(errorCode(reply), QStringLiteral("NOT_FOUND"));
}

QTEST_MAIN(ApiEfxCollectionDomain_Test)
