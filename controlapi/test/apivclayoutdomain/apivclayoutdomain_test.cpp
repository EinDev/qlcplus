/*
  Q Light Controller Plus - Control API unit test
  apivclayoutdomain_test.cpp

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

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonValue>
#include <QSignalSpy>
#include <QWebSocket>
#include <QtTest>

#include "apivclayoutdomain_test.h"
#include "apiserver.h"
#include "chaser.h"
#include "doc.h"
#include "fakevchost.h"
#include "fixture.h"
#include "scene.h"

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

static QStringList idsOf(const QJsonArray &arr)
{
    QStringList out;
    for (const QJsonValue &v : arr)
        out.append(v.toString());
    return out;
}

void ApiVcLayoutDomain_Test::init()
{
    m_doc = new Doc(nullptr);
    m_vcHost = new FakeVcHost();
    m_apiServer = new ApiServer(m_vcHost, m_doc);
    QVERIFY(m_apiServer->listen(0, QHostAddress::LocalHost));

    m_client = new QWebSocket();
    m_client->open(QUrl(QStringLiteral("ws://127.0.0.1:%1/qlcplusapi").arg(m_apiServer->serverPort())));
    QVERIFY(QTest::qWaitFor([this]() { return m_client->state() == QAbstractSocket::ConnectedState; }, 2000));
}

void ApiVcLayoutDomain_Test::cleanup()
{
    delete m_client;
    m_client = nullptr;
    delete m_apiServer;
    m_apiServer = nullptr;
    delete m_vcHost;
    m_vcHost = nullptr;
    delete m_doc;
    m_doc = nullptr;
}

QJsonObject ApiVcLayoutDomain_Test::sendAndWaitForReply(const QString &method, const QJsonObject &params, const QString &requestId)
{
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

QString ApiVcLayoutDomain_Test::helloAndGetClientId()
{
    QJsonObject reply = sendAndWaitForReply(QStringLiteral("hello"), QJsonObject(), QStringLiteral("t-hello"));
    return resultOf(reply).value(QStringLiteral("clientId")).toString();
}

int ApiVcLayoutDomain_Test::currentDocRevision()
{
    QJsonObject reply = sendAndWaitForReply(QStringLiteral("hello"), QJsonObject(), QStringLiteral("t-rev"));
    return resultOf(reply).value(QStringLiteral("docRevision")).toInt();
}

QJsonObject ApiVcLayoutDomain_Test::geometry(double x, double y, double width, double height)
{
    QJsonObject g;
    g.insert(QStringLiteral("x"), x); g.insert(QStringLiteral("y"), y);
    g.insert(QStringLiteral("width"), width); g.insert(QStringLiteral("height"), height);
    return g;
}

QJsonObject ApiVcLayoutDomain_Test::widgetParams(const QString &widgetId, const QJsonObject &extra)
{
    QJsonObject p = extra;
    p.insert(QStringLiteral("widgetId"), widgetId);
    return p;
}

QString ApiVcLayoutDomain_Test::createWidget(const QString &widgetType, const QJsonObject &typeConfig,
                                             const QJsonObject &geom, const QString &parentId)
{
    QJsonObject params;
    params.insert(QStringLiteral("widgetType"), widgetType);
    params.insert(QStringLiteral("page"), 0);
    params.insert(QStringLiteral("geometry"), geom.isEmpty() ? geometry(0, 0, 10, 10) : geom);
    if (typeConfig.isEmpty() == false)
        params.insert(QStringLiteral("typeConfig"), typeConfig);
    if (parentId.isEmpty() == false)
        params.insert(QStringLiteral("parentId"), parentId);
    params.insert(QStringLiteral("baseRevision"), currentDocRevision());
    QJsonObject reply = sendAndWaitForReply(QStringLiteral("vc.widget.create"), params, QStringLiteral("t-create"));
    return resultOf(reply).value(QStringLiteral("widgetId")).toString();
}

QJsonObject ApiVcLayoutDomain_Test::geometryOf(const QString &widgetId)
{
    QJsonObject reply = sendAndWaitForReply(QStringLiteral("vc.widget.get"), widgetParams(widgetId), QStringLiteral("t-geom"));
    return resultOf(reply).value(QStringLiteral("geometry")).toObject();
}

/*****************************************************************************
 * vc.frame.*
 *****************************************************************************/

void ApiVcLayoutDomain_Test::frameSetPinRequiresCurrentPinAndBroadcasts()
{
    QString clientId = helloAndGetClientId();
    QString frameId = createWidget(QStringLiteral("Frame"));
    QString buttonId = createWidget(QStringLiteral("Button"));

    // Set a PIN on a frame without one
    QSignalSpy spy(m_client, &QWebSocket::textMessageReceived);
    QJsonObject set; set.insert(QStringLiteral("newPIN"), QStringLiteral("1234")); set.insert(QStringLiteral("baseRevision"), currentDocRevision());
    QJsonObject reply = sendAndWaitForReply(QStringLiteral("vc.frame.setPin"), widgetParams(frameId, set), QStringLiteral("t-p1"));
    QCOMPARE(isOk(reply), true);
    QCOMPARE(resultOf(reply).value(QStringLiteral("docRevision")).toInt(), int(m_doc->docRevision()));

    QList<QJsonObject> events = eventsWithTopic(spy, QStringLiteral("vc.widget.configChanged"));
    QCOMPARE(events.size(), 1);
    QJsonObject widget = events.at(0).value(QStringLiteral("widget")).toObject();
    QCOMPARE(widget.value(QStringLiteral("id")).toString(), frameId);
    QCOMPARE(widget.value(QStringLiteral("typeConfig")).toObject().value(QStringLiteral("hasPin")).toBool(), true);
    QCOMPARE(events.at(0).value(QStringLiteral("_origin")).toString(), clientId);

    // Changing it needs the current one
    QJsonObject wrong; wrong.insert(QStringLiteral("currentPIN"), QStringLiteral("0000")); wrong.insert(QStringLiteral("newPIN"), QStringLiteral("5678"));
    wrong.insert(QStringLiteral("baseRevision"), currentDocRevision());
    QCOMPARE(errorCode(sendAndWaitForReply(QStringLiteral("vc.frame.setPin"), widgetParams(frameId, wrong), QStringLiteral("t-p2"))), QStringLiteral("INVALID_PARAMS"));

    QJsonObject right; right.insert(QStringLiteral("currentPIN"), QStringLiteral("1234")); right.insert(QStringLiteral("newPIN"), QString());
    right.insert(QStringLiteral("baseRevision"), currentDocRevision());
    QCOMPARE(isOk(sendAndWaitForReply(QStringLiteral("vc.frame.setPin"), widgetParams(frameId, right), QStringLiteral("t-p3"))), true);
    QJsonObject get = sendAndWaitForReply(QStringLiteral("vc.widget.get"), widgetParams(frameId), QStringLiteral("t-p4"));
    QCOMPARE(resultOf(get).value(QStringLiteral("typeConfig")).toObject().value(QStringLiteral("hasPin")).toBool(), false);

    // Shape validation: 4 digits, Frame/SoloFrame only, unknown widget
    QJsonObject bad; bad.insert(QStringLiteral("newPIN"), QStringLiteral("12")); bad.insert(QStringLiteral("baseRevision"), currentDocRevision());
    QCOMPARE(errorCode(sendAndWaitForReply(QStringLiteral("vc.frame.setPin"), widgetParams(frameId, bad), QStringLiteral("t-p5"))), QStringLiteral("INVALID_PARAMS"));
    QJsonObject letters; letters.insert(QStringLiteral("newPIN"), QStringLiteral("12ab")); letters.insert(QStringLiteral("baseRevision"), currentDocRevision());
    QCOMPARE(errorCode(sendAndWaitForReply(QStringLiteral("vc.frame.setPin"), widgetParams(frameId, letters), QStringLiteral("t-p6"))), QStringLiteral("INVALID_PARAMS"));
    set.insert(QStringLiteral("baseRevision"), currentDocRevision());
    QCOMPARE(errorCode(sendAndWaitForReply(QStringLiteral("vc.frame.setPin"), widgetParams(buttonId, set), QStringLiteral("t-p7"))), QStringLiteral("INVALID_PARAMS"));
    QCOMPARE(errorCode(sendAndWaitForReply(QStringLiteral("vc.frame.setPin"), widgetParams(QStringLiteral("999"), set), QStringLiteral("t-p8"))), QStringLiteral("NOT_FOUND"));
}

void ApiVcLayoutDomain_Test::frameValidatePinChecksValue()
{
    helloAndGetClientId();
    QString frameId = createWidget(QStringLiteral("SoloFrame"));

    QJsonObject any; any.insert(QStringLiteral("pin"), QStringLiteral("0000"));
    QJsonObject reply = sendAndWaitForReply(QStringLiteral("vc.frame.validatePin"), widgetParams(frameId, any), QStringLiteral("t-v1"));
    QCOMPARE(isOk(reply), true);
    QCOMPARE(resultOf(reply).value(QStringLiteral("valid")).toBool(), true); // no PIN: everything validates

    QJsonObject set; set.insert(QStringLiteral("newPIN"), QStringLiteral("4321")); set.insert(QStringLiteral("baseRevision"), currentDocRevision());
    QCOMPARE(isOk(sendAndWaitForReply(QStringLiteral("vc.frame.setPin"), widgetParams(frameId, set), QStringLiteral("t-v2"))), true);
    int rev = currentDocRevision();

    QCOMPARE(resultOf(sendAndWaitForReply(QStringLiteral("vc.frame.validatePin"), widgetParams(frameId, any), QStringLiteral("t-v3"))).value(QStringLiteral("valid")).toBool(), false);
    QJsonObject good; good.insert(QStringLiteral("pin"), QStringLiteral("4321"));
    QCOMPARE(resultOf(sendAndWaitForReply(QStringLiteral("vc.frame.validatePin"), widgetParams(frameId, good), QStringLiteral("t-v4"))).value(QStringLiteral("valid")).toBool(), true);
    // Live: no revision bump
    QCOMPARE(currentDocRevision(), rev);
    QCOMPARE(errorCode(sendAndWaitForReply(QStringLiteral("vc.frame.validatePin"), widgetParams(QStringLiteral("999"), good), QStringLiteral("t-v5"))), QStringLiteral("NOT_FOUND"));
}

void ApiVcLayoutDomain_Test::frameCloneFirstPageCreatesCopies()
{
    helloAndGetClientId();
    QJsonObject cfg; cfg.insert(QStringLiteral("multiPageMode"), true); cfg.insert(QStringLiteral("totalPagesNumber"), 3);
    QString frameId = createWidget(QStringLiteral("Frame"), cfg, geometry(0, 0, 300, 200));
    QString singleId = createWidget(QStringLiteral("Frame"));
    QString b1 = createWidget(QStringLiteral("Button"), QJsonObject(), geometry(5, 5, 20, 20), frameId);
    QString b2 = createWidget(QStringLiteral("Button"), QJsonObject(), geometry(30, 5, 20, 20), frameId);

    QSignalSpy spy(m_client, &QWebSocket::textMessageReceived);
    QJsonObject p; p.insert(QStringLiteral("baseRevision"), currentDocRevision());
    QJsonObject reply = sendAndWaitForReply(QStringLiteral("vc.frame.cloneFirstPage"), widgetParams(frameId, p), QStringLiteral("t-c1"));
    QCOMPARE(isOk(reply), true);

    // 2 children x 2 further pages = 4 copies, all under the frame; reported on bulkUpdated with the frame first
    QList<QJsonObject> events = eventsWithTopic(spy, QStringLiteral("vc.widget.bulkUpdated"));
    QCOMPARE(events.size(), 1);
    QJsonArray widgets = events.at(0).value(QStringLiteral("widgets")).toArray();
    QCOMPARE(widgets.size(), 5);
    QCOMPARE(widgets.at(0).toObject().value(QStringLiteral("id")).toString(), frameId);
    for (int i = 1; i < widgets.size(); i++)
        QCOMPARE(widgets.at(i).toObject().value(QStringLiteral("parentId")).toString(), frameId);

    QJsonObject listParams; listParams.insert(QStringLiteral("parentId"), frameId);
    QJsonObject list = sendAndWaitForReply(QStringLiteral("vc.widget.list"), listParams, QStringLiteral("t-c2"));
    QCOMPARE(resultOf(list).value(QStringLiteral("widgets")).toArray().size(), 6);

    // A single-page frame has nothing to clone onto
    p.insert(QStringLiteral("baseRevision"), currentDocRevision());
    QCOMPARE(errorCode(sendAndWaitForReply(QStringLiteral("vc.frame.cloneFirstPage"), widgetParams(singleId, p), QStringLiteral("t-c3"))), QStringLiteral("INVALID_STATE"));
    QCOMPARE(errorCode(sendAndWaitForReply(QStringLiteral("vc.frame.cloneFirstPage"), widgetParams(b1, p), QStringLiteral("t-c4"))), QStringLiteral("INVALID_PARAMS"));
    Q_UNUSED(b2)
}

/*****************************************************************************
 * vc.slider.*
 *****************************************************************************/

void ApiVcLayoutDomain_Test::sliderSetLevelChannelsValidatesFixturesAndChannels()
{
    helloAndGetClientId();
    Fixture *fixture = new Fixture(m_doc);
    fixture->setName(QStringLiteral("Dimmer"));
    fixture->setChannels(4);
    fixture->setAddress(0);
    fixture->setUniverse(0);
    QVERIFY(m_doc->addFixture(fixture));
    QString fixtureId = QString::number(fixture->id());

    QJsonObject cfg; cfg.insert(QStringLiteral("sliderMode"), QStringLiteral("Level"));
    QString sliderId = createWidget(QStringLiteral("Slider"), cfg);
    QString buttonId = createWidget(QStringLiteral("Button"));

    auto entry = [](const QString &fid, int ch) { QJsonObject e; e.insert(QStringLiteral("fixtureId"), fid); e.insert(QStringLiteral("channel"), ch); return e; };

    QSignalSpy spy(m_client, &QWebSocket::textMessageReceived);
    QJsonObject p;
    p.insert(QStringLiteral("channels"), QJsonArray({ entry(fixtureId, 0), entry(fixtureId, 3), entry(fixtureId, 0) }));
    p.insert(QStringLiteral("baseRevision"), currentDocRevision());
    QJsonObject reply = sendAndWaitForReply(QStringLiteral("vc.slider.setLevelChannels"), widgetParams(sliderId, p), QStringLiteral("t-l1"));
    QCOMPARE(isOk(reply), true);

    QJsonObject get = sendAndWaitForReply(QStringLiteral("vc.widget.get"), widgetParams(sliderId), QStringLiteral("t-l2"));
    QJsonArray channels = resultOf(get).value(QStringLiteral("typeConfig")).toObject().value(QStringLiteral("levelChannels")).toArray();
    QCOMPARE(channels.size(), 2); // duplicate collapsed
    QCOMPARE(channels.at(0).toObject().value(QStringLiteral("fixtureId")).toString(), fixtureId);
    QCOMPARE(channels.at(1).toObject().value(QStringLiteral("channel")).toInt(), 3);
    QCOMPARE(eventsWithTopic(spy, QStringLiteral("vc.widget.configChanged")).size(), 1);

    // Unknown fixture, channel out of range, non-array, wrong widget type
    p.insert(QStringLiteral("channels"), QJsonArray({ entry(QStringLiteral("999"), 0) }));
    p.insert(QStringLiteral("baseRevision"), currentDocRevision());
    QCOMPARE(errorCode(sendAndWaitForReply(QStringLiteral("vc.slider.setLevelChannels"), widgetParams(sliderId, p), QStringLiteral("t-l3"))), QStringLiteral("NOT_FOUND"));
    p.insert(QStringLiteral("channels"), QJsonArray({ entry(fixtureId, 4) }));
    QJsonObject bad = sendAndWaitForReply(QStringLiteral("vc.slider.setLevelChannels"), widgetParams(sliderId, p), QStringLiteral("t-l4"));
    QCOMPARE(errorCode(bad), QStringLiteral("INVALID_PARAMS"));
    QCOMPARE(bad.value(QStringLiteral("error")).toObject().value(QStringLiteral("details")).toObject().value(QStringLiteral("channels")).toInt(), 4);
    p.insert(QStringLiteral("channels"), QStringLiteral("nope"));
    QCOMPARE(errorCode(sendAndWaitForReply(QStringLiteral("vc.slider.setLevelChannels"), widgetParams(sliderId, p), QStringLiteral("t-l5"))), QStringLiteral("INVALID_PARAMS"));
    p.insert(QStringLiteral("channels"), QJsonArray());
    QCOMPARE(errorCode(sendAndWaitForReply(QStringLiteral("vc.slider.setLevelChannels"), widgetParams(buttonId, p), QStringLiteral("t-l6"))), QStringLiteral("INVALID_PARAMS"));

    // Nothing above changed the list
    get = sendAndWaitForReply(QStringLiteral("vc.widget.get"), widgetParams(sliderId), QStringLiteral("t-l7"));
    QCOMPARE(resultOf(get).value(QStringLiteral("typeConfig")).toObject().value(QStringLiteral("levelChannels")).toArray().size(), 2);

    // Empty list clears
    QCOMPARE(isOk(sendAndWaitForReply(QStringLiteral("vc.slider.setLevelChannels"), widgetParams(sliderId, p), QStringLiteral("t-l8"))), true);
    get = sendAndWaitForReply(QStringLiteral("vc.widget.get"), widgetParams(sliderId), QStringLiteral("t-l9"));
    QCOMPARE(resultOf(get).value(QStringLiteral("typeConfig")).toObject().value(QStringLiteral("levelChannels")).toArray().size(), 0);
}

void ApiVcLayoutDomain_Test::sliderFlashRequiresAdjustModeWithFlashButton()
{
    helloAndGetClientId();
    QJsonObject level; level.insert(QStringLiteral("sliderMode"), QStringLiteral("Level"));
    QString levelId = createWidget(QStringLiteral("Slider"), level);
    QJsonObject adjust;
    adjust.insert(QStringLiteral("sliderMode"), QStringLiteral("Adjust"));
    adjust.insert(QStringLiteral("adjustFlashEnabled"), true);
    adjust.insert(QStringLiteral("controlledFunction"), QStringLiteral("7"));
    QString adjustId = createWidget(QStringLiteral("Slider"), adjust);
    QJsonObject noFlash = adjust; noFlash.remove(QStringLiteral("adjustFlashEnabled"));
    QString noFlashId = createWidget(QStringLiteral("Slider"), noFlash);
    int rev = currentDocRevision();

    QJsonObject on; on.insert(QStringLiteral("on"), true);
    QJsonObject off; off.insert(QStringLiteral("on"), false);
    QCOMPARE(isOk(sendAndWaitForReply(QStringLiteral("vc.slider.flash"), widgetParams(adjustId, on), QStringLiteral("t-f1"))), true);
    QCOMPARE(isOk(sendAndWaitForReply(QStringLiteral("vc.slider.flash"), widgetParams(adjustId, off), QStringLiteral("t-f2"))), true);
    QCOMPARE(errorCode(sendAndWaitForReply(QStringLiteral("vc.slider.flash"), widgetParams(levelId, on), QStringLiteral("t-f3"))), QStringLiteral("INVALID_STATE"));
    QCOMPARE(errorCode(sendAndWaitForReply(QStringLiteral("vc.slider.flash"), widgetParams(noFlashId, on), QStringLiteral("t-f4"))), QStringLiteral("INVALID_STATE"));
    QJsonObject notBool; notBool.insert(QStringLiteral("on"), 1);
    QCOMPARE(errorCode(sendAndWaitForReply(QStringLiteral("vc.slider.flash"), widgetParams(adjustId, notBool), QStringLiteral("t-f5"))), QStringLiteral("INVALID_PARAMS"));
    QCOMPARE(errorCode(sendAndWaitForReply(QStringLiteral("vc.slider.flash"), widgetParams(QStringLiteral("999"), on), QStringLiteral("t-f6"))), QStringLiteral("NOT_FOUND"));

    // Disabled widgets refuse live input
    QJsonObject disable; disable.insert(QStringLiteral("isDisabled"), true); disable.insert(QStringLiteral("baseRevision"), currentDocRevision());
    QCOMPARE(isOk(sendAndWaitForReply(QStringLiteral("vc.widget.update"), widgetParams(adjustId, disable), QStringLiteral("t-f7"))), true);
    QCOMPARE(errorCode(sendAndWaitForReply(QStringLiteral("vc.slider.flash"), widgetParams(adjustId, on), QStringLiteral("t-f8"))), QStringLiteral("INVALID_STATE"));

    // Live: the flashes themselves never bumped the revision (only the update above did)
    QCOMPARE(currentDocRevision(), rev + 1);
}

/*****************************************************************************
 * vc.widget.align / distribute / bulkStyle
 *****************************************************************************/

void ApiVcLayoutDomain_Test::widgetAlignMovesToReference()
{
    QString clientId = helloAndGetClientId();
    QString ref = createWidget(QStringLiteral("Button"), QJsonObject(), geometry(100, 100, 50, 50));
    QString a = createWidget(QStringLiteral("Button"), QJsonObject(), geometry(10, 20, 30, 40));
    QString b = createWidget(QStringLiteral("Slider"), QJsonObject(), geometry(200, 300, 20, 100));

    auto align = [&](const QString &alignment, const QString &reqId)
    {
        QJsonObject p;
        p.insert(QStringLiteral("widgetIds"), QJsonArray({ a, b, ref }));
        p.insert(QStringLiteral("referenceWidgetId"), ref);
        p.insert(QStringLiteral("alignment"), alignment);
        p.insert(QStringLiteral("baseRevision"), currentDocRevision());
        return sendAndWaitForReply(QStringLiteral("vc.widget.align"), p, reqId);
    };

    QSignalSpy spy(m_client, &QWebSocket::textMessageReceived);
    QCOMPARE(isOk(align(QStringLiteral("left"), QStringLiteral("t-a1"))), true);
    QCOMPARE(geometryOf(a).value(QStringLiteral("x")).toDouble(), 100.0);
    QCOMPARE(geometryOf(a).value(QStringLiteral("y")).toDouble(), 20.0); // untouched axis
    QCOMPARE(geometryOf(b).value(QStringLiteral("x")).toDouble(), 100.0);
    QCOMPARE(geometryOf(ref).value(QStringLiteral("x")).toDouble(), 100.0); // the reference never moves

    QList<QJsonObject> events = eventsWithTopic(spy, QStringLiteral("vc.widget.bulkUpdated"));
    QCOMPARE(events.size(), 1);
    QCOMPARE(events.at(0).value(QStringLiteral("widgets")).toArray().size(), 3);
    QCOMPARE(events.at(0).value(QStringLiteral("_origin")).toString(), clientId);
    QCOMPARE(events.at(0).value(QStringLiteral("docRevision")).toInt(), int(m_doc->docRevision()));

    QCOMPARE(isOk(align(QStringLiteral("right"), QStringLiteral("t-a2"))), true);
    QCOMPARE(geometryOf(a).value(QStringLiteral("x")).toDouble(), 120.0); // 150 - 30
    QCOMPARE(geometryOf(b).value(QStringLiteral("x")).toDouble(), 130.0); // 150 - 20

    QCOMPARE(isOk(align(QStringLiteral("top"), QStringLiteral("t-a3"))), true);
    QCOMPARE(geometryOf(a).value(QStringLiteral("y")).toDouble(), 100.0);
    QCOMPARE(geometryOf(b).value(QStringLiteral("y")).toDouble(), 100.0);

    QCOMPARE(isOk(align(QStringLiteral("bottom"), QStringLiteral("t-a4"))), true);
    QCOMPARE(geometryOf(a).value(QStringLiteral("y")).toDouble(), 110.0); // 150 - 40
    QCOMPARE(geometryOf(b).value(QStringLiteral("y")).toDouble(), 50.0);  // 150 - 100

    QCOMPARE(isOk(align(QStringLiteral("hcenter"), QStringLiteral("t-a5"))), true);
    QCOMPARE(geometryOf(a).value(QStringLiteral("x")).toDouble(), 110.0); // 125 - 15
    QCOMPARE(geometryOf(b).value(QStringLiteral("x")).toDouble(), 115.0); // 125 - 10

    QCOMPARE(isOk(align(QStringLiteral("vcenter"), QStringLiteral("t-a6"))), true);
    QCOMPARE(geometryOf(a).value(QStringLiteral("y")).toDouble(), 105.0); // 125 - 20
    QCOMPARE(geometryOf(b).value(QStringLiteral("y")).toDouble(), 75.0);  // 125 - 50
    QCOMPARE(geometryOf(a).value(QStringLiteral("width")).toDouble(), 30.0); // sizes never change
}

void ApiVcLayoutDomain_Test::widgetAlignRejectsMixedParentsAndBadAlignment()
{
    helloAndGetClientId();
    QString frameId = createWidget(QStringLiteral("Frame"), QJsonObject(), geometry(0, 0, 300, 300));
    QString inside = createWidget(QStringLiteral("Button"), QJsonObject(), geometry(10, 10, 20, 20), frameId);
    QString outside = createWidget(QStringLiteral("Button"), QJsonObject(), geometry(400, 10, 20, 20));

    QJsonObject p;
    p.insert(QStringLiteral("widgetIds"), QJsonArray({ inside, outside }));
    p.insert(QStringLiteral("referenceWidgetId"), outside);
    p.insert(QStringLiteral("alignment"), QStringLiteral("left"));
    p.insert(QStringLiteral("baseRevision"), currentDocRevision());
    QCOMPARE(errorCode(sendAndWaitForReply(QStringLiteral("vc.widget.align"), p, QStringLiteral("t-b1"))), QStringLiteral("INVALID_PARAMS"));
    QCOMPARE(geometryOf(inside).value(QStringLiteral("x")).toDouble(), 10.0);

    p.insert(QStringLiteral("widgetIds"), QJsonArray({ outside }));
    p.insert(QStringLiteral("alignment"), QStringLiteral("middle"));
    QCOMPARE(errorCode(sendAndWaitForReply(QStringLiteral("vc.widget.align"), p, QStringLiteral("t-b2"))), QStringLiteral("INVALID_PARAMS"));
    p.insert(QStringLiteral("alignment"), QStringLiteral("left"));
    p.insert(QStringLiteral("referenceWidgetId"), QStringLiteral("999"));
    QCOMPARE(errorCode(sendAndWaitForReply(QStringLiteral("vc.widget.align"), p, QStringLiteral("t-b3"))), QStringLiteral("NOT_FOUND"));
    p.insert(QStringLiteral("referenceWidgetId"), outside);
    p.insert(QStringLiteral("widgetIds"), QJsonArray({ QStringLiteral("999") }));
    QCOMPARE(errorCode(sendAndWaitForReply(QStringLiteral("vc.widget.align"), p, QStringLiteral("t-b4"))), QStringLiteral("NOT_FOUND"));
    p.insert(QStringLiteral("widgetIds"), QJsonArray());
    QCOMPARE(errorCode(sendAndWaitForReply(QStringLiteral("vc.widget.align"), p, QStringLiteral("t-b5"))), QStringLiteral("INVALID_PARAMS"));
}

void ApiVcLayoutDomain_Test::widgetDistributeSpreadsEvenly()
{
    helloAndGetClientId();
    // Widths 10/20/30 between x=0 and x=200 (right edge): free space 200-60 = 140, two gaps of 70.
    QString first = createWidget(QStringLiteral("Button"), QJsonObject(), geometry(0, 0, 10, 10));
    QString middle = createWidget(QStringLiteral("Button"), QJsonObject(), geometry(15, 50, 20, 10));
    QString last = createWidget(QStringLiteral("Button"), QJsonObject(), geometry(170, 90, 30, 10));

    QJsonObject p;
    p.insert(QStringLiteral("widgetIds"), QJsonArray({ last, first, middle })); // any order
    p.insert(QStringLiteral("direction"), QStringLiteral("horizontal"));
    p.insert(QStringLiteral("baseRevision"), currentDocRevision());
    QSignalSpy spy(m_client, &QWebSocket::textMessageReceived);
    QCOMPARE(isOk(sendAndWaitForReply(QStringLiteral("vc.widget.distribute"), p, QStringLiteral("t-d1"))), true);
    QCOMPARE(geometryOf(first).value(QStringLiteral("x")).toDouble(), 0.0);
    QCOMPARE(geometryOf(middle).value(QStringLiteral("x")).toDouble(), 80.0); // 0 + 10 + 70
    QCOMPARE(geometryOf(middle).value(QStringLiteral("y")).toDouble(), 50.0); // other axis untouched
    QCOMPARE(geometryOf(last).value(QStringLiteral("x")).toDouble(), 170.0);
    QCOMPARE(eventsWithTopic(spy, QStringLiteral("vc.widget.bulkUpdated")).size(), 1);

    // Vertical: heights 10 each from y=0 to y=100 -> gaps of 35
    p.insert(QStringLiteral("direction"), QStringLiteral("vertical"));
    p.insert(QStringLiteral("baseRevision"), currentDocRevision());
    QCOMPARE(isOk(sendAndWaitForReply(QStringLiteral("vc.widget.distribute"), p, QStringLiteral("t-d2"))), true);
    QCOMPARE(geometryOf(middle).value(QStringLiteral("y")).toDouble(), 45.0);

    // Fewer than 3, bad direction
    p.insert(QStringLiteral("widgetIds"), QJsonArray({ first, last }));
    p.insert(QStringLiteral("baseRevision"), currentDocRevision());
    QCOMPARE(errorCode(sendAndWaitForReply(QStringLiteral("vc.widget.distribute"), p, QStringLiteral("t-d3"))), QStringLiteral("INVALID_PARAMS"));
    p.insert(QStringLiteral("widgetIds"), QJsonArray({ first, middle, last }));
    p.insert(QStringLiteral("direction"), QStringLiteral("diagonal"));
    QCOMPARE(errorCode(sendAndWaitForReply(QStringLiteral("vc.widget.distribute"), p, QStringLiteral("t-d4"))), QStringLiteral("INVALID_PARAMS"));
}

void ApiVcLayoutDomain_Test::widgetBulkStyleAppliesToEveryWidget()
{
    helloAndGetClientId();
    QString a = createWidget(QStringLiteral("Button"));
    QString b = createWidget(QStringLiteral("Label"));
    QString c = createWidget(QStringLiteral("Slider"));

    QJsonObject font; font.insert(QStringLiteral("pointSize"), 18); font.insert(QStringLiteral("bold"), true);
    QJsonObject p;
    p.insert(QStringLiteral("widgetIds"), QJsonArray({ a, b }));
    p.insert(QStringLiteral("caption"), QStringLiteral("Same"));
    p.insert(QStringLiteral("backgroundColor"), QStringLiteral("#112233"));
    p.insert(QStringLiteral("font"), font);
    p.insert(QStringLiteral("baseRevision"), currentDocRevision());
    QSignalSpy spy(m_client, &QWebSocket::textMessageReceived);
    QCOMPARE(isOk(sendAndWaitForReply(QStringLiteral("vc.widget.bulkStyle"), p, QStringLiteral("t-s1"))), true);

    for (const QString &wid : { a, b })
    {
        QJsonObject style = resultOf(sendAndWaitForReply(QStringLiteral("vc.widget.get"), widgetParams(wid), QStringLiteral("t-s2"))).value(QStringLiteral("style")).toObject();
        QCOMPARE(style.value(QStringLiteral("caption")).toString(), QStringLiteral("Same"));
        QCOMPARE(style.value(QStringLiteral("backgroundColor")).toString(), QStringLiteral("#112233"));
        QCOMPARE(style.value(QStringLiteral("font")).toObject().value(QStringLiteral("pointSize")).toInt(), 18);
    }
    QJsonObject untouched = resultOf(sendAndWaitForReply(QStringLiteral("vc.widget.get"), widgetParams(c), QStringLiteral("t-s3"))).value(QStringLiteral("style")).toObject();
    QCOMPARE(untouched.value(QStringLiteral("caption")).toString(), QString());

    QList<QJsonObject> events = eventsWithTopic(spy, QStringLiteral("vc.widget.bulkUpdated"));
    QCOMPARE(events.size(), 1);
    QCOMPARE(events.at(0).value(QStringLiteral("widgets")).toArray().size(), 2);

    // null resets a colour; a request without any style field is rejected
    QJsonObject reset;
    reset.insert(QStringLiteral("widgetIds"), QJsonArray({ a }));
    reset.insert(QStringLiteral("backgroundColor"), QJsonValue::Null);
    reset.insert(QStringLiteral("baseRevision"), currentDocRevision());
    QCOMPARE(isOk(sendAndWaitForReply(QStringLiteral("vc.widget.bulkStyle"), reset, QStringLiteral("t-s4"))), true);
    QJsonObject style = resultOf(sendAndWaitForReply(QStringLiteral("vc.widget.get"), widgetParams(a), QStringLiteral("t-s5"))).value(QStringLiteral("style")).toObject();
    QVERIFY(style.value(QStringLiteral("backgroundColor")).isNull());

    QJsonObject empty;
    empty.insert(QStringLiteral("widgetIds"), QJsonArray({ a }));
    empty.insert(QStringLiteral("baseRevision"), currentDocRevision());
    QCOMPARE(errorCode(sendAndWaitForReply(QStringLiteral("vc.widget.bulkStyle"), empty, QStringLiteral("t-s6"))), QStringLiteral("INVALID_PARAMS"));
}

/*****************************************************************************
 * vc.widget.createFromFunctions / createMatrix / usage
 *****************************************************************************/

void ApiVcLayoutDomain_Test::createFromFunctionsCreatesOnePerFunction()
{
    QString clientId = helloAndGetClientId();
    Scene *s1 = new Scene(m_doc); s1->setName(QStringLiteral("Red")); QVERIFY(m_doc->addFunction(s1));
    Scene *s2 = new Scene(m_doc); s2->setName(QStringLiteral("Blue")); QVERIFY(m_doc->addFunction(s2));
    Chaser *ch = new Chaser(m_doc); ch->setName(QStringLiteral("Chase")); QVERIFY(m_doc->addFunction(ch));
    QString frameId = createWidget(QStringLiteral("Frame"), QJsonObject(), geometry(0, 0, 500, 300));

    QJsonObject pos; pos.insert(QStringLiteral("x"), 20); pos.insert(QStringLiteral("y"), 30);
    QJsonObject p;
    p.insert(QStringLiteral("page"), 0);
    p.insert(QStringLiteral("parentId"), frameId);
    p.insert(QStringLiteral("functionIds"), QJsonArray({ QString::number(s1->id()), QString::number(s2->id()), QString::number(ch->id()) }));
    p.insert(QStringLiteral("position"), pos);
    p.insert(QStringLiteral("widgetHint"), QStringLiteral("button"));
    p.insert(QStringLiteral("baseRevision"), currentDocRevision());

    QSignalSpy spy(m_client, &QWebSocket::textMessageReceived);
    QJsonObject reply = sendAndWaitForReply(QStringLiteral("vc.widget.createFromFunctions"), p, QStringLiteral("t-cf1"));
    QCOMPARE(isOk(reply), true);
    QStringList ids = idsOf(resultOf(reply).value(QStringLiteral("widgetIds")).toArray());
    QCOMPARE(ids.size(), 3);
    QCOMPARE(resultOf(reply).value(QStringLiteral("docRevision")).toInt(), int(m_doc->docRevision()));

    // Per the spec the bulk creators report on vc.widget.bulkUpdated, not vc.widget.created
    QList<QJsonObject> events = eventsWithTopic(spy, QStringLiteral("vc.widget.bulkUpdated"));
    QCOMPARE(events.size(), 1);
    QCOMPARE(events.at(0).value(QStringLiteral("_origin")).toString(), clientId);
    QJsonArray widgets = events.at(0).value(QStringLiteral("widgets")).toArray();
    QCOMPARE(widgets.size(), 3);
    QCOMPARE(eventsWithTopic(spy, QStringLiteral("vc.widget.created")).size(), 0);
    for (int i = 0; i < widgets.size(); i++)
    {
        QJsonObject w = widgets.at(i).toObject();
        QCOMPARE(w.value(QStringLiteral("widgetType")).toString(), QStringLiteral("Button"));
        QCOMPARE(w.value(QStringLiteral("parentId")).toString(), frameId);
        QCOMPARE(ids.contains(w.value(QStringLiteral("id")).toString()), true);
    }
    QCOMPARE(widgets.at(0).toObject().value(QStringLiteral("typeConfig")).toObject().value(QStringLiteral("functionID")).toString(), QString::number(s1->id()));

    // Adjust sliders and a cue list on the page root
    p.remove(QStringLiteral("parentId"));
    p.insert(QStringLiteral("widgetHint"), QStringLiteral("adjustSlider"));
    p.insert(QStringLiteral("baseRevision"), currentDocRevision());
    reply = sendAndWaitForReply(QStringLiteral("vc.widget.createFromFunctions"), p, QStringLiteral("t-cf2"));
    QCOMPARE(isOk(reply), true);
    QString sliderId = idsOf(resultOf(reply).value(QStringLiteral("widgetIds")).toArray()).first();
    QJsonObject slider = resultOf(sendAndWaitForReply(QStringLiteral("vc.widget.get"), widgetParams(sliderId), QStringLiteral("t-cf3")));
    QCOMPARE(slider.value(QStringLiteral("widgetType")).toString(), QStringLiteral("Slider"));
    QCOMPARE(slider.contains(QStringLiteral("parentId")), false);
    QCOMPARE(slider.value(QStringLiteral("typeConfig")).toObject().value(QStringLiteral("sliderMode")).toString(), QStringLiteral("Adjust"));

    p.insert(QStringLiteral("functionIds"), QJsonArray({ QString::number(ch->id()) }));
    p.insert(QStringLiteral("widgetHint"), QStringLiteral("cueList"));
    p.insert(QStringLiteral("baseRevision"), currentDocRevision());
    reply = sendAndWaitForReply(QStringLiteral("vc.widget.createFromFunctions"), p, QStringLiteral("t-cf4"));
    QCOMPARE(isOk(reply), true);
    QString cueId = idsOf(resultOf(reply).value(QStringLiteral("widgetIds")).toArray()).first();
    QJsonObject cue = resultOf(sendAndWaitForReply(QStringLiteral("vc.widget.get"), widgetParams(cueId), QStringLiteral("t-cf5")));
    QCOMPARE(cue.value(QStringLiteral("widgetType")).toString(), QStringLiteral("CueList"));
    QCOMPARE(cue.value(QStringLiteral("typeConfig")).toObject().value(QStringLiteral("chaserID")).toString(), QString::number(ch->id()));
}

void ApiVcLayoutDomain_Test::createFromFunctionsValidates()
{
    helloAndGetClientId();
    Scene *scene = new Scene(m_doc); QVERIFY(m_doc->addFunction(scene));
    QString buttonId = createWidget(QStringLiteral("Button"));
    int rev = currentDocRevision();

    QJsonObject pos; pos.insert(QStringLiteral("x"), 0); pos.insert(QStringLiteral("y"), 0);
    QJsonObject base;
    base.insert(QStringLiteral("page"), 0);
    base.insert(QStringLiteral("functionIds"), QJsonArray({ QString::number(scene->id()) }));
    base.insert(QStringLiteral("position"), pos);
    base.insert(QStringLiteral("widgetHint"), QStringLiteral("button"));
    base.insert(QStringLiteral("baseRevision"), rev);

    QJsonObject p = base; p.insert(QStringLiteral("functionIds"), QJsonArray({ QStringLiteral("999") }));
    QCOMPARE(errorCode(sendAndWaitForReply(QStringLiteral("vc.widget.createFromFunctions"), p, QStringLiteral("t-cv1"))), QStringLiteral("NOT_FOUND"));
    p = base; p.insert(QStringLiteral("functionIds"), QJsonArray());
    QCOMPARE(errorCode(sendAndWaitForReply(QStringLiteral("vc.widget.createFromFunctions"), p, QStringLiteral("t-cv2"))), QStringLiteral("INVALID_PARAMS"));
    p = base; p.insert(QStringLiteral("widgetHint"), QStringLiteral("cueList")); // a Scene is not a Chaser
    QCOMPARE(errorCode(sendAndWaitForReply(QStringLiteral("vc.widget.createFromFunctions"), p, QStringLiteral("t-cv3"))), QStringLiteral("INVALID_PARAMS"));
    p = base; p.insert(QStringLiteral("widgetHint"), QStringLiteral("knob"));
    QCOMPARE(errorCode(sendAndWaitForReply(QStringLiteral("vc.widget.createFromFunctions"), p, QStringLiteral("t-cv4"))), QStringLiteral("INVALID_PARAMS"));
    p = base; p.insert(QStringLiteral("page"), 5);
    QCOMPARE(errorCode(sendAndWaitForReply(QStringLiteral("vc.widget.createFromFunctions"), p, QStringLiteral("t-cv5"))), QStringLiteral("NOT_FOUND"));
    p = base; p.insert(QStringLiteral("parentId"), buttonId); // not a container
    QCOMPARE(errorCode(sendAndWaitForReply(QStringLiteral("vc.widget.createFromFunctions"), p, QStringLiteral("t-cv6"))), QStringLiteral("INVALID_PARAMS"));
    p = base; p.insert(QStringLiteral("parentId"), QStringLiteral("999"));
    QCOMPARE(errorCode(sendAndWaitForReply(QStringLiteral("vc.widget.createFromFunctions"), p, QStringLiteral("t-cv7"))), QStringLiteral("NOT_FOUND"));
    p = base; p.insert(QStringLiteral("baseRevision"), rev - 1);
    QCOMPARE(errorCode(sendAndWaitForReply(QStringLiteral("vc.widget.createFromFunctions"), p, QStringLiteral("t-cv8"))), QStringLiteral("CONFLICT"));

    // Nothing was created by any of the refusals
    QJsonObject list = sendAndWaitForReply(QStringLiteral("vc.widget.list"), QJsonObject(), QStringLiteral("t-cv9"));
    QCOMPARE(resultOf(list).value(QStringLiteral("widgets")).toArray().size(), 1);
    QCOMPARE(currentDocRevision(), rev);
}

void ApiVcLayoutDomain_Test::createMatrixCreatesContainerAndCells()
{
    helloAndGetClientId();
    QJsonObject pos; pos.insert(QStringLiteral("x"), 40); pos.insert(QStringLiteral("y"), 60);
    QJsonObject size; size.insert(QStringLiteral("columns"), 3); size.insert(QStringLiteral("rows"), 2);
    QJsonObject wsize; wsize.insert(QStringLiteral("width"), 50); wsize.insert(QStringLiteral("height"), 40);
    QJsonObject p;
    p.insert(QStringLiteral("page"), 0);
    p.insert(QStringLiteral("matrixType"), QStringLiteral("Button"));
    p.insert(QStringLiteral("position"), pos);
    p.insert(QStringLiteral("matrixSize"), size);
    p.insert(QStringLiteral("widgetSize"), wsize);
    p.insert(QStringLiteral("baseRevision"), currentDocRevision());

    QSignalSpy spy(m_client, &QWebSocket::textMessageReceived);
    QJsonObject reply = sendAndWaitForReply(QStringLiteral("vc.widget.createMatrix"), p, QStringLiteral("t-m1"));
    QCOMPARE(isOk(reply), true);
    QStringList ids = idsOf(resultOf(reply).value(QStringLiteral("widgetIds")).toArray());
    QCOMPARE(ids.size(), 7); // container + 3x2

    QList<QJsonObject> events = eventsWithTopic(spy, QStringLiteral("vc.widget.bulkUpdated"));
    QCOMPARE(events.size(), 1);
    QJsonArray widgets = events.at(0).value(QStringLiteral("widgets")).toArray();
    QCOMPARE(widgets.size(), 7);
    QJsonObject container = widgets.at(0).toObject();
    QCOMPARE(container.value(QStringLiteral("widgetType")).toString(), QStringLiteral("Frame"));
    QCOMPARE(container.value(QStringLiteral("geometry")).toObject().value(QStringLiteral("x")).toDouble(), 40.0);
    for (int i = 1; i < widgets.size(); i++)
    {
        QCOMPARE(widgets.at(i).toObject().value(QStringLiteral("widgetType")).toString(), QStringLiteral("Button"));
        QCOMPARE(widgets.at(i).toObject().value(QStringLiteral("parentId")).toString(), container.value(QStringLiteral("id")).toString());
        QCOMPARE(widgets.at(i).toObject().value(QStringLiteral("geometry")).toObject().value(QStringLiteral("width")).toDouble(), 50.0);
    }

    // Slider matrix in a solo frame
    p.insert(QStringLiteral("matrixType"), QStringLiteral("Slider"));
    p.insert(QStringLiteral("soloFrame"), true);
    p.insert(QStringLiteral("baseRevision"), currentDocRevision());
    reply = sendAndWaitForReply(QStringLiteral("vc.widget.createMatrix"), p, QStringLiteral("t-m2"));
    QCOMPARE(isOk(reply), true);
    QStringList solo = idsOf(resultOf(reply).value(QStringLiteral("widgetIds")).toArray());
    QJsonObject soloFrame = resultOf(sendAndWaitForReply(QStringLiteral("vc.widget.get"), widgetParams(solo.first()), QStringLiteral("t-m3")));
    QCOMPARE(soloFrame.value(QStringLiteral("widgetType")).toString(), QStringLiteral("SoloFrame"));
    QJsonObject cell = resultOf(sendAndWaitForReply(QStringLiteral("vc.widget.get"), widgetParams(solo.last()), QStringLiteral("t-m4")));
    QCOMPARE(cell.value(QStringLiteral("widgetType")).toString(), QStringLiteral("Slider"));
}

void ApiVcLayoutDomain_Test::createMatrixValidates()
{
    helloAndGetClientId();
    int rev = currentDocRevision();
    QJsonObject pos; pos.insert(QStringLiteral("x"), 0); pos.insert(QStringLiteral("y"), 0);
    QJsonObject size; size.insert(QStringLiteral("columns"), 2); size.insert(QStringLiteral("rows"), 2);
    QJsonObject wsize; wsize.insert(QStringLiteral("width"), 30); wsize.insert(QStringLiteral("height"), 30);
    QJsonObject base;
    base.insert(QStringLiteral("page"), 0);
    base.insert(QStringLiteral("matrixType"), QStringLiteral("Button"));
    base.insert(QStringLiteral("position"), pos);
    base.insert(QStringLiteral("matrixSize"), size);
    base.insert(QStringLiteral("widgetSize"), wsize);
    base.insert(QStringLiteral("baseRevision"), rev);

    QJsonObject p = base; p.insert(QStringLiteral("matrixType"), QStringLiteral("XYPad"));
    QCOMPARE(errorCode(sendAndWaitForReply(QStringLiteral("vc.widget.createMatrix"), p, QStringLiteral("t-mv1"))), QStringLiteral("INVALID_PARAMS"));
    QJsonObject zero = size; zero.insert(QStringLiteral("rows"), 0);
    p = base; p.insert(QStringLiteral("matrixSize"), zero);
    QCOMPARE(errorCode(sendAndWaitForReply(QStringLiteral("vc.widget.createMatrix"), p, QStringLiteral("t-mv2"))), QStringLiteral("INVALID_PARAMS"));
    QJsonObject frac = wsize; frac.insert(QStringLiteral("width"), 1.5);
    p = base; p.insert(QStringLiteral("widgetSize"), frac);
    QCOMPARE(errorCode(sendAndWaitForReply(QStringLiteral("vc.widget.createMatrix"), p, QStringLiteral("t-mv3"))), QStringLiteral("INVALID_PARAMS"));
    p = base; p.remove(QStringLiteral("matrixSize"));
    QCOMPARE(errorCode(sendAndWaitForReply(QStringLiteral("vc.widget.createMatrix"), p, QStringLiteral("t-mv4"))), QStringLiteral("INVALID_PARAMS"));
    p = base; p.insert(QStringLiteral("page"), 1);
    QCOMPARE(errorCode(sendAndWaitForReply(QStringLiteral("vc.widget.createMatrix"), p, QStringLiteral("t-mv5"))), QStringLiteral("NOT_FOUND"));
    p = base; p.insert(QStringLiteral("baseRevision"), rev + 1);
    QCOMPARE(errorCode(sendAndWaitForReply(QStringLiteral("vc.widget.createMatrix"), p, QStringLiteral("t-mv6"))), QStringLiteral("CONFLICT"));

    QJsonObject list = sendAndWaitForReply(QStringLiteral("vc.widget.list"), QJsonObject(), QStringLiteral("t-mv7"));
    QCOMPARE(resultOf(list).value(QStringLiteral("widgets")).toArray().size(), 0);
    QCOMPARE(currentDocRevision(), rev);
}

void ApiVcLayoutDomain_Test::usageListsReferencingWidgets()
{
    helloAndGetClientId();
    QJsonObject b7; b7.insert(QStringLiteral("functionID"), QStringLiteral("7"));
    QString button = createWidget(QStringLiteral("Button"), b7);
    QJsonObject b8; b8.insert(QStringLiteral("functionID"), QStringLiteral("8"));
    QString other = createWidget(QStringLiteral("Button"), b8);
    QJsonObject s7; s7.insert(QStringLiteral("sliderMode"), QStringLiteral("Adjust")); s7.insert(QStringLiteral("controlledFunction"), QStringLiteral("7"));
    QString slider = createWidget(QStringLiteral("Slider"), s7);
    QJsonObject c7; c7.insert(QStringLiteral("chaserID"), QStringLiteral("7"));
    QString cue = createWidget(QStringLiteral("CueList"), c7);
    createWidget(QStringLiteral("Label"));

    QJsonObject p; p.insert(QStringLiteral("functionId"), QStringLiteral("7"));
    QJsonObject reply = sendAndWaitForReply(QStringLiteral("vc.widget.usage"), p, QStringLiteral("t-u1"));
    QCOMPARE(isOk(reply), true);
    QJsonArray widgets = resultOf(reply).value(QStringLiteral("widgets")).toArray();
    QStringList ids;
    for (const QJsonValue &v : widgets)
        ids.append(v.toObject().value(QStringLiteral("id")).toString());
    QCOMPARE(ids, QStringList({ button, slider, cue }));
    QCOMPARE(ids.contains(other), false);
    QCOMPARE(widgets.at(0).toObject().value(QStringLiteral("widgetType")).toString(), QStringLiteral("Button"));

    p.insert(QStringLiteral("functionId"), QStringLiteral("42"));
    QCOMPARE(resultOf(sendAndWaitForReply(QStringLiteral("vc.widget.usage"), p, QStringLiteral("t-u2"))).value(QStringLiteral("widgets")).toArray().size(), 0);
    p.insert(QStringLiteral("functionId"), QStringLiteral("abc"));
    QCOMPARE(errorCode(sendAndWaitForReply(QStringLiteral("vc.widget.usage"), p, QStringLiteral("t-u3"))), QStringLiteral("INVALID_PARAMS"));
}

void ApiVcLayoutDomain_Test::structuralMethodsConflictOnStaleRevision()
{
    helloAndGetClientId();
    QString frameId = createWidget(QStringLiteral("Frame"), QJsonObject(), geometry(0, 0, 100, 100));
    QString a = createWidget(QStringLiteral("Button"), QJsonObject(), geometry(0, 0, 10, 10));
    QString b = createWidget(QStringLiteral("Button"), QJsonObject(), geometry(50, 0, 10, 10));
    QString c = createWidget(QStringLiteral("Button"), QJsonObject(), geometry(90, 0, 10, 10));
    QString sliderId = createWidget(QStringLiteral("Slider"));
    int rev = currentDocRevision();
    int stale = rev - 1;

    QJsonObject align;
    align.insert(QStringLiteral("widgetIds"), QJsonArray({ a, b })); align.insert(QStringLiteral("referenceWidgetId"), a);
    align.insert(QStringLiteral("alignment"), QStringLiteral("left")); align.insert(QStringLiteral("baseRevision"), stale);
    QCOMPARE(errorCode(sendAndWaitForReply(QStringLiteral("vc.widget.align"), align, QStringLiteral("t-r1"))), QStringLiteral("CONFLICT"));

    QJsonObject distribute;
    distribute.insert(QStringLiteral("widgetIds"), QJsonArray({ a, b, c })); distribute.insert(QStringLiteral("direction"), QStringLiteral("horizontal"));
    distribute.insert(QStringLiteral("baseRevision"), stale);
    QCOMPARE(errorCode(sendAndWaitForReply(QStringLiteral("vc.widget.distribute"), distribute, QStringLiteral("t-r2"))), QStringLiteral("CONFLICT"));

    QJsonObject style;
    style.insert(QStringLiteral("widgetIds"), QJsonArray({ a })); style.insert(QStringLiteral("caption"), QStringLiteral("x"));
    style.insert(QStringLiteral("baseRevision"), stale);
    QJsonObject styleReply = sendAndWaitForReply(QStringLiteral("vc.widget.bulkStyle"), style, QStringLiteral("t-r3"));
    QCOMPARE(errorCode(styleReply), QStringLiteral("CONFLICT"));
    QCOMPARE(styleReply.value(QStringLiteral("error")).toObject().value(QStringLiteral("details")).toObject().value(QStringLiteral("docRevision")).toInt(), rev);

    QJsonObject pin; pin.insert(QStringLiteral("newPIN"), QStringLiteral("1111")); pin.insert(QStringLiteral("baseRevision"), stale);
    QCOMPARE(errorCode(sendAndWaitForReply(QStringLiteral("vc.frame.setPin"), widgetParams(frameId, pin), QStringLiteral("t-r4"))), QStringLiteral("CONFLICT"));
    QJsonObject clone; clone.insert(QStringLiteral("baseRevision"), stale);
    QCOMPARE(errorCode(sendAndWaitForReply(QStringLiteral("vc.frame.cloneFirstPage"), widgetParams(frameId, clone), QStringLiteral("t-r5"))), QStringLiteral("CONFLICT"));
    QJsonObject channels; channels.insert(QStringLiteral("channels"), QJsonArray()); channels.insert(QStringLiteral("baseRevision"), stale);
    QCOMPARE(errorCode(sendAndWaitForReply(QStringLiteral("vc.slider.setLevelChannels"), widgetParams(sliderId, channels), QStringLiteral("t-r6"))), QStringLiteral("CONFLICT"));

    QCOMPARE(currentDocRevision(), rev);
    QCOMPARE(geometryOf(b).value(QStringLiteral("x")).toDouble(), 50.0);
}

QTEST_MAIN(ApiVcLayoutDomain_Test)
