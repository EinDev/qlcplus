/*
  Q Light Controller Plus - Control API unit test
  apiimportdomain_test.cpp

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

#include <QXmlStreamWriter>
#include <QSignalSpy>
#include <QJsonDocument>
#include <QJsonArray>
#include <QWebSocket>
#include <QtTest>

#include "apiimportdomain_test.h"
#include "apiserver.h"

#include "fixturegroup.h"
#include "efxfixture.h"
#include "qlcpalette.h"
#include "chaserstep.h"
#include "qlcfile.h"
#include "fixture.h"
#include "chaser.h"
#include "scene.h"
#include "efx.h"
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

static Fixture *addDimmer(Doc *doc, const QString &name, quint32 address, quint32 channels)
{
    Fixture *fixture = new Fixture(doc);
    fixture->setName(name);
    fixture->setUniverse(0);
    fixture->setAddress(address);
    fixture->setChannels(channels);
    if (doc->addFixture(fixture) == false)
    {
        delete fixture;
        return nullptr;
    }
    return fixture;
}

static QJsonObject findById(const QJsonArray &list, const QString &id)
{
    for (const QJsonValue &v : list)
        if (v.toObject().value(QStringLiteral("id")).toString() == id)
            return v.toObject();
    return QJsonObject();
}

static QStringList strings(const QJsonArray &a)
{
    QStringList out;
    for (const QJsonValue &v : a)
        out << v.toString();
    return out;
}

void ApiImportDomain_Test::initTestCase()
{
    QVERIFY(m_dir.isValid());
    m_sourcePath = m_dir.filePath(QStringLiteral("source.qxw"));

    // Build the source project and write it the way App::saveXML() does
    Doc src(nullptr);
    Fixture *a = addDimmer(&src, QStringLiteral("Dimmer A"), 0, 1);
    Fixture *b = addDimmer(&src, QStringLiteral("Dimmer B"), 1, 1);
    QVERIFY(a != nullptr && b != nullptr);
    QCOMPARE(a->id(), quint32(0));
    QCOMPARE(b->id(), quint32(1));

    FixtureGroup *grp = new FixtureGroup(&src);
    grp->setName(QStringLiteral("Both"));
    grp->setSize(QSize(2, 1));
    grp->assignFixture(a->id(), QLCPoint(0, 0));
    grp->assignFixture(b->id(), QLCPoint(1, 0));
    QVERIFY(src.addFixtureGroup(grp));

    QLCPalette *warm = new QLCPalette(QLCPalette::Dimmer);
    warm->setName(QStringLiteral("Warm"));
    warm->setValue(128);
    QVERIFY(src.addPalette(warm));

    Scene *look = new Scene(&src);
    look->setName(QStringLiteral("Look"));
    look->setValue(a->id(), 0, 200);
    look->setValue(b->id(), 0, 100);
    look->addPalette(warm->id());
    QVERIFY(src.addFunction(look));

    EFX *move = new EFX(&src);
    move->setName(QStringLiteral("Move"));
    QVERIFY(move->addFixture(a->id(), 0));
    QVERIFY(move->addFixture(b->id(), 0));
    QVERIFY(src.addFunction(move));

    Chaser *run = new Chaser(&src);
    run->setName(QStringLiteral("Run"));
    QVERIFY(run->addStep(ChaserStep(look->id())));
    QVERIFY(run->addStep(ChaserStep(move->id())));
    QVERIFY(src.addFunction(run));

    Scene *solo = new Scene(&src);
    solo->setName(QStringLiteral("Solo"));
    solo->setValue(b->id(), 0, 255);
    QVERIFY(src.addFunction(solo));

    QFile file(m_sourcePath);
    QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
    QXmlStreamWriter xml(&file);
    xml.setAutoFormatting(true);
    QVERIFY(QLCFile::writeXMLHeader(&xml, QStringLiteral("Workspace"), QStringLiteral("test")));
    QVERIFY(src.saveXML(&xml));
    xml.writeEndElement(); // Workspace
    xml.writeEndDocument();
    file.close();
}

void ApiImportDomain_Test::init()
{
    m_doc = new Doc(nullptr);

    // Target: an address blocker (so "Dimmer B" cannot keep address 1), a fixture named like
    // source "Dimmer A" (matched, not copied), a function and a same-name palette
    QVERIFY(addDimmer(m_doc, QStringLiteral("Blocker"), 0, 4) != nullptr);
    Fixture *a = addDimmer(m_doc, QStringLiteral("Dimmer A"), 10, 1);
    QVERIFY(a != nullptr);
    m_targetA = a->id();

    Scene *existing = new Scene(m_doc);
    existing->setName(QStringLiteral("Existing"));
    QVERIFY(m_doc->addFunction(existing));

    QLCPalette *warm = new QLCPalette(QLCPalette::Dimmer);
    warm->setName(QStringLiteral("Warm"));
    warm->setValue(50);
    QVERIFY(m_doc->addPalette(warm));
    m_targetPalette = warm->id();

    m_apiServer = new ApiServer(nullptr, m_doc);
    QVERIFY(m_apiServer->listen(0, QHostAddress::LocalHost));

    m_client = new QWebSocket();
    m_client->open(QUrl(QStringLiteral("ws://127.0.0.1:%1/qlcplusapi").arg(m_apiServer->serverPort())));
    QVERIFY(QTest::qWaitFor([this]() { return m_client->state() == QAbstractSocket::ConnectedState; }, 2000));
    QVERIFY(sendAndWaitForReply(QStringLiteral("hello"), QJsonObject()).value(QStringLiteral("ok")).toBool());
}

void ApiImportDomain_Test::cleanup()
{
    delete m_client;
    m_client = nullptr;
    delete m_apiServer;
    m_apiServer = nullptr;
    delete m_doc;
    m_doc = nullptr;
}

QJsonObject ApiImportDomain_Test::sendAndWaitForReply(const QString &method, const QJsonObject &params)
{
    static int seq = 0;
    const QString requestId = QStringLiteral("t-%1").arg(++seq);
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
    }, 5000);
    return found;
}

QJsonObject ApiImportDomain_Test::waitForEvent(QSignalSpy &spy, const QString &topic,
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

int ApiImportDomain_Test::revision()
{
    return int(m_doc->docRevision());
}

QJsonObject ApiImportDomain_Test::pathSource() const
{
    QJsonObject p;
    p.insert(QStringLiteral("source"), QStringLiteral("path"));
    p.insert(QStringLiteral("path"), m_sourcePath);
    return p;
}

void ApiImportDomain_Test::listReportsContentsAndDependencies()
{
    const QJsonObject reply = sendAndWaitForReply(QStringLiteral("core.project.importList"), pathSource());
    QVERIFY2(reply.value(QStringLiteral("ok")).toBool(), qPrintable(QJsonDocument(reply).toJson()));
    const QJsonObject r = reply.value(QStringLiteral("result")).toObject();

    QCOMPARE(r.value(QStringLiteral("fileName")).toString(), QStringLiteral("source.qxw"));

    const QJsonArray fixtures = r.value(QStringLiteral("fixtures")).toArray();
    QCOMPARE(fixtures.count(), 2);
    QCOMPARE(findById(fixtures, QStringLiteral("0")).value(QStringLiteral("name")).toString(), QStringLiteral("Dimmer A"));
    QVERIFY(findById(fixtures, QStringLiteral("0")).value(QStringLiteral("existsInProject")).toBool());
    QVERIFY(findById(fixtures, QStringLiteral("1")).value(QStringLiteral("existsInProject")).toBool() == false);
    QCOMPARE(r.value(QStringLiteral("universes")).toArray().count(), 1);

    const QJsonArray groups = r.value(QStringLiteral("fixtureGroups")).toArray();
    QCOMPARE(groups.count(), 1);
    QCOMPARE(strings(groups.at(0).toObject().value(QStringLiteral("fixtureIds")).toArray()),
             QStringList({ QStringLiteral("0"), QStringLiteral("1") }));
    QCOMPARE(r.value(QStringLiteral("palettes")).toArray().count(), 1);

    const QJsonArray functions = r.value(QStringLiteral("functions")).toArray();
    QCOMPARE(functions.count(), 4);
    QJsonObject run;
    for (const QJsonValue &v : functions)
        if (v.toObject().value(QStringLiteral("name")).toString() == QStringLiteral("Run"))
            run = v.toObject();
    QCOMPARE(run.value(QStringLiteral("type")).toString(), QStringLiteral("Chaser"));
    const QJsonObject deps = run.value(QStringLiteral("dependencies")).toObject();
    QCOMPARE(deps.value(QStringLiteral("functionIds")).toArray().count(), 2);  // Look, Move
    QCOMPARE(strings(deps.value(QStringLiteral("fixtureIds")).toArray()),
             QStringList({ QStringLiteral("0"), QStringLiteral("1") }));
    QCOMPARE(deps.value(QStringLiteral("paletteIds")).toArray().count(), 1);

    // Read-only: nothing changed
    QCOMPARE(m_doc->fixtures().count(), 2);
}

void ApiImportDomain_Test::importRemapsEveryReference()
{
    // find the source ids by name from the list
    const QJsonObject list = sendAndWaitForReply(QStringLiteral("core.project.importList"), pathSource())
                                 .value(QStringLiteral("result")).toObject();
    QString runId;
    for (const QJsonValue &v : list.value(QStringLiteral("functions")).toArray())
        if (v.toObject().value(QStringLiteral("name")).toString() == QStringLiteral("Run"))
            runId = v.toObject().value(QStringLiteral("id")).toString();
    QVERIFY(runId.isEmpty() == false);

    QSignalSpy spy(m_client, &QWebSocket::textMessageReceived);
    QJsonObject params = pathSource();
    params.insert(QStringLiteral("baseRevision"), revision());
    params.insert(QStringLiteral("functionIds"), QJsonArray({ runId }));
    const QJsonObject reply = sendAndWaitForReply(QStringLiteral("core.project.import"), params);
    QVERIFY2(reply.value(QStringLiteral("ok")).toBool(), qPrintable(QJsonDocument(reply).toJson()));
    const QJsonObject r = reply.value(QStringLiteral("result")).toObject();

    // Fixtures: A matched by name, B created at the first free address after the blocker
    const QJsonObject fxMap = r.value(QStringLiteral("fixtureIdMap")).toObject();
    QCOMPARE(fxMap.value(QStringLiteral("0")).toString(), QString::number(m_targetA));
    const quint32 newB = fxMap.value(QStringLiteral("1")).toString().toUInt();
    QVERIFY(newB != 1 && newB != m_targetA);
    QVERIFY(m_doc->fixture(newB) != nullptr);
    QCOMPARE(m_doc->fixture(newB)->name(), QStringLiteral("Dimmer B"));
    QCOMPARE(m_doc->fixture(newB)->address(), quint32(4));
    QCOMPARE(strings(r.value(QStringLiteral("createdFixtureIds")).toArray()), QStringList({ QStringLiteral("1") }));

    // Palette matched by name: the scene points at the TARGET's palette
    QCOMPARE(r.value(QStringLiteral("paletteIdMap")).toObject().value(QStringLiteral("0")).toString(),
             QString::number(m_targetPalette));

    const QJsonObject fnMap = r.value(QStringLiteral("functionIdMap")).toObject();
    QCOMPARE(fnMap.count(), 3);   // Run + Look + Move
    Chaser *run = qobject_cast<Chaser *>(m_doc->function(fnMap.value(runId).toString().toUInt()));
    QVERIFY(run != nullptr);
    QCOMPARE(run->stepsCount(), 2);

    Scene *look = qobject_cast<Scene *>(m_doc->function(run->stepAt(0)->fid));
    EFX *move = qobject_cast<EFX *>(m_doc->function(run->stepAt(1)->fid));
    QVERIFY(look != nullptr);
    QVERIFY(move != nullptr);
    QCOMPARE(look->name(), QStringLiteral("Look"));
    QCOMPARE(look->value(m_targetA, 0), uchar(200));
    QCOMPARE(look->value(newB, 0), uchar(100));
    QCOMPARE(look->palettes(), QList<quint32>({ m_targetPalette }));

    // EFX heads follow the FIXTURE remap (they used to be looked up in the function map)
    QCOMPARE(move->fixtures().count(), 2);
    QCOMPARE(move->fixtures().at(0)->head().fxi, m_targetA);
    QCOMPARE(move->fixtures().at(1)->head().fxi, newB);

    // Announced with the usual events, then the summary
    QVERIFY(waitForEvent(spy, QStringLiteral("fixtures.patched"), [](const QJsonObject &d) {
        const QJsonArray f = d.value(QStringLiteral("fixtures")).toArray();
        return f.count() == 1;
    }).isEmpty() == false);
    QVERIFY(waitForEvent(spy, QStringLiteral("functions.created"), [run](const QJsonObject &d) {
        return d.value(QStringLiteral("function")).toObject().value(QStringLiteral("id")).toString() == QString::number(run->id());
    }).isEmpty() == false);
    QVERIFY(waitForEvent(spy, QStringLiteral("core.project.imported"), [](const QJsonObject &d) {
        return d.value(QStringLiteral("functionIdMap")).toObject().count() == 3;
    }).isEmpty() == false);
}

void ApiImportDomain_Test::importFromUploadedContent()
{
    QFile file(m_sourcePath);
    QVERIFY(file.open(QIODevice::ReadOnly));
    const QByteArray xml = file.readAll();

    QJsonObject params;
    params.insert(QStringLiteral("source"), QStringLiteral("upload"));
    params.insert(QStringLiteral("fileName"), QStringLiteral("source.qxw"));
    params.insert(QStringLiteral("contentBase64"), QString::fromLatin1(xml.toBase64()));

    const QJsonObject list = sendAndWaitForReply(QStringLiteral("core.project.importList"), params);
    QVERIFY(list.value(QStringLiteral("ok")).toBool());
    QString soloId;
    for (const QJsonValue &v : list.value(QStringLiteral("result")).toObject().value(QStringLiteral("functions")).toArray())
        if (v.toObject().value(QStringLiteral("name")).toString() == QStringLiteral("Solo"))
            soloId = v.toObject().value(QStringLiteral("id")).toString();

    params.insert(QStringLiteral("baseRevision"), revision());
    params.insert(QStringLiteral("functionIds"), QJsonArray({ soloId }));
    const QJsonObject reply = sendAndWaitForReply(QStringLiteral("core.project.import"), params);
    QVERIFY2(reply.value(QStringLiteral("ok")).toBool(), qPrintable(QJsonDocument(reply).toJson()));
    const QJsonObject r = reply.value(QStringLiteral("result")).toObject();

    // Only B is a dependency of Solo; A is not touched
    QCOMPARE(r.value(QStringLiteral("fixtureIdMap")).toObject().keys(), QStringList({ QStringLiteral("1") }));
    const quint32 newB = r.value(QStringLiteral("fixtureIdMap")).toObject().value(QStringLiteral("1")).toString().toUInt();
    Scene *solo = qobject_cast<Scene *>(m_doc->function(r.value(QStringLiteral("functionIdMap")).toObject().value(soloId).toString().toUInt()));
    QVERIFY(solo != nullptr);
    QCOMPARE(solo->value(newB, 0), uchar(255));
}

void ApiImportDomain_Test::importFixtureGroupBringsFixtures()
{
    QSignalSpy spy(m_client, &QWebSocket::textMessageReceived);
    QJsonObject params = pathSource();
    params.insert(QStringLiteral("baseRevision"), revision());
    params.insert(QStringLiteral("fixtureGroupIds"), QJsonArray({ QStringLiteral("0") }));
    const QJsonObject reply = sendAndWaitForReply(QStringLiteral("core.project.import"), params);
    QVERIFY2(reply.value(QStringLiteral("ok")).toBool(), qPrintable(QJsonDocument(reply).toJson()));
    const QJsonObject r = reply.value(QStringLiteral("result")).toObject();

    const quint32 gid = r.value(QStringLiteral("fixtureGroupIdMap")).toObject().value(QStringLiteral("0")).toString().toUInt();
    FixtureGroup *grp = m_doc->fixtureGroup(gid);
    QVERIFY(grp != nullptr);
    QCOMPARE(grp->name(), QStringLiteral("Both"));
    const quint32 newB = r.value(QStringLiteral("fixtureIdMap")).toObject().value(QStringLiteral("1")).toString().toUInt();
    QCOMPARE(grp->head(QLCPoint(0, 0)).fxi, m_targetA);
    QCOMPARE(grp->head(QLCPoint(1, 0)).fxi, newB);
    QVERIFY(r.value(QStringLiteral("functionIdMap")).toObject().isEmpty());

    QVERIFY(waitForEvent(spy, QStringLiteral("fixtures.group.created"), [gid](const QJsonObject &d) {
        return d.value(QStringLiteral("group")).toObject().value(QStringLiteral("id")).toString() == QString::number(gid);
    }).isEmpty() == false);
}

void ApiImportDomain_Test::errors()
{
    // stale revision
    QJsonObject params = pathSource();
    params.insert(QStringLiteral("baseRevision"), revision() + 5);
    params.insert(QStringLiteral("functionIds"), QJsonArray({ QStringLiteral("0") }));
    QJsonObject reply = sendAndWaitForReply(QStringLiteral("core.project.import"), params);
    QCOMPARE(reply.value(QStringLiteral("error")).toObject().value(QStringLiteral("code")).toString(), QStringLiteral("CONFLICT"));

    // unknown function in the source
    params.insert(QStringLiteral("baseRevision"), revision());
    params.insert(QStringLiteral("functionIds"), QJsonArray({ QStringLiteral("999") }));
    reply = sendAndWaitForReply(QStringLiteral("core.project.import"), params);
    QCOMPARE(reply.value(QStringLiteral("error")).toObject().value(QStringLiteral("code")).toString(), QStringLiteral("NOT_FOUND"));

    // nothing selected
    params.remove(QStringLiteral("functionIds"));
    reply = sendAndWaitForReply(QStringLiteral("core.project.import"), params);
    QCOMPARE(reply.value(QStringLiteral("error")).toObject().value(QStringLiteral("code")).toString(), QStringLiteral("INVALID_PARAMS"));

    // missing file / bad source / not a workspace
    QJsonObject missing;
    missing.insert(QStringLiteral("source"), QStringLiteral("path"));
    missing.insert(QStringLiteral("path"), m_dir.filePath(QStringLiteral("nope.qxw")));
    reply = sendAndWaitForReply(QStringLiteral("core.project.importList"), missing);
    QCOMPARE(reply.value(QStringLiteral("error")).toObject().value(QStringLiteral("code")).toString(), QStringLiteral("NOT_FOUND"));

    QJsonObject bad;
    bad.insert(QStringLiteral("source"), QStringLiteral("somewhere"));
    reply = sendAndWaitForReply(QStringLiteral("core.project.importList"), bad);
    QCOMPARE(reply.value(QStringLiteral("error")).toObject().value(QStringLiteral("code")).toString(), QStringLiteral("INVALID_PARAMS"));

    QJsonObject junk;
    junk.insert(QStringLiteral("source"), QStringLiteral("upload"));
    junk.insert(QStringLiteral("contentBase64"), QString::fromLatin1(QByteArray("<html/>").toBase64()));
    reply = sendAndWaitForReply(QStringLiteral("core.project.importList"), junk);
    QCOMPARE(reply.value(QStringLiteral("error")).toObject().value(QStringLiteral("code")).toString(), QStringLiteral("INVALID_PARAMS"));

    // nothing was imported by any of these
    QCOMPARE(m_doc->fixtures().count(), 2);
    QCOMPARE(m_doc->functions().count(), 1);
}

QTEST_GUILESS_MAIN(ApiImportDomain_Test)
