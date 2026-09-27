/*
  Q Light Controller Plus - Control API unit test
  apivcdomain_test.cpp

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
#include <QJsonValue>
#include <QWebSocket>
#include <QtTest>

#include "apivcdomain_test.h"
#include "apiserver.h"
#include "doc.h"
#include "fakevchost.h"

static QString buildRequest(const QString &method, const QJsonObject &params, const QString &id)
{
    QJsonObject obj;
    obj.insert(QStringLiteral("type"), QStringLiteral("request"));
    obj.insert(QStringLiteral("id"), id);
    obj.insert(QStringLiteral("method"), method);
    obj.insert(QStringLiteral("params"), params);
    return QString::fromUtf8(QJsonDocument(obj).toJson(QJsonDocument::Compact));
}

/** Every "event" frame with $topic captured by $spy so far, in arrival order (data objects, with the
 *  frame's originClientId folded in as "_origin" - QJsonValue::Null when the server sent null). */
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

void ApiVcDomain_Test::init()
{
    m_doc = new Doc(nullptr);
    m_vcHost = new FakeVcHost();
    m_apiServer = new ApiServer(m_vcHost, m_doc);
    QVERIFY(m_apiServer->listen(0, QHostAddress::LocalHost));

    m_client = new QWebSocket();
    m_client->open(QUrl(QStringLiteral("ws://127.0.0.1:%1/qlcplusapi").arg(m_apiServer->serverPort())));
    QVERIFY(QTest::qWaitFor([this]() { return m_client->state() == QAbstractSocket::ConnectedState; }, 2000));
}

void ApiVcDomain_Test::cleanup()
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

QJsonObject ApiVcDomain_Test::sendAndWaitForReply(const QString &method, const QJsonObject &params, const QString &requestId)
{
    // A mutation's response and its broadcast event can arrive in either order - scan every frame
    // received so far for the matching "response", not just the first one, polling until it shows
    // up or we time out. Same pattern as controlapi/test/apiiodomain.
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

QString ApiVcDomain_Test::helloAndGetClientId()
{
    QJsonObject reply = sendAndWaitForReply(QStringLiteral("hello"), QJsonObject(), QStringLiteral("t-hello"));
    return reply.value(QStringLiteral("result")).toObject().value(QStringLiteral("clientId")).toString();
}

int ApiVcDomain_Test::currentDocRevision()
{
    // vc.page.list deliberately carries no docRevision (see VcPageListOkResponse in the spec) -
    // "hello" is harmless to resend (it just re-sets an already-true helloed flag) and always
    // returns the live docRevision, so reuse it here as a docRevision probe.
    QJsonObject reply = sendAndWaitForReply(QStringLiteral("hello"), QJsonObject(), QStringLiteral("t-rev"));
    return reply.value(QStringLiteral("result")).toObject().value(QStringLiteral("docRevision")).toInt();
}

void ApiVcDomain_Test::pageListReturnsSingleDefaultPage()
{
    helloAndGetClientId();
    QJsonObject reply = sendAndWaitForReply(QStringLiteral("vc.page.list"), QJsonObject());
    QCOMPARE(reply.value(QStringLiteral("ok")).toBool(), true);
    QJsonArray pages = reply.value(QStringLiteral("result")).toObject().value(QStringLiteral("pages")).toArray();
    QCOMPARE(pages.size(), 1);
    QCOMPARE(pages.at(0).toObject().value(QStringLiteral("index")).toInt(), 0);
    QCOMPARE(pages.at(0).toObject().value(QStringLiteral("hasPin")).toBool(), false);
    QCOMPARE(reply.value(QStringLiteral("result")).toObject().value(QStringLiteral("selectedPage")).toInt(), 0);
}

void ApiVcDomain_Test::pageCreateBumpsRevisionAndBroadcasts()
{
    QString clientId = helloAndGetClientId();
    int rev = currentDocRevision();

    QSignalSpy spy(m_client, &QWebSocket::textMessageReceived);
    QJsonObject params;
    params.insert(QStringLiteral("index"), 1);
    params.insert(QStringLiteral("baseRevision"), rev);
    m_client->sendTextMessage(buildRequest(QStringLiteral("vc.page.create"), params, QStringLiteral("t-pc")));

    QVERIFY(QTest::qWaitFor([&]() { return spy.count() >= 2; }, 2000));

    bool sawResponse = false, sawEvent = false;
    for (const QList<QVariant> &frame : spy)
    {
        QJsonObject obj = QJsonDocument::fromJson(frame.at(0).toString().toUtf8()).object();
        QString type = obj.value(QStringLiteral("type")).toString();
        if (type == QStringLiteral("response") && obj.value(QStringLiteral("id")).toString() == QStringLiteral("t-pc"))
        {
            QCOMPARE(obj.value(QStringLiteral("ok")).toBool(), true);
            QVERIFY(obj.value(QStringLiteral("result")).toObject().value(QStringLiteral("docRevision")).toInt() > rev);
            sawResponse = true;
        }
        else if (type == QStringLiteral("event") && obj.value(QStringLiteral("topic")).toString() == QStringLiteral("vc.page.created"))
        {
            QCOMPARE(obj.value(QStringLiteral("originClientId")).toString(), clientId);
            QCOMPARE(obj.value(QStringLiteral("data")).toObject().value(QStringLiteral("page")).toObject()
                     .value(QStringLiteral("index")).toInt(), 1);
            sawEvent = true;
        }
    }
    QVERIFY(sawResponse);
    QVERIFY(sawEvent);
    QCOMPARE(m_doc->docRevision(), quint32(rev + 1));
}

void ApiVcDomain_Test::pageCreateWithStaleRevisionConflicts()
{
    helloAndGetClientId();
    int rev = currentDocRevision();

    QJsonObject params;
    params.insert(QStringLiteral("index"), 1);
    params.insert(QStringLiteral("baseRevision"), rev - 1);
    QJsonObject reply = sendAndWaitForReply(QStringLiteral("vc.page.create"), params);

    QCOMPARE(reply.value(QStringLiteral("ok")).toBool(), false);
    QJsonObject error = reply.value(QStringLiteral("error")).toObject();
    QCOMPARE(error.value(QStringLiteral("code")).toString(), QStringLiteral("CONFLICT"));
    QCOMPARE(error.value(QStringLiteral("details")).toObject().value(QStringLiteral("docRevision")).toInt(), rev);
}

void ApiVcDomain_Test::pageCreateShiftsWidgetsAndSelection()
{
    helloAndGetClientId();
    int rev = currentDocRevision();

    // Create a widget on page 0.
    QJsonObject createParams;
    createParams.insert(QStringLiteral("widgetType"), QStringLiteral("Label"));
    createParams.insert(QStringLiteral("page"), 0);
    QJsonObject geom;
    geom.insert(QStringLiteral("x"), 0); geom.insert(QStringLiteral("y"), 0);
    geom.insert(QStringLiteral("width"), 10); geom.insert(QStringLiteral("height"), 10);
    createParams.insert(QStringLiteral("geometry"), geom);
    createParams.insert(QStringLiteral("baseRevision"), rev);
    QJsonObject createReply = sendAndWaitForReply(QStringLiteral("vc.widget.create"), createParams);
    QString widgetId = createReply.value(QStringLiteral("result")).toObject().value(QStringLiteral("widgetId")).toString();
    rev = currentDocRevision();

    // Insert a new page at index 0 - the widget (and page 0 itself) should shift to index 1.
    QJsonObject pageParams;
    pageParams.insert(QStringLiteral("index"), 0);
    pageParams.insert(QStringLiteral("baseRevision"), rev);
    sendAndWaitForReply(QStringLiteral("vc.page.create"), pageParams, QStringLiteral("t-shift"));

    QJsonObject getParams;
    getParams.insert(QStringLiteral("widgetId"), widgetId);
    QJsonObject getReply = sendAndWaitForReply(QStringLiteral("vc.widget.get"), getParams, QStringLiteral("t-get"));
    QCOMPARE(getReply.value(QStringLiteral("result")).toObject().value(QStringLiteral("page")).toInt(), 1);

    QJsonObject listReply = sendAndWaitForReply(QStringLiteral("vc.page.list"), QJsonObject(), QStringLiteral("t-list"));
    QCOMPARE(listReply.value(QStringLiteral("result")).toObject().value(QStringLiteral("selectedPage")).toInt(), 1);
}

void ApiVcDomain_Test::pageDeleteRefusesLastPage()
{
    helloAndGetClientId();
    int rev = currentDocRevision();

    QJsonObject params;
    params.insert(QStringLiteral("index"), 0);
    params.insert(QStringLiteral("baseRevision"), rev);
    QJsonObject reply = sendAndWaitForReply(QStringLiteral("vc.page.delete"), params);

    QCOMPARE(reply.value(QStringLiteral("ok")).toBool(), false);
    QCOMPARE(reply.value(QStringLiteral("error")).toObject().value(QStringLiteral("code")).toString(),
              QStringLiteral("INVALID_STATE"));
}

void ApiVcDomain_Test::pageDeleteRenumbersWidgetsOnLaterPages()
{
    helloAndGetClientId();
    int rev = currentDocRevision();

    // Add a second page (index 1).
    QJsonObject pageParams;
    pageParams.insert(QStringLiteral("index"), 1);
    pageParams.insert(QStringLiteral("baseRevision"), rev);
    sendAndWaitForReply(QStringLiteral("vc.page.create"), pageParams, QStringLiteral("t-p1"));
    rev = currentDocRevision();

    // Create a widget on page 1.
    QJsonObject createParams;
    createParams.insert(QStringLiteral("widgetType"), QStringLiteral("Label"));
    createParams.insert(QStringLiteral("page"), 1);
    QJsonObject geom;
    geom.insert(QStringLiteral("x"), 0); geom.insert(QStringLiteral("y"), 0);
    geom.insert(QStringLiteral("width"), 10); geom.insert(QStringLiteral("height"), 10);
    createParams.insert(QStringLiteral("geometry"), geom);
    createParams.insert(QStringLiteral("baseRevision"), rev);
    QJsonObject createReply = sendAndWaitForReply(QStringLiteral("vc.widget.create"), createParams, QStringLiteral("t-w1"));
    QString widgetId = createReply.value(QStringLiteral("result")).toObject().value(QStringLiteral("widgetId")).toString();
    rev = currentDocRevision();

    // Delete page 0 - the widget on (old) page 1 should now be on page 0.
    QJsonObject deleteParams;
    deleteParams.insert(QStringLiteral("index"), 0);
    deleteParams.insert(QStringLiteral("baseRevision"), rev);
    QJsonObject deleteReply = sendAndWaitForReply(QStringLiteral("vc.page.delete"), deleteParams, QStringLiteral("t-del"));
    QCOMPARE(deleteReply.value(QStringLiteral("ok")).toBool(), true);

    QJsonObject getParams;
    getParams.insert(QStringLiteral("widgetId"), widgetId);
    QJsonObject getReply = sendAndWaitForReply(QStringLiteral("vc.widget.get"), getParams, QStringLiteral("t-get"));
    QCOMPARE(getReply.value(QStringLiteral("result")).toObject().value(QStringLiteral("page")).toInt(), 0);
}

void ApiVcDomain_Test::pageRenameBroadcastsEvent()
{
    QString clientId = helloAndGetClientId();
    int rev = currentDocRevision();

    QSignalSpy spy(m_client, &QWebSocket::textMessageReceived);
    QJsonObject params;
    params.insert(QStringLiteral("index"), 0);
    params.insert(QStringLiteral("name"), QStringLiteral("Main"));
    params.insert(QStringLiteral("baseRevision"), rev);
    m_client->sendTextMessage(buildRequest(QStringLiteral("vc.page.rename"), params, QStringLiteral("t-rn")));
    QVERIFY(QTest::qWaitFor([&]() { return spy.count() >= 2; }, 2000));

    bool sawEvent = false;
    for (const QList<QVariant> &frame : spy)
    {
        QJsonObject obj = QJsonDocument::fromJson(frame.at(0).toString().toUtf8()).object();
        if (obj.value(QStringLiteral("type")).toString() == QStringLiteral("event"))
        {
            QCOMPARE(obj.value(QStringLiteral("topic")).toString(), QStringLiteral("vc.page.renamed"));
            QCOMPARE(obj.value(QStringLiteral("data")).toObject().value(QStringLiteral("name")).toString(), QStringLiteral("Main"));
            QCOMPARE(obj.value(QStringLiteral("originClientId")).toString(), clientId);
            sawEvent = true;
        }
    }
    QVERIFY(sawEvent);
}

void ApiVcDomain_Test::pageSetPinRequiresCurrentPin()
{
    helloAndGetClientId();
    int rev = currentDocRevision();

    QJsonObject setParams;
    setParams.insert(QStringLiteral("index"), 0);
    setParams.insert(QStringLiteral("newPIN"), QStringLiteral("1234"));
    setParams.insert(QStringLiteral("baseRevision"), rev);
    QJsonObject setReply = sendAndWaitForReply(QStringLiteral("vc.page.setPin"), setParams, QStringLiteral("t-pin1"));
    QCOMPARE(setReply.value(QStringLiteral("ok")).toBool(), true);
    rev = currentDocRevision();

    // Wrong current PIN must be rejected.
    QJsonObject wrongParams;
    wrongParams.insert(QStringLiteral("index"), 0);
    wrongParams.insert(QStringLiteral("currentPIN"), QStringLiteral("0000"));
    wrongParams.insert(QStringLiteral("newPIN"), QStringLiteral(""));
    wrongParams.insert(QStringLiteral("baseRevision"), rev);
    QJsonObject wrongReply = sendAndWaitForReply(QStringLiteral("vc.page.setPin"), wrongParams, QStringLiteral("t-pin2"));
    QCOMPARE(wrongReply.value(QStringLiteral("ok")).toBool(), false);

    // Correct current PIN clears protection.
    QJsonObject clearParams;
    clearParams.insert(QStringLiteral("index"), 0);
    clearParams.insert(QStringLiteral("currentPIN"), QStringLiteral("1234"));
    clearParams.insert(QStringLiteral("newPIN"), QStringLiteral(""));
    clearParams.insert(QStringLiteral("baseRevision"), rev);
    QJsonObject clearReply = sendAndWaitForReply(QStringLiteral("vc.page.setPin"), clearParams, QStringLiteral("t-pin3"));
    QCOMPARE(clearReply.value(QStringLiteral("ok")).toBool(), true);

    QJsonObject listReply = sendAndWaitForReply(QStringLiteral("vc.page.list"), QJsonObject(), QStringLiteral("t-list"));
    QJsonArray pages = listReply.value(QStringLiteral("result")).toObject().value(QStringLiteral("pages")).toArray();
    QCOMPARE(pages.at(0).toObject().value(QStringLiteral("hasPin")).toBool(), false);
}

void ApiVcDomain_Test::pageValidatePinChecksValue()
{
    helloAndGetClientId();
    int rev = currentDocRevision();

    QJsonObject setParams;
    setParams.insert(QStringLiteral("index"), 0);
    setParams.insert(QStringLiteral("newPIN"), QStringLiteral("4242"));
    setParams.insert(QStringLiteral("baseRevision"), rev);
    sendAndWaitForReply(QStringLiteral("vc.page.setPin"), setParams, QStringLiteral("t-pin"));

    QJsonObject badParams;
    badParams.insert(QStringLiteral("index"), 0);
    badParams.insert(QStringLiteral("pin"), QStringLiteral("0000"));
    QJsonObject badReply = sendAndWaitForReply(QStringLiteral("vc.page.validatePin"), badParams, QStringLiteral("t-v1"));
    QCOMPARE(badReply.value(QStringLiteral("result")).toObject().value(QStringLiteral("valid")).toBool(), false);

    QJsonObject goodParams;
    goodParams.insert(QStringLiteral("index"), 0);
    goodParams.insert(QStringLiteral("pin"), QStringLiteral("4242"));
    QJsonObject goodReply = sendAndWaitForReply(QStringLiteral("vc.page.validatePin"), goodParams, QStringLiteral("t-v2"));
    QCOMPARE(goodReply.value(QStringLiteral("result")).toObject().value(QStringLiteral("valid")).toBool(), true);
}

void ApiVcDomain_Test::pageSelectDoesNotBumpDocRevision()
{
    helloAndGetClientId();
    int rev = currentDocRevision();

    // Need a second page to select.
    QJsonObject pageParams;
    pageParams.insert(QStringLiteral("index"), 1);
    pageParams.insert(QStringLiteral("baseRevision"), rev);
    sendAndWaitForReply(QStringLiteral("vc.page.create"), pageParams, QStringLiteral("t-p1"));
    rev = currentDocRevision();

    QJsonObject selectParams;
    selectParams.insert(QStringLiteral("index"), 1);
    QJsonObject selectReply = sendAndWaitForReply(QStringLiteral("vc.page.select"), selectParams, QStringLiteral("t-sel"));
    QCOMPARE(selectReply.value(QStringLiteral("ok")).toBool(), true);
    QVERIFY(selectReply.value(QStringLiteral("result")).toObject().isEmpty());

    QCOMPARE(currentDocRevision(), rev);
    QCOMPARE(int(m_doc->docRevision()), rev);
}

void ApiVcDomain_Test::widgetCreateAndGet()
{
    helloAndGetClientId();
    int rev = currentDocRevision();

    QJsonObject createParams;
    createParams.insert(QStringLiteral("widgetType"), QStringLiteral("Button"));
    createParams.insert(QStringLiteral("page"), 0);
    QJsonObject geom;
    geom.insert(QStringLiteral("x"), 5); geom.insert(QStringLiteral("y"), 6);
    geom.insert(QStringLiteral("width"), 40); geom.insert(QStringLiteral("height"), 20);
    createParams.insert(QStringLiteral("geometry"), geom);
    QJsonObject typeConfig;
    typeConfig.insert(QStringLiteral("actionType"), QStringLiteral("Toggle"));
    createParams.insert(QStringLiteral("typeConfig"), typeConfig);
    createParams.insert(QStringLiteral("baseRevision"), rev);

    QJsonObject createReply = sendAndWaitForReply(QStringLiteral("vc.widget.create"), createParams);
    QCOMPARE(createReply.value(QStringLiteral("ok")).toBool(), true);
    QString widgetId = createReply.value(QStringLiteral("result")).toObject().value(QStringLiteral("widgetId")).toString();
    QVERIFY(widgetId.isEmpty() == false);

    QJsonObject getParams;
    getParams.insert(QStringLiteral("widgetId"), widgetId);
    QJsonObject getReply = sendAndWaitForReply(QStringLiteral("vc.widget.get"), getParams, QStringLiteral("t-get"));
    QJsonObject widget = getReply.value(QStringLiteral("result")).toObject();
    QCOMPARE(widget.value(QStringLiteral("id")).toString(), widgetId);
    QCOMPARE(widget.value(QStringLiteral("widgetType")).toString(), QStringLiteral("Button"));
    QCOMPARE(widget.value(QStringLiteral("geometry")).toObject().value(QStringLiteral("width")).toDouble(), 40.0);
    QCOMPARE(widget.value(QStringLiteral("typeConfig")).toObject().value(QStringLiteral("actionType")).toString(), QStringLiteral("Toggle"));
    QVERIFY(widget.contains(QStringLiteral("inputSources")));
    QVERIFY(widget.contains(QStringLiteral("keySequences")));
    QVERIFY(widget.contains(QStringLiteral("externalControls")));
}

void ApiVcDomain_Test::widgetCreateRejectsUnknownType()
{
    helloAndGetClientId();
    int rev = currentDocRevision();

    QJsonObject createParams;
    createParams.insert(QStringLiteral("widgetType"), QStringLiteral("NotAWidget"));
    createParams.insert(QStringLiteral("page"), 0);
    QJsonObject geom;
    geom.insert(QStringLiteral("x"), 0); geom.insert(QStringLiteral("y"), 0);
    geom.insert(QStringLiteral("width"), 1); geom.insert(QStringLiteral("height"), 1);
    createParams.insert(QStringLiteral("geometry"), geom);
    createParams.insert(QStringLiteral("baseRevision"), rev);

    QJsonObject reply = sendAndWaitForReply(QStringLiteral("vc.widget.create"), createParams);
    QCOMPARE(reply.value(QStringLiteral("ok")).toBool(), false);
    QCOMPARE(reply.value(QStringLiteral("error")).toObject().value(QStringLiteral("code")).toString(),
              QStringLiteral("INVALID_PARAMS"));
}

void ApiVcDomain_Test::widgetUpdateRequiresAtLeastOneField()
{
    helloAndGetClientId();
    int rev = currentDocRevision();

    QJsonObject createParams;
    createParams.insert(QStringLiteral("widgetType"), QStringLiteral("Label"));
    createParams.insert(QStringLiteral("page"), 0);
    QJsonObject geom;
    geom.insert(QStringLiteral("x"), 0); geom.insert(QStringLiteral("y"), 0);
    geom.insert(QStringLiteral("width"), 1); geom.insert(QStringLiteral("height"), 1);
    createParams.insert(QStringLiteral("geometry"), geom);
    createParams.insert(QStringLiteral("baseRevision"), rev);
    QJsonObject createReply = sendAndWaitForReply(QStringLiteral("vc.widget.create"), createParams);
    QString widgetId = createReply.value(QStringLiteral("result")).toObject().value(QStringLiteral("widgetId")).toString();
    rev = currentDocRevision();

    QJsonObject updateParams;
    updateParams.insert(QStringLiteral("widgetId"), widgetId);
    updateParams.insert(QStringLiteral("baseRevision"), rev);
    QJsonObject reply = sendAndWaitForReply(QStringLiteral("vc.widget.update"), updateParams, QStringLiteral("t-upd"));
    QCOMPARE(reply.value(QStringLiteral("ok")).toBool(), false);
    QCOMPARE(reply.value(QStringLiteral("error")).toObject().value(QStringLiteral("code")).toString(),
              QStringLiteral("INVALID_PARAMS"));
}

void ApiVcDomain_Test::widgetUpdateAppliesGeometry()
{
    helloAndGetClientId();
    int rev = currentDocRevision();

    QJsonObject createParams;
    createParams.insert(QStringLiteral("widgetType"), QStringLiteral("Label"));
    createParams.insert(QStringLiteral("page"), 0);
    QJsonObject geom;
    geom.insert(QStringLiteral("x"), 0); geom.insert(QStringLiteral("y"), 0);
    geom.insert(QStringLiteral("width"), 1); geom.insert(QStringLiteral("height"), 1);
    createParams.insert(QStringLiteral("geometry"), geom);
    createParams.insert(QStringLiteral("baseRevision"), rev);
    QJsonObject createReply = sendAndWaitForReply(QStringLiteral("vc.widget.create"), createParams);
    QString widgetId = createReply.value(QStringLiteral("result")).toObject().value(QStringLiteral("widgetId")).toString();
    rev = currentDocRevision();

    QJsonObject newGeom;
    newGeom.insert(QStringLiteral("x"), 99); newGeom.insert(QStringLiteral("y"), 88);
    newGeom.insert(QStringLiteral("width"), 77); newGeom.insert(QStringLiteral("height"), 66);
    QJsonObject updateParams;
    updateParams.insert(QStringLiteral("widgetId"), widgetId);
    updateParams.insert(QStringLiteral("geometry"), newGeom);
    updateParams.insert(QStringLiteral("isDisabled"), true);
    updateParams.insert(QStringLiteral("baseRevision"), rev);
    QJsonObject reply = sendAndWaitForReply(QStringLiteral("vc.widget.update"), updateParams, QStringLiteral("t-upd"));
    QCOMPARE(reply.value(QStringLiteral("ok")).toBool(), true);

    QJsonObject getParams;
    getParams.insert(QStringLiteral("widgetId"), widgetId);
    QJsonObject getReply = sendAndWaitForReply(QStringLiteral("vc.widget.get"), getParams, QStringLiteral("t-get"));
    QJsonObject widget = getReply.value(QStringLiteral("result")).toObject();
    QCOMPARE(widget.value(QStringLiteral("geometry")).toObject().value(QStringLiteral("x")).toDouble(), 99.0);
    QCOMPARE(widget.value(QStringLiteral("isDisabled")).toBool(), true);
}

void ApiVcDomain_Test::widgetUpdateWithInvalidPageAppliesNothing()
{
    helloAndGetClientId();
    int rev = currentDocRevision();

    QJsonObject createParams;
    createParams.insert(QStringLiteral("widgetType"), QStringLiteral("Label"));
    createParams.insert(QStringLiteral("page"), 0);
    QJsonObject geom;
    geom.insert(QStringLiteral("x"), 0); geom.insert(QStringLiteral("y"), 0);
    geom.insert(QStringLiteral("width"), 1); geom.insert(QStringLiteral("height"), 1);
    createParams.insert(QStringLiteral("geometry"), geom);
    createParams.insert(QStringLiteral("baseRevision"), rev);
    QJsonObject createReply = sendAndWaitForReply(QStringLiteral("vc.widget.create"), createParams);
    QString widgetId = createReply.value(QStringLiteral("result")).toObject().value(QStringLiteral("widgetId")).toString();
    rev = currentDocRevision();

    // isDisabled is valid and would normally apply cleanly, but page=99 does not exist - the whole
    // request must be rejected atomically, with isDisabled left untouched (regression test for a
    // bug where the page-range check ran AFTER geometry/isDisabled/etc. had already been written).
    QJsonObject updateParams;
    updateParams.insert(QStringLiteral("widgetId"), widgetId);
    updateParams.insert(QStringLiteral("isDisabled"), true);
    updateParams.insert(QStringLiteral("page"), 99);
    updateParams.insert(QStringLiteral("baseRevision"), rev);
    QJsonObject reply = sendAndWaitForReply(QStringLiteral("vc.widget.update"), updateParams, QStringLiteral("t-upd"));
    QCOMPARE(reply.value(QStringLiteral("ok")).toBool(), false);
    QCOMPARE(reply.value(QStringLiteral("error")).toObject().value(QStringLiteral("code")).toString(),
              QStringLiteral("NOT_FOUND"));

    QJsonObject getParams;
    getParams.insert(QStringLiteral("widgetId"), widgetId);
    QJsonObject getReply = sendAndWaitForReply(QStringLiteral("vc.widget.get"), getParams, QStringLiteral("t-get"));
    QCOMPARE(getReply.value(QStringLiteral("result")).toObject().value(QStringLiteral("isDisabled")).toBool(), false);
    QCOMPARE(getReply.value(QStringLiteral("result")).toObject().value(QStringLiteral("page")).toInt(), 0);
    QCOMPARE(currentDocRevision(), rev); // no setModified() should have happened either
}

void ApiVcDomain_Test::widgetUpdateRejectsPageChangeOnNestedWidget()
{
    helloAndGetClientId();
    int rev = currentDocRevision();

    QJsonObject frameParams;
    frameParams.insert(QStringLiteral("widgetType"), QStringLiteral("Frame"));
    frameParams.insert(QStringLiteral("page"), 0);
    QJsonObject frameGeom;
    frameGeom.insert(QStringLiteral("x"), 0); frameGeom.insert(QStringLiteral("y"), 0);
    frameGeom.insert(QStringLiteral("width"), 100); frameGeom.insert(QStringLiteral("height"), 100);
    frameParams.insert(QStringLiteral("geometry"), frameGeom);
    frameParams.insert(QStringLiteral("baseRevision"), rev);
    QJsonObject frameReply = sendAndWaitForReply(QStringLiteral("vc.widget.create"), frameParams, QStringLiteral("t-frame"));
    QString frameId = frameReply.value(QStringLiteral("result")).toObject().value(QStringLiteral("widgetId")).toString();
    rev = currentDocRevision();

    QJsonObject childParams;
    childParams.insert(QStringLiteral("widgetType"), QStringLiteral("Button"));
    childParams.insert(QStringLiteral("page"), 0);
    childParams.insert(QStringLiteral("parentId"), frameId);
    QJsonObject childGeom;
    childGeom.insert(QStringLiteral("x"), 5); childGeom.insert(QStringLiteral("y"), 5);
    childGeom.insert(QStringLiteral("width"), 10); childGeom.insert(QStringLiteral("height"), 10);
    childParams.insert(QStringLiteral("geometry"), childGeom);
    childParams.insert(QStringLiteral("baseRevision"), rev);
    QJsonObject childReply = sendAndWaitForReply(QStringLiteral("vc.widget.create"), childParams, QStringLiteral("t-child"));
    QString childId = childReply.value(QStringLiteral("result")).toObject().value(QStringLiteral("widgetId")).toString();
    rev = currentDocRevision();

    // Second page for the child to (attempt to) move to.
    QJsonObject pageParams;
    pageParams.insert(QStringLiteral("index"), 1);
    pageParams.insert(QStringLiteral("baseRevision"), rev);
    sendAndWaitForReply(QStringLiteral("vc.page.create"), pageParams, QStringLiteral("t-p1"));
    rev = currentDocRevision();

    QJsonObject updateParams;
    updateParams.insert(QStringLiteral("widgetId"), childId);
    updateParams.insert(QStringLiteral("page"), 1);
    updateParams.insert(QStringLiteral("baseRevision"), rev);
    QJsonObject reply = sendAndWaitForReply(QStringLiteral("vc.widget.update"), updateParams, QStringLiteral("t-upd"));
    QCOMPARE(reply.value(QStringLiteral("ok")).toBool(), false);
    QCOMPARE(reply.value(QStringLiteral("error")).toObject().value(QStringLiteral("code")).toString(),
              QStringLiteral("INVALID_PARAMS"));
}

void ApiVcDomain_Test::widgetSetConfigMergesPatch()
{
    helloAndGetClientId();
    int rev = currentDocRevision();

    QJsonObject createParams;
    createParams.insert(QStringLiteral("widgetType"), QStringLiteral("Slider"));
    createParams.insert(QStringLiteral("page"), 0);
    QJsonObject geom;
    geom.insert(QStringLiteral("x"), 0); geom.insert(QStringLiteral("y"), 0);
    geom.insert(QStringLiteral("width"), 1); geom.insert(QStringLiteral("height"), 1);
    createParams.insert(QStringLiteral("geometry"), geom);
    QJsonObject initialConfig;
    initialConfig.insert(QStringLiteral("sliderMode"), QStringLiteral("Level"));
    initialConfig.insert(QStringLiteral("catchValues"), true);
    createParams.insert(QStringLiteral("typeConfig"), initialConfig);
    createParams.insert(QStringLiteral("baseRevision"), rev);
    QJsonObject createReply = sendAndWaitForReply(QStringLiteral("vc.widget.create"), createParams);
    QString widgetId = createReply.value(QStringLiteral("result")).toObject().value(QStringLiteral("widgetId")).toString();
    rev = currentDocRevision();

    // Patch only sliderMode - catchValues must survive untouched (merge, not replace).
    QJsonObject patch;
    patch.insert(QStringLiteral("sliderMode"), QStringLiteral("Adjust"));
    QJsonObject setConfigParams;
    setConfigParams.insert(QStringLiteral("widgetId"), widgetId);
    setConfigParams.insert(QStringLiteral("config"), patch);
    setConfigParams.insert(QStringLiteral("baseRevision"), rev);
    QJsonObject reply = sendAndWaitForReply(QStringLiteral("vc.widget.setConfig"), setConfigParams, QStringLiteral("t-cfg"));
    QCOMPARE(reply.value(QStringLiteral("ok")).toBool(), true);

    QJsonObject getParams;
    getParams.insert(QStringLiteral("widgetId"), widgetId);
    QJsonObject getReply = sendAndWaitForReply(QStringLiteral("vc.widget.get"), getParams, QStringLiteral("t-get"));
    QJsonObject typeConfig = getReply.value(QStringLiteral("result")).toObject().value(QStringLiteral("typeConfig")).toObject();
    QCOMPARE(typeConfig.value(QStringLiteral("sliderMode")).toString(), QStringLiteral("Adjust"));
    QCOMPARE(typeConfig.value(QStringLiteral("catchValues")).toBool(), true);
}

void ApiVcDomain_Test::widgetDeleteRecursivelyDeletesChildren()
{
    helloAndGetClientId();
    int rev = currentDocRevision();

    QJsonObject frameParams;
    frameParams.insert(QStringLiteral("widgetType"), QStringLiteral("Frame"));
    frameParams.insert(QStringLiteral("page"), 0);
    QJsonObject geom;
    geom.insert(QStringLiteral("x"), 0); geom.insert(QStringLiteral("y"), 0);
    geom.insert(QStringLiteral("width"), 200); geom.insert(QStringLiteral("height"), 200);
    frameParams.insert(QStringLiteral("geometry"), geom);
    frameParams.insert(QStringLiteral("baseRevision"), rev);
    QJsonObject frameReply = sendAndWaitForReply(QStringLiteral("vc.widget.create"), frameParams, QStringLiteral("t-frame"));
    QString frameId = frameReply.value(QStringLiteral("result")).toObject().value(QStringLiteral("widgetId")).toString();
    rev = currentDocRevision();

    QJsonObject childParams;
    childParams.insert(QStringLiteral("widgetType"), QStringLiteral("Button"));
    childParams.insert(QStringLiteral("page"), 0);
    childParams.insert(QStringLiteral("parentId"), frameId);
    QJsonObject childGeom;
    childGeom.insert(QStringLiteral("x"), 5); childGeom.insert(QStringLiteral("y"), 5);
    childGeom.insert(QStringLiteral("width"), 20); childGeom.insert(QStringLiteral("height"), 20);
    childParams.insert(QStringLiteral("geometry"), childGeom);
    childParams.insert(QStringLiteral("baseRevision"), rev);
    QJsonObject childReply = sendAndWaitForReply(QStringLiteral("vc.widget.create"), childParams, QStringLiteral("t-child"));
    QString childId = childReply.value(QStringLiteral("result")).toObject().value(QStringLiteral("widgetId")).toString();
    rev = currentDocRevision();

    QJsonObject deleteParams;
    deleteParams.insert(QStringLiteral("widgetIds"), QJsonArray{frameId});
    deleteParams.insert(QStringLiteral("baseRevision"), rev);
    QSignalSpy spy(m_client, &QWebSocket::textMessageReceived);
    m_client->sendTextMessage(buildRequest(QStringLiteral("vc.widget.delete"), deleteParams, QStringLiteral("t-del")));
    QVERIFY(QTest::qWaitFor([&]() { return spy.count() >= 2; }, 2000));

    QJsonArray deletedIds;
    for (const QList<QVariant> &frame : spy)
    {
        QJsonObject obj = QJsonDocument::fromJson(frame.at(0).toString().toUtf8()).object();
        if (obj.value(QStringLiteral("type")).toString() == QStringLiteral("event"))
            deletedIds = obj.value(QStringLiteral("data")).toObject().value(QStringLiteral("widgetIds")).toArray();
    }
    QCOMPARE(deletedIds.size(), 2);
    QVERIFY(deletedIds.contains(QJsonValue(frameId)));
    QVERIFY(deletedIds.contains(QJsonValue(childId)));

    QJsonObject getParams;
    getParams.insert(QStringLiteral("widgetId"), childId);
    QJsonObject getReply = sendAndWaitForReply(QStringLiteral("vc.widget.get"), getParams, QStringLiteral("t-get"));
    QCOMPARE(getReply.value(QStringLiteral("ok")).toBool(), false);
}

void ApiVcDomain_Test::widgetReparentMovesWidgetAndAdoptsPage()
{
    helloAndGetClientId();
    int rev = currentDocRevision();

    // Second page.
    QJsonObject pageParams;
    pageParams.insert(QStringLiteral("index"), 1);
    pageParams.insert(QStringLiteral("baseRevision"), rev);
    sendAndWaitForReply(QStringLiteral("vc.page.create"), pageParams, QStringLiteral("t-p1"));
    rev = currentDocRevision();

    // Frame on page 1.
    QJsonObject frameParams;
    frameParams.insert(QStringLiteral("widgetType"), QStringLiteral("Frame"));
    frameParams.insert(QStringLiteral("page"), 1);
    QJsonObject frameGeom;
    frameGeom.insert(QStringLiteral("x"), 0); frameGeom.insert(QStringLiteral("y"), 0);
    frameGeom.insert(QStringLiteral("width"), 200); frameGeom.insert(QStringLiteral("height"), 200);
    frameParams.insert(QStringLiteral("geometry"), frameGeom);
    frameParams.insert(QStringLiteral("baseRevision"), rev);
    QJsonObject frameReply = sendAndWaitForReply(QStringLiteral("vc.widget.create"), frameParams, QStringLiteral("t-frame"));
    QString frameId = frameReply.value(QStringLiteral("result")).toObject().value(QStringLiteral("widgetId")).toString();
    rev = currentDocRevision();

    // Button on page 0 (root).
    QJsonObject btnParams;
    btnParams.insert(QStringLiteral("widgetType"), QStringLiteral("Button"));
    btnParams.insert(QStringLiteral("page"), 0);
    QJsonObject btnGeom;
    btnGeom.insert(QStringLiteral("x"), 1); btnGeom.insert(QStringLiteral("y"), 1);
    btnGeom.insert(QStringLiteral("width"), 10); btnGeom.insert(QStringLiteral("height"), 10);
    btnParams.insert(QStringLiteral("geometry"), btnGeom);
    btnParams.insert(QStringLiteral("baseRevision"), rev);
    QJsonObject btnReply = sendAndWaitForReply(QStringLiteral("vc.widget.create"), btnParams, QStringLiteral("t-btn"));
    QString btnId = btnReply.value(QStringLiteral("result")).toObject().value(QStringLiteral("widgetId")).toString();
    rev = currentDocRevision();

    QJsonObject reparentParams;
    reparentParams.insert(QStringLiteral("widgetId"), btnId);
    reparentParams.insert(QStringLiteral("newParentId"), frameId);
    QJsonObject pos;
    pos.insert(QStringLiteral("x"), 15); pos.insert(QStringLiteral("y"), 25);
    reparentParams.insert(QStringLiteral("position"), pos);
    reparentParams.insert(QStringLiteral("baseRevision"), rev);
    QJsonObject reparentReply = sendAndWaitForReply(QStringLiteral("vc.widget.reparent"), reparentParams, QStringLiteral("t-rp"));
    QCOMPARE(reparentReply.value(QStringLiteral("ok")).toBool(), true);

    QJsonObject getParams;
    getParams.insert(QStringLiteral("widgetId"), btnId);
    QJsonObject getReply = sendAndWaitForReply(QStringLiteral("vc.widget.get"), getParams, QStringLiteral("t-get"));
    QJsonObject widget = getReply.value(QStringLiteral("result")).toObject();
    QCOMPARE(widget.value(QStringLiteral("parentId")).toString(), frameId);
    QCOMPARE(widget.value(QStringLiteral("page")).toInt(), 1); // adopted the frame's page
    QCOMPARE(widget.value(QStringLiteral("geometry")).toObject().value(QStringLiteral("x")).toDouble(), 15.0);
    QCOMPARE(widget.value(QStringLiteral("geometry")).toObject().value(QStringLiteral("width")).toDouble(), 10.0); // unchanged
}

void ApiVcDomain_Test::widgetReparentRejectsCycle()
{
    helloAndGetClientId();
    int rev = currentDocRevision();

    QJsonObject frameParams;
    frameParams.insert(QStringLiteral("widgetType"), QStringLiteral("Frame"));
    frameParams.insert(QStringLiteral("page"), 0);
    QJsonObject frameGeom;
    frameGeom.insert(QStringLiteral("x"), 0); frameGeom.insert(QStringLiteral("y"), 0);
    frameGeom.insert(QStringLiteral("width"), 200); frameGeom.insert(QStringLiteral("height"), 200);
    frameParams.insert(QStringLiteral("geometry"), frameGeom);
    frameParams.insert(QStringLiteral("baseRevision"), rev);
    QJsonObject frameReply = sendAndWaitForReply(QStringLiteral("vc.widget.create"), frameParams, QStringLiteral("t-frame"));
    QString frameId = frameReply.value(QStringLiteral("result")).toObject().value(QStringLiteral("widgetId")).toString();
    rev = currentDocRevision();

    QJsonObject childFrameParams;
    childFrameParams.insert(QStringLiteral("widgetType"), QStringLiteral("SoloFrame"));
    childFrameParams.insert(QStringLiteral("page"), 0);
    childFrameParams.insert(QStringLiteral("parentId"), frameId);
    QJsonObject childGeom;
    childGeom.insert(QStringLiteral("x"), 5); childGeom.insert(QStringLiteral("y"), 5);
    childGeom.insert(QStringLiteral("width"), 50); childGeom.insert(QStringLiteral("height"), 50);
    childFrameParams.insert(QStringLiteral("geometry"), childGeom);
    childFrameParams.insert(QStringLiteral("baseRevision"), rev);
    QJsonObject childFrameReply = sendAndWaitForReply(QStringLiteral("vc.widget.create"), childFrameParams, QStringLiteral("t-child"));
    QString childFrameId = childFrameReply.value(QStringLiteral("result")).toObject().value(QStringLiteral("widgetId")).toString();
    rev = currentDocRevision();

    // Try to reparent the outer frame into its own child - must be rejected.
    QJsonObject reparentParams;
    reparentParams.insert(QStringLiteral("widgetId"), frameId);
    reparentParams.insert(QStringLiteral("newParentId"), childFrameId);
    QJsonObject pos;
    pos.insert(QStringLiteral("x"), 0); pos.insert(QStringLiteral("y"), 0);
    reparentParams.insert(QStringLiteral("position"), pos);
    reparentParams.insert(QStringLiteral("baseRevision"), rev);
    QJsonObject reply = sendAndWaitForReply(QStringLiteral("vc.widget.reparent"), reparentParams, QStringLiteral("t-rp"));
    QCOMPARE(reply.value(QStringLiteral("ok")).toBool(), false);
    QCOMPARE(reply.value(QStringLiteral("error")).toObject().value(QStringLiteral("code")).toString(),
              QStringLiteral("INVALID_PARAMS"));
}

void ApiVcDomain_Test::widgetRepositionBulkUpdatesAllOrNothing()
{
    helloAndGetClientId();
    int rev = currentDocRevision();

    QJsonObject aParams;
    aParams.insert(QStringLiteral("widgetType"), QStringLiteral("Label"));
    aParams.insert(QStringLiteral("page"), 0);
    QJsonObject aGeom;
    aGeom.insert(QStringLiteral("x"), 0); aGeom.insert(QStringLiteral("y"), 0);
    aGeom.insert(QStringLiteral("width"), 1); aGeom.insert(QStringLiteral("height"), 1);
    aParams.insert(QStringLiteral("geometry"), aGeom);
    aParams.insert(QStringLiteral("baseRevision"), rev);
    QJsonObject aReply = sendAndWaitForReply(QStringLiteral("vc.widget.create"), aParams, QStringLiteral("t-a"));
    QString aId = aReply.value(QStringLiteral("result")).toObject().value(QStringLiteral("widgetId")).toString();
    rev = currentDocRevision();

    QJsonObject bParams = aParams;
    bParams.insert(QStringLiteral("baseRevision"), rev);
    QJsonObject bReply = sendAndWaitForReply(QStringLiteral("vc.widget.create"), bParams, QStringLiteral("t-b"));
    QString bId = bReply.value(QStringLiteral("result")).toObject().value(QStringLiteral("widgetId")).toString();
    rev = currentDocRevision();

    // First attempt references a non-existent widget id - must fail atomically, touching neither widget.
    QJsonObject badGeom;
    badGeom.insert(QStringLiteral("x"), 500); badGeom.insert(QStringLiteral("y"), 500);
    badGeom.insert(QStringLiteral("width"), 1); badGeom.insert(QStringLiteral("height"), 1);
    QJsonObject badEntryA; badEntryA.insert(QStringLiteral("widgetId"), aId); badEntryA.insert(QStringLiteral("geometry"), badGeom);
    QJsonObject badEntryMissing; badEntryMissing.insert(QStringLiteral("widgetId"), QStringLiteral("999999")); badEntryMissing.insert(QStringLiteral("geometry"), badGeom);
    QJsonObject badParams;
    badParams.insert(QStringLiteral("widgets"), QJsonArray{badEntryA, badEntryMissing});
    badParams.insert(QStringLiteral("baseRevision"), rev);
    QJsonObject badReply = sendAndWaitForReply(QStringLiteral("vc.widget.reposition"), badParams, QStringLiteral("t-bad"));
    QCOMPARE(badReply.value(QStringLiteral("ok")).toBool(), false);

    QJsonObject getAParams; getAParams.insert(QStringLiteral("widgetId"), aId);
    QJsonObject getAReply = sendAndWaitForReply(QStringLiteral("vc.widget.get"), getAParams, QStringLiteral("t-geta"));
    QCOMPARE(getAReply.value(QStringLiteral("result")).toObject().value(QStringLiteral("geometry")).toObject()
             .value(QStringLiteral("x")).toDouble(), 0.0);

    // Now a valid bulk reposition of both widgets together.
    QJsonObject geomA; geomA.insert(QStringLiteral("x"), 10); geomA.insert(QStringLiteral("y"), 10);
    geomA.insert(QStringLiteral("width"), 5); geomA.insert(QStringLiteral("height"), 5);
    QJsonObject geomB; geomB.insert(QStringLiteral("x"), 20); geomB.insert(QStringLiteral("y"), 20);
    geomB.insert(QStringLiteral("width"), 6); geomB.insert(QStringLiteral("height"), 6);
    QJsonObject entryA; entryA.insert(QStringLiteral("widgetId"), aId); entryA.insert(QStringLiteral("geometry"), geomA);
    QJsonObject entryB; entryB.insert(QStringLiteral("widgetId"), bId); entryB.insert(QStringLiteral("geometry"), geomB);
    QJsonObject goodParams;
    goodParams.insert(QStringLiteral("widgets"), QJsonArray{entryA, entryB});
    goodParams.insert(QStringLiteral("baseRevision"), rev);
    QJsonObject goodReply = sendAndWaitForReply(QStringLiteral("vc.widget.reposition"), goodParams, QStringLiteral("t-good"));
    QCOMPARE(goodReply.value(QStringLiteral("ok")).toBool(), true);

    QJsonObject getAReply2 = sendAndWaitForReply(QStringLiteral("vc.widget.get"), getAParams, QStringLiteral("t-geta2"));
    QCOMPARE(getAReply2.value(QStringLiteral("result")).toObject().value(QStringLiteral("geometry")).toObject()
             .value(QStringLiteral("x")).toDouble(), 10.0);
}

QTEST_MAIN(ApiVcDomain_Test)

/*****************************************************************************
 * Live interaction
 *****************************************************************************/

QString ApiVcDomain_Test::createWidget(const QString &widgetType, const QJsonObject &typeConfig)
{
    QJsonObject params;
    params.insert(QStringLiteral("widgetType"), widgetType);
    params.insert(QStringLiteral("page"), 0);
    QJsonObject geom;
    geom.insert(QStringLiteral("x"), 0); geom.insert(QStringLiteral("y"), 0);
    geom.insert(QStringLiteral("width"), 1); geom.insert(QStringLiteral("height"), 1);
    params.insert(QStringLiteral("geometry"), geom);
    if (typeConfig.isEmpty() == false)
        params.insert(QStringLiteral("typeConfig"), typeConfig);
    params.insert(QStringLiteral("baseRevision"), currentDocRevision());
    QJsonObject reply = sendAndWaitForReply(QStringLiteral("vc.widget.create"), params, QStringLiteral("t-create-") + widgetType);
    return reply.value(QStringLiteral("result")).toObject().value(QStringLiteral("widgetId")).toString();
}

QJsonObject ApiVcDomain_Test::widgetParams(const QString &widgetId, const QJsonObject &extra)
{
    QJsonObject params = extra;
    params.insert(QStringLiteral("widgetId"), widgetId);
    return params;
}

void ApiVcDomain_Test::liveButtonPressTogglesOnDownEdgeAndBroadcasts()
{
    QString clientId = helloAndGetClientId();
    QJsonObject cfg;
    cfg.insert(QStringLiteral("actionType"), QStringLiteral("Toggle"));
    cfg.insert(QStringLiteral("functionID"), QStringLiteral("7"));
    QString buttonId = createWidget(QStringLiteral("Button"), cfg);
    QVERIFY(buttonId.isEmpty() == false);

    QSignalSpy spy(m_client, &QWebSocket::textMessageReceived);

    // Down-edge: toggles on -> exactly one stateChanged(active) with our clientId as origin.
    QJsonObject down;
    down.insert(QStringLiteral("pressed"), true);
    QJsonObject reply = sendAndWaitForReply(QStringLiteral("vc.button.press"), widgetParams(buttonId, down), QStringLiteral("t-down"));
    QCOMPARE(reply.value(QStringLiteral("ok")).toBool(), true);
    QVERIFY(reply.value(QStringLiteral("result")).toObject().isEmpty()); // bare ack per §4b

    QList<QJsonObject> events = eventsWithTopic(spy, QStringLiteral("vc.button.stateChanged"));
    QCOMPARE(events.size(), 1);
    QCOMPARE(events.at(0).value(QStringLiteral("widgetId")).toString(), buttonId);
    QCOMPARE(events.at(0).value(QStringLiteral("state")).toString(), QStringLiteral("active"));
    QCOMPARE(events.at(0).value(QStringLiteral("_origin")).toString(), clientId);

    // Up-edge: a no-op for Toggle - no second event, still active.
    QJsonObject up;
    up.insert(QStringLiteral("pressed"), false);
    reply = sendAndWaitForReply(QStringLiteral("vc.button.press"), widgetParams(buttonId, up), QStringLiteral("t-up"));
    QCOMPARE(reply.value(QStringLiteral("ok")).toBool(), true);
    QCOMPARE(eventsWithTopic(spy, QStringLiteral("vc.button.stateChanged")).size(), 1);

    QJsonObject get = sendAndWaitForReply(QStringLiteral("vc.widget.get"), widgetParams(buttonId), QStringLiteral("t-get1"));
    QCOMPARE(get.value(QStringLiteral("result")).toObject().value(QStringLiteral("state")).toString(), QStringLiteral("active"));

    // Second down-edge toggles back off.
    reply = sendAndWaitForReply(QStringLiteral("vc.button.press"), widgetParams(buttonId, down), QStringLiteral("t-down2"));
    QCOMPARE(reply.value(QStringLiteral("ok")).toBool(), true);
    events = eventsWithTopic(spy, QStringLiteral("vc.button.stateChanged"));
    QCOMPARE(events.size(), 2);
    QCOMPARE(events.at(1).value(QStringLiteral("state")).toString(), QStringLiteral("inactive"));
}

void ApiVcDomain_Test::liveButtonPressFlashFollowsBothEdges()
{
    helloAndGetClientId();
    QJsonObject cfg;
    cfg.insert(QStringLiteral("actionType"), QStringLiteral("Flash"));
    cfg.insert(QStringLiteral("functionID"), QStringLiteral("7"));
    QString buttonId = createWidget(QStringLiteral("Button"), cfg);

    QSignalSpy spy(m_client, &QWebSocket::textMessageReceived);
    QJsonObject down; down.insert(QStringLiteral("pressed"), true);
    QJsonObject up; up.insert(QStringLiteral("pressed"), false);

    QCOMPARE(sendAndWaitForReply(QStringLiteral("vc.button.press"), widgetParams(buttonId, down), QStringLiteral("t-fd")).value(QStringLiteral("ok")).toBool(), true);
    QCOMPARE(sendAndWaitForReply(QStringLiteral("vc.button.press"), widgetParams(buttonId, up), QStringLiteral("t-fu")).value(QStringLiteral("ok")).toBool(), true);

    QList<QJsonObject> events = eventsWithTopic(spy, QStringLiteral("vc.button.stateChanged"));
    QCOMPARE(events.size(), 2);
    QCOMPARE(events.at(0).value(QStringLiteral("state")).toString(), QStringLiteral("active"));
    QCOMPARE(events.at(1).value(QStringLiteral("state")).toString(), QStringLiteral("inactive"));
}

void ApiVcDomain_Test::liveButtonPressRejectsUnknownWidgetWrongTypeAndBadParams()
{
    helloAndGetClientId();
    QString sliderId = createWidget(QStringLiteral("Slider"));
    QJsonObject down; down.insert(QStringLiteral("pressed"), true);

    // Unknown id -> NOT_FOUND
    QJsonObject reply = sendAndWaitForReply(QStringLiteral("vc.button.press"), widgetParams(QStringLiteral("9999"), down), QStringLiteral("t-nf"));
    QCOMPARE(reply.value(QStringLiteral("ok")).toBool(), false);
    QCOMPARE(errorCode(reply), QStringLiteral("NOT_FOUND"));

    // Unparseable id -> NOT_FOUND too
    reply = sendAndWaitForReply(QStringLiteral("vc.button.press"), widgetParams(QStringLiteral("not-an-id"), down), QStringLiteral("t-nf2"));
    QCOMPARE(errorCode(reply), QStringLiteral("NOT_FOUND"));

    // Exists but is a Slider -> INVALID_PARAMS, details name the actual type
    reply = sendAndWaitForReply(QStringLiteral("vc.button.press"), widgetParams(sliderId, down), QStringLiteral("t-wt"));
    QCOMPARE(reply.value(QStringLiteral("ok")).toBool(), false);
    QCOMPARE(errorCode(reply), QStringLiteral("INVALID_PARAMS"));
    QCOMPARE(reply.value(QStringLiteral("error")).toObject().value(QStringLiteral("details")).toObject()
                 .value(QStringLiteral("widgetType")).toString(), QStringLiteral("Slider"));

    // Missing / non-boolean pressed -> INVALID_PARAMS (checked before the widget lookup)
    QString buttonId = createWidget(QStringLiteral("Button"));
    reply = sendAndWaitForReply(QStringLiteral("vc.button.press"), widgetParams(buttonId), QStringLiteral("t-np"));
    QCOMPARE(errorCode(reply), QStringLiteral("INVALID_PARAMS"));
    QJsonObject stringy; stringy.insert(QStringLiteral("pressed"), QStringLiteral("true"));
    reply = sendAndWaitForReply(QStringLiteral("vc.button.press"), widgetParams(buttonId, stringy), QStringLiteral("t-sp"));
    QCOMPARE(errorCode(reply), QStringLiteral("INVALID_PARAMS"));
}

void ApiVcDomain_Test::liveButtonPressRefusesDisabledWidgetAndMissingFunction()
{
    helloAndGetClientId();
    QJsonObject down; down.insert(QStringLiteral("pressed"), true);

    // Toggle button without a function: the engine's requestStateChange() would silently do nothing.
    QJsonObject cfg;
    cfg.insert(QStringLiteral("actionType"), QStringLiteral("Toggle"));
    QString orphanId = createWidget(QStringLiteral("Button"), cfg);
    QJsonObject reply = sendAndWaitForReply(QStringLiteral("vc.button.press"), widgetParams(orphanId, down), QStringLiteral("t-orphan"));
    QCOMPARE(reply.value(QStringLiteral("ok")).toBool(), false);
    QCOMPARE(errorCode(reply), QStringLiteral("INVALID_STATE"));

    // Disabled widget: the on-screen button ignores input, so must the API.
    cfg.insert(QStringLiteral("functionID"), QStringLiteral("7"));
    QString buttonId = createWidget(QStringLiteral("Button"), cfg);
    QJsonObject update;
    update.insert(QStringLiteral("widgetId"), buttonId);
    update.insert(QStringLiteral("isDisabled"), true);
    update.insert(QStringLiteral("baseRevision"), currentDocRevision());
    QCOMPARE(sendAndWaitForReply(QStringLiteral("vc.widget.update"), update, QStringLiteral("t-dis")).value(QStringLiteral("ok")).toBool(), true);

    reply = sendAndWaitForReply(QStringLiteral("vc.button.press"), widgetParams(buttonId, down), QStringLiteral("t-dis2"));
    QCOMPARE(reply.value(QStringLiteral("ok")).toBool(), false);
    QCOMPARE(errorCode(reply), QStringLiteral("INVALID_STATE"));
}

void ApiVcDomain_Test::liveSliderSetValueBroadcastsAndValidates()
{
    QString clientId = helloAndGetClientId();
    QString sliderId = createWidget(QStringLiteral("Slider"));
    QString buttonId = createWidget(QStringLiteral("Button"));

    QSignalSpy spy(m_client, &QWebSocket::textMessageReceived);
    QJsonObject v; v.insert(QStringLiteral("value"), 200);
    QJsonObject reply = sendAndWaitForReply(QStringLiteral("vc.slider.setValue"), widgetParams(sliderId, v), QStringLiteral("t-sv"));
    QCOMPARE(reply.value(QStringLiteral("ok")).toBool(), true);
    QVERIFY(reply.value(QStringLiteral("result")).toObject().isEmpty());

    QList<QJsonObject> events = eventsWithTopic(spy, QStringLiteral("vc.slider.valueChanged"));
    QCOMPARE(events.size(), 1);
    QCOMPARE(events.at(0).value(QStringLiteral("widgetId")).toString(), sliderId);
    QCOMPARE(events.at(0).value(QStringLiteral("value")).toInt(), 200);
    QCOMPARE(events.at(0).value(QStringLiteral("_origin")).toString(), clientId);

    // Same value again: no change, no second event (last-write-wins, but nothing to report).
    reply = sendAndWaitForReply(QStringLiteral("vc.slider.setValue"), widgetParams(sliderId, v), QStringLiteral("t-sv2"));
    QCOMPARE(reply.value(QStringLiteral("ok")).toBool(), true);
    QCOMPARE(eventsWithTopic(spy, QStringLiteral("vc.slider.valueChanged")).size(), 1);

    QJsonObject get = sendAndWaitForReply(QStringLiteral("vc.widget.get"), widgetParams(sliderId), QStringLiteral("t-sg"));
    QJsonObject w = get.value(QStringLiteral("result")).toObject();
    QCOMPARE(w.value(QStringLiteral("value")).toInt(), 200);
    QCOMPARE(w.value(QStringLiteral("min")).toDouble(), 0.0);
    QCOMPARE(w.value(QStringLiteral("max")).toDouble(), 255.0);

    // Out of range / wrong type / missing -> INVALID_PARAMS
    QJsonObject big; big.insert(QStringLiteral("value"), 256);
    QCOMPARE(errorCode(sendAndWaitForReply(QStringLiteral("vc.slider.setValue"), widgetParams(sliderId, big), QStringLiteral("t-big"))), QStringLiteral("INVALID_PARAMS"));
    QJsonObject neg; neg.insert(QStringLiteral("value"), -1);
    QCOMPARE(errorCode(sendAndWaitForReply(QStringLiteral("vc.slider.setValue"), widgetParams(sliderId, neg), QStringLiteral("t-neg"))), QStringLiteral("INVALID_PARAMS"));
    QJsonObject frac; frac.insert(QStringLiteral("value"), 12.5);
    QCOMPARE(errorCode(sendAndWaitForReply(QStringLiteral("vc.slider.setValue"), widgetParams(sliderId, frac), QStringLiteral("t-frac"))), QStringLiteral("INVALID_PARAMS"));
    // Whole numbers outside qint32 read back as 0 through QJsonValue::toInt() - they must be
    // rejected, not silently applied as value 0.
    QJsonObject huge; huge.insert(QStringLiteral("value"), 4294967296.0);
    QCOMPARE(errorCode(sendAndWaitForReply(QStringLiteral("vc.slider.setValue"), widgetParams(sliderId, huge), QStringLiteral("t-huge"))), QStringLiteral("INVALID_PARAMS"));
    QJsonObject astro; astro.insert(QStringLiteral("value"), 1e300);
    QCOMPARE(errorCode(sendAndWaitForReply(QStringLiteral("vc.slider.setValue"), widgetParams(sliderId, astro), QStringLiteral("t-astro"))), QStringLiteral("INVALID_PARAMS"));
    QCOMPARE(sendAndWaitForReply(QStringLiteral("vc.widget.get"), widgetParams(sliderId), QStringLiteral("t-sg2"))
                 .value(QStringLiteral("result")).toObject().value(QStringLiteral("value")).toInt(), 200);
    QJsonObject str; str.insert(QStringLiteral("value"), QStringLiteral("100"));
    QCOMPARE(errorCode(sendAndWaitForReply(QStringLiteral("vc.slider.setValue"), widgetParams(sliderId, str), QStringLiteral("t-str"))), QStringLiteral("INVALID_PARAMS"));
    QCOMPARE(errorCode(sendAndWaitForReply(QStringLiteral("vc.slider.setValue"), widgetParams(sliderId), QStringLiteral("t-miss"))), QStringLiteral("INVALID_PARAMS"));

    // Wrong widget type / unknown widget
    QCOMPARE(errorCode(sendAndWaitForReply(QStringLiteral("vc.slider.setValue"), widgetParams(buttonId, v), QStringLiteral("t-swt"))), QStringLiteral("INVALID_PARAMS"));
    QCOMPARE(errorCode(sendAndWaitForReply(QStringLiteral("vc.slider.setValue"), widgetParams(QStringLiteral("4242"), v), QStringLiteral("t-snf"))), QStringLiteral("NOT_FOUND"));
}

void ApiVcDomain_Test::liveCueListTransportAndGet()
{
    QString clientId = helloAndGetClientId();
    QJsonObject cfg;
    cfg.insert(QStringLiteral("chaserID"), QStringLiteral("3"));
    QString cueListId = createWidget(QStringLiteral("CueList"), cfg);
    QString orphanId = createWidget(QStringLiteral("CueList"));
    QString buttonId = createWidget(QStringLiteral("Button"));

    // Initial get: 3 fake steps, stopped, nothing selected.
    QJsonObject get = sendAndWaitForReply(QStringLiteral("vc.cueList.get"), widgetParams(cueListId), QStringLiteral("t-cg"));
    QCOMPARE(get.value(QStringLiteral("ok")).toBool(), true);
    QJsonObject r = get.value(QStringLiteral("result")).toObject();
    QJsonArray steps = r.value(QStringLiteral("steps")).toArray();
    QCOMPARE(steps.size(), 3);
    QJsonObject step0 = steps.at(0).toObject();
    QCOMPARE(step0.value(QStringLiteral("index")).toInt(), 0);
    QVERIFY(step0.contains(QStringLiteral("name")));
    QVERIFY(step0.contains(QStringLiteral("functionId")));
    QVERIFY(step0.contains(QStringLiteral("fadeIn")));
    QVERIFY(step0.contains(QStringLiteral("fadeOut")));
    QVERIFY(step0.contains(QStringLiteral("hold")));
    QVERIFY(step0.contains(QStringLiteral("notes")));
    QCOMPARE(r.value(QStringLiteral("playbackIndex")).toInt(), -1);
    QCOMPARE(r.value(QStringLiteral("running")).toBool(), false);
    QCOMPARE(r.value(QStringLiteral("paused")).toBool(), false);

    QSignalSpy spy(m_client, &QWebSocket::textMessageReceived);

    // play -> running at step 0
    QJsonObject reply = sendAndWaitForReply(QStringLiteral("vc.cueList.play"), widgetParams(cueListId), QStringLiteral("t-play"));
    QCOMPARE(reply.value(QStringLiteral("ok")).toBool(), true);
    QList<QJsonObject> events = eventsWithTopic(spy, QStringLiteral("vc.cueList.playbackChanged"));
    QCOMPARE(events.size(), 1);
    QCOMPARE(events.at(0).value(QStringLiteral("widgetId")).toString(), cueListId);
    QCOMPARE(events.at(0).value(QStringLiteral("playbackIndex")).toInt(), 0);
    QCOMPARE(events.at(0).value(QStringLiteral("running")).toBool(), true);
    QCOMPARE(events.at(0).value(QStringLiteral("paused")).toBool(), false);
    QCOMPARE(events.at(0).value(QStringLiteral("_origin")).toString(), clientId);

    // next / previous move the index
    QCOMPARE(sendAndWaitForReply(QStringLiteral("vc.cueList.next"), widgetParams(cueListId), QStringLiteral("t-next")).value(QStringLiteral("ok")).toBool(), true);
    events = eventsWithTopic(spy, QStringLiteral("vc.cueList.playbackChanged"));
    QCOMPARE(events.size(), 2);
    QCOMPARE(events.at(1).value(QStringLiteral("playbackIndex")).toInt(), 1);
    QCOMPARE(sendAndWaitForReply(QStringLiteral("vc.cueList.previous"), widgetParams(cueListId), QStringLiteral("t-prev")).value(QStringLiteral("ok")).toBool(), true);
    events = eventsWithTopic(spy, QStringLiteral("vc.cueList.playbackChanged"));
    QCOMPARE(events.size(), 3);
    QCOMPARE(events.at(2).value(QStringLiteral("playbackIndex")).toInt(), 0);

    // play while running = pause (PlayPauseStop layout), stop -> stopped
    QCOMPARE(sendAndWaitForReply(QStringLiteral("vc.cueList.play"), widgetParams(cueListId), QStringLiteral("t-pause")).value(QStringLiteral("ok")).toBool(), true);
    events = eventsWithTopic(spy, QStringLiteral("vc.cueList.playbackChanged"));
    QCOMPARE(events.size(), 4);
    QCOMPARE(events.at(3).value(QStringLiteral("running")).toBool(), true);
    QCOMPARE(events.at(3).value(QStringLiteral("paused")).toBool(), true);
    QCOMPARE(sendAndWaitForReply(QStringLiteral("vc.cueList.stop"), widgetParams(cueListId), QStringLiteral("t-stop")).value(QStringLiteral("ok")).toBool(), true);
    events = eventsWithTopic(spy, QStringLiteral("vc.cueList.playbackChanged"));
    QCOMPARE(events.size(), 5);
    QCOMPARE(events.at(4).value(QStringLiteral("running")).toBool(), false);
    QCOMPARE(events.at(4).value(QStringLiteral("paused")).toBool(), false);

    // widget.get mirrors the live state (no steps there - those are cueList.get only)
    get = sendAndWaitForReply(QStringLiteral("vc.widget.get"), widgetParams(cueListId), QStringLiteral("t-cwg"));
    r = get.value(QStringLiteral("result")).toObject();
    QCOMPARE(r.value(QStringLiteral("playbackIndex")).toInt(), 0);
    QCOMPARE(r.value(QStringLiteral("running")).toBool(), false);
    QVERIFY(r.contains(QStringLiteral("steps")) == false);

    // No Chaser attached -> INVALID_STATE; wrong type -> INVALID_PARAMS; unknown -> NOT_FOUND
    QCOMPARE(errorCode(sendAndWaitForReply(QStringLiteral("vc.cueList.play"), widgetParams(orphanId), QStringLiteral("t-orph"))), QStringLiteral("INVALID_STATE"));
    QCOMPARE(errorCode(sendAndWaitForReply(QStringLiteral("vc.cueList.play"), widgetParams(buttonId), QStringLiteral("t-cwt"))), QStringLiteral("INVALID_PARAMS"));
    QCOMPARE(errorCode(sendAndWaitForReply(QStringLiteral("vc.cueList.get"), widgetParams(buttonId), QStringLiteral("t-cgt"))), QStringLiteral("INVALID_PARAMS"));
    QCOMPARE(errorCode(sendAndWaitForReply(QStringLiteral("vc.cueList.stop"), widgetParams(QStringLiteral("777")), QStringLiteral("t-cnf"))), QStringLiteral("NOT_FOUND"));
    QCOMPARE(errorCode(sendAndWaitForReply(QStringLiteral("vc.cueList.get"), widgetParams(QStringLiteral("777")), QStringLiteral("t-cgnf"))), QStringLiteral("NOT_FOUND"));
    // get on a chaser-less cue list is fine (read-only) and just has no steps
    get = sendAndWaitForReply(QStringLiteral("vc.cueList.get"), widgetParams(orphanId), QStringLiteral("t-og"));
    QCOMPARE(get.value(QStringLiteral("ok")).toBool(), true);
    QCOMPARE(get.value(QStringLiteral("result")).toObject().value(QStringLiteral("steps")).toArray().size(), 0);
}

void ApiVcDomain_Test::liveCueListSetPlaybackIndexValidatesRange()
{
    helloAndGetClientId();
    QJsonObject cfg;
    cfg.insert(QStringLiteral("chaserID"), QStringLiteral("3"));
    QString cueListId = createWidget(QStringLiteral("CueList"), cfg);

    QSignalSpy spy(m_client, &QWebSocket::textMessageReceived);
    QJsonObject idx; idx.insert(QStringLiteral("index"), 2);
    QJsonObject reply = sendAndWaitForReply(QStringLiteral("vc.cueList.setPlaybackIndex"), widgetParams(cueListId, idx), QStringLiteral("t-spi"));
    QCOMPARE(reply.value(QStringLiteral("ok")).toBool(), true);
    QList<QJsonObject> events = eventsWithTopic(spy, QStringLiteral("vc.cueList.playbackChanged"));
    QCOMPARE(events.size(), 1);
    QCOMPARE(events.at(0).value(QStringLiteral("playbackIndex")).toInt(), 2);
    QCOMPARE(events.at(0).value(QStringLiteral("running")).toBool(), true);

    // -1 is allowed (clears the selection)
    QJsonObject none; none.insert(QStringLiteral("index"), -1);
    QCOMPARE(sendAndWaitForReply(QStringLiteral("vc.cueList.setPlaybackIndex"), widgetParams(cueListId, none), QStringLiteral("t-spn")).value(QStringLiteral("ok")).toBool(), true);

    // Out of range / non-integer -> INVALID_PARAMS with the step count in details
    QJsonObject big; big.insert(QStringLiteral("index"), 3);
    reply = sendAndWaitForReply(QStringLiteral("vc.cueList.setPlaybackIndex"), widgetParams(cueListId, big), QStringLiteral("t-spb"));
    QCOMPARE(errorCode(reply), QStringLiteral("INVALID_PARAMS"));
    QCOMPARE(reply.value(QStringLiteral("error")).toObject().value(QStringLiteral("details")).toObject().value(QStringLiteral("stepCount")).toInt(), 3);
    QJsonObject tooLow; tooLow.insert(QStringLiteral("index"), -2);
    QCOMPARE(errorCode(sendAndWaitForReply(QStringLiteral("vc.cueList.setPlaybackIndex"), widgetParams(cueListId, tooLow), QStringLiteral("t-spl"))), QStringLiteral("INVALID_PARAMS"));
    QJsonObject str; str.insert(QStringLiteral("index"), QStringLiteral("1"));
    QCOMPARE(errorCode(sendAndWaitForReply(QStringLiteral("vc.cueList.setPlaybackIndex"), widgetParams(cueListId, str), QStringLiteral("t-sps"))), QStringLiteral("INVALID_PARAMS"));
    QCOMPARE(errorCode(sendAndWaitForReply(QStringLiteral("vc.cueList.setPlaybackIndex"), widgetParams(cueListId), QStringLiteral("t-spm"))), QStringLiteral("INVALID_PARAMS"));

    // Unknown widget / wrong widget type, for the transport methods not covered in liveCueListTransportAndGet
    QString buttonId = createWidget(QStringLiteral("Button"));
    QCOMPARE(errorCode(sendAndWaitForReply(QStringLiteral("vc.cueList.setPlaybackIndex"), widgetParams(QStringLiteral("777"), idx), QStringLiteral("t-spnf"))), QStringLiteral("NOT_FOUND"));
    QCOMPARE(errorCode(sendAndWaitForReply(QStringLiteral("vc.cueList.setPlaybackIndex"), widgetParams(buttonId, idx), QStringLiteral("t-spwt"))), QStringLiteral("INVALID_PARAMS"));
    QCOMPARE(errorCode(sendAndWaitForReply(QStringLiteral("vc.cueList.next"), widgetParams(QStringLiteral("777")), QStringLiteral("t-nxnf"))), QStringLiteral("NOT_FOUND"));
    QCOMPARE(errorCode(sendAndWaitForReply(QStringLiteral("vc.cueList.next"), widgetParams(buttonId), QStringLiteral("t-nxwt"))), QStringLiteral("INVALID_PARAMS"));
    QCOMPARE(errorCode(sendAndWaitForReply(QStringLiteral("vc.cueList.previous"), widgetParams(QStringLiteral("777")), QStringLiteral("t-pvnf"))), QStringLiteral("NOT_FOUND"));
    QCOMPARE(errorCode(sendAndWaitForReply(QStringLiteral("vc.cueList.previous"), widgetParams(buttonId), QStringLiteral("t-pvwt"))), QStringLiteral("INVALID_PARAMS"));

    // The spec's original "playbackIndex" spelling is accepted as an alias
    QJsonObject alias; alias.insert(QStringLiteral("playbackIndex"), 1);
    QCOMPARE(sendAndWaitForReply(QStringLiteral("vc.cueList.setPlaybackIndex"), widgetParams(cueListId, alias), QStringLiteral("t-spa")).value(QStringLiteral("ok")).toBool(), true);
    events = eventsWithTopic(spy, QStringLiteral("vc.cueList.playbackChanged"));
    QCOMPARE(events.last().value(QStringLiteral("playbackIndex")).toInt(), 1);
}

void ApiVcDomain_Test::liveXyPadSetPositionBroadcastsAndValidates()
{
    QString clientId = helloAndGetClientId();
    QString padId = createWidget(QStringLiteral("XYPad"));
    QString buttonId = createWidget(QStringLiteral("Button"));

    QSignalSpy spy(m_client, &QWebSocket::textMessageReceived);
    QJsonObject pos; pos.insert(QStringLiteral("x"), 0.25); pos.insert(QStringLiteral("y"), 1.0);
    QJsonObject reply = sendAndWaitForReply(QStringLiteral("vc.xyPad.setPosition"), widgetParams(padId, pos), QStringLiteral("t-xy"));
    QCOMPARE(reply.value(QStringLiteral("ok")).toBool(), true);

    QList<QJsonObject> events = eventsWithTopic(spy, QStringLiteral("vc.xyPad.positionChanged"));
    QCOMPARE(events.size(), 1);
    QCOMPARE(events.at(0).value(QStringLiteral("widgetId")).toString(), padId);
    QCOMPARE(events.at(0).value(QStringLiteral("x")).toDouble(), 0.25);
    QCOMPARE(events.at(0).value(QStringLiteral("y")).toDouble(), 1.0);
    QCOMPARE(events.at(0).value(QStringLiteral("_origin")).toString(), clientId);

    QJsonObject get = sendAndWaitForReply(QStringLiteral("vc.widget.get"), widgetParams(padId), QStringLiteral("t-xg"));
    QCOMPARE(get.value(QStringLiteral("result")).toObject().value(QStringLiteral("x")).toDouble(), 0.25);
    QCOMPARE(get.value(QStringLiteral("result")).toObject().value(QStringLiteral("y")).toDouble(), 1.0);

    // Out of 0..1 / missing / non-numeric -> INVALID_PARAMS
    QJsonObject big; big.insert(QStringLiteral("x"), 1.5); big.insert(QStringLiteral("y"), 0.5);
    QCOMPARE(errorCode(sendAndWaitForReply(QStringLiteral("vc.xyPad.setPosition"), widgetParams(padId, big), QStringLiteral("t-xb"))), QStringLiteral("INVALID_PARAMS"));
    QJsonObject neg; neg.insert(QStringLiteral("x"), 0.5); neg.insert(QStringLiteral("y"), -0.1);
    QCOMPARE(errorCode(sendAndWaitForReply(QStringLiteral("vc.xyPad.setPosition"), widgetParams(padId, neg), QStringLiteral("t-xn"))), QStringLiteral("INVALID_PARAMS"));
    QJsonObject onlyX; onlyX.insert(QStringLiteral("x"), 0.5);
    QCOMPARE(errorCode(sendAndWaitForReply(QStringLiteral("vc.xyPad.setPosition"), widgetParams(padId, onlyX), QStringLiteral("t-xo"))), QStringLiteral("INVALID_PARAMS"));
    QJsonObject str; str.insert(QStringLiteral("x"), QStringLiteral("0.5")); str.insert(QStringLiteral("y"), 0.5);
    QCOMPARE(errorCode(sendAndWaitForReply(QStringLiteral("vc.xyPad.setPosition"), widgetParams(padId, str), QStringLiteral("t-xs"))), QStringLiteral("INVALID_PARAMS"));

    // Wrong type / unknown
    QCOMPARE(errorCode(sendAndWaitForReply(QStringLiteral("vc.xyPad.setPosition"), widgetParams(buttonId, pos), QStringLiteral("t-xwt"))), QStringLiteral("INVALID_PARAMS"));
    QCOMPARE(errorCode(sendAndWaitForReply(QStringLiteral("vc.xyPad.setPosition"), widgetParams(QStringLiteral("555"), pos), QStringLiteral("t-xnf"))), QStringLiteral("NOT_FOUND"));
}

void ApiVcDomain_Test::liveSpeedDialSetValueAndTap()
{
    QString clientId = helloAndGetClientId();
    QString dialId = createWidget(QStringLiteral("Speed"));
    QString buttonId = createWidget(QStringLiteral("Button"));

    QSignalSpy spy(m_client, &QWebSocket::textMessageReceived);
    QJsonObject ms; ms.insert(QStringLiteral("ms"), 500);
    QJsonObject reply = sendAndWaitForReply(QStringLiteral("vc.speedDial.setValue"), widgetParams(dialId, ms), QStringLiteral("t-sd"));
    QCOMPARE(reply.value(QStringLiteral("ok")).toBool(), true);

    QList<QJsonObject> events = eventsWithTopic(spy, QStringLiteral("vc.speedDial.valueChanged"));
    QCOMPARE(events.size(), 1);
    QCOMPARE(events.at(0).value(QStringLiteral("widgetId")).toString(), dialId);
    QCOMPARE(events.at(0).value(QStringLiteral("ms")).toInt(), 500);
    QCOMPARE(events.at(0).value(QStringLiteral("_origin")).toString(), clientId);

    QJsonObject get = sendAndWaitForReply(QStringLiteral("vc.widget.get"), widgetParams(dialId), QStringLiteral("t-sdg"));
    QCOMPARE(get.value(QStringLiteral("result")).toObject().value(QStringLiteral("ms")).toInt(), 500);

    // Negative / non-integer / missing -> INVALID_PARAMS; wrong type; unknown
    QJsonObject neg; neg.insert(QStringLiteral("ms"), -5);
    QCOMPARE(errorCode(sendAndWaitForReply(QStringLiteral("vc.speedDial.setValue"), widgetParams(dialId, neg), QStringLiteral("t-sdn"))), QStringLiteral("INVALID_PARAMS"));
    QJsonObject str; str.insert(QStringLiteral("ms"), QStringLiteral("500"));
    QCOMPARE(errorCode(sendAndWaitForReply(QStringLiteral("vc.speedDial.setValue"), widgetParams(dialId, str), QStringLiteral("t-sds"))), QStringLiteral("INVALID_PARAMS"));
    QCOMPARE(errorCode(sendAndWaitForReply(QStringLiteral("vc.speedDial.setValue"), widgetParams(dialId), QStringLiteral("t-sdm"))), QStringLiteral("INVALID_PARAMS"));
    QCOMPARE(errorCode(sendAndWaitForReply(QStringLiteral("vc.speedDial.setValue"), widgetParams(buttonId, ms), QStringLiteral("t-sdwt"))), QStringLiteral("INVALID_PARAMS"));
    QCOMPARE(errorCode(sendAndWaitForReply(QStringLiteral("vc.speedDial.setValue"), widgetParams(QStringLiteral("888"), ms), QStringLiteral("t-sdnf"))), QStringLiteral("NOT_FOUND"));

    // tap: first tap arms (no event), a quick second tap sets the interval -> valueChanged
    QCOMPARE(sendAndWaitForReply(QStringLiteral("vc.speedDial.tap"), widgetParams(dialId), QStringLiteral("t-tap1")).value(QStringLiteral("ok")).toBool(), true);
    QCOMPARE(eventsWithTopic(spy, QStringLiteral("vc.speedDial.valueChanged")).size(), 1);
    QTest::qWait(60);
    QCOMPARE(sendAndWaitForReply(QStringLiteral("vc.speedDial.tap"), widgetParams(dialId), QStringLiteral("t-tap2")).value(QStringLiteral("ok")).toBool(), true);
    events = eventsWithTopic(spy, QStringLiteral("vc.speedDial.valueChanged"));
    QCOMPARE(events.size(), 2);
    int tapped = events.at(1).value(QStringLiteral("ms")).toInt();
    QVERIFY2(tapped > 0 && tapped < 1500, qPrintable(QString::number(tapped)));
    QCOMPARE(events.at(1).value(QStringLiteral("_origin")).toString(), clientId);

    QCOMPARE(errorCode(sendAndWaitForReply(QStringLiteral("vc.speedDial.tap"), widgetParams(buttonId), QStringLiteral("t-tapwt"))), QStringLiteral("INVALID_PARAMS"));
    QCOMPARE(errorCode(sendAndWaitForReply(QStringLiteral("vc.speedDial.tap"), widgetParams(QStringLiteral("888")), QStringLiteral("t-tapnf"))), QStringLiteral("NOT_FOUND"));
}

void ApiVcDomain_Test::liveFrameGotoPageAndGet()
{
    QString clientId = helloAndGetClientId();
    QJsonObject cfg;
    cfg.insert(QStringLiteral("multiPageMode"), true);
    cfg.insert(QStringLiteral("totalPagesNumber"), 3);
    QString frameId = createWidget(QStringLiteral("Frame"), cfg);
    QString soloId = createWidget(QStringLiteral("SoloFrame"));
    QString buttonId = createWidget(QStringLiteral("Button"));

    QJsonObject get = sendAndWaitForReply(QStringLiteral("vc.frame.get"), widgetParams(frameId), QStringLiteral("t-fg"));
    QCOMPARE(get.value(QStringLiteral("ok")).toBool(), true);
    QCOMPARE(get.value(QStringLiteral("result")).toObject().value(QStringLiteral("pages")).toInt(), 3);
    QCOMPARE(get.value(QStringLiteral("result")).toObject().value(QStringLiteral("currentPage")).toInt(), 0);
    QCOMPARE(get.value(QStringLiteral("result")).toObject().value(QStringLiteral("multipage")).toBool(), true);

    QSignalSpy spy(m_client, &QWebSocket::textMessageReceived);
    QJsonObject page; page.insert(QStringLiteral("page"), 2);
    QJsonObject reply = sendAndWaitForReply(QStringLiteral("vc.frame.gotoPage"), widgetParams(frameId, page), QStringLiteral("t-gp"));
    QCOMPARE(reply.value(QStringLiteral("ok")).toBool(), true);
    QList<QJsonObject> events = eventsWithTopic(spy, QStringLiteral("vc.frame.pageChanged"));
    QCOMPARE(events.size(), 1);
    QCOMPARE(events.at(0).value(QStringLiteral("widgetId")).toString(), frameId);
    QCOMPARE(events.at(0).value(QStringLiteral("page")).toInt(), 2);
    QCOMPARE(events.at(0).value(QStringLiteral("_origin")).toString(), clientId);
    // the page flip is the one live change that bumps docRevision, so the event carries it
    QVERIFY(events.at(0).contains(QStringLiteral("docRevision")));
    QCOMPARE(events.at(0).value(QStringLiteral("docRevision")).toInt(), int(m_doc->docRevision()));

    // Same page again: ack, no event
    QCOMPARE(sendAndWaitForReply(QStringLiteral("vc.frame.gotoPage"), widgetParams(frameId, page), QStringLiteral("t-gp2")).value(QStringLiteral("ok")).toBool(), true);
    QCOMPARE(eventsWithTopic(spy, QStringLiteral("vc.frame.pageChanged")).size(), 1);

    get = sendAndWaitForReply(QStringLiteral("vc.widget.get"), widgetParams(frameId), QStringLiteral("t-fwg"));
    QCOMPARE(get.value(QStringLiteral("result")).toObject().value(QStringLiteral("currentPage")).toInt(), 2);
    QCOMPARE(get.value(QStringLiteral("result")).toObject().value(QStringLiteral("pages")).toInt(), 3);

    // Out of range -> INVALID_PARAMS; non-integer; SoloFrame is a valid target (single page: only 0)
    QJsonObject big; big.insert(QStringLiteral("page"), 3);
    QCOMPARE(errorCode(sendAndWaitForReply(QStringLiteral("vc.frame.gotoPage"), widgetParams(frameId, big), QStringLiteral("t-gpb"))), QStringLiteral("INVALID_PARAMS"));
    QJsonObject neg; neg.insert(QStringLiteral("page"), -1);
    QCOMPARE(errorCode(sendAndWaitForReply(QStringLiteral("vc.frame.gotoPage"), widgetParams(frameId, neg), QStringLiteral("t-gpn"))), QStringLiteral("INVALID_PARAMS"));
    QJsonObject str; str.insert(QStringLiteral("page"), QStringLiteral("1"));
    QCOMPARE(errorCode(sendAndWaitForReply(QStringLiteral("vc.frame.gotoPage"), widgetParams(frameId, str), QStringLiteral("t-gps"))), QStringLiteral("INVALID_PARAMS"));
    QJsonObject zero; zero.insert(QStringLiteral("page"), 0);
    QCOMPARE(sendAndWaitForReply(QStringLiteral("vc.frame.gotoPage"), widgetParams(soloId, zero), QStringLiteral("t-solo")).value(QStringLiteral("ok")).toBool(), true);
    QJsonObject one; one.insert(QStringLiteral("page"), 1);
    QCOMPARE(errorCode(sendAndWaitForReply(QStringLiteral("vc.frame.gotoPage"), widgetParams(soloId, one), QStringLiteral("t-solo1"))), QStringLiteral("INVALID_PARAMS"));

    // The spec's original "pageIndex" spelling is accepted as an alias
    QJsonObject alias; alias.insert(QStringLiteral("pageIndex"), 1);
    QCOMPARE(sendAndWaitForReply(QStringLiteral("vc.frame.gotoPage"), widgetParams(frameId, alias), QStringLiteral("t-gpa")).value(QStringLiteral("ok")).toBool(), true);
    QCOMPARE(eventsWithTopic(spy, QStringLiteral("vc.frame.pageChanged")).last().value(QStringLiteral("page")).toInt(), 1);

    // Wrong type / unknown
    QCOMPARE(errorCode(sendAndWaitForReply(QStringLiteral("vc.frame.gotoPage"), widgetParams(buttonId, zero), QStringLiteral("t-gpwt"))), QStringLiteral("INVALID_PARAMS"));
    QCOMPARE(errorCode(sendAndWaitForReply(QStringLiteral("vc.frame.get"), widgetParams(buttonId), QStringLiteral("t-fgwt"))), QStringLiteral("INVALID_PARAMS"));
    QCOMPARE(errorCode(sendAndWaitForReply(QStringLiteral("vc.frame.gotoPage"), widgetParams(QStringLiteral("999"), zero), QStringLiteral("t-gpnf"))), QStringLiteral("NOT_FOUND"));
    QCOMPARE(errorCode(sendAndWaitForReply(QStringLiteral("vc.frame.get"), widgetParams(QStringLiteral("999")), QStringLiteral("t-fgnf"))), QStringLiteral("NOT_FOUND"));
}

void ApiVcDomain_Test::liveWidgetSnapshotsExposeLiveState()
{
    helloAndGetClientId();
    QJsonObject buttonCfg; buttonCfg.insert(QStringLiteral("functionID"), QStringLiteral("7"));
    QString buttonId = createWidget(QStringLiteral("Button"), buttonCfg);
    QString sliderId = createWidget(QStringLiteral("Slider"));
    QJsonObject cueCfg; cueCfg.insert(QStringLiteral("chaserID"), QStringLiteral("3"));
    QString cueListId = createWidget(QStringLiteral("CueList"), cueCfg);
    QString padId = createWidget(QStringLiteral("XYPad"));
    QString dialId = createWidget(QStringLiteral("Speed"));
    QString frameId = createWidget(QStringLiteral("Frame"));
    QString labelId = createWidget(QStringLiteral("Label"));

    QJsonObject list = sendAndWaitForReply(QStringLiteral("vc.widget.list"), QJsonObject(), QStringLiteral("t-list"));
    QCOMPARE(list.value(QStringLiteral("ok")).toBool(), true);
    QHash<QString, QJsonObject> byId;
    for (const QJsonValue &v : list.value(QStringLiteral("result")).toObject().value(QStringLiteral("widgets")).toArray())
        byId.insert(v.toObject().value(QStringLiteral("id")).toString(), v.toObject());
    QCOMPARE(byId.size(), 7);

    // Every pre-existing field is still there (additive extension)...
    for (const QJsonObject &w : byId)
    {
        for (const char *key : { "id", "widgetType", "page", "geometry", "zIndex", "allowResize", "isDisabled", "isVisible", "style", "typeConfig" })
            QVERIFY2(w.contains(QLatin1String(key)), key);
    }

    // ...plus the per-type live seed state in its initial values.
    QCOMPARE(byId.value(buttonId).value(QStringLiteral("state")).toString(), QStringLiteral("inactive"));
    QCOMPARE(byId.value(sliderId).value(QStringLiteral("value")).toInt(), 0);
    QVERIFY(byId.value(sliderId).contains(QStringLiteral("min")));
    QVERIFY(byId.value(sliderId).contains(QStringLiteral("max")));
    QCOMPARE(byId.value(cueListId).value(QStringLiteral("playbackIndex")).toInt(), -1);
    QCOMPARE(byId.value(cueListId).value(QStringLiteral("running")).toBool(), false);
    QCOMPARE(byId.value(cueListId).value(QStringLiteral("paused")).toBool(), false);
    QCOMPARE(byId.value(padId).value(QStringLiteral("x")).toDouble(), 0.0);
    QCOMPARE(byId.value(padId).value(QStringLiteral("y")).toDouble(), 0.0);
    QCOMPARE(byId.value(dialId).value(QStringLiteral("ms")).toInt(), 0);
    QCOMPARE(byId.value(frameId).value(QStringLiteral("currentPage")).toInt(), 0);
    QCOMPARE(byId.value(frameId).value(QStringLiteral("pages")).toInt(), 1);
    QCOMPARE(byId.value(frameId).value(QStringLiteral("multipage")).toBool(), false);
    // A Label has no live state and gets none of these keys
    for (const char *key : { "state", "value", "playbackIndex", "x", "ms", "currentPage" })
        QVERIFY2(byId.value(labelId).contains(QLatin1String(key)) == false, key);
}

void ApiVcDomain_Test::liveEngineDrivenChangeBroadcastsWithNullOrigin()
{
    helloAndGetClientId();
    QJsonObject buttonCfg; buttonCfg.insert(QStringLiteral("functionID"), QStringLiteral("7"));
    QString buttonId = createWidget(QStringLiteral("Button"), buttonCfg);
    QString sliderId = createWidget(QStringLiteral("Slider"));
    QJsonObject cueCfg; cueCfg.insert(QStringLiteral("chaserID"), QStringLiteral("3"));
    QString cueListId = createWidget(QStringLiteral("CueList"), cueCfg);

    // Nobody asked over the API - the "engine" (QML UI / external input / Function stopping) did.
    QSignalSpy spy(m_client, &QWebSocket::textMessageReceived);
    m_vcHost->simulateButtonState(buttonId.toUInt(), QStringLiteral("monitoring"));
    m_vcHost->simulateSliderValue(sliderId.toUInt(), 42);
    m_vcHost->simulateCueListAdvance(cueListId.toUInt(), 2);
    QVERIFY(QTest::qWaitFor([&]() { return spy.count() >= 3; }, 2000));

    QList<QJsonObject> events = eventsWithTopic(spy, QStringLiteral("vc.button.stateChanged"));
    QCOMPARE(events.size(), 1);
    QCOMPARE(events.at(0).value(QStringLiteral("widgetId")).toString(), buttonId);
    QCOMPARE(events.at(0).value(QStringLiteral("state")).toString(), QStringLiteral("monitoring"));
    QVERIFY(events.at(0).value(QStringLiteral("_origin")).isNull());

    events = eventsWithTopic(spy, QStringLiteral("vc.slider.valueChanged"));
    QCOMPARE(events.size(), 1);
    QCOMPARE(events.at(0).value(QStringLiteral("value")).toInt(), 42);
    QVERIFY(events.at(0).value(QStringLiteral("_origin")).isNull());

    events = eventsWithTopic(spy, QStringLiteral("vc.cueList.playbackChanged"));
    QCOMPARE(events.size(), 1);
    QCOMPARE(events.at(0).value(QStringLiteral("playbackIndex")).toInt(), 2);
    QCOMPARE(events.at(0).value(QStringLiteral("running")).toBool(), true);
    QVERIFY(events.at(0).value(QStringLiteral("_origin")).isNull());

    // The seed state a late-joining client reads back matches
    QJsonObject get = sendAndWaitForReply(QStringLiteral("vc.widget.get"), widgetParams(buttonId), QStringLiteral("t-eg"));
    QCOMPARE(get.value(QStringLiteral("result")).toObject().value(QStringLiteral("state")).toString(), QStringLiteral("monitoring"));
}

void ApiVcDomain_Test::liveMethodsDoNotBumpDocRevision()
{
    helloAndGetClientId();
    QJsonObject buttonCfg; buttonCfg.insert(QStringLiteral("functionID"), QStringLiteral("7"));
    QString buttonId = createWidget(QStringLiteral("Button"), buttonCfg);
    QString sliderId = createWidget(QStringLiteral("Slider"));
    QString padId = createWidget(QStringLiteral("XYPad"));
    QString dialId = createWidget(QStringLiteral("Speed"));
    int rev = currentDocRevision();

    QJsonObject down; down.insert(QStringLiteral("pressed"), true);
    QCOMPARE(sendAndWaitForReply(QStringLiteral("vc.button.press"), widgetParams(buttonId, down), QStringLiteral("t-r1")).value(QStringLiteral("ok")).toBool(), true);
    QJsonObject v; v.insert(QStringLiteral("value"), 10);
    QCOMPARE(sendAndWaitForReply(QStringLiteral("vc.slider.setValue"), widgetParams(sliderId, v), QStringLiteral("t-r2")).value(QStringLiteral("ok")).toBool(), true);
    QJsonObject pos; pos.insert(QStringLiteral("x"), 0.5); pos.insert(QStringLiteral("y"), 0.5);
    QCOMPARE(sendAndWaitForReply(QStringLiteral("vc.xyPad.setPosition"), widgetParams(padId, pos), QStringLiteral("t-r3")).value(QStringLiteral("ok")).toBool(), true);
    QJsonObject ms; ms.insert(QStringLiteral("ms"), 250);
    QCOMPARE(sendAndWaitForReply(QStringLiteral("vc.speedDial.setValue"), widgetParams(dialId, ms), QStringLiteral("t-r4")).value(QStringLiteral("ok")).toBool(), true);

    // §4b: live/runtime state never touches docRevision. (vc.frame.gotoPage is the documented
    // exception in the real engine - VCFrame::setCurrentPage() persists the page - and is not covered
    // by the headless fake either way.)
    QCOMPARE(currentDocRevision(), rev);
}

/*****************************************************************************
 * Cue list side fader, speed dial extras, widget presets
 *****************************************************************************/

void ApiVcDomain_Test::liveCueListSideFaderLevel()
{
    QString clientId = helloAndGetClientId();
    QJsonObject cfg;
    cfg.insert(QStringLiteral("chaserID"), QStringLiteral("5"));
    cfg.insert(QStringLiteral("sideFaderMode"), QStringLiteral("Crossfade"));
    QString cueId = createWidget(QStringLiteral("CueList"), cfg);
    QString noneId = createWidget(QStringLiteral("CueList"));
    QString buttonId = createWidget(QStringLiteral("Button"));
    int rev = currentDocRevision();

    // Seed state: the fake starts at VCCueList's default level of 100
    QJsonObject get = sendAndWaitForReply(QStringLiteral("vc.widget.get"), widgetParams(cueId), QStringLiteral("t-sf0"));
    QCOMPARE(get.value(QStringLiteral("result")).toObject().value(QStringLiteral("sideFaderLevel")).toInt(), 100);

    QSignalSpy spy(m_client, &QWebSocket::textMessageReceived);
    QJsonObject lvl; lvl.insert(QStringLiteral("level"), 40);
    QJsonObject reply = sendAndWaitForReply(QStringLiteral("vc.cueList.setSideFaderLevel"), widgetParams(cueId, lvl), QStringLiteral("t-sf1"));
    QCOMPARE(reply.value(QStringLiteral("ok")).toBool(), true);
    QVERIFY(reply.value(QStringLiteral("result")).toObject().isEmpty());

    QList<QJsonObject> events = eventsWithTopic(spy, QStringLiteral("vc.cueList.sideFaderChanged"));
    QCOMPARE(events.size(), 1);
    QCOMPARE(events.at(0).value(QStringLiteral("widgetId")).toString(), cueId);
    QCOMPARE(events.at(0).value(QStringLiteral("level")).toInt(), 40);
    QVERIFY(events.at(0).contains(QStringLiteral("nextStepIndex")));
    QVERIFY(events.at(0).contains(QStringLiteral("primaryTop")));
    QCOMPARE(events.at(0).value(QStringLiteral("_origin")).toString(), clientId);

    get = sendAndWaitForReply(QStringLiteral("vc.widget.get"), widgetParams(cueId), QStringLiteral("t-sf2"));
    QCOMPARE(get.value(QStringLiteral("result")).toObject().value(QStringLiteral("sideFaderLevel")).toInt(), 40);

    // Same value again -> ack, no second event
    QCOMPARE(sendAndWaitForReply(QStringLiteral("vc.cueList.setSideFaderLevel"), widgetParams(cueId, lvl), QStringLiteral("t-sf3")).value(QStringLiteral("ok")).toBool(), true);
    QCOMPARE(eventsWithTopic(spy, QStringLiteral("vc.cueList.sideFaderChanged")).size(), 1);

    // Crossfade confines to 0..100 (the on-screen fader's range in that mode)
    QJsonObject big; big.insert(QStringLiteral("level"), 200);
    QCOMPARE(sendAndWaitForReply(QStringLiteral("vc.cueList.setSideFaderLevel"), widgetParams(cueId, big), QStringLiteral("t-sf4")).value(QStringLiteral("ok")).toBool(), true);
    events = eventsWithTopic(spy, QStringLiteral("vc.cueList.sideFaderChanged"));
    QCOMPARE(events.size(), 2);
    QCOMPARE(events.at(1).value(QStringLiteral("level")).toInt(), 100);

    // Validation: out of range / non-integer / missing -> INVALID_PARAMS; wrong type; unknown widget
    QJsonObject neg; neg.insert(QStringLiteral("level"), -1);
    QCOMPARE(errorCode(sendAndWaitForReply(QStringLiteral("vc.cueList.setSideFaderLevel"), widgetParams(cueId, neg), QStringLiteral("t-sfn"))), QStringLiteral("INVALID_PARAMS"));
    QJsonObject over; over.insert(QStringLiteral("level"), 256);
    QCOMPARE(errorCode(sendAndWaitForReply(QStringLiteral("vc.cueList.setSideFaderLevel"), widgetParams(cueId, over), QStringLiteral("t-sfo"))), QStringLiteral("INVALID_PARAMS"));
    QJsonObject str; str.insert(QStringLiteral("level"), QStringLiteral("40"));
    QCOMPARE(errorCode(sendAndWaitForReply(QStringLiteral("vc.cueList.setSideFaderLevel"), widgetParams(cueId, str), QStringLiteral("t-sfs"))), QStringLiteral("INVALID_PARAMS"));
    QCOMPARE(errorCode(sendAndWaitForReply(QStringLiteral("vc.cueList.setSideFaderLevel"), widgetParams(cueId), QStringLiteral("t-sfm"))), QStringLiteral("INVALID_PARAMS"));
    QCOMPARE(errorCode(sendAndWaitForReply(QStringLiteral("vc.cueList.setSideFaderLevel"), widgetParams(buttonId, lvl), QStringLiteral("t-sfwt"))), QStringLiteral("INVALID_PARAMS"));
    QCOMPARE(errorCode(sendAndWaitForReply(QStringLiteral("vc.cueList.setSideFaderLevel"), widgetParams(QStringLiteral("888"), lvl), QStringLiteral("t-sfnf"))), QStringLiteral("NOT_FOUND"));

    // No side fader (mode None) -> INVALID_STATE
    QCOMPARE(errorCode(sendAndWaitForReply(QStringLiteral("vc.cueList.setSideFaderLevel"), widgetParams(noneId, lvl), QStringLiteral("t-sfnone"))), QStringLiteral("INVALID_STATE"));

    // Live: docRevision untouched
    QCOMPARE(currentDocRevision(), rev);
}

void ApiVcDomain_Test::liveSpeedDialFactorApplyAndResetTap()
{
    QString clientId = helloAndGetClientId();
    QString dialId = createWidget(QStringLiteral("Speed"));
    QString buttonId = createWidget(QStringLiteral("Button"));
    int rev = currentDocRevision();

    // Seed state
    QJsonObject get = sendAndWaitForReply(QStringLiteral("vc.widget.get"), widgetParams(dialId), QStringLiteral("t-fa0"));
    QCOMPARE(get.value(QStringLiteral("result")).toObject().value(QStringLiteral("factor")).toString(), QStringLiteral("One"));
    QCOMPARE(get.value(QStringLiteral("result")).toObject().value(QStringLiteral("tapTimeValue")).toInt(), 0);

    QSignalSpy spy(m_client, &QWebSocket::textMessageReceived);

    // --- setFactor ---
    QJsonObject f; f.insert(QStringLiteral("factor"), QStringLiteral("Half"));
    QJsonObject reply = sendAndWaitForReply(QStringLiteral("vc.speedDial.setFactor"), widgetParams(dialId, f), QStringLiteral("t-fa1"));
    QCOMPARE(reply.value(QStringLiteral("ok")).toBool(), true);
    QList<QJsonObject> events = eventsWithTopic(spy, QStringLiteral("vc.speedDial.factorChanged"));
    QCOMPARE(events.size(), 1);
    QCOMPARE(events.at(0).value(QStringLiteral("widgetId")).toString(), dialId);
    QCOMPARE(events.at(0).value(QStringLiteral("factor")).toString(), QStringLiteral("Half"));
    QCOMPARE(events.at(0).value(QStringLiteral("_origin")).toString(), clientId);

    get = sendAndWaitForReply(QStringLiteral("vc.widget.get"), widgetParams(dialId), QStringLiteral("t-fa2"));
    QCOMPARE(get.value(QStringLiteral("result")).toObject().value(QStringLiteral("factor")).toString(), QStringLiteral("Half"));

    // Same factor again -> no event; None/Zero/garbage -> INVALID_PARAMS; wrong type; unknown widget
    QCOMPARE(sendAndWaitForReply(QStringLiteral("vc.speedDial.setFactor"), widgetParams(dialId, f), QStringLiteral("t-fa3")).value(QStringLiteral("ok")).toBool(), true);
    QCOMPARE(eventsWithTopic(spy, QStringLiteral("vc.speedDial.factorChanged")).size(), 1);
    for (const QString &bad : { QStringLiteral("None"), QStringLiteral("Zero"), QStringLiteral("Double"), QString() })
    {
        QJsonObject b; b.insert(QStringLiteral("factor"), bad);
        QCOMPARE(errorCode(sendAndWaitForReply(QStringLiteral("vc.speedDial.setFactor"), widgetParams(dialId, b), QStringLiteral("t-fab-") + bad)), QStringLiteral("INVALID_PARAMS"));
    }
    QCOMPARE(errorCode(sendAndWaitForReply(QStringLiteral("vc.speedDial.setFactor"), widgetParams(buttonId, f), QStringLiteral("t-fawt"))), QStringLiteral("INVALID_PARAMS"));
    QCOMPARE(errorCode(sendAndWaitForReply(QStringLiteral("vc.speedDial.setFactor"), widgetParams(QStringLiteral("888"), f), QStringLiteral("t-fanf"))), QStringLiteral("NOT_FOUND"));

    // --- apply: bare ack, nothing observable on the fake ---
    reply = sendAndWaitForReply(QStringLiteral("vc.speedDial.apply"), widgetParams(dialId), QStringLiteral("t-ap1"));
    QCOMPARE(reply.value(QStringLiteral("ok")).toBool(), true);
    QVERIFY(reply.value(QStringLiteral("result")).toObject().isEmpty());
    QCOMPARE(errorCode(sendAndWaitForReply(QStringLiteral("vc.speedDial.apply"), widgetParams(buttonId), QStringLiteral("t-apwt"))), QStringLiteral("INVALID_PARAMS"));

    // --- tap twice -> tapChanged with the interval; resetTap -> tapChanged with 0 ---
    QCOMPARE(sendAndWaitForReply(QStringLiteral("vc.speedDial.tap"), widgetParams(dialId), QStringLiteral("t-tp1")).value(QStringLiteral("ok")).toBool(), true);
    QCOMPARE(eventsWithTopic(spy, QStringLiteral("vc.speedDial.tapChanged")).size(), 0);
    QTest::qWait(60);
    QCOMPARE(sendAndWaitForReply(QStringLiteral("vc.speedDial.tap"), widgetParams(dialId), QStringLiteral("t-tp2")).value(QStringLiteral("ok")).toBool(), true);
    events = eventsWithTopic(spy, QStringLiteral("vc.speedDial.tapChanged"));
    QCOMPARE(events.size(), 1);
    int tapped = events.at(0).value(QStringLiteral("tapTimeValue")).toInt();
    QVERIFY2(tapped > 0 && tapped < 1500, qPrintable(QString::number(tapped)));
    QCOMPARE(events.at(0).value(QStringLiteral("currentTimeMs")).toInt(), tapped);
    QCOMPARE(events.at(0).value(QStringLiteral("_origin")).toString(), clientId);

    get = sendAndWaitForReply(QStringLiteral("vc.widget.get"), widgetParams(dialId), QStringLiteral("t-tp3"));
    QCOMPARE(get.value(QStringLiteral("result")).toObject().value(QStringLiteral("tapTimeValue")).toInt(), tapped);

    reply = sendAndWaitForReply(QStringLiteral("vc.speedDial.resetTap"), widgetParams(dialId), QStringLiteral("t-rt1"));
    QCOMPARE(reply.value(QStringLiteral("ok")).toBool(), true);
    events = eventsWithTopic(spy, QStringLiteral("vc.speedDial.tapChanged"));
    QCOMPARE(events.size(), 2);
    QCOMPARE(events.at(1).value(QStringLiteral("tapTimeValue")).toInt(), 0);
    QCOMPARE(events.at(1).value(QStringLiteral("currentTimeMs")).toInt(), tapped);

    // A second reset changes nothing -> no event
    QCOMPARE(sendAndWaitForReply(QStringLiteral("vc.speedDial.resetTap"), widgetParams(dialId), QStringLiteral("t-rt2")).value(QStringLiteral("ok")).toBool(), true);
    QCOMPARE(eventsWithTopic(spy, QStringLiteral("vc.speedDial.tapChanged")).size(), 2);
    QCOMPARE(errorCode(sendAndWaitForReply(QStringLiteral("vc.speedDial.resetTap"), widgetParams(buttonId), QStringLiteral("t-rtwt"))), QStringLiteral("INVALID_PARAMS"));
    QCOMPARE(errorCode(sendAndWaitForReply(QStringLiteral("vc.speedDial.resetTap"), widgetParams(QStringLiteral("888")), QStringLiteral("t-rtnf"))), QStringLiteral("NOT_FOUND"));

    // Live: docRevision untouched
    QCOMPARE(currentDocRevision(), rev);
}

void ApiVcDomain_Test::widgetPresetAddUpdateApplyRemove()
{
    QString clientId = helloAndGetClientId();
    QString dialId = createWidget(QStringLiteral("Speed"));
    int rev = currentDocRevision();

    // Starts empty, and the read-only list rides along inside typeConfig
    QJsonObject get = sendAndWaitForReply(QStringLiteral("vc.widget.get"), widgetParams(dialId), QStringLiteral("t-pr0"));
    QVERIFY(get.value(QStringLiteral("result")).toObject().value(QStringLiteral("typeConfig")).toObject().value(QStringLiteral("presets")).toArray().isEmpty());

    QSignalSpy spy(m_client, &QWebSocket::textMessageReceived);

    // --- add ---
    QJsonObject preset; preset.insert(QStringLiteral("name"), QStringLiteral("Slow")); preset.insert(QStringLiteral("valueMs"), 2000);
    QJsonObject addParams = widgetParams(dialId);
    addParams.insert(QStringLiteral("baseRevision"), rev);
    addParams.insert(QStringLiteral("preset"), preset);
    QJsonObject reply = sendAndWaitForReply(QStringLiteral("vc.widget.preset.add"), addParams, QStringLiteral("t-pr1"));
    QCOMPARE(reply.value(QStringLiteral("ok")).toBool(), true);
    int presetId = reply.value(QStringLiteral("result")).toObject().value(QStringLiteral("presetId")).toInt(-1);
    QVERIFY(presetId >= 0);
    int revAfterAdd = reply.value(QStringLiteral("result")).toObject().value(QStringLiteral("docRevision")).toInt();
    QVERIFY(revAfterAdd > rev);

    QList<QJsonObject> events = eventsWithTopic(spy, QStringLiteral("vc.speedDial.presetsChanged"));
    QCOMPARE(events.size(), 1);
    QCOMPARE(events.at(0).value(QStringLiteral("widgetId")).toString(), dialId);
    QCOMPARE(events.at(0).value(QStringLiteral("docRevision")).toInt(), revAfterAdd);
    QCOMPARE(events.at(0).value(QStringLiteral("_origin")).toString(), clientId);
    QJsonArray presets = events.at(0).value(QStringLiteral("presets")).toArray();
    QCOMPARE(presets.size(), 1);
    QCOMPARE(presets.at(0).toObject().value(QStringLiteral("presetId")).toInt(), presetId);
    QCOMPARE(presets.at(0).toObject().value(QStringLiteral("name")).toString(), QStringLiteral("Slow"));
    QCOMPARE(presets.at(0).toObject().value(QStringLiteral("valueMs")).toInt(), 2000);

    // Stale baseRevision -> CONFLICT with the current revision in details; bad payload -> INVALID_PARAMS
    QCOMPARE(errorCode(sendAndWaitForReply(QStringLiteral("vc.widget.preset.add"), addParams, QStringLiteral("t-pr2"))), QStringLiteral("CONFLICT"));
    QJsonObject badParams = widgetParams(dialId);
    badParams.insert(QStringLiteral("baseRevision"), revAfterAdd);
    QJsonObject noName; noName.insert(QStringLiteral("valueMs"), 10);
    badParams.insert(QStringLiteral("preset"), noName);
    QCOMPARE(errorCode(sendAndWaitForReply(QStringLiteral("vc.widget.preset.add"), badParams, QStringLiteral("t-pr3"))), QStringLiteral("INVALID_PARAMS"));
    badParams.insert(QStringLiteral("preset"), QStringLiteral("not an object"));
    QCOMPARE(errorCode(sendAndWaitForReply(QStringLiteral("vc.widget.preset.add"), badParams, QStringLiteral("t-pr4"))), QStringLiteral("INVALID_PARAMS"));
    QCOMPARE(currentDocRevision(), revAfterAdd);

    // --- update (vc.speedDial.preset.update) ---
    QJsonObject upd = widgetParams(dialId);
    upd.insert(QStringLiteral("baseRevision"), revAfterAdd);
    upd.insert(QStringLiteral("presetId"), presetId);
    upd.insert(QStringLiteral("name"), QStringLiteral("Slower"));
    upd.insert(QStringLiteral("valueMs"), 3000);
    reply = sendAndWaitForReply(QStringLiteral("vc.speedDial.preset.update"), upd, QStringLiteral("t-pu1"));
    QCOMPARE(reply.value(QStringLiteral("ok")).toBool(), true);
    int revAfterUpdate = reply.value(QStringLiteral("result")).toObject().value(QStringLiteral("docRevision")).toInt();
    QVERIFY(revAfterUpdate > revAfterAdd);
    events = eventsWithTopic(spy, QStringLiteral("vc.speedDial.presetsChanged"));
    QCOMPARE(events.size(), 2);
    presets = events.at(1).value(QStringLiteral("presets")).toArray();
    QCOMPARE(presets.at(0).toObject().value(QStringLiteral("name")).toString(), QStringLiteral("Slower"));
    QCOMPARE(presets.at(0).toObject().value(QStringLiteral("valueMs")).toInt(), 3000);

    get = sendAndWaitForReply(QStringLiteral("vc.widget.get"), widgetParams(dialId), QStringLiteral("t-pu2"));
    presets = get.value(QStringLiteral("result")).toObject().value(QStringLiteral("typeConfig")).toObject().value(QStringLiteral("presets")).toArray();
    QCOMPARE(presets.size(), 1);
    QCOMPARE(presets.at(0).toObject().value(QStringLiteral("valueMs")).toInt(), 3000);

    // update validation: unknown preset -> NOT_FOUND; nothing to change / empty name -> INVALID_PARAMS
    QJsonObject updBad = widgetParams(dialId);
    updBad.insert(QStringLiteral("baseRevision"), revAfterUpdate);
    updBad.insert(QStringLiteral("presetId"), 999);
    updBad.insert(QStringLiteral("name"), QStringLiteral("x"));
    QCOMPARE(errorCode(sendAndWaitForReply(QStringLiteral("vc.speedDial.preset.update"), updBad, QStringLiteral("t-pu3"))), QStringLiteral("NOT_FOUND"));
    updBad.insert(QStringLiteral("presetId"), presetId);
    updBad.remove(QStringLiteral("name"));
    QCOMPARE(errorCode(sendAndWaitForReply(QStringLiteral("vc.speedDial.preset.update"), updBad, QStringLiteral("t-pu4"))), QStringLiteral("INVALID_PARAMS"));
    updBad.insert(QStringLiteral("name"), QString());
    QCOMPARE(errorCode(sendAndWaitForReply(QStringLiteral("vc.speedDial.preset.update"), updBad, QStringLiteral("t-pu5"))), QStringLiteral("INVALID_PARAMS"));

    // --- apply (live): Speed -> currentTime = preset value -> vc.speedDial.valueChanged, no revision bump ---
    QJsonObject ap = widgetParams(dialId);
    ap.insert(QStringLiteral("presetId"), presetId);
    reply = sendAndWaitForReply(QStringLiteral("vc.widget.preset.apply"), ap, QStringLiteral("t-pa1"));
    QCOMPARE(reply.value(QStringLiteral("ok")).toBool(), true);
    QVERIFY(reply.value(QStringLiteral("result")).toObject().isEmpty());
    events = eventsWithTopic(spy, QStringLiteral("vc.speedDial.valueChanged"));
    QCOMPARE(events.size(), 1);
    QCOMPARE(events.at(0).value(QStringLiteral("ms")).toInt(), 3000);
    QCOMPARE(events.at(0).value(QStringLiteral("_origin")).toString(), clientId);
    QCOMPARE(currentDocRevision(), revAfterUpdate);
    ap.insert(QStringLiteral("presetId"), 999);
    QCOMPARE(errorCode(sendAndWaitForReply(QStringLiteral("vc.widget.preset.apply"), ap, QStringLiteral("t-pa2"))), QStringLiteral("NOT_FOUND"));

    // Disabled widget refuses apply like every other live method
    QJsonObject dis = widgetParams(dialId);
    dis.insert(QStringLiteral("baseRevision"), revAfterUpdate);
    dis.insert(QStringLiteral("isDisabled"), true);
    QCOMPARE(sendAndWaitForReply(QStringLiteral("vc.widget.update"), dis, QStringLiteral("t-pa3")).value(QStringLiteral("ok")).toBool(), true);
    ap.insert(QStringLiteral("presetId"), presetId);
    QCOMPARE(errorCode(sendAndWaitForReply(QStringLiteral("vc.widget.preset.apply"), ap, QStringLiteral("t-pa4"))), QStringLiteral("INVALID_STATE"));
    int revNow = currentDocRevision();

    // --- remove ---
    QJsonObject rm = widgetParams(dialId);
    rm.insert(QStringLiteral("baseRevision"), revNow);
    rm.insert(QStringLiteral("presetId"), 999);
    QCOMPARE(errorCode(sendAndWaitForReply(QStringLiteral("vc.widget.preset.remove"), rm, QStringLiteral("t-pm1"))), QStringLiteral("NOT_FOUND"));
    rm.insert(QStringLiteral("presetId"), presetId);
    reply = sendAndWaitForReply(QStringLiteral("vc.widget.preset.remove"), rm, QStringLiteral("t-pm2"));
    QCOMPARE(reply.value(QStringLiteral("ok")).toBool(), true);
    QVERIFY(reply.value(QStringLiteral("result")).toObject().value(QStringLiteral("docRevision")).toInt() > revNow);
    events = eventsWithTopic(spy, QStringLiteral("vc.speedDial.presetsChanged"));
    QCOMPARE(events.size(), 3);
    QVERIFY(events.at(2).value(QStringLiteral("presets")).toArray().isEmpty());
    // Stale now
    QCOMPARE(errorCode(sendAndWaitForReply(QStringLiteral("vc.widget.preset.remove"), rm, QStringLiteral("t-pm3"))), QStringLiteral("CONFLICT"));
}

void ApiVcDomain_Test::widgetPresetRejectsWidgetsWithoutPresets()
{
    helloAndGetClientId();
    QString buttonId = createWidget(QStringLiteral("Button"));
    QString padId = createWidget(QStringLiteral("XYPad"));
    int rev = currentDocRevision();

    QJsonObject preset; preset.insert(QStringLiteral("name"), QStringLiteral("x")); preset.insert(QStringLiteral("valueMs"), 1);
    QJsonObject p = widgetParams(buttonId);
    p.insert(QStringLiteral("baseRevision"), rev);
    p.insert(QStringLiteral("preset"), preset);
    QJsonObject reply = sendAndWaitForReply(QStringLiteral("vc.widget.preset.add"), p, QStringLiteral("t-np1"));
    QCOMPARE(errorCode(reply), QStringLiteral("INVALID_PARAMS"));
    QCOMPARE(reply.value(QStringLiteral("error")).toObject().value(QStringLiteral("details")).toObject().value(QStringLiteral("widgetType")).toString(), QStringLiteral("Button"));
    p.insert(QStringLiteral("presetId"), 16);
    QCOMPARE(errorCode(sendAndWaitForReply(QStringLiteral("vc.widget.preset.remove"), p, QStringLiteral("t-np2"))), QStringLiteral("INVALID_PARAMS"));
    QCOMPARE(errorCode(sendAndWaitForReply(QStringLiteral("vc.widget.preset.apply"), p, QStringLiteral("t-np3"))), QStringLiteral("INVALID_PARAMS"));
    QCOMPARE(errorCode(sendAndWaitForReply(QStringLiteral("vc.speedDial.preset.update"), p, QStringLiteral("t-np4"))), QStringLiteral("INVALID_PARAMS"));

    // vc.speedDial.preset.update is Speed-only even though XYPad has presets
    QJsonObject xp = widgetParams(padId);
    xp.insert(QStringLiteral("baseRevision"), rev);
    xp.insert(QStringLiteral("presetId"), 16);
    xp.insert(QStringLiteral("name"), QStringLiteral("x"));
    QCOMPARE(errorCode(sendAndWaitForReply(QStringLiteral("vc.speedDial.preset.update"), xp, QStringLiteral("t-np5"))), QStringLiteral("INVALID_PARAMS"));

    // Unknown widget -> NOT_FOUND
    QJsonObject nf = widgetParams(QStringLiteral("888"));
    nf.insert(QStringLiteral("baseRevision"), rev);
    nf.insert(QStringLiteral("preset"), preset);
    QCOMPARE(errorCode(sendAndWaitForReply(QStringLiteral("vc.widget.preset.add"), nf, QStringLiteral("t-np6"))), QStringLiteral("NOT_FOUND"));

    QCOMPARE(currentDocRevision(), rev);
}
