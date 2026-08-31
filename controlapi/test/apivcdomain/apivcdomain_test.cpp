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

static QString buildRequest(const QString &method, const QJsonObject &params, const QString &id)
{
    QJsonObject obj;
    obj.insert(QStringLiteral("type"), QStringLiteral("request"));
    obj.insert(QStringLiteral("id"), id);
    obj.insert(QStringLiteral("method"), method);
    obj.insert(QStringLiteral("params"), params);
    return QString::fromUtf8(QJsonDocument(obj).toJson(QJsonDocument::Compact));
}

void ApiVcDomain_Test::init()
{
    m_doc = new Doc(nullptr);
    m_apiServer = new ApiServer(nullptr, m_doc);
    QVERIFY(m_apiServer->listen(0));

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
