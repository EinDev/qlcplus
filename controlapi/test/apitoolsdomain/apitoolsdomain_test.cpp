/*
  Q Light Controller Plus - Control API unit test
  apitoolsdomain_test.cpp

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
#include <QTemporaryDir>
#include <QFile>
#include <QtTest>

#include "apitoolsdomain_test.h"
#include "apiprojecthost.h"
#include "apiserver.h"
#include "functionparent.h"
#include "inputoutputmap.h"
#include "mastertimer.h"
#include "showfunction.h"
#include "fixture.h"
#include "scene.h"
#include "show.h"
#include "track.h"
#include "doc.h"

namespace
{

/** Minimal ApiProjectHost: path/in-memory loads only clear the Doc */
class FakeProjectHost : public QObject, public ApiProjectHost
{
public:
    explicit FakeProjectHost(Doc *doc) : m_doc(doc) {}

    QString fileName() const override { return m_fileName; }
    void setFileName(const QString &fileName) override { m_fileName = fileName; }
    bool newWorkspace() override { m_doc->clearContents(); m_fileName.clear(); return true; }
    bool loadWorkspace(const QString &fileName) override { m_doc->clearContents(); m_fileName = fileName; return true; }
    bool saveWorkspace(const QString &) override { return false; }
    void slotLoadDocFromMemory(QByteArray &) override { m_doc->clearContents(); }
    QStringList recentFiles() const override { return QStringList(); }
    QString workingPath() const override { return QString(); }
    void setWorkingPath(QString) override {}

    Doc *m_doc;
    QString m_fileName;
};

QByteArray workspaceXml(const QString &version)
{
    return QStringLiteral(
        "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n<!DOCTYPE Workspace>\n"
        "<Workspace xmlns=\"http://www.qlcplus.org/Workspace\">\n"
        " <Creator><Name>Q Light Controller Plus</Name><Version>%1</Version></Creator>\n"
        " <Engine/>\n</Workspace>\n").arg(version).toUtf8();
}

} // namespace

void ApiToolsDomain_Test::init()
{
    m_doc = new Doc(nullptr);
    m_doc->masterTimer()->start();
    m_doc->inputOutputMap()->startUniverses();
    m_apiServer = new ApiServer(nullptr, m_doc);
    QVERIFY(m_apiServer->listen(0, QHostAddress::LocalHost));
    m_client = new QWebSocket();
    m_client->open(QUrl(QStringLiteral("ws://127.0.0.1:%1/qlcplusapi").arg(m_apiServer->serverPort())));
    QVERIFY(QTest::qWaitFor([this]() { return m_client->state() == QAbstractSocket::ConnectedState; }, 2000));
    QCOMPARE(call(QStringLiteral("hello"), QJsonObject()).value(QStringLiteral("ok")).toBool(), true);
}

void ApiToolsDomain_Test::cleanup()
{
    delete m_client;
    m_client = nullptr;
    delete m_apiServer;
    m_apiServer = nullptr;
    delete m_host;
    m_host = nullptr;
    m_doc->masterTimer()->stop();
    delete m_doc;
    m_doc = nullptr;
}

void ApiToolsDomain_Test::useFakeHost()
{
    delete m_client;
    delete m_apiServer;
    m_host = new FakeProjectHost(m_doc);
    m_apiServer = new ApiServer(m_host, m_doc);
    QVERIFY(m_apiServer->listen(0, QHostAddress::LocalHost));
    m_client = new QWebSocket();
    m_client->open(QUrl(QStringLiteral("ws://127.0.0.1:%1/qlcplusapi").arg(m_apiServer->serverPort())));
    QVERIFY(QTest::qWaitFor([this]() { return m_client->state() == QAbstractSocket::ConnectedState; }, 2000));
    QCOMPARE(call(QStringLiteral("hello"), QJsonObject()).value(QStringLiteral("ok")).toBool(), true);
}

QJsonObject ApiToolsDomain_Test::call(const QString &method, const QJsonObject &params)
{
    const QString requestId = QStringLiteral("t-%1").arg(++m_nextId);
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
    }, 3000);
    return found;
}

QJsonObject ApiToolsDomain_Test::result(const QString &method, const QJsonObject &params)
{
    QJsonObject reply = call(method, params);
    if (reply.value(QStringLiteral("ok")).toBool() == false)
        qWarning() << method << "failed:" << reply;
    return reply.value(QStringLiteral("result")).toObject();
}

QString ApiToolsDomain_Test::errorCode(const QJsonObject &reply) const
{
    return reply.value(QStringLiteral("error")).toObject().value(QStringLiteral("code")).toString();
}

Show *ApiToolsDomain_Test::addShow(const QString &name, const QList<QPair<quint32, quint32>> &items)
{
    Scene *scene = new Scene(m_doc);
    scene->setName(name + QStringLiteral(" scene"));
    m_doc->addFunction(scene);

    Show *show = new Show(m_doc);
    show->setName(name);
    // Only beat-based Shows (or ones holding Beats-tempo items) can carry
    // the legacy beat-pseudo-count values (Doc::possiblyAffectedLegacyBeatShows)
    show->setTimeDivision(Show::BPM_4_4, 120);
    m_doc->addFunction(show);
    Track *track = new Track(Function::invalidId(), show);
    show->addTrack(track);
    for (const auto &it : items)
    {
        ShowFunction *sf = track->createShowFunction(scene->id());
        sf->setStartTime(it.first);
        sf->setDuration(it.second);
    }
    return show;
}

/*********************************************************************
 * io.dmx.channel.inspect
 *********************************************************************/

void ApiToolsDomain_Test::inspectRejectsBadParams()
{
    QJsonObject p;
    QCOMPARE(errorCode(call(QStringLiteral("io.dmx.channel.inspect"), p)), QStringLiteral("INVALID_PARAMS"));
    p.insert(QStringLiteral("universeId"), 0);
    p.insert(QStringLiteral("channel"), 512);
    QCOMPARE(errorCode(call(QStringLiteral("io.dmx.channel.inspect"), p)), QStringLiteral("INVALID_PARAMS"));
    p.insert(QStringLiteral("universeId"), int(m_doc->inputOutputMap()->universesCount()));
    p.insert(QStringLiteral("channel"), 0);
    QCOMPARE(errorCode(call(QStringLiteral("io.dmx.channel.inspect"), p)), QStringLiteral("INVALID_PARAMS"));
}

void ApiToolsDomain_Test::inspectReportsFixtureOverrideAndFader()
{
    Fixture *fx = new Fixture(m_doc);
    fx->setName(QStringLiteral("Dimmer U1"));
    fx->setUniverse(1);
    fx->setAddress(10);
    fx->setChannels(4);
    QVERIFY(m_doc->addFixture(fx));

    // nothing on the channel yet
    QJsonObject p;
    p.insert(QStringLiteral("universeId"), 1);
    p.insert(QStringLiteral("channel"), 12);
    QJsonObject r = result(QStringLiteral("io.dmx.channel.inspect"), p);
    QCOMPARE(r.value(QStringLiteral("address")).toInt(), (1 << 9) + 12);
    QJsonObject fixture = r.value(QStringLiteral("fixture")).toObject();
    QCOMPARE(fixture.value(QStringLiteral("id")).toString(), QString::number(fx->id()));
    QCOMPARE(fixture.value(QStringLiteral("channelIndex")).toInt(), 2);
    QCOMPARE(fixture.value(QStringLiteral("group")).toString(), QStringLiteral("Intensity"));
    QVERIFY(r.value(QStringLiteral("simpleDeskOverride")).isNull());
    QCOMPARE(r.value(QStringLiteral("faders")).toArray().count(), 0);

    // a web Simple Desk override on it
    QJsonObject entry;
    entry.insert(QStringLiteral("address"), (1 << 9) + 12);
    entry.insert(QStringLiteral("value"), 180);
    QJsonObject set;
    set.insert(QStringLiteral("channels"), QJsonArray({ entry }));
    QCOMPARE(call(QStringLiteral("io.simpleDesk.setChannels"), set).value(QStringLiteral("ok")).toBool(), true);

    QVERIFY(QTest::qWaitFor([&]()
    {
        r = result(QStringLiteral("io.dmx.channel.inspect"), p);
        return r.value(QStringLiteral("postGMValue")).toInt() == 180 && r.value(QStringLiteral("faders")).toArray().count() == 1;
    }, 3000));
    QCOMPARE(r.value(QStringLiteral("simpleDeskOverride")).toInt(), 180);
    QJsonObject fader = r.value(QStringLiteral("faders")).toArray().first().toObject();
    QCOMPARE(fader.value(QStringLiteral("source")).toString(), QStringLiteral("controlApiSimpleDesk"));
    QCOMPARE(fader.value(QStringLiteral("current")).toInt(), 180);
    QVERIFY(fader.value(QStringLiteral("flags")).toArray().contains(QStringLiteral("Override")));
    QVERIFY(r.value(QStringLiteral("lastWrite")).toObject().value(QStringLiteral("value")).toInt() == 180);
}

void ApiToolsDomain_Test::inspectAttributesRunningScene()
{
    Fixture *fx = new Fixture(m_doc);
    fx->setName(QStringLiteral("Dimmer"));
    fx->setUniverse(0);
    fx->setAddress(0);
    fx->setChannels(2);
    QVERIFY(m_doc->addFixture(fx));
    Scene *scene = new Scene(m_doc);
    scene->setName(QStringLiteral("Look"));
    scene->setValue(fx->id(), 1, 99);
    QVERIFY(m_doc->addFunction(scene));
    scene->start(m_doc->masterTimer(), FunctionParent::master(FunctionParent::ControlApi));

    QJsonObject p;
    p.insert(QStringLiteral("universeId"), 0);
    p.insert(QStringLiteral("channel"), 1);
    QJsonObject r;
    QVERIFY(QTest::qWaitFor([&]()
    {
        r = result(QStringLiteral("io.dmx.channel.inspect"), p);
        return r.value(QStringLiteral("faders")).toArray().count() == 1 && r.value(QStringLiteral("preGMValue")).toInt() == 99;
    }, 3000));
    QJsonObject fader = r.value(QStringLiteral("faders")).toArray().first().toObject();
    QCOMPARE(fader.value(QStringLiteral("source")).toString(), QStringLiteral("function"));
    QCOMPARE(fader.value(QStringLiteral("function")).toObject().value(QStringLiteral("id")).toString(), QString::number(scene->id()));
    QCOMPARE(fader.value(QStringLiteral("function")).toObject().value(QStringLiteral("type")).toString(), QStringLiteral("Scene"));
    QJsonArray startedBy = fader.value(QStringLiteral("startedBy")).toArray();
    QCOMPARE(startedBy.count(), 1);
    QCOMPARE(startedBy.first().toObject().value(QStringLiteral("type")).toString(), QStringLiteral("master"));
    QCOMPARE(startedBy.first().toObject().value(QStringLiteral("name")).toString(), QStringLiteral("controlApi"));

    scene->stop(FunctionParent::master(FunctionParent::ControlApi));
    QVERIFY(QTest::qWaitFor([&]() { return scene->isRunning() == false; }, 3000));
}

/*********************************************************************
 * functions.show.legacyTiming.*
 *********************************************************************/

void ApiToolsDomain_Test::legacyTimingNothingFlaggedWithoutAProjectFile()
{
    // A project built in this session (no file, no upload) is never flagged,
    // even though a Doc-level check with an empty version would flag it
    addShow(QStringLiteral("Fresh"), { { 1000, 2000 } });
    QJsonObject r = result(QStringLiteral("functions.show.legacyTiming.get"), QJsonObject());
    QCOMPARE(r.value(QStringLiteral("source")).toString(), QStringLiteral("none"));
    QCOMPARE(r.value(QStringLiteral("shows")).toArray().count(), 0);
}

void ApiToolsDomain_Test::legacyTimingFlagsOldUploadPreviewsAndConverts()
{
    useFakeHost();
    QJsonObject open;
    open.insert(QStringLiteral("source"), QStringLiteral("upload"));
    open.insert(QStringLiteral("fileName"), QStringLiteral("old.qxw"));
    open.insert(QStringLiteral("contentBase64"), QString::fromLatin1(workspaceXml(QStringLiteral("4.12.0")).toBase64()));
    QCOMPARE(call(QStringLiteral("core.project.open"), open).value(QStringLiteral("ok")).toBool(), true);

    // (the fake host loads nothing - build the "loaded" project by hand)
    Show *show = addShow(QStringLiteral("Old show"), { { 4000, 2000 }, { 8000, 1000 } });
    addShow(QStringLiteral("Empty show"), {}); // no items: never flagged

    QJsonObject r = result(QStringLiteral("functions.show.legacyTiming.get"), QJsonObject());
    QCOMPARE(r.value(QStringLiteral("source")).toString(), QStringLiteral("upload"));
    QCOMPARE(r.value(QStringLiteral("creatorVersion")).toString(), QStringLiteral("4.12.0"));
    QJsonArray shows = r.value(QStringLiteral("shows")).toArray();
    QCOMPARE(shows.count(), 1);
    QCOMPARE(shows.first().toObject().value(QStringLiteral("id")).toString(), QString::number(show->id()));
    QCOMPARE(shows.first().toObject().value(QStringLiteral("itemCount")).toInt(), 2);

    // preview at 120 BPM: one legacy unit = 0.5 ms
    QJsonObject pp;
    pp.insert(QStringLiteral("showId"), QString::number(show->id()));
    pp.insert(QStringLiteral("bpm"), 120);
    r = result(QStringLiteral("functions.show.legacyTiming.get"), pp);
    QJsonArray preview = r.value(QStringLiteral("preview")).toArray();
    QCOMPARE(preview.count(), 2);
    QCOMPARE(preview.at(0).toObject().value(QStringLiteral("oldStart")).toInt(), 4000);
    QCOMPARE(preview.at(0).toObject().value(QStringLiteral("newStart")).toInt(), 2000);
    QCOMPARE(preview.at(1).toObject().value(QStringLiteral("newDuration")).toInt(), 500);

    // convert: stale revision first, then for real
    QJsonObject cp = pp;
    cp.insert(QStringLiteral("baseRevision"), int(m_doc->docRevision()) + 5);
    QCOMPARE(errorCode(call(QStringLiteral("functions.show.legacyTiming.convert"), cp)), QStringLiteral("CONFLICT"));
    cp.insert(QStringLiteral("bpm"), 0);
    cp.insert(QStringLiteral("baseRevision"), int(m_doc->docRevision()));
    QCOMPARE(errorCode(call(QStringLiteral("functions.show.legacyTiming.convert"), cp)), QStringLiteral("INVALID_PARAMS"));

    quint32 before = m_doc->docRevision();
    cp.insert(QStringLiteral("bpm"), 120);
    QSignalSpy spy(m_client, &QWebSocket::textMessageReceived);
    r = result(QStringLiteral("functions.show.legacyTiming.convert"), cp);
    QCOMPARE(r.value(QStringLiteral("itemsChanged")).toInt(), 2);
    QVERIFY(quint32(r.value(QStringLiteral("docRevision")).toInt()) > before);

    QList<ShowFunction *> items = show->tracks().first()->showFunctions();
    QCOMPARE(int(items.at(0)->startTime()), 2000);
    QCOMPARE(int(items.at(0)->duration()), 1000);
    QCOMPARE(int(items.at(1)->startTime()), 4000);
    QCOMPARE(int(items.at(1)->duration()), 500);

    bool sawUpdated = false;
    for (const QList<QVariant> &frame : spy)
    {
        QJsonObject obj = QJsonDocument::fromJson(frame.at(0).toString().toUtf8()).object();
        if (obj.value(QStringLiteral("topic")).toString() == QStringLiteral("functions.updated") &&
            obj.value(QStringLiteral("data")).toObject().value(QStringLiteral("functionId")).toString() == QString::number(show->id()))
            sawUpdated = true;
    }
    QVERIFY(sawUpdated);

    // no longer flagged for the rest of this project's life
    r = result(QStringLiteral("functions.show.legacyTiming.get"), QJsonObject());
    QCOMPARE(r.value(QStringLiteral("shows")).toArray().count(), 0);
}

void ApiToolsDomain_Test::legacyTimingDismissAndNewerFileAreNotFlagged()
{
    useFakeHost();
    QTemporaryDir dir;
    QVERIFY(dir.isValid());

    // an old file on disk (source "file")
    const QString oldPath = dir.path() + QStringLiteral("/old.qxw");
    QFile oldFile(oldPath);
    QVERIFY(oldFile.open(QIODevice::WriteOnly));
    oldFile.write(workspaceXml(QStringLiteral("5.0.0 GIT")));
    oldFile.close();
    QJsonObject open;
    open.insert(QStringLiteral("source"), QStringLiteral("path"));
    open.insert(QStringLiteral("path"), oldPath);
    QCOMPARE(call(QStringLiteral("core.project.open"), open).value(QStringLiteral("ok")).toBool(), true);
    Show *show = addShow(QStringLiteral("Show A"), { { 100, 100 } });

    QJsonObject r = result(QStringLiteral("functions.show.legacyTiming.get"), QJsonObject());
    QCOMPARE(r.value(QStringLiteral("source")).toString(), QStringLiteral("file"));
    QCOMPARE(r.value(QStringLiteral("shows")).toArray().count(), 1);

    QJsonObject dp;
    dp.insert(QStringLiteral("showId"), QString::number(show->id()));
    QCOMPARE(call(QStringLiteral("functions.show.legacyTiming.dismiss"), dp).value(QStringLiteral("ok")).toBool(), true);
    QCOMPARE(result(QStringLiteral("functions.show.legacyTiming.get"), QJsonObject()).value(QStringLiteral("shows")).toArray().count(), 0);
    QCOMPARE(int(show->tracks().first()->showFunctions().first()->startTime()), 100); // untouched

    // re-opening starts over; a file from 5.3.1 on is never flagged
    const QString newPath = dir.path() + QStringLiteral("/new.qxw");
    QFile newFile(newPath);
    QVERIFY(newFile.open(QIODevice::WriteOnly));
    newFile.write(workspaceXml(QStringLiteral("5.3.1")));
    newFile.close();
    open.insert(QStringLiteral("path"), newPath);
    QCOMPARE(call(QStringLiteral("core.project.open"), open).value(QStringLiteral("ok")).toBool(), true);
    addShow(QStringLiteral("Show B"), { { 100, 100 } });
    r = result(QStringLiteral("functions.show.legacyTiming.get"), QJsonObject());
    QCOMPARE(r.value(QStringLiteral("creatorVersion")).toString(), QStringLiteral("5.3.1"));
    QCOMPARE(r.value(QStringLiteral("shows")).toArray().count(), 0);

    QCOMPARE(errorCode(call(QStringLiteral("functions.show.legacyTiming.dismiss"), QJsonObject())), QStringLiteral("NOT_FOUND"));
}

QTEST_GUILESS_MAIN(ApiToolsDomain_Test)
