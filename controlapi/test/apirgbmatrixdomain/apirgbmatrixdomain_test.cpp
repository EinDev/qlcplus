/*
  Q Light Controller Plus - Control API unit test
  apirgbmatrixdomain_test.cpp

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

#include "apirgbmatrixdomain_test.h"
#include "apiserver.h"
#include "rgbscriptscache.h"
#include "rgbalgorithm.h"
#include "fixturegroup.h"
#include "rgbmatrix.h"
#include "rgbtext.h"
#include "universe.h"
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

static QJsonObject algorithmByName(const QJsonArray &algorithms, const QString &name)
{
    for (const QJsonValue &v : algorithms)
        if (v.toObject().value(QStringLiteral("name")).toString() == name)
            return v.toObject();
    return QJsonObject();
}

static QJsonObject propertyByName(const QJsonArray &properties, const QString &name)
{
    for (const QJsonValue &v : properties)
        if (v.toObject().value(QStringLiteral("name")).toString() == name)
            return v.toObject();
    return QJsonObject();
}

void ApiRgbMatrixDomain_Test::init()
{
    m_doc = new Doc(nullptr);
    // Before any RGBMatrix exists: its constructor installs the "Stripes"
    // script, which is an empty, broken RGBScript if the cache is empty.
    QVERIFY(m_doc->rgbScriptsCache()->load(QDir(QString::fromUtf8(RGBSCRIPTS_DIR))));
    QVERIFY(m_doc->rgbScriptsCache()->names().contains(QStringLiteral("Waves")));

    // A group with a size but no heads is enough: RGBMatrix::previewMap()
    // renders the full size, heads only matter when writing DMX.
    m_group = new FixtureGroup(m_doc);
    m_group->setName(QStringLiteral("Test Group"));
    m_group->setSize(QSize(4, 2));
    QVERIFY(m_doc->addFixtureGroup(m_group));

    m_matrix = new RGBMatrix(m_doc);
    m_matrix->setName(QStringLiteral("Test Matrix"));
    m_matrix->setFixtureGroup(m_group->id());
    QVERIFY(m_doc->addFunction(m_matrix));

    m_apiServer = new ApiServer(nullptr, m_doc);
    QVERIFY(m_apiServer->listen(0, QHostAddress::LocalHost));

    m_client = new QWebSocket();
    m_client->open(QUrl(QStringLiteral("ws://127.0.0.1:%1/qlcplusapi").arg(m_apiServer->serverPort())));
    QVERIFY(QTest::qWaitFor([this]() { return m_client->state() == QAbstractSocket::ConnectedState; }, 2000));
    QVERIFY(helloAndGetClientId().isEmpty() == false);
}

void ApiRgbMatrixDomain_Test::cleanup()
{
    delete m_client;
    m_client = nullptr;
    delete m_apiServer;
    m_apiServer = nullptr;
    delete m_doc; // owns m_group and m_matrix
    m_doc = nullptr;
    m_group = nullptr;
    m_matrix = nullptr;
}

QJsonObject ApiRgbMatrixDomain_Test::sendAndWaitForReply(const QString &method, const QJsonObject &params)
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
    }, 3000);
    return found;
}

QString ApiRgbMatrixDomain_Test::helloAndGetClientId()
{
    QJsonObject reply = sendAndWaitForReply(QStringLiteral("hello"), QJsonObject());
    return reply.value(QStringLiteral("result")).toObject().value(QStringLiteral("clientId")).toString();
}

QJsonObject ApiRgbMatrixDomain_Test::waitForEvent(QSignalSpy &spy, const QString &topic,
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
                accept(obj.value(QStringLiteral("data")).toObject()))
            {
                found = obj;
                return true;
            }
        }
        return false;
    }, timeoutMs);
    return found;
}

QJsonObject ApiRgbMatrixDomain_Test::getConfig()
{
    QJsonObject params;
    params.insert(QStringLiteral("functionId"), QString::number(m_matrix->id()));
    QJsonObject reply = sendAndWaitForReply(QStringLiteral("functions.get"), params);
    return reply.value(QStringLiteral("result")).toObject()
                .value(QStringLiteral("typeDetail")).toObject()
                .value(QStringLiteral("config")).toObject();
}

/*****************************************************************************
 * Catalog
 *****************************************************************************/

void ApiRgbMatrixDomain_Test::listAlgorithmsHasBuiltinsAndScripts()
{
    QJsonObject reply = sendAndWaitForReply(QStringLiteral("functions.rgbmatrix.listAlgorithms"), QJsonObject());
    QVERIFY(reply.value(QStringLiteral("ok")).toBool());
    QJsonArray algorithms = reply.value(QStringLiteral("result")).toObject().value(QStringLiteral("algorithms")).toArray();

    // the 4 built-ins, each with its RGBAlgorithm::Type and acceptColors()
    QCOMPARE(algorithmByName(algorithms, QStringLiteral("Plain Color")).value(QStringLiteral("type")).toString(), QStringLiteral("plain"));
    QCOMPARE(algorithmByName(algorithms, QStringLiteral("Plain Color")).value(QStringLiteral("acceptedColors")).toInt(), 1);
    QCOMPARE(algorithmByName(algorithms, QStringLiteral("Text")).value(QStringLiteral("type")).toString(), QStringLiteral("text"));
    QCOMPARE(algorithmByName(algorithms, QStringLiteral("Image")).value(QStringLiteral("type")).toString(), QStringLiteral("image"));
    QCOMPARE(algorithmByName(algorithms, QStringLiteral("Audio Spectrum")).value(QStringLiteral("type")).toString(), QStringLiteral("audio"));

    // every installed script, with its script-only metadata
    QCOMPARE(algorithms.count(), 4 + m_doc->rgbScriptsCache()->names().count());
    QJsonObject waves = algorithmByName(algorithms, QStringLiteral("Waves"));
    QCOMPARE(waves.value(QStringLiteral("type")).toString(), QStringLiteral("script"));
    QVERIFY(waves.value(QStringLiteral("apiVersion")).toInt() >= 2);
    QVERIFY(waves.value(QStringLiteral("author")).toString().isEmpty() == false);
    QCOMPARE(waves.value(QStringLiteral("acceptedColors")).toInt(), 2);
}

void ApiRgbMatrixDomain_Test::getScriptPropertiesReturnsDefinitions()
{
    QJsonObject params;
    params.insert(QStringLiteral("scriptName"), QStringLiteral("Waves"));
    QJsonObject reply = sendAndWaitForReply(QStringLiteral("functions.rgbmatrix.getScriptProperties"), params);
    QVERIFY(reply.value(QStringLiteral("ok")).toBool());
    QJsonArray properties = reply.value(QStringLiteral("result")).toObject().value(QStringLiteral("properties")).toArray();
    QCOMPARE(properties.count(), 5);

    QJsonObject direction = propertyByName(properties, QStringLiteral("direction"));
    QCOMPARE(direction.value(QStringLiteral("displayName")).toString(), QStringLiteral("Direction"));
    QCOMPARE(direction.value(QStringLiteral("type")).toString(), QStringLiteral("list"));
    QJsonArray values = direction.value(QStringLiteral("listValues")).toArray();
    QCOMPARE(values.count(), 4);
    QCOMPARE(values.at(0).toString(), QStringLiteral("Right"));
    QCOMPARE(values.at(3).toString(), QStringLiteral("Out"));

    QJsonObject tail = propertyByName(properties, QStringLiteral("taillength"));
    QCOMPARE(tail.value(QStringLiteral("type")).toString(), QStringLiteral("range"));
    QCOMPARE(tail.value(QStringLiteral("rangeMin")).toInt(), 0);
    QCOMPARE(tail.value(QStringLiteral("rangeMax")).toInt(), 100);
}

void ApiRgbMatrixDomain_Test::getScriptPropertiesOnUnknownScriptIsNotFound()
{
    QJsonObject params;
    params.insert(QStringLiteral("scriptName"), QStringLiteral("No Such Script"));
    QJsonObject reply = sendAndWaitForReply(QStringLiteral("functions.rgbmatrix.getScriptProperties"), params);
    QCOMPARE(reply.value(QStringLiteral("ok")).toBool(), false);
    QCOMPARE(reply.value(QStringLiteral("error")).toObject().value(QStringLiteral("code")).toString(), QStringLiteral("NOT_FOUND"));
}

/*****************************************************************************
 * typeDetail
 *****************************************************************************/

void ApiRgbMatrixDomain_Test::getReturnsRgbMatrixTypeDetail()
{
    QJsonObject params;
    params.insert(QStringLiteral("functionId"), QString::number(m_matrix->id()));
    QJsonObject reply = sendAndWaitForReply(QStringLiteral("functions.get"), params);
    QVERIFY(reply.value(QStringLiteral("ok")).toBool());
    QJsonObject detail = reply.value(QStringLiteral("result")).toObject().value(QStringLiteral("typeDetail")).toObject();
    QCOMPARE(detail.value(QStringLiteral("functionId")).toString(), QString::number(m_matrix->id()));
    QCOMPARE(int(detail.value(QStringLiteral("docRevision")).toInt()), int(m_doc->docRevision()));

    QJsonObject config = detail.value(QStringLiteral("config")).toObject();
    QCOMPARE(config.value(QStringLiteral("fixtureGroupId")).toString(), QString::number(m_group->id()));
    // RGBMatrix's constructor defaults: the "Stripes" script, red in slot 0, RGB, Normal
    QJsonObject algorithm = config.value(QStringLiteral("algorithm")).toObject();
    QCOMPARE(algorithm.value(QStringLiteral("type")).toString(), QStringLiteral("script"));
    QCOMPARE(algorithm.value(QStringLiteral("scriptName")).toString(), QStringLiteral("Stripes"));
    QJsonArray props = algorithm.value(QStringLiteral("scriptProperties")).toArray();
    QCOMPARE(props.count(), 1);
    QCOMPARE(props.at(0).toObject().value(QStringLiteral("name")).toString(), QStringLiteral("orientation"));
    QVERIFY(props.at(0).toObject().value(QStringLiteral("value")).toString().isEmpty() == false);

    QJsonArray colors = config.value(QStringLiteral("colors")).toArray();
    QCOMPARE(colors.count(), 5);
    QCOMPARE(colors.at(0).toString(), QStringLiteral("#ff0000"));
    QVERIFY(colors.at(1).isNull());
    QCOMPARE(config.value(QStringLiteral("controlMode")).toString(), QStringLiteral("rgb"));
    QCOMPARE(config.value(QStringLiteral("blendMode")).toString(), QStringLiteral("Normal"));
    QCOMPARE(config.value(QStringLiteral("dimmerControl")).toBool(), false);
}

/*****************************************************************************
 * setConfig
 *****************************************************************************/

void ApiRgbMatrixDomain_Test::setConfigAppliesEverythingAndBroadcasts()
{
    FixtureGroup *other = new FixtureGroup(m_doc);
    other->setName(QStringLiteral("Other Group"));
    other->setSize(QSize(3, 3));
    QVERIFY(m_doc->addFixtureGroup(other));

    QJsonObject scriptProp;
    scriptProp.insert(QStringLiteral("name"), QStringLiteral("direction"));
    scriptProp.insert(QStringLiteral("value"), QStringLiteral("Left"));
    QJsonObject algorithm;
    algorithm.insert(QStringLiteral("type"), QStringLiteral("script"));
    algorithm.insert(QStringLiteral("scriptName"), QStringLiteral("Waves"));
    algorithm.insert(QStringLiteral("scriptProperties"), QJsonArray() << scriptProp);

    QJsonObject config;
    config.insert(QStringLiteral("fixtureGroupId"), QString::number(other->id()));
    config.insert(QStringLiteral("algorithm"), algorithm);
    config.insert(QStringLiteral("colors"), QJsonArray() << QStringLiteral("#00ff00") << QStringLiteral("#0000ff")
                                                          << QJsonValue() << QJsonValue() << QJsonValue());
    config.insert(QStringLiteral("controlMode"), QStringLiteral("dimmer"));
    config.insert(QStringLiteral("blendMode"), QStringLiteral("Additive"));
    config.insert(QStringLiteral("dimmerControl"), true);

    QJsonObject params;
    params.insert(QStringLiteral("functionId"), QString::number(m_matrix->id()));
    params.insert(QStringLiteral("config"), config);
    params.insert(QStringLiteral("baseRevision"), int(m_doc->docRevision()));

    quint32 before = m_doc->docRevision();
    QSignalSpy spy(m_client, &QWebSocket::textMessageReceived);
    m_client->sendTextMessage(buildRequest(QStringLiteral("functions.rgbmatrix.setConfig"), params));

    QJsonObject event = waitForEvent(spy, QStringLiteral("functions.rgbmatrix.configChanged"), [](const QJsonObject &) { return true; });
    QVERIFY(event.isEmpty() == false);

    // engine state
    QCOMPARE(m_matrix->fixtureGroup(), other->id());
    QVERIFY(m_matrix->algorithm() != nullptr);
    QCOMPARE(m_matrix->algorithm()->name(), QStringLiteral("Waves"));
    QCOMPARE(m_matrix->property(QStringLiteral("direction")), QStringLiteral("Left"));
    QCOMPARE(m_matrix->getColor(0), QColor(0, 255, 0));
    QCOMPARE(m_matrix->getColor(1), QColor(0, 0, 255));
    QCOMPARE(m_matrix->getColor(2).isValid(), false);
    QCOMPARE(m_matrix->controlMode(), RGBMatrix::ControlModeDimmer);
    QCOMPARE(m_matrix->blendMode(), Universe::AdditiveBlend);
    QCOMPARE(m_matrix->dimmerControl(), true);
    // Every RGBMatrix setter's changed() is turned into Doc::setModified()
    // (doc.cpp, slotFunctionChanged), so one call advances the revision
    // several times - what matters is that it moved and that the response/
    // event carry the final value.
    QVERIFY(m_doc->docRevision() > before);

    // event carries the config read back from the engine
    QJsonObject data = event.value(QStringLiteral("data")).toObject();
    QCOMPARE(data.value(QStringLiteral("functionId")).toString(), QString::number(m_matrix->id()));
    QCOMPARE(int(data.value(QStringLiteral("docRevision")).toInt()), int(m_doc->docRevision()));
    QJsonObject eventConfig = data.value(QStringLiteral("config")).toObject();
    QCOMPARE(eventConfig.value(QStringLiteral("fixtureGroupId")).toString(), QString::number(other->id()));
    QCOMPARE(eventConfig.value(QStringLiteral("algorithm")).toObject().value(QStringLiteral("scriptName")).toString(), QStringLiteral("Waves"));
    QCOMPARE(eventConfig.value(QStringLiteral("colors")).toArray().at(1).toString(), QStringLiteral("#0000ff"));
    QCOMPARE(eventConfig.value(QStringLiteral("controlMode")).toString(), QStringLiteral("dimmer"));
    QCOMPARE(eventConfig.value(QStringLiteral("blendMode")).toString(), QStringLiteral("Additive"));

    // and functions.get agrees
    QJsonObject got = getConfig();
    QCOMPARE(got, eventConfig);
}

void ApiRgbMatrixDomain_Test::setConfigAppliesPartialConfig()
{
    // Only colours: algorithm, group and modes stay exactly as they were.
    QJsonObject config;
    config.insert(QStringLiteral("colors"), QJsonArray() << QStringLiteral("#123456") << QStringLiteral("#abcdef"));

    QJsonObject params;
    params.insert(QStringLiteral("functionId"), QString::number(m_matrix->id()));
    params.insert(QStringLiteral("config"), config);
    params.insert(QStringLiteral("baseRevision"), int(m_doc->docRevision()));
    QJsonObject reply = sendAndWaitForReply(QStringLiteral("functions.rgbmatrix.setConfig"), params);
    QVERIFY(reply.value(QStringLiteral("ok")).toBool());
    QCOMPARE(int(reply.value(QStringLiteral("result")).toObject().value(QStringLiteral("docRevision")).toInt()), int(m_doc->docRevision()));

    QCOMPARE(m_matrix->getColor(0), QColor(QStringLiteral("#123456")));
    QCOMPARE(m_matrix->getColor(1), QColor(QStringLiteral("#abcdef")));
    QCOMPARE(m_matrix->algorithm()->name(), QStringLiteral("Stripes"));
    QCOMPARE(m_matrix->fixtureGroup(), m_group->id());
    QCOMPARE(m_matrix->controlMode(), RGBMatrix::ControlModeRgb);
}

void ApiRgbMatrixDomain_Test::setConfigOnStaleRevisionIsConflict()
{
    QJsonObject config;
    config.insert(QStringLiteral("controlMode"), QStringLiteral("white"));
    QJsonObject params;
    params.insert(QStringLiteral("functionId"), QString::number(m_matrix->id()));
    params.insert(QStringLiteral("config"), config);
    params.insert(QStringLiteral("baseRevision"), int(m_doc->docRevision()) + 7);
    QJsonObject reply = sendAndWaitForReply(QStringLiteral("functions.rgbmatrix.setConfig"), params);
    QCOMPARE(reply.value(QStringLiteral("ok")).toBool(), false);
    QJsonObject error = reply.value(QStringLiteral("error")).toObject();
    QCOMPARE(error.value(QStringLiteral("code")).toString(), QStringLiteral("CONFLICT"));
    QCOMPARE(int(error.value(QStringLiteral("details")).toObject().value(QStringLiteral("docRevision")).toInt()), int(m_doc->docRevision()));
    QCOMPARE(m_matrix->controlMode(), RGBMatrix::ControlModeRgb);
}

void ApiRgbMatrixDomain_Test::setConfigWithUnknownScriptIsInvalidParams()
{
    QJsonObject algorithm;
    algorithm.insert(QStringLiteral("type"), QStringLiteral("script"));
    algorithm.insert(QStringLiteral("scriptName"), QStringLiteral("No Such Script"));
    QJsonObject config;
    config.insert(QStringLiteral("algorithm"), algorithm);
    config.insert(QStringLiteral("controlMode"), QStringLiteral("white"));
    QJsonObject params;
    params.insert(QStringLiteral("functionId"), QString::number(m_matrix->id()));
    params.insert(QStringLiteral("config"), config);
    params.insert(QStringLiteral("baseRevision"), int(m_doc->docRevision()));
    quint32 before = m_doc->docRevision();
    QJsonObject reply = sendAndWaitForReply(QStringLiteral("functions.rgbmatrix.setConfig"), params);
    QCOMPARE(reply.value(QStringLiteral("ok")).toBool(), false);
    QCOMPARE(reply.value(QStringLiteral("error")).toObject().value(QStringLiteral("code")).toString(), QStringLiteral("INVALID_PARAMS"));
    // nothing applied, not even the valid controlMode next to the bad algorithm
    QCOMPARE(m_matrix->algorithm()->name(), QStringLiteral("Stripes"));
    QCOMPARE(m_matrix->controlMode(), RGBMatrix::ControlModeRgb);
    QCOMPARE(m_doc->docRevision(), before);
}

void ApiRgbMatrixDomain_Test::setConfigWithUnknownGroupIsNotFound()
{
    QJsonObject config;
    config.insert(QStringLiteral("fixtureGroupId"), QStringLiteral("4242"));
    QJsonObject params;
    params.insert(QStringLiteral("functionId"), QString::number(m_matrix->id()));
    params.insert(QStringLiteral("config"), config);
    params.insert(QStringLiteral("baseRevision"), int(m_doc->docRevision()));
    QJsonObject reply = sendAndWaitForReply(QStringLiteral("functions.rgbmatrix.setConfig"), params);
    QCOMPARE(reply.value(QStringLiteral("ok")).toBool(), false);
    QCOMPARE(reply.value(QStringLiteral("error")).toObject().value(QStringLiteral("code")).toString(), QStringLiteral("NOT_FOUND"));
    QCOMPARE(m_matrix->fixtureGroup(), m_group->id());
}

void ApiRgbMatrixDomain_Test::setConfigTextAlgorithmRoundTrips()
{
    QJsonObject font;
    font.insert(QStringLiteral("family"), QStringLiteral("Arial"));
    font.insert(QStringLiteral("pointSize"), 9);
    font.insert(QStringLiteral("bold"), true);
    font.insert(QStringLiteral("italic"), false);
    QJsonObject algorithm;
    algorithm.insert(QStringLiteral("type"), QStringLiteral("text"));
    algorithm.insert(QStringLiteral("text"), QStringLiteral("HELLO"));
    algorithm.insert(QStringLiteral("font"), font);
    algorithm.insert(QStringLiteral("animationStyle"), QStringLiteral("vertical"));
    algorithm.insert(QStringLiteral("xOffset"), 2);
    algorithm.insert(QStringLiteral("yOffset"), -1);
    QJsonObject config;
    config.insert(QStringLiteral("algorithm"), algorithm);

    QJsonObject params;
    params.insert(QStringLiteral("functionId"), QString::number(m_matrix->id()));
    params.insert(QStringLiteral("config"), config);
    params.insert(QStringLiteral("baseRevision"), int(m_doc->docRevision()));
    QJsonObject reply = sendAndWaitForReply(QStringLiteral("functions.rgbmatrix.setConfig"), params);
    QVERIFY(reply.value(QStringLiteral("ok")).toBool());

    QVERIFY(m_matrix->algorithm() != nullptr);
    QCOMPARE(m_matrix->algorithm()->type(), RGBAlgorithm::Text);
    RGBText *text = static_cast<RGBText *>(m_matrix->algorithm());
    QCOMPARE(text->text(), QStringLiteral("HELLO"));
    QCOMPARE(text->font().family(), QStringLiteral("Arial"));
    QCOMPARE(text->font().pointSize(), 9);
    QCOMPARE(text->font().bold(), true);
    QCOMPARE(text->animationStyle(), RGBText::Vertical);
    QCOMPARE(text->xOffset(), 2);
    QCOMPARE(text->yOffset(), -1);

    QJsonObject got = getConfig().value(QStringLiteral("algorithm")).toObject();
    QCOMPARE(got.value(QStringLiteral("type")).toString(), QStringLiteral("text"));
    QCOMPARE(got.value(QStringLiteral("text")).toString(), QStringLiteral("HELLO"));
    QCOMPARE(got.value(QStringLiteral("font")).toObject().value(QStringLiteral("family")).toString(), QStringLiteral("Arial"));
    QCOMPARE(got.value(QStringLiteral("font")).toObject().value(QStringLiteral("bold")).toBool(), true);
    QCOMPARE(got.value(QStringLiteral("animationStyle")).toString(), QStringLiteral("vertical"));
    QCOMPARE(got.value(QStringLiteral("xOffset")).toInt(), 2);
    QCOMPARE(got.value(QStringLiteral("yOffset")).toInt(), -1);

    // a second call touching only the text keeps the font
    QJsonObject algorithm2;
    algorithm2.insert(QStringLiteral("type"), QStringLiteral("text"));
    algorithm2.insert(QStringLiteral("text"), QStringLiteral("BYE"));
    QJsonObject config2;
    config2.insert(QStringLiteral("algorithm"), algorithm2);
    params.insert(QStringLiteral("config"), config2);
    params.insert(QStringLiteral("baseRevision"), int(m_doc->docRevision()));
    reply = sendAndWaitForReply(QStringLiteral("functions.rgbmatrix.setConfig"), params);
    QVERIFY(reply.value(QStringLiteral("ok")).toBool());
    QCOMPARE(static_cast<RGBText *>(m_matrix->algorithm())->text(), QStringLiteral("BYE"));
    QCOMPARE(static_cast<RGBText *>(m_matrix->algorithm())->font().family(), QStringLiteral("Arial"));
    QCOMPARE(static_cast<RGBText *>(m_matrix->algorithm())->animationStyle(), RGBText::Vertical);
}

/*****************************************************************************
 * setScriptProperty
 *****************************************************************************/

void ApiRgbMatrixDomain_Test::setScriptPropertyChangesValueAndBroadcasts()
{
    QString clientId = helloAndGetClientId();
    QCOMPARE(m_matrix->property(QStringLiteral("orientation")), QStringLiteral("Horizontal"));

    QJsonObject params;
    params.insert(QStringLiteral("functionId"), QString::number(m_matrix->id()));
    params.insert(QStringLiteral("propertyName"), QStringLiteral("orientation"));
    params.insert(QStringLiteral("value"), QStringLiteral("Vertical"));
    params.insert(QStringLiteral("baseRevision"), int(m_doc->docRevision()));

    quint32 before = m_doc->docRevision();
    QSignalSpy spy(m_client, &QWebSocket::textMessageReceived);
    m_client->sendTextMessage(buildRequest(QStringLiteral("functions.rgbmatrix.setScriptProperty"), params));
    QJsonObject event = waitForEvent(spy, QStringLiteral("functions.rgbmatrix.scriptPropertyChanged"), [](const QJsonObject &) { return true; });
    QVERIFY(event.isEmpty() == false);

    QCOMPARE(m_matrix->property(QStringLiteral("orientation")), QStringLiteral("Vertical"));
    QVERIFY(m_doc->docRevision() > before);
    QJsonObject data = event.value(QStringLiteral("data")).toObject();
    QCOMPARE(data.value(QStringLiteral("functionId")).toString(), QString::number(m_matrix->id()));
    QCOMPARE(data.value(QStringLiteral("propertyName")).toString(), QStringLiteral("orientation"));
    QCOMPARE(data.value(QStringLiteral("value")).toString(), QStringLiteral("Vertical"));
    QCOMPARE(int(data.value(QStringLiteral("docRevision")).toInt()), int(m_doc->docRevision()));
    QCOMPARE(event.value(QStringLiteral("originClientId")).toString(), clientId);

    QJsonArray props = getConfig().value(QStringLiteral("algorithm")).toObject().value(QStringLiteral("scriptProperties")).toArray();
    QCOMPARE(propertyByName(props, QStringLiteral("orientation")).value(QStringLiteral("value")).toString(), QStringLiteral("Vertical"));
}

void ApiRgbMatrixDomain_Test::setScriptPropertyOnUnknownPropertyIsInvalidParams()
{
    QJsonObject params;
    params.insert(QStringLiteral("functionId"), QString::number(m_matrix->id()));
    params.insert(QStringLiteral("propertyName"), QStringLiteral("bogus"));
    params.insert(QStringLiteral("value"), QStringLiteral("1"));
    params.insert(QStringLiteral("baseRevision"), int(m_doc->docRevision()));
    quint32 before = m_doc->docRevision();
    QJsonObject reply = sendAndWaitForReply(QStringLiteral("functions.rgbmatrix.setScriptProperty"), params);
    QCOMPARE(reply.value(QStringLiteral("ok")).toBool(), false);
    QCOMPARE(reply.value(QStringLiteral("error")).toObject().value(QStringLiteral("code")).toString(), QStringLiteral("INVALID_PARAMS"));
    QCOMPARE(m_doc->docRevision(), before);
    // never cached into the matrix's property map (which would be saved to XML)
    QVERIFY(m_matrix->property(QStringLiteral("bogus")).isEmpty());
}

void ApiRgbMatrixDomain_Test::setScriptPropertyOnBuiltinAlgorithmIsInvalidParams()
{
    m_matrix->setAlgorithm(RGBAlgorithm::algorithm(m_doc, QStringLiteral("Plain Color")));
    QJsonObject params;
    params.insert(QStringLiteral("functionId"), QString::number(m_matrix->id()));
    params.insert(QStringLiteral("propertyName"), QStringLiteral("orientation"));
    params.insert(QStringLiteral("value"), QStringLiteral("Vertical"));
    params.insert(QStringLiteral("baseRevision"), int(m_doc->docRevision()));
    QJsonObject reply = sendAndWaitForReply(QStringLiteral("functions.rgbmatrix.setScriptProperty"), params);
    QCOMPARE(reply.value(QStringLiteral("ok")).toBool(), false);
    QCOMPARE(reply.value(QStringLiteral("error")).toObject().value(QStringLiteral("code")).toString(), QStringLiteral("INVALID_PARAMS"));
}

/*****************************************************************************
 * getPreview
 *****************************************************************************/

void ApiRgbMatrixDomain_Test::getPreviewRendersPlainColour()
{
    m_matrix->setAlgorithm(RGBAlgorithm::algorithm(m_doc, QStringLiteral("Plain Color")));
    m_matrix->setColor(0, QColor(0x12, 0x34, 0x56));

    QJsonObject params;
    params.insert(QStringLiteral("functionId"), QString::number(m_matrix->id()));
    params.insert(QStringLiteral("step"), 0);
    QJsonObject reply = sendAndWaitForReply(QStringLiteral("functions.rgbmatrix.getPreview"), params);
    QVERIFY(reply.value(QStringLiteral("ok")).toBool());
    QJsonObject result = reply.value(QStringLiteral("result")).toObject();
    QCOMPARE(result.value(QStringLiteral("stepsCount")).toInt(), 1);
    QCOMPARE(result.value(QStringLiteral("step")).toInt(), 0);
    QCOMPARE(result.value(QStringLiteral("width")).toInt(), 4);
    QCOMPARE(result.value(QStringLiteral("height")).toInt(), 2);
    QJsonArray pixels = result.value(QStringLiteral("pixels")).toArray();
    QCOMPARE(pixels.count(), 2);
    for (const QJsonValue &rowValue : pixels)
    {
        QJsonArray row = rowValue.toArray();
        QCOMPARE(row.count(), 4);
        for (const QJsonValue &px : row)
            QCOMPARE(px.toInt(), 0x123456);
    }
}

void ApiRgbMatrixDomain_Test::getPreviewWrapsStepAndInterpolatesColour()
{
    // Stripes (the default) on 4x2 has more than one step; with Color1 and
    // Color2 set, the frame colour interpolates between them per step
    // (RGBMatrixStep::updateStepColor), so a wrapped step index must render
    // the same frame as its canonical index.
    m_matrix->setColor(0, QColor(255, 0, 0));
    m_matrix->setColor(1, QColor(0, 0, 255));

    QJsonObject params;
    params.insert(QStringLiteral("functionId"), QString::number(m_matrix->id()));
    params.insert(QStringLiteral("step"), 1);
    QJsonObject first = sendAndWaitForReply(QStringLiteral("functions.rgbmatrix.getPreview"), params).value(QStringLiteral("result")).toObject();
    int stepsCount = first.value(QStringLiteral("stepsCount")).toInt();
    QVERIFY(stepsCount > 1);

    params.insert(QStringLiteral("step"), 1 + stepsCount);
    QJsonObject wrapped = sendAndWaitForReply(QStringLiteral("functions.rgbmatrix.getPreview"), params).value(QStringLiteral("result")).toObject();
    QCOMPARE(wrapped.value(QStringLiteral("step")).toInt(), 1);
    QCOMPARE(wrapped.value(QStringLiteral("pixels")), first.value(QStringLiteral("pixels")));

    // the last step is rendered in (or towards) Color2, the first in Color1
    params.insert(QStringLiteral("step"), 0);
    QJsonArray firstRow = sendAndWaitForReply(QStringLiteral("functions.rgbmatrix.getPreview"), params)
                              .value(QStringLiteral("result")).toObject().value(QStringLiteral("pixels")).toArray().at(0).toArray();
    params.insert(QStringLiteral("step"), stepsCount - 1);
    QJsonArray lastRow = sendAndWaitForReply(QStringLiteral("functions.rgbmatrix.getPreview"), params)
                             .value(QStringLiteral("result")).toObject().value(QStringLiteral("pixels")).toArray().at(0).toArray();
    int litFirst = 0, litLast = 0;
    for (const QJsonValue &px : firstRow)
        if (px.toInt() != 0) { litFirst = px.toInt(); break; }
    for (const QJsonValue &px : lastRow)
        if (px.toInt() != 0) { litLast = px.toInt(); break; }
    QCOMPARE(litFirst, 0xff0000);
    QCOMPARE(litLast, 0x0000ff);
}

void ApiRgbMatrixDomain_Test::getPreviewWithoutGroupIsEmpty()
{
    m_matrix->setFixtureGroup(FixtureGroup::invalidId());
    QJsonObject params;
    params.insert(QStringLiteral("functionId"), QString::number(m_matrix->id()));
    params.insert(QStringLiteral("step"), 3);
    QJsonObject reply = sendAndWaitForReply(QStringLiteral("functions.rgbmatrix.getPreview"), params);
    QVERIFY(reply.value(QStringLiteral("ok")).toBool());
    QJsonObject result = reply.value(QStringLiteral("result")).toObject();
    QCOMPARE(result.value(QStringLiteral("stepsCount")).toInt(), 0);
    QCOMPARE(result.value(QStringLiteral("step")).toInt(), 0); // normalised even when there is nothing to render
    QCOMPARE(result.value(QStringLiteral("width")).toInt(), 0);
    QCOMPARE(result.value(QStringLiteral("height")).toInt(), 0);
    QCOMPARE(result.value(QStringLiteral("pixels")).toArray().count(), 0);

    // and functions.get reports the unbound group as null, not a sentinel id
    QVERIFY(getConfig().value(QStringLiteral("fixtureGroupId")).isNull());
}

QTEST_MAIN(ApiRgbMatrixDomain_Test)
