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
#include <QBuffer>
#include <QXmlStreamWriter>

#include "apipalettedomain_test.h"
#include "apiserver.h"
#include "doc.h"
#include "qlcpalette.h"
#include "monitorproperties.h"
#include "qlcfixturemode.h"
#include "qlcfixturehead.h"
#include "qlcfixturedef.h"
#include "qlccapability.h"
#include "qlcphysical.h"
#include "qlcchannel.h"
#include "scenevalue.h"
#include "fixture.h"

/*****************************************************************************
 * In-memory fixture definitions for palette.apply (same shapes as
 * engine/test/qlcpalette's): a mover with every channel a palette type can
 * write, and a two-head RGB bar with a master dimmer.
 *****************************************************************************/

static QLCChannel *addPresetChannel(QLCFixtureDef *def, QLCChannel::Preset preset,
                                    const QString &name = QString())
{
    QLCChannel *ch = new QLCChannel();
    if (name.isEmpty() == false)
        ch->setName(name);
    ch->setPreset(preset);
    ch->addPresetCapability();
    def->addChannel(ch);
    return ch;
}

enum MoverChannel
{
    MoverDimmer = 0, MoverPan, MoverPanFine, MoverTilt, MoverTiltFine,
    MoverRed, MoverGreen, MoverBlue, MoverWhite, MoverAmber, MoverUV,
    MoverShutter, MoverGobo, MoverZoom, MoverZoomFine, MoverChannelCount
};

static QLCFixtureDef *makeMoverDef()
{
    QLCFixtureDef *def = new QLCFixtureDef();
    def->setManufacturer("Test");
    def->setModel("Mover");
    def->setType(QLCFixtureDef::MovingHead);

    addPresetChannel(def, QLCChannel::IntensityDimmer);
    addPresetChannel(def, QLCChannel::PositionPan);
    addPresetChannel(def, QLCChannel::PositionPanFine);
    addPresetChannel(def, QLCChannel::PositionTilt);
    addPresetChannel(def, QLCChannel::PositionTiltFine);
    addPresetChannel(def, QLCChannel::IntensityRed);
    addPresetChannel(def, QLCChannel::IntensityGreen);
    addPresetChannel(def, QLCChannel::IntensityBlue);
    addPresetChannel(def, QLCChannel::IntensityWhite);
    addPresetChannel(def, QLCChannel::IntensityAmber);
    addPresetChannel(def, QLCChannel::IntensityUV);

    QLCChannel *shutter = new QLCChannel();
    shutter->setName("Shutter");
    shutter->setGroup(QLCChannel::Shutter);
    QLCCapability *closed = new QLCCapability(0, 9, "Closed");
    closed->setPreset(QLCCapability::ShutterClose);
    QLCCapability *open = new QLCCapability(10, 19, "Open");
    open->setPreset(QLCCapability::ShutterOpen);
    QLCCapability *strobe = new QLCCapability(20, 255, "Strobe");
    strobe->setPreset(QLCCapability::StrobeSlowToFast);
    shutter->addCapability(closed);
    shutter->addCapability(open);
    shutter->addCapability(strobe);
    def->addChannel(shutter);

    addPresetChannel(def, QLCChannel::GoboWheel);
    addPresetChannel(def, QLCChannel::BeamZoomSmallBig);
    addPresetChannel(def, QLCChannel::BeamZoomFine);

    QLCFixtureMode *mode = new QLCFixtureMode(def);
    mode->setName("Test mode");
    for (int i = 0; i < def->channels().size(); i++)
        mode->insertChannel(def->channels().at(i), i);

    // one head holding everything but the master dimmer
    QLCFixtureHead head;
    for (int i = MoverPan; i < MoverChannelCount; i++)
        head.addChannel(i);
    mode->insertHead(-1, head);

    QLCPhysical phy;
    phy.setFocusPanMax(540);
    phy.setFocusTiltMax(270);
    phy.setLensDegreesMin(10);
    phy.setLensDegreesMax(60);
    mode->setPhysical(phy);

    def->addMode(mode);
    return def;
}

enum BarChannel
{
    BarMaster = 0, BarDimmer1, BarRed1, BarGreen1, BarBlue1,
    BarDimmer2, BarRed2, BarGreen2, BarBlue2, BarChannelCount
};

static QLCFixtureDef *makeBarDef()
{
    QLCFixtureDef *def = new QLCFixtureDef();
    def->setManufacturer("Test");
    def->setModel("Bar");
    def->setType(QLCFixtureDef::LEDBarPixels);

    addPresetChannel(def, QLCChannel::IntensityMasterDimmer, "Master");
    addPresetChannel(def, QLCChannel::IntensityDimmer, "Dimmer 1");
    addPresetChannel(def, QLCChannel::IntensityRed, "Red 1");
    addPresetChannel(def, QLCChannel::IntensityGreen, "Green 1");
    addPresetChannel(def, QLCChannel::IntensityBlue, "Blue 1");
    addPresetChannel(def, QLCChannel::IntensityDimmer, "Dimmer 2");
    addPresetChannel(def, QLCChannel::IntensityRed, "Red 2");
    addPresetChannel(def, QLCChannel::IntensityGreen, "Green 2");
    addPresetChannel(def, QLCChannel::IntensityBlue, "Blue 2");

    QLCFixtureMode *mode = new QLCFixtureMode(def);
    mode->setName("9 Channel");
    for (int i = 0; i < def->channels().size(); i++)
        mode->insertChannel(def->channels().at(i), i);

    QLCFixtureHead head1;
    for (int i = BarDimmer1; i <= BarBlue1; i++)
        head1.addChannel(i);
    mode->insertHead(-1, head1);

    QLCFixtureHead head2;
    for (int i = BarDimmer2; i <= BarBlue2; i++)
        head2.addChannel(i);
    mode->insertHead(-1, head2);

    def->addMode(mode);
    return def;
}

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
    m_moverDef = makeMoverDef();
    m_barDef = makeBarDef();
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
    delete m_moverDef;
    m_moverDef = nullptr;
    delete m_barDef;
    m_barDef = nullptr;
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

void ApiPaletteDomain_Test::createPanTiltWithOneValueSavesSafely()
{
    // Crash audit: palette.create/update accept any number of values;
    // QLCPalette::saveXML() read m_values.at(1) for PanTilt unguarded, so
    // the next project save (or autosave) aborted on QList's bounds assert.
    helloAndGetClientId();
    QJsonObject params;
    params.insert(QStringLiteral("type"), QStringLiteral("PanTilt"));
    params.insert(QStringLiteral("name"), QStringLiteral("Half position"));
    params.insert(QStringLiteral("values"), QJsonArray{ 90 });
    params.insert(QStringLiteral("baseRevision"), currentDocRevision());
    QJsonObject reply = sendAndWaitForReply(QStringLiteral("palette.create"), params, QStringLiteral("t-pt"));
    QCOMPARE(reply.value(QStringLiteral("ok")).toBool(), true);
    QLCPalette *palette = m_doc->palette(quint32(reply.value(QStringLiteral("result")).toObject().value(QStringLiteral("paletteId")).toInt()));
    QVERIFY(palette != nullptr);

    QBuffer buffer;
    buffer.open(QIODevice::WriteOnly);
    QXmlStreamWriter writer(&buffer);
    QVERIFY(palette->saveXML(&writer));
    QVERIFY(buffer.data().contains("PanTilt"));
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

/*****************************************************************************
 * palette.apply
 *****************************************************************************/

quint32 ApiPaletteDomain_Test::addFixture(QLCFixtureDef *def, quint32 address, float xMm)
{
    Fixture *fxi = new Fixture(m_doc);
    fxi->setName(QString("%1 @%2").arg(def->model()).arg(address));
    fxi->setFixtureDefinition(def, def->modes().first());
    fxi->setAddress(address);
    if (m_doc->addFixture(fxi) == false)
    {
        delete fxi;
        return Fixture::invalidId();
    }
    m_doc->monitorProperties()->setFixturePosition(fxi->id(), 0, 0, QVector3D(xMm, 3000, 1000));
    return fxi->id();
}

QJsonObject ApiPaletteDomain_Test::applyPalette(QLCPalette *palette, const QJsonArray &ids)
{
    if (palette->id() == QLCPalette::invalidId())
        m_doc->addPalette(palette);
    QJsonObject params;
    params.insert(QStringLiteral("paletteId"), int(palette->id()));
    params.insert(QStringLiteral("fixtureIds"), ids);
    return sendAndWaitForReply(QStringLiteral("palette.apply"), params, QStringLiteral("t-apply"));
}

void ApiPaletteDomain_Test::verifyMatchesEngine(const QJsonObject &reply, QLCPalette *palette, const QList<quint32> &ids)
{
    QCOMPARE(reply.value(QStringLiteral("ok")).toBool(), true);
    QJsonArray channels = reply.value(QStringLiteral("result")).toObject().value(QStringLiteral("channels")).toArray();

    // The desktop's maths, computed directly: PaletteManager::previewPalette()
    QList<SceneValue> expected = palette->valuesFromFixtures(m_doc, ids);
    QCOMPARE(channels.count(), expected.count());
    for (int i = 0; i < expected.count(); i++)
    {
        QJsonObject ch = channels.at(i).toObject();
        const SceneValue &sv = expected.at(i);
        Fixture *fixture = m_doc->fixture(sv.fxi);
        QVERIFY(fixture != nullptr);
        QCOMPARE(ch.value(QStringLiteral("fixtureId")).toString(), QString::number(sv.fxi));
        QCOMPARE(ch.value(QStringLiteral("channel")).toInt(), int(sv.channel));
        QCOMPARE(ch.value(QStringLiteral("address")).toInt(), int(fixture->universeAddress() + sv.channel));
        QCOMPARE(ch.value(QStringLiteral("value")).toInt(), int(sv.value));
    }

    // ... and every one of them reached the Simple Desk override store
    QJsonObject sd;
    sd.insert(QStringLiteral("universeId"), 0);
    QJsonObject desk = sendAndWaitForReply(QStringLiteral("io.simpleDesk.get"), sd, QStringLiteral("t-sd"));
    QHash<int, int> deskValues;
    for (const QJsonValue &v : desk.value(QStringLiteral("result")).toObject().value(QStringLiteral("channels")).toArray())
        deskValues.insert(v.toObject().value(QStringLiteral("address")).toInt(), v.toObject().value(QStringLiteral("value")).toInt());
    for (const QJsonValue &v : channels)
    {
        int address = v.toObject().value(QStringLiteral("address")).toInt();
        QVERIFY2(deskValues.contains(address), qPrintable(QString("address %1 not overridden").arg(address)));
        // the last write to an address wins (valuesFromFixtures order)
    }
}

void ApiPaletteDomain_Test::applyEveryTypeMatchesEngine_data()
{
    QTest::addColumn<QString>("type");
    QTest::addColumn<QVariantList>("values");
    QTest::addColumn<int>("moverChannel");  // a mover channel the palette must write
    QTest::addColumn<int>("moverValue");    // its expected value, -1 = only "written"

    // Dimmer values are DMX (IntensityTool stores percent * 2.55)
    QTest::newRow("Dimmer") << "Dimmer" << QVariantList{ 200 } << int(MoverDimmer) << 200;
    QTest::newRow("Color") << "Color" << QVariantList{ QStringLiteral("#ff8000") } << int(MoverGreen) << 128;
    QTest::newRow("Color WAUV") << "Color" << QVariantList{ QStringLiteral("#ff8000102030") } << int(MoverUV) << 0x30;
    // 90 of 540 degrees -> 16 bit 10922 -> MSB 42
    QTest::newRow("Pan") << "Pan" << QVariantList{ 90 } << int(MoverPan) << 42;
    // 135 of 270 degrees -> 32767 -> MSB 127
    QTest::newRow("Tilt") << "Tilt" << QVariantList{ 135 } << int(MoverTilt) << 127;
    QTest::newRow("PanTilt") << "PanTilt" << QVariantList{ 270, 135 } << int(MoverPan) << 127;
    QTest::newRow("Position3D") << "Position3D" << QVariantList{ 5.0, 0.0, 8.0 } << int(MoverTilt) << -1;
    // Shutter open: the ShutterOpen capability's midpoint (10..19)
    QTest::newRow("Shutter open") << "Shutter" << QVariantList{ int(QLCCapability::ShutterOpen), 0 } << int(MoverShutter) << 14;
    // Strobe 50%: 20 + (255 - 20) * 50 / 100
    QTest::newRow("Shutter strobe") << "Shutter" << QVariantList{ int(QLCCapability::StrobeSlowToFast), 50 } << int(MoverShutter) << 137;
    QTest::newRow("Gobo") << "Gobo" << QVariantList{ 42 } << int(MoverGobo) << 42;
    // 35 degrees in a 10..60 lens -> half way -> 32767 -> MSB 127
    QTest::newRow("Zoom") << "Zoom" << QVariantList{ 35.0 } << int(MoverZoom) << 127;
}

void ApiPaletteDomain_Test::applyEveryTypeMatchesEngine()
{
    QFETCH(QString, type);
    QFETCH(QVariantList, values);
    QFETCH(int, moverChannel);
    QFETCH(int, moverValue);

    MonitorProperties *mp = m_doc->monitorProperties();
    mp->setGridSize(QVector3D(10, 3, 10));
    quint32 mover = addFixture(m_moverDef, 0, 5000);
    quint32 bar = addFixture(m_barDef, 20, 2000);
    QVERIFY(mover != Fixture::invalidId() && bar != Fixture::invalidId());

    QLCPalette *palette = new QLCPalette(QLCPalette::stringToType(type));
    if (values.count() == 1)
        palette->setValue(values.at(0));
    else if (values.count() == 2)
        palette->setValue(values.at(0), values.at(1));
    else
        palette->setValue(values.at(0), values.at(1), values.at(2));

    QList<quint32> ids{ mover, bar };
    QJsonObject reply = applyPalette(palette, QJsonArray{ QString::number(mover), QString::number(bar) });
    verifyMatchesEngine(reply, palette, ids);

    bool found = false;
    for (const QJsonValue &v : reply.value(QStringLiteral("result")).toObject().value(QStringLiteral("channels")).toArray())
    {
        QJsonObject ch = v.toObject();
        if (ch.value(QStringLiteral("fixtureId")).toString() == QString::number(mover) &&
            ch.value(QStringLiteral("channel")).toInt() == moverChannel)
        {
            found = true;
            if (moverValue >= 0)
                QCOMPARE(ch.value(QStringLiteral("value")).toInt(), moverValue);
        }
    }
    QVERIFY2(found, "the palette wrote nothing to the mover's channel");
}

void ApiPaletteDomain_Test::applyFansDimmerAcrossFixtures()
{
    quint32 m1 = addFixture(m_moverDef, 0, 0);
    quint32 m2 = addFixture(m_moverDef, 20, 1000);
    quint32 m3 = addFixture(m_moverDef, 40, 2000);

    QLCPalette *palette = new QLCPalette(QLCPalette::Dimmer);
    palette->setValue(0);
    palette->setFanningType(QLCPalette::Linear);
    palette->setFanningLayout(QLCPalette::XAscending);
    palette->setFanningAmount(100);
    palette->setFanningValue(255);

    // given out of order: the fan follows the X layout, not the request order
    QJsonObject reply = applyPalette(palette, QJsonArray{ QString::number(m3), QString::number(m1), QString::number(m2) });
    verifyMatchesEngine(reply, palette, QList<quint32>{ m3, m1, m2 });

    QHash<QString, int> master;
    for (const QJsonValue &v : reply.value(QStringLiteral("result")).toObject().value(QStringLiteral("channels")).toArray())
    {
        QJsonObject ch = v.toObject();
        if (ch.value(QStringLiteral("channel")).toInt() == MoverDimmer)
            master.insert(ch.value(QStringLiteral("fixtureId")).toString(), ch.value(QStringLiteral("value")).toInt());
    }
    QCOMPARE(master.value(QString::number(m1), -1), 0);
    QCOMPARE(master.value(QString::number(m2), -1), 127);
    QCOMPARE(master.value(QString::number(m3), -1), 255);
}

void ApiPaletteDomain_Test::applyWritesGoboWheel()
{
    quint32 mover = addFixture(m_moverDef, 0);
    quint32 bar = addFixture(m_barDef, 20);

    QLCPalette *palette = new QLCPalette(QLCPalette::Gobo);
    palette->setValue(42);
    QJsonObject reply = applyPalette(palette, QJsonArray{ int(mover), int(bar) }); // numeric ids work too
    verifyMatchesEngine(reply, palette, QList<quint32>{ mover, bar });

    QJsonArray channels = reply.value(QStringLiteral("result")).toObject().value(QStringLiteral("channels")).toArray();
    QCOMPARE(channels.count(), 1); // the bar has no gobo wheel
    QCOMPARE(channels.at(0).toObject().value(QStringLiteral("channel")).toInt(), int(MoverGobo));
    QCOMPARE(channels.at(0).toObject().value(QStringLiteral("value")).toInt(), 42);

    // nothing to write is still ok
    reply = applyPalette(palette, QJsonArray{ QString::number(bar) });
    QCOMPARE(reply.value(QStringLiteral("ok")).toBool(), true);
    QCOMPARE(reply.value(QStringLiteral("result")).toObject().value(QStringLiteral("channels")).toArray().count(), 0);
}

void ApiPaletteDomain_Test::applyValidatesParams()
{
    quint32 mover = addFixture(m_moverDef, 0);
    QLCPalette *palette = new QLCPalette(QLCPalette::Dimmer);
    palette->setValue(100);
    m_doc->addPalette(palette);

    QJsonObject params;
    params.insert(QStringLiteral("paletteId"), 9999);
    params.insert(QStringLiteral("fixtureIds"), QJsonArray{ QString::number(mover) });
    QJsonObject reply = sendAndWaitForReply(QStringLiteral("palette.apply"), params);
    QCOMPARE(reply.value(QStringLiteral("ok")).toBool(), false);
    QCOMPARE(reply.value(QStringLiteral("error")).toObject().value(QStringLiteral("code")).toString(), QStringLiteral("NOT_FOUND"));

    params.insert(QStringLiteral("paletteId"), int(palette->id()));
    params.insert(QStringLiteral("fixtureIds"), QJsonArray());
    reply = sendAndWaitForReply(QStringLiteral("palette.apply"), params);
    QCOMPARE(reply.value(QStringLiteral("error")).toObject().value(QStringLiteral("code")).toString(), QStringLiteral("INVALID_PARAMS"));

    params.remove(QStringLiteral("fixtureIds"));
    reply = sendAndWaitForReply(QStringLiteral("palette.apply"), params);
    QCOMPARE(reply.value(QStringLiteral("error")).toObject().value(QStringLiteral("code")).toString(), QStringLiteral("INVALID_PARAMS"));

    // an unknown fixture fails the whole call (it would skew the fanning)
    params.insert(QStringLiteral("fixtureIds"), QJsonArray{ QString::number(mover), QStringLiteral("777") });
    reply = sendAndWaitForReply(QStringLiteral("palette.apply"), params);
    QCOMPARE(reply.value(QStringLiteral("error")).toObject().value(QStringLiteral("code")).toString(), QStringLiteral("NOT_FOUND"));

    // live control: no revision bump, nothing written for the failed calls
    QJsonObject sd;
    sd.insert(QStringLiteral("universeId"), 0);
    reply = sendAndWaitForReply(QStringLiteral("io.simpleDesk.get"), sd);
    QCOMPARE(reply.value(QStringLiteral("result")).toObject().value(QStringLiteral("channels")).toArray().count(), 0);
}

void ApiPaletteDomain_Test::applyOverridesAreReleasable()
{
    QString clientId = helloAndGetClientId();
    quint32 mover = addFixture(m_moverDef, 0);
    int revision = currentDocRevision();

    QLCPalette *palette = new QLCPalette(QLCPalette::PanTilt);
    palette->setValue(270, 135);
    m_doc->addPalette(palette);
    revision = currentDocRevision();

    QSignalSpy spy(m_client, &QWebSocket::textMessageReceived);
    QJsonObject reply = applyPalette(palette, QJsonArray{ QString::number(mover) });
    QJsonArray channels = reply.value(QStringLiteral("result")).toObject().value(QStringLiteral("channels")).toArray();
    QCOMPARE(channels.count(), 4); // pan, pan fine, tilt, tilt fine
    QCOMPARE(currentDocRevision(), revision); // live, not a document edit

    int events = 0;
    for (const QList<QVariant> &frame : spy)
    {
        QJsonObject obj = QJsonDocument::fromJson(frame.at(0).toString().toUtf8()).object();
        if (obj.value(QStringLiteral("topic")).toString() == QStringLiteral("io.simpleDesk.channelChanged") &&
            obj.value(QStringLiteral("originClientId")).toString() == clientId)
            events++;
    }
    QCOMPARE(events, 4);

    // "Release fixtures" in the web UI: io.simpleDesk.resetChannel per address
    for (const QJsonValue &v : channels)
    {
        QJsonObject params;
        params.insert(QStringLiteral("address"), v.toObject().value(QStringLiteral("address")).toInt());
        QCOMPARE(sendAndWaitForReply(QStringLiteral("io.simpleDesk.resetChannel"), params, QStringLiteral("t-reset")).value(QStringLiteral("ok")).toBool(), true);
    }
    QJsonObject sd;
    sd.insert(QStringLiteral("universeId"), 0);
    reply = sendAndWaitForReply(QStringLiteral("io.simpleDesk.get"), sd);
    QCOMPARE(reply.value(QStringLiteral("result")).toObject().value(QStringLiteral("channels")).toArray().count(), 0);
}

QTEST_MAIN(ApiPaletteDomain_Test)
