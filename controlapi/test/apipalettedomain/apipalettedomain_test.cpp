/*
  Q Light Controller Plus - Control API unit test
  apipalettedomain_test.cpp

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

#include "apipalettedomain_test.h"
#include "apiserver.h"
#include "doc.h"
#include "qlcpalette.h"

static QString buildRequest(const QString &method, const QJsonObject &params, const QString &id)
{
    QJsonObject obj;
    obj.insert(QStringLiteral("type"), QStringLiteral("request"));
    obj.insert(QStringLiteral("id"), id);
    obj.insert(QStringLiteral("method"), method);
    obj.insert(QStringLiteral("params"), params);
    return QString::fromUtf8(QJsonDocument(obj).toJson(QJsonDocument::Compact));
}

void ApiPaletteDomain_Test::init()
{
    m_doc = new Doc(nullptr);
    m_apiServer = new ApiServer(nullptr, m_doc);
    QVERIFY(m_apiServer->listen(0, QHostAddress::LocalHost));

    m_client = new QWebSocket();
    m_client->open(QUrl(QStringLiteral("ws://127.0.0.1:%1/qlcplusapi").arg(m_apiServer->serverPort())));
    QVERIFY(QTest::qWaitFor([this]() { return m_client->state() == QAbstractSocket::ConnectedState; }, 2000));
}

void ApiPaletteDomain_Test::cleanup()
{
    delete m_client;
    m_client = nullptr;
    delete m_apiServer;
    m_apiServer = nullptr;
    delete m_doc;
    m_doc = nullptr;
}

QJsonObject ApiPaletteDomain_Test::sendAndWaitForReply(const QString &method, const QJsonObject &params, const QString &requestId)
{
    // A mutation's response and its broadcast event can arrive in either
    // order - scan every frame received so far for the matching "response",
    // not just the first one, polling until it shows up or we time out. Same
    // pattern as ApiIoDomain_Test::sendAndWaitForReply.
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

QString ApiPaletteDomain_Test::helloAndGetClientId()
{
    QJsonObject reply = sendAndWaitForReply(QStringLiteral("hello"), QJsonObject(), QStringLiteral("t-hello"));
    return reply.value(QStringLiteral("result")).toObject().value(QStringLiteral("clientId")).toString();
}

int ApiPaletteDomain_Test::currentDocRevision()
{
    QJsonObject reply = sendAndWaitForReply(QStringLiteral("palette.list"), QJsonObject(), QStringLiteral("t-rev"));
    return reply.value(QStringLiteral("result")).toObject().value(QStringLiteral("docRevision")).toInt();
}

void ApiPaletteDomain_Test::createAddsColorPalette()
{
    QString clientId = helloAndGetClientId();
    int baseRevision = currentDocRevision();

    QJsonObject params;
    params.insert(QStringLiteral("type"), QStringLiteral("Color"));
    params.insert(QStringLiteral("name"), QStringLiteral("Color A"));
    params.insert(QStringLiteral("values"), QJsonArray{QStringLiteral("#ff0033")});
    params.insert(QStringLiteral("baseRevision"), baseRevision);

    QSignalSpy spy(m_client, &QWebSocket::textMessageReceived);
    m_client->sendTextMessage(buildRequest(QStringLiteral("palette.create"), params, QStringLiteral("t-create")));
    QVERIFY(QTest::qWaitFor([&]() { return spy.count() >= 2; }, 2000));

    bool sawResponse = false, sawEvent = false;
    int paletteId = -1;
    for (const QList<QVariant> &frame : spy)
    {
        QJsonObject obj = QJsonDocument::fromJson(frame.at(0).toString().toUtf8()).object();
        QString type = obj.value(QStringLiteral("type")).toString();
        if (type == QStringLiteral("response") && obj.value(QStringLiteral("id")).toString() == QStringLiteral("t-create"))
        {
            QCOMPARE(obj.value(QStringLiteral("ok")).toBool(), true);
            QJsonObject result = obj.value(QStringLiteral("result")).toObject();
            QVERIFY(result.contains(QStringLiteral("paletteId")));
            QVERIFY(result.value(QStringLiteral("docRevision")).toInt() > baseRevision);
            paletteId = result.value(QStringLiteral("paletteId")).toInt();
            sawResponse = true;
        }
        else if (type == QStringLiteral("event") && obj.value(QStringLiteral("topic")).toString() == QStringLiteral("palette.created"))
        {
            QJsonObject data = obj.value(QStringLiteral("data")).toObject();
            QJsonObject palette = data.value(QStringLiteral("palette")).toObject();
            QCOMPARE(palette.value(QStringLiteral("name")).toString(), QStringLiteral("Color A"));
            QCOMPARE(palette.value(QStringLiteral("type")).toString(), QStringLiteral("Color"));
            QCOMPARE(palette.value(QStringLiteral("values")).toArray().first().toString(), QStringLiteral("#ff0033"));
            QCOMPARE(obj.value(QStringLiteral("originClientId")).toString(), clientId);
            sawEvent = true;
        }
    }
    QVERIFY(sawResponse);
    QVERIFY(sawEvent);
    QVERIFY(paletteId >= 0);

    QLCPalette *palette = m_doc->palette(quint32(paletteId));
    QVERIFY(palette != nullptr);
    QCOMPARE(palette->type(), QLCPalette::Color);
    QCOMPARE(palette->name(), QStringLiteral("Color A"));
}

void ApiPaletteDomain_Test::createWithStaleRevisionConflicts()
{
    helloAndGetClientId();
    int baseRevision = currentDocRevision();

    QJsonObject params;
    params.insert(QStringLiteral("type"), QStringLiteral("Dimmer"));
    params.insert(QStringLiteral("name"), QStringLiteral("Dim A"));
    params.insert(QStringLiteral("baseRevision"), baseRevision - 1); // deliberately stale

    QJsonObject reply = sendAndWaitForReply(QStringLiteral("palette.create"), params, QStringLiteral("t-stale"));
    QCOMPARE(reply.value(QStringLiteral("ok")).toBool(), false);
    QJsonObject error = reply.value(QStringLiteral("error")).toObject();
    QCOMPARE(error.value(QStringLiteral("code")).toString(), QStringLiteral("CONFLICT"));
    QCOMPARE(error.value(QStringLiteral("details")).toObject().value(QStringLiteral("docRevision")).toInt(), baseRevision);
}

void ApiPaletteDomain_Test::createWithUnknownTypeIsInvalidParams()
{
    helloAndGetClientId();
    int baseRevision = currentDocRevision();

    QJsonObject params;
    params.insert(QStringLiteral("type"), QStringLiteral("NotAType"));
    params.insert(QStringLiteral("name"), QStringLiteral("Bad"));
    params.insert(QStringLiteral("baseRevision"), baseRevision);

    QJsonObject reply = sendAndWaitForReply(QStringLiteral("palette.create"), params, QStringLiteral("t-badtype"));
    QCOMPARE(reply.value(QStringLiteral("ok")).toBool(), false);
    QCOMPARE(reply.value(QStringLiteral("error")).toObject().value(QStringLiteral("code")).toString(),
              QStringLiteral("INVALID_PARAMS"));
}

void ApiPaletteDomain_Test::getReturnsFullDetail()
{
    helloAndGetClientId();
    int baseRevision = currentDocRevision();

    QJsonObject createParams;
    createParams.insert(QStringLiteral("type"), QStringLiteral("PanTilt"));
    createParams.insert(QStringLiteral("name"), QStringLiteral("Center"));
    createParams.insert(QStringLiteral("values"), QJsonArray{128, 200});
    createParams.insert(QStringLiteral("baseRevision"), baseRevision);
    QJsonObject createReply = sendAndWaitForReply(QStringLiteral("palette.create"), createParams, QStringLiteral("t-create2"));
    int paletteId = createReply.value(QStringLiteral("result")).toObject().value(QStringLiteral("paletteId")).toInt();

    QJsonObject getParams;
    getParams.insert(QStringLiteral("paletteId"), paletteId);
    QJsonObject reply = sendAndWaitForReply(QStringLiteral("palette.get"), getParams, QStringLiteral("t-get"));
    QCOMPARE(reply.value(QStringLiteral("ok")).toBool(), true);
    QJsonObject result = reply.value(QStringLiteral("result")).toObject();
    QCOMPARE(result.value(QStringLiteral("id")).toInt(), paletteId);
    QCOMPARE(result.value(QStringLiteral("name")).toString(), QStringLiteral("Center"));
    QCOMPARE(result.value(QStringLiteral("type")).toString(), QStringLiteral("PanTilt"));
    QJsonArray values = result.value(QStringLiteral("values")).toArray();
    QCOMPARE(values.count(), 2);
    QCOMPARE(values.at(0).toInt(), 128);
    QCOMPARE(values.at(1).toInt(), 200);
}

void ApiPaletteDomain_Test::getMissingPaletteIsNotFound()
{
    helloAndGetClientId();
    QJsonObject params;
    params.insert(QStringLiteral("paletteId"), 9999);
    QJsonObject reply = sendAndWaitForReply(QStringLiteral("palette.get"), params, QStringLiteral("t-missing"));
    QCOMPARE(reply.value(QStringLiteral("ok")).toBool(), false);
    QCOMPARE(reply.value(QStringLiteral("error")).toObject().value(QStringLiteral("code")).toString(),
              QStringLiteral("NOT_FOUND"));
}

void ApiPaletteDomain_Test::listReturnsSummaries()
{
    helloAndGetClientId();
    int baseRevision = currentDocRevision();

    QJsonObject createParams;
    createParams.insert(QStringLiteral("type"), QStringLiteral("Dimmer"));
    createParams.insert(QStringLiteral("name"), QStringLiteral("Dim A"));
    createParams.insert(QStringLiteral("baseRevision"), baseRevision);
    sendAndWaitForReply(QStringLiteral("palette.create"), createParams, QStringLiteral("t-create3"));

    QJsonObject reply = sendAndWaitForReply(QStringLiteral("palette.list"), QJsonObject(), QStringLiteral("t-list"));
    QCOMPARE(reply.value(QStringLiteral("ok")).toBool(), true);
    QJsonArray palettes = reply.value(QStringLiteral("result")).toObject().value(QStringLiteral("palettes")).toArray();
    QCOMPARE(palettes.count(), 1);
    QJsonObject summary = palettes.first().toObject();
    QCOMPARE(summary.value(QStringLiteral("name")).toString(), QStringLiteral("Dim A"));
    QCOMPARE(summary.value(QStringLiteral("type")).toString(), QStringLiteral("Dimmer"));
    QVERIFY(summary.contains(QStringLiteral("values")) == false);
}

void ApiPaletteDomain_Test::updateRenamesAndBroadcasts()
{
    QString clientId = helloAndGetClientId();
    int baseRevision = currentDocRevision();

    QJsonObject createParams;
    createParams.insert(QStringLiteral("type"), QStringLiteral("Color"));
    createParams.insert(QStringLiteral("name"), QStringLiteral("Color A"));
    createParams.insert(QStringLiteral("values"), QJsonArray{QStringLiteral("#000000")});
    createParams.insert(QStringLiteral("baseRevision"), baseRevision);
    QJsonObject createReply = sendAndWaitForReply(QStringLiteral("palette.create"), createParams, QStringLiteral("t-create4"));
    int paletteId = createReply.value(QStringLiteral("result")).toObject().value(QStringLiteral("paletteId")).toInt();
    int revisionAfterCreate = createReply.value(QStringLiteral("result")).toObject().value(QStringLiteral("docRevision")).toInt();

    QJsonObject updateParams;
    updateParams.insert(QStringLiteral("paletteId"), paletteId);
    updateParams.insert(QStringLiteral("name"), QStringLiteral("Color A Renamed"));
    updateParams.insert(QStringLiteral("values"), QJsonArray{QStringLiteral("#ff00ff")});
    updateParams.insert(QStringLiteral("baseRevision"), revisionAfterCreate);

    QSignalSpy spy(m_client, &QWebSocket::textMessageReceived);
    m_client->sendTextMessage(buildRequest(QStringLiteral("palette.update"), updateParams, QStringLiteral("t-update")));
    QVERIFY(QTest::qWaitFor([&]() { return spy.count() >= 2; }, 2000));

    bool sawResponse = false, sawEvent = false;
    for (const QList<QVariant> &frame : spy)
    {
        QJsonObject obj = QJsonDocument::fromJson(frame.at(0).toString().toUtf8()).object();
        QString type = obj.value(QStringLiteral("type")).toString();
        if (type == QStringLiteral("response") && obj.value(QStringLiteral("id")).toString() == QStringLiteral("t-update"))
        {
            QCOMPARE(obj.value(QStringLiteral("ok")).toBool(), true);
            QVERIFY(obj.value(QStringLiteral("result")).toObject().value(QStringLiteral("docRevision")).toInt() > revisionAfterCreate);
            sawResponse = true;
        }
        else if (type == QStringLiteral("event") && obj.value(QStringLiteral("topic")).toString() == QStringLiteral("palette.updated"))
        {
            QJsonObject palette = obj.value(QStringLiteral("data")).toObject().value(QStringLiteral("palette")).toObject();
            QCOMPARE(palette.value(QStringLiteral("name")).toString(), QStringLiteral("Color A Renamed"));
            QCOMPARE(palette.value(QStringLiteral("values")).toArray().first().toString(), QStringLiteral("#ff00ff"));
            QCOMPARE(obj.value(QStringLiteral("originClientId")).toString(), clientId);
            sawEvent = true;
        }
    }
    QVERIFY(sawResponse);
    QVERIFY(sawEvent);

    QLCPalette *palette = m_doc->palette(quint32(paletteId));
    QVERIFY(palette != nullptr);
    QCOMPARE(palette->name(), QStringLiteral("Color A Renamed"));
}

void ApiPaletteDomain_Test::updateWithNoActualChangeDoesNotBumpRevision()
{
    helloAndGetClientId();
    int baseRevision = currentDocRevision();

    QJsonObject createParams;
    createParams.insert(QStringLiteral("type"), QStringLiteral("Dimmer"));
    createParams.insert(QStringLiteral("name"), QStringLiteral("Dim A"));
    createParams.insert(QStringLiteral("baseRevision"), baseRevision);
    QJsonObject createReply = sendAndWaitForReply(QStringLiteral("palette.create"), createParams, QStringLiteral("t-create5"));
    int paletteId = createReply.value(QStringLiteral("result")).toObject().value(QStringLiteral("paletteId")).toInt();
    int revisionAfterCreate = createReply.value(QStringLiteral("result")).toObject().value(QStringLiteral("docRevision")).toInt();

    // Same name as it already has - a genuine no-op.
    QJsonObject updateParams;
    updateParams.insert(QStringLiteral("paletteId"), paletteId);
    updateParams.insert(QStringLiteral("name"), QStringLiteral("Dim A"));
    updateParams.insert(QStringLiteral("baseRevision"), revisionAfterCreate);

    QSignalSpy spy(m_client, &QWebSocket::textMessageReceived);
    QJsonObject reply = sendAndWaitForReply(QStringLiteral("palette.update"), updateParams, QStringLiteral("t-noop"));
    QCOMPARE(reply.value(QStringLiteral("ok")).toBool(), true);
    QCOMPARE(reply.value(QStringLiteral("result")).toObject().value(QStringLiteral("docRevision")).toInt(), revisionAfterCreate);

    // No palette.updated event should have been broadcast for a no-op change.
    QTest::qWait(200);
    bool sawEvent = false;
    for (const QList<QVariant> &frame : spy)
    {
        QJsonObject obj = QJsonDocument::fromJson(frame.at(0).toString().toUtf8()).object();
        if (obj.value(QStringLiteral("type")).toString() == QStringLiteral("event") &&
            obj.value(QStringLiteral("topic")).toString() == QStringLiteral("palette.updated"))
            sawEvent = true;
    }
    QVERIFY(sawEvent == false);
    QCOMPARE(int(m_doc->docRevision()), revisionAfterCreate);
}

void ApiPaletteDomain_Test::updateMissingPaletteIsNotFound()
{
    helloAndGetClientId();
    int baseRevision = currentDocRevision();

    QJsonObject params;
    params.insert(QStringLiteral("paletteId"), 9999);
    params.insert(QStringLiteral("name"), QStringLiteral("X"));
    params.insert(QStringLiteral("baseRevision"), baseRevision);
    QJsonObject reply = sendAndWaitForReply(QStringLiteral("palette.update"), params, QStringLiteral("t-updatemissing"));
    QCOMPARE(reply.value(QStringLiteral("ok")).toBool(), false);
    QCOMPARE(reply.value(QStringLiteral("error")).toObject().value(QStringLiteral("code")).toString(),
              QStringLiteral("NOT_FOUND"));
}

void ApiPaletteDomain_Test::deleteRemovesPaletteAndBroadcasts()
{
    QString clientId = helloAndGetClientId();
    int baseRevision = currentDocRevision();

    QJsonObject createParams;
    createParams.insert(QStringLiteral("type"), QStringLiteral("Dimmer"));
    createParams.insert(QStringLiteral("name"), QStringLiteral("Dim A"));
    createParams.insert(QStringLiteral("baseRevision"), baseRevision);
    QJsonObject createReply = sendAndWaitForReply(QStringLiteral("palette.create"), createParams, QStringLiteral("t-create6"));
    int paletteId = createReply.value(QStringLiteral("result")).toObject().value(QStringLiteral("paletteId")).toInt();
    int revisionAfterCreate = createReply.value(QStringLiteral("result")).toObject().value(QStringLiteral("docRevision")).toInt();

    QJsonObject deleteParams;
    deleteParams.insert(QStringLiteral("paletteId"), paletteId);
    deleteParams.insert(QStringLiteral("baseRevision"), revisionAfterCreate);

    QSignalSpy spy(m_client, &QWebSocket::textMessageReceived);
    m_client->sendTextMessage(buildRequest(QStringLiteral("palette.delete"), deleteParams, QStringLiteral("t-delete")));
    QVERIFY(QTest::qWaitFor([&]() { return spy.count() >= 2; }, 2000));

    bool sawResponse = false, sawEvent = false;
    for (const QList<QVariant> &frame : spy)
    {
        QJsonObject obj = QJsonDocument::fromJson(frame.at(0).toString().toUtf8()).object();
        QString type = obj.value(QStringLiteral("type")).toString();
        if (type == QStringLiteral("response") && obj.value(QStringLiteral("id")).toString() == QStringLiteral("t-delete"))
        {
            QCOMPARE(obj.value(QStringLiteral("ok")).toBool(), true);
            QVERIFY(obj.value(QStringLiteral("result")).toObject().value(QStringLiteral("docRevision")).toInt() > revisionAfterCreate);
            sawResponse = true;
        }
        else if (type == QStringLiteral("event") && obj.value(QStringLiteral("topic")).toString() == QStringLiteral("palette.deleted"))
        {
            QJsonObject data = obj.value(QStringLiteral("data")).toObject();
            QCOMPARE(data.value(QStringLiteral("paletteId")).toInt(), paletteId);
            QCOMPARE(obj.value(QStringLiteral("originClientId")).toString(), clientId);
            sawEvent = true;
        }
    }
    QVERIFY(sawResponse);
    QVERIFY(sawEvent);
    QVERIFY(m_doc->palette(quint32(paletteId)) == nullptr);
}

void ApiPaletteDomain_Test::deleteWithStaleRevisionConflicts()
{
    helloAndGetClientId();
    int baseRevision = currentDocRevision();

    QJsonObject createParams;
    createParams.insert(QStringLiteral("type"), QStringLiteral("Dimmer"));
    createParams.insert(QStringLiteral("name"), QStringLiteral("Dim A"));
    createParams.insert(QStringLiteral("baseRevision"), baseRevision);
    QJsonObject createReply = sendAndWaitForReply(QStringLiteral("palette.create"), createParams, QStringLiteral("t-create7"));
    int paletteId = createReply.value(QStringLiteral("result")).toObject().value(QStringLiteral("paletteId")).toInt();
    int revisionAfterCreate = createReply.value(QStringLiteral("result")).toObject().value(QStringLiteral("docRevision")).toInt();

    QJsonObject deleteParams;
    deleteParams.insert(QStringLiteral("paletteId"), paletteId);
    deleteParams.insert(QStringLiteral("baseRevision"), revisionAfterCreate - 1); // stale

    QJsonObject reply = sendAndWaitForReply(QStringLiteral("palette.delete"), deleteParams, QStringLiteral("t-deletestale"));
    QCOMPARE(reply.value(QStringLiteral("ok")).toBool(), false);
    QCOMPARE(reply.value(QStringLiteral("error")).toObject().value(QStringLiteral("code")).toString(),
              QStringLiteral("CONFLICT"));
    QVERIFY(m_doc->palette(quint32(paletteId)) != nullptr);
}

QTEST_MAIN(ApiPaletteDomain_Test)
