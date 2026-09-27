/*
  Q Light Controller Plus - Control API unit test
  apishowdomain_test.cpp

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

#include "apishowdomain_test.h"
#include "apiserver.h"
#include "mastertimer.h"
#include "showfunction.h"
#include "chaserstep.h"
#include "fixture.h"
#include "chaser.h"
#include "scene.h"
#include "track.h"
#include "show.h"
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

static QJsonObject trackById(const QJsonArray &tracks, const QString &id)
{
    for (const QJsonValue &v : tracks)
        if (v.toObject().value(QStringLiteral("id")).toString() == id)
            return v.toObject();
    return QJsonObject();
}

static QJsonObject itemById(const QJsonObject &detail, const QString &itemId, QString *trackId = nullptr)
{
    for (const QJsonValue &t : detail.value(QStringLiteral("tracks")).toArray())
    {
        for (const QJsonValue &i : t.toObject().value(QStringLiteral("items")).toArray())
        {
            if (i.toObject().value(QStringLiteral("id")).toString() == itemId)
            {
                if (trackId != nullptr)
                    *trackId = t.toObject().value(QStringLiteral("id")).toString();
                return i.toObject();
            }
        }
    }
    return QJsonObject();
}

void ApiShowDomain_Test::init()
{
    m_doc = new Doc(nullptr);
    // The playhead test starts the Show for real: Function::start() only queues
    // the request, MasterTimer's own thread performs it on its next tick.
    m_doc->masterTimer()->start();

    // A Scene with no values self-stops the instant it starts (Scene::write);
    // a raw, non-fixture channel value keeps it running without any output.
    m_scene = new Scene(m_doc);
    m_scene->setName(QStringLiteral("Scene A"));
    m_scene->setValue(Fixture::invalidId(), 0, 255);
    QVERIFY(m_doc->addFunction(m_scene));

    m_scene2 = new Scene(m_doc);
    m_scene2->setName(QStringLiteral("Scene B"));
    m_scene2->setValue(Fixture::invalidId(), 1, 255);
    QVERIFY(m_doc->addFunction(m_scene2));

    // 3 steps of 1000 ms in Common duration mode (like a freshly created Chaser)
    m_chaser = new Chaser(m_doc);
    m_chaser->setName(QStringLiteral("Chaser C"));
    m_chaser->setDuration(1000);
    for (int i = 0; i < 3; i++)
        QVERIFY(m_chaser->addStep(ChaserStep(m_scene->id(), 0, 1000, 0)));
    QVERIFY(m_doc->addFunction(m_chaser));

    m_show = new Show(m_doc);
    m_show->setName(QStringLiteral("Test Show"));
    QVERIFY(m_doc->addFunction(m_show));

    m_track = new Track(Function::invalidId(), m_show);
    m_track->setName(QStringLiteral("Track 1"));
    QVERIFY(m_show->addTrack(m_track));

    ShowFunction *sf = m_track->createShowFunction(m_scene->id());
    sf->setStartTime(0);
    sf->setDuration(5000);
    sf->setColor(ShowFunction::defaultColor(Function::SceneType));
    m_itemId = sf->id();

    m_apiServer = new ApiServer(nullptr, m_doc);
    QVERIFY(m_apiServer->listen(0, QHostAddress::LocalHost));

    m_client = new QWebSocket();
    m_client->open(QUrl(QStringLiteral("ws://127.0.0.1:%1/qlcplusapi").arg(m_apiServer->serverPort())));
    QVERIFY(QTest::qWaitFor([this]() { return m_client->state() == QAbstractSocket::ConnectedState; }, 2000));
    QVERIFY(helloAndGetClientId().isEmpty() == false);
}

void ApiShowDomain_Test::cleanup()
{
    if (m_show != nullptr && m_show->isRunning())
    {
        m_show->stop(FunctionParent::master());
        (void)QTest::qWaitFor([this]() { return m_show->isRunning() == false; }, 2000);
    }
    delete m_client;
    m_client = nullptr;
    delete m_apiServer;
    m_apiServer = nullptr;
    m_doc->masterTimer()->stop();
    delete m_doc; // owns every function
    m_doc = nullptr;
    m_scene = nullptr;
    m_scene2 = nullptr;
    m_chaser = nullptr;
    m_show = nullptr;
    m_track = nullptr;
}

QJsonObject ApiShowDomain_Test::sendAndWaitForReply(const QString &method, const QJsonObject &params)
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

QString ApiShowDomain_Test::helloAndGetClientId()
{
    QJsonObject reply = sendAndWaitForReply(QStringLiteral("hello"), QJsonObject());
    return reply.value(QStringLiteral("result")).toObject().value(QStringLiteral("clientId")).toString();
}

QJsonObject ApiShowDomain_Test::waitForEvent(QSignalSpy &spy, const QString &topic,
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

QJsonObject ApiShowDomain_Test::getDetail()
{
    QJsonObject params;
    params.insert(QStringLiteral("functionId"), QString::number(m_show->id()));
    QJsonObject reply = sendAndWaitForReply(QStringLiteral("functions.get"), params);
    return reply.value(QStringLiteral("result")).toObject().value(QStringLiteral("typeDetail")).toObject();
}

int ApiShowDomain_Test::revision()
{
    return int(m_doc->docRevision());
}

QJsonObject ApiShowDomain_Test::showParams(const QJsonObject &extra)
{
    QJsonObject params = extra;
    params.insert(QStringLiteral("showId"), QString::number(m_show->id()));
    params.insert(QStringLiteral("baseRevision"), revision());
    return params;
}

QJsonObject ApiShowDomain_Test::mutate(const QString &method, const QJsonObject &extra)
{
    QJsonObject reply = sendAndWaitForReply(method, showParams(extra));
    if (reply.value(QStringLiteral("ok")).toBool() == false)
        qWarning() << method << "failed:" << reply;
    return reply;
}

QJsonObject ApiShowDomain_Test::firstItem(const QJsonObject &detail, int trackIndex, int itemIndex)
{
    return detail.value(QStringLiteral("tracks")).toArray().at(trackIndex).toObject()
                 .value(QStringLiteral("items")).toArray().at(itemIndex).toObject();
}

/*****************************************************************************
 * typeDetail
 *****************************************************************************/

void ApiShowDomain_Test::getReturnsShowTypeDetail()
{
    QJsonObject detail = getDetail();
    QCOMPARE(detail.value(QStringLiteral("functionId")).toString(), QString::number(m_show->id()));
    QCOMPARE(detail.value(QStringLiteral("timeDivisionType")).toString(), QStringLiteral("time"));
    QCOMPARE(detail.value(QStringLiteral("timeDivisionBPM")).toInt(), 120);
    QCOMPARE(detail.value(QStringLiteral("totalDuration")).toInt(), 5000);
    QCOMPARE(detail.value(QStringLiteral("docRevision")).toInt(), revision());

    QJsonArray tracks = detail.value(QStringLiteral("tracks")).toArray();
    QCOMPARE(tracks.count(), 1);
    QJsonObject track = tracks.at(0).toObject();
    QCOMPARE(track.value(QStringLiteral("id")).toString(), QString::number(m_track->id()));
    QCOMPARE(track.value(QStringLiteral("name")).toString(), QStringLiteral("Track 1"));
    QCOMPARE(track.value(QStringLiteral("mute")).toBool(), false);
    QVERIFY(track.contains(QStringLiteral("sceneId")) == false); // audio/video-style track, no Scene bound

    QJsonObject item = firstItem(detail);
    QCOMPARE(item.value(QStringLiteral("id")).toString(), QString::number(m_itemId));
    QCOMPARE(item.value(QStringLiteral("functionId")).toString(), QString::number(m_scene->id()));
    QCOMPARE(item.value(QStringLiteral("functionType")).toString(), QStringLiteral("Scene"));
    QCOMPARE(item.value(QStringLiteral("functionName")).toString(), QStringLiteral("Scene A"));
    QCOMPARE(item.value(QStringLiteral("startTime")).toInt(), 0);
    QCOMPARE(item.value(QStringLiteral("duration")).toInt(), 5000);
    QCOMPARE(item.value(QStringLiteral("color")).toString(), ShowFunction::defaultColor(Function::SceneType).name());
    QCOMPARE(item.value(QStringLiteral("locked")).toBool(), false);
}

/*****************************************************************************
 * time division
 *****************************************************************************/

void ApiShowDomain_Test::setTimeDivisionAppliesAndBroadcasts()
{
    QSignalSpy spy(m_client, &QWebSocket::textMessageReceived);
    int before = revision();
    QJsonObject extra;
    extra.insert(QStringLiteral("timeDivisionType"), QStringLiteral("bpm_3_4"));
    extra.insert(QStringLiteral("bpm"), 128);
    QJsonObject reply = mutate(QStringLiteral("functions.show.setTimeDivision"), extra);
    QVERIFY(reply.value(QStringLiteral("ok")).toBool());
    QVERIFY(reply.value(QStringLiteral("result")).toObject().value(QStringLiteral("docRevision")).toInt() > before);

    QCOMPARE(m_show->timeDivisionType(), Show::BPM_3_4);
    QCOMPARE(m_show->beatsDivision(), 3);
    QCOMPARE(m_show->timeDivisionBPM(), 128);
    QCOMPARE(m_show->tempoType(), Function::Beats);

    QJsonObject ev = waitForEvent(spy, QStringLiteral("functions.show.timeDivisionChanged"), [](const QJsonObject &) { return true; });
    QVERIFY(ev.isEmpty() == false);
    QCOMPARE(ev.value(QStringLiteral("data")).toObject().value(QStringLiteral("timeDivisionType")).toString(), QStringLiteral("bpm_3_4"));
    QCOMPARE(ev.value(QStringLiteral("data")).toObject().value(QStringLiteral("timeDivisionBPM")).toInt(), 128);

    // the functions.show.setTimeDivision spec spells the id "functionId"
    QJsonObject params;
    params.insert(QStringLiteral("functionId"), QString::number(m_show->id()));
    params.insert(QStringLiteral("timeDivisionType"), QStringLiteral("time"));
    params.insert(QStringLiteral("baseRevision"), revision());
    reply = sendAndWaitForReply(QStringLiteral("functions.show.setTimeDivision"), params);
    QVERIFY(reply.value(QStringLiteral("ok")).toBool());
    QCOMPARE(m_show->timeDivisionType(), Show::Time);
    QCOMPARE(m_show->tempoType(), Function::Time);
    QCOMPARE(m_show->timeDivisionBPM(), 128); // kept
    QCOMPARE(getDetail().value(QStringLiteral("timeDivisionType")).toString(), QStringLiteral("time"));
}

void ApiShowDomain_Test::setTimeDivisionRequiresBpmForBeats()
{
    QJsonObject extra;
    extra.insert(QStringLiteral("timeDivisionType"), QStringLiteral("bpm_4_4"));
    QJsonObject reply = sendAndWaitForReply(QStringLiteral("functions.show.setTimeDivision"), showParams(extra));
    QCOMPARE(reply.value(QStringLiteral("ok")).toBool(), false);
    QCOMPARE(reply.value(QStringLiteral("error")).toObject().value(QStringLiteral("code")).toString(), QStringLiteral("INVALID_PARAMS"));
    QCOMPARE(m_show->timeDivisionType(), Show::Time);

    extra.insert(QStringLiteral("timeDivisionType"), QStringLiteral("waltz"));
    extra.insert(QStringLiteral("bpm"), 100);
    reply = sendAndWaitForReply(QStringLiteral("functions.show.setTimeDivision"), showParams(extra));
    QCOMPARE(reply.value(QStringLiteral("error")).toObject().value(QStringLiteral("code")).toString(), QStringLiteral("INVALID_PARAMS"));
}

/*****************************************************************************
 * tracks
 *****************************************************************************/

void ApiShowDomain_Test::trackAddRemoveRename()
{
    QSignalSpy spy(m_client, &QWebSocket::textMessageReceived);

    // add, bound to a Scene, with the default name
    QJsonObject extra;
    extra.insert(QStringLiteral("sceneId"), QString::number(m_scene2->id()));
    QJsonObject reply = mutate(QStringLiteral("functions.show.track.add"), extra);
    QVERIFY(reply.value(QStringLiteral("ok")).toBool());
    QString trackId = reply.value(QStringLiteral("result")).toObject().value(QStringLiteral("trackId")).toString();
    QVERIFY(trackId.isEmpty() == false);
    QCOMPARE(m_show->tracks().count(), 2);
    Track *added = m_show->track(trackId.toUInt());
    QVERIFY(added != nullptr);
    QCOMPARE(added->name(), QStringLiteral("Track 2"));
    QCOMPARE(added->getSceneID(), m_scene2->id());
    QCOMPARE(added->parent(), m_show);

    QJsonObject ev = waitForEvent(spy, QStringLiteral("functions.show.track.added"), [](const QJsonObject &) { return true; });
    QVERIFY(ev.isEmpty() == false);
    QJsonObject evTrack = ev.value(QStringLiteral("data")).toObject().value(QStringLiteral("track")).toObject();
    QCOMPARE(evTrack.value(QStringLiteral("id")).toString(), trackId);
    QCOMPARE(evTrack.value(QStringLiteral("sceneId")).toString(), QString::number(m_scene2->id()));
    QCOMPARE(evTrack.value(QStringLiteral("items")).toArray().count(), 0);

    // rename
    extra = QJsonObject();
    extra.insert(QStringLiteral("trackId"), trackId);
    extra.insert(QStringLiteral("name"), QStringLiteral("Vocals"));
    reply = mutate(QStringLiteral("functions.show.track.rename"), extra);
    QVERIFY(reply.value(QStringLiteral("ok")).toBool());
    QCOMPARE(added->name(), QStringLiteral("Vocals"));
    ev = waitForEvent(spy, QStringLiteral("functions.show.track.renamed"), [](const QJsonObject &d) { return d.value(QStringLiteral("name")).toString() == QStringLiteral("Vocals"); });
    QVERIFY(ev.isEmpty() == false);
    QCOMPARE(trackById(getDetail().value(QStringLiteral("tracks")).toArray(), trackId).value(QStringLiteral("name")).toString(), QStringLiteral("Vocals"));

    // an empty name is refused
    extra.insert(QStringLiteral("name"), QString());
    reply = sendAndWaitForReply(QStringLiteral("functions.show.track.rename"), showParams(extra));
    QCOMPARE(reply.value(QStringLiteral("error")).toObject().value(QStringLiteral("code")).toString(), QStringLiteral("INVALID_PARAMS"));

    // remove
    extra = QJsonObject();
    extra.insert(QStringLiteral("trackId"), trackId);
    reply = mutate(QStringLiteral("functions.show.track.remove"), extra);
    QVERIFY(reply.value(QStringLiteral("ok")).toBool());
    QCOMPARE(m_show->tracks().count(), 1);
    QVERIFY(m_show->track(trackId.toUInt()) == nullptr);
    ev = waitForEvent(spy, QStringLiteral("functions.show.track.removed"), [trackId](const QJsonObject &d) { return d.value(QStringLiteral("trackId")).toString() == trackId; });
    QVERIFY(ev.isEmpty() == false);

    // unknown track
    reply = sendAndWaitForReply(QStringLiteral("functions.show.track.remove"), showParams(extra));
    QCOMPARE(reply.value(QStringLiteral("error")).toObject().value(QStringLiteral("code")).toString(), QStringLiteral("NOT_FOUND"));
}

void ApiShowDomain_Test::trackAddRejectsNonScene()
{
    QJsonObject extra;
    extra.insert(QStringLiteral("sceneId"), QString::number(m_chaser->id()));
    QJsonObject reply = sendAndWaitForReply(QStringLiteral("functions.show.track.add"), showParams(extra));
    QCOMPARE(reply.value(QStringLiteral("error")).toObject().value(QStringLiteral("code")).toString(), QStringLiteral("INVALID_PARAMS"));

    extra.insert(QStringLiteral("sceneId"), QStringLiteral("99999"));
    reply = sendAndWaitForReply(QStringLiteral("functions.show.track.add"), showParams(extra));
    QCOMPARE(reply.value(QStringLiteral("error")).toObject().value(QStringLiteral("code")).toString(), QStringLiteral("NOT_FOUND"));
    QCOMPARE(m_show->tracks().count(), 1);
}

void ApiShowDomain_Test::trackSetMuteAndSolo()
{
    QSignalSpy spy(m_client, &QWebSocket::textMessageReceived);
    QJsonObject reply = mutate(QStringLiteral("functions.show.track.add"), QJsonObject());
    QString secondId = reply.value(QStringLiteral("result")).toObject().value(QStringLiteral("trackId")).toString();
    Track *second = m_show->track(secondId.toUInt());
    QVERIFY(second != nullptr);

    QJsonObject extra;
    extra.insert(QStringLiteral("trackId"), QString::number(m_track->id()));
    extra.insert(QStringLiteral("mute"), true);
    reply = mutate(QStringLiteral("functions.show.track.setMute"), extra);
    QVERIFY(reply.value(QStringLiteral("ok")).toBool());
    QCOMPARE(m_track->isMute(), true);
    QJsonObject ev = waitForEvent(spy, QStringLiteral("functions.show.track.muteChanged"), [](const QJsonObject &d) { return d.value(QStringLiteral("mute")).toBool(); });
    QVERIFY(ev.isEmpty() == false);

    // solo the second track: it is unmuted, the first stays/becomes muted
    extra = QJsonObject();
    extra.insert(QStringLiteral("trackId"), secondId);
    extra.insert(QStringLiteral("solo"), true);
    reply = mutate(QStringLiteral("functions.show.track.setSolo"), extra);
    QVERIFY(reply.value(QStringLiteral("ok")).toBool());
    QCOMPARE(m_track->isMute(), true);
    QCOMPARE(second->isMute(), false);
    ev = waitForEvent(spy, QStringLiteral("functions.show.tracksChanged"), [](const QJsonObject &) { return true; });
    QVERIFY(ev.isEmpty() == false);
    QJsonArray patch = ev.value(QStringLiteral("data")).toObject().value(QStringLiteral("patch")).toArray();
    QCOMPARE(patch.count(), 1);
    QCOMPARE(patch.at(0).toObject().value(QStringLiteral("op")).toString(), QStringLiteral("replace"));
    QCOMPARE(patch.at(0).toObject().value(QStringLiteral("path")).toString(), QStringLiteral("/tracks"));
    QCOMPARE(patch.at(0).toObject().value(QStringLiteral("value")).toArray().count(), 2);

    // solo off unmutes every track (ShowManager::setTrackSolo semantics)
    extra.insert(QStringLiteral("solo"), false);
    reply = mutate(QStringLiteral("functions.show.track.setSolo"), extra);
    QVERIFY(reply.value(QStringLiteral("ok")).toBool());
    QCOMPARE(m_track->isMute(), false);
    QCOMPARE(second->isMute(), false);
}

void ApiShowDomain_Test::trackMoveSwapsIdsAndBroadcastsTracksChanged()
{
    QSignalSpy spy(m_client, &QWebSocket::textMessageReceived);
    QJsonObject extra;
    extra.insert(QStringLiteral("name"), QStringLiteral("Second"));
    QJsonObject reply = mutate(QStringLiteral("functions.show.track.add"), extra);
    QString secondId = reply.value(QStringLiteral("result")).toObject().value(QStringLiteral("trackId")).toString();
    QString firstId = QString::number(m_track->id());

    // the first track cannot go up, the last cannot go down
    extra = QJsonObject();
    extra.insert(QStringLiteral("trackId"), firstId);
    extra.insert(QStringLiteral("direction"), QStringLiteral("up"));
    reply = sendAndWaitForReply(QStringLiteral("functions.show.track.move"), showParams(extra));
    QCOMPARE(reply.value(QStringLiteral("error")).toObject().value(QStringLiteral("code")).toString(), QStringLiteral("INVALID_PARAMS"));
    extra.insert(QStringLiteral("trackId"), secondId);
    extra.insert(QStringLiteral("direction"), QStringLiteral("down"));
    reply = sendAndWaitForReply(QStringLiteral("functions.show.track.move"), showParams(extra));
    QCOMPARE(reply.value(QStringLiteral("error")).toObject().value(QStringLiteral("code")).toString(), QStringLiteral("INVALID_PARAMS"));

    // move "Second" up: Show::moveTrack swaps the two ids, so the moved
    // Track object now answers to the first id
    extra.insert(QStringLiteral("direction"), QStringLiteral("up"));
    reply = mutate(QStringLiteral("functions.show.track.move"), extra);
    QVERIFY(reply.value(QStringLiteral("ok")).toBool());
    QCOMPARE(reply.value(QStringLiteral("result")).toObject().value(QStringLiteral("trackId")).toString(), firstId);
    QJsonArray tracks = getDetail().value(QStringLiteral("tracks")).toArray();
    QCOMPARE(tracks.count(), 2);
    QCOMPARE(tracks.at(0).toObject().value(QStringLiteral("name")).toString(), QStringLiteral("Second"));
    QCOMPARE(tracks.at(1).toObject().value(QStringLiteral("name")).toString(), QStringLiteral("Track 1"));
    QCOMPARE(tracks.at(1).toObject().value(QStringLiteral("items")).toArray().count(), 1); // the item followed its track

    QJsonObject ev = waitForEvent(spy, QStringLiteral("functions.show.tracksChanged"), [](const QJsonObject &d) { return d.contains(QStringLiteral("trackId")); });
    QVERIFY(ev.isEmpty() == false);
    QCOMPARE(ev.value(QStringLiteral("data")).toObject().value(QStringLiteral("trackId")).toString(), firstId);
    QJsonArray value = ev.value(QStringLiteral("data")).toObject().value(QStringLiteral("patch")).toArray().at(0).toObject().value(QStringLiteral("value")).toArray();
    QCOMPARE(value.at(0).toObject().value(QStringLiteral("name")).toString(), QStringLiteral("Second"));
}

/*****************************************************************************
 * items
 *****************************************************************************/

void ApiShowDomain_Test::itemAddUsesDefaultsAndBroadcasts()
{
    QSignalSpy spy(m_client, &QWebSocket::textMessageReceived);
    int before = revision();

    // a Scene has totalDuration 0 -> the Time-mode default of 5000 ms
    QJsonObject extra;
    extra.insert(QStringLiteral("trackId"), QString::number(m_track->id()));
    extra.insert(QStringLiteral("functionId"), QString::number(m_scene2->id()));
    extra.insert(QStringLiteral("startTime"), 7000);
    QJsonObject reply = mutate(QStringLiteral("functions.show.item.add"), extra);
    QVERIFY(reply.value(QStringLiteral("ok")).toBool());
    QVERIFY(reply.value(QStringLiteral("result")).toObject().value(QStringLiteral("docRevision")).toInt() > before);
    QString itemId = reply.value(QStringLiteral("result")).toObject().value(QStringLiteral("itemId")).toString();
    ShowFunction *sf = m_show->showFunction(itemId.toUInt());
    QVERIFY(sf != nullptr);
    QCOMPARE(sf->functionID(), m_scene2->id());
    QCOMPARE(sf->startTime(), quint32(7000));
    QCOMPARE(sf->duration(), quint32(5000));
    QCOMPARE(sf->color(), ShowFunction::defaultColor(Function::SceneType));
    QCOMPARE(sf->isLocked(), false);
    QCOMPARE(m_track->showFunctions().count(), 2);

    QJsonObject ev = waitForEvent(spy, QStringLiteral("functions.show.item.added"), [](const QJsonObject &) { return true; });
    QVERIFY(ev.isEmpty() == false);
    QJsonObject data = ev.value(QStringLiteral("data")).toObject();
    QCOMPARE(data.value(QStringLiteral("trackId")).toString(), QString::number(m_track->id()));
    QCOMPARE(data.value(QStringLiteral("item")).toObject().value(QStringLiteral("id")).toString(), itemId);
    QCOMPARE(data.value(QStringLiteral("item")).toObject().value(QStringLiteral("functionName")).toString(), QStringLiteral("Scene B"));
    QCOMPARE(data.value(QStringLiteral("item")).toObject().value(QStringLiteral("startTime")).toInt(), 7000);

    // the Chaser has its own length (3 x 1000 ms); explicit colour honoured
    extra.insert(QStringLiteral("functionId"), QString::number(m_chaser->id()));
    extra.insert(QStringLiteral("startTime"), 20000);
    extra.insert(QStringLiteral("color"), QStringLiteral("#ff8800"));
    reply = mutate(QStringLiteral("functions.show.item.add"), extra);
    QVERIFY(reply.value(QStringLiteral("ok")).toBool());
    sf = m_show->showFunction(reply.value(QStringLiteral("result")).toObject().value(QStringLiteral("itemId")).toString().toUInt());
    QVERIFY(sf != nullptr);
    QCOMPARE(sf->duration(), quint32(3000));
    QCOMPARE(sf->color().name(), QStringLiteral("#ff8800"));

    QCOMPARE(getDetail().value(QStringLiteral("totalDuration")).toInt(), 23000);
}

void ApiShowDomain_Test::itemAddRejectsOverlapWithSuggestion()
{
    // the existing item covers 0..5000: 3000 with the 5000 default overlaps
    QJsonObject extra;
    extra.insert(QStringLiteral("trackId"), QString::number(m_track->id()));
    extra.insert(QStringLiteral("functionId"), QString::number(m_scene2->id()));
    extra.insert(QStringLiteral("startTime"), 3000);
    QJsonObject reply = sendAndWaitForReply(QStringLiteral("functions.show.item.add"), showParams(extra));
    QCOMPARE(reply.value(QStringLiteral("ok")).toBool(), false);
    QJsonObject error = reply.value(QStringLiteral("error")).toObject();
    QCOMPARE(error.value(QStringLiteral("code")).toString(), QStringLiteral("INVALID_PARAMS"));
    QCOMPARE(error.value(QStringLiteral("details")).toObject().value(QStringLiteral("blockingItemId")).toString(), QString::number(m_itemId));
    // nearest free spot: right after the blocker (5000, shift 2000) beats
    // before it (would need -2000 -> clamped, no room)
    QCOMPARE(error.value(QStringLiteral("details")).toObject().value(QStringLiteral("suggestedStartTime")).toInt(), 5000);
    QCOMPARE(m_track->showFunctions().count(), 1);

    // touching edges are legal (half-open intervals)
    extra.insert(QStringLiteral("startTime"), 5000);
    reply = mutate(QStringLiteral("functions.show.item.add"), extra);
    QVERIFY(reply.value(QStringLiteral("ok")).toBool());
    QCOMPARE(m_track->showFunctions().count(), 2);
}

void ApiShowDomain_Test::itemAddRejectsTheShowItself()
{
    QJsonObject extra;
    extra.insert(QStringLiteral("trackId"), QString::number(m_track->id()));
    extra.insert(QStringLiteral("functionId"), QString::number(m_show->id()));
    extra.insert(QStringLiteral("startTime"), 10000);
    QJsonObject reply = sendAndWaitForReply(QStringLiteral("functions.show.item.add"), showParams(extra));
    QCOMPARE(reply.value(QStringLiteral("error")).toObject().value(QStringLiteral("code")).toString(), QStringLiteral("INVALID_PARAMS"));

    extra.insert(QStringLiteral("functionId"), QStringLiteral("424242"));
    reply = sendAndWaitForReply(QStringLiteral("functions.show.item.add"), showParams(extra));
    QCOMPARE(reply.value(QStringLiteral("error")).toObject().value(QStringLiteral("code")).toString(), QStringLiteral("NOT_FOUND"));

    extra.insert(QStringLiteral("functionId"), QString::number(m_scene2->id()));
    extra.remove(QStringLiteral("startTime"));
    reply = sendAndWaitForReply(QStringLiteral("functions.show.item.add"), showParams(extra));
    QCOMPARE(reply.value(QStringLiteral("error")).toObject().value(QStringLiteral("code")).toString(), QStringLiteral("INVALID_PARAMS"));
    QCOMPARE(m_track->showFunctions().count(), 1);
}

void ApiShowDomain_Test::itemMoveSameAndOtherTrack()
{
    QSignalSpy spy(m_client, &QWebSocket::textMessageReceived);
    QJsonObject reply = mutate(QStringLiteral("functions.show.track.add"), QJsonObject());
    QString secondId = reply.value(QStringLiteral("result")).toObject().value(QStringLiteral("trackId")).toString();
    Track *second = m_show->track(secondId.toUInt());

    // same track, later
    QJsonObject extra;
    extra.insert(QStringLiteral("itemId"), QString::number(m_itemId));
    extra.insert(QStringLiteral("trackId"), QString::number(m_track->id()));
    extra.insert(QStringLiteral("startTime"), 2500);
    reply = mutate(QStringLiteral("functions.show.item.move"), extra);
    QVERIFY(reply.value(QStringLiteral("ok")).toBool());
    ShowFunction *sf = m_show->showFunction(m_itemId);
    QCOMPARE(sf->startTime(), quint32(2500));
    QJsonObject ev = waitForEvent(spy, QStringLiteral("functions.show.item.moved"), [](const QJsonObject &d) { return d.value(QStringLiteral("startTime")).toInt() == 2500; });
    QVERIFY(ev.isEmpty() == false);
    QCOMPARE(ev.value(QStringLiteral("data")).toObject().value(QStringLiteral("trackId")).toString(), QString::number(m_track->id()));

    // other track, negative time clamps to 0
    extra.insert(QStringLiteral("trackId"), secondId);
    extra.insert(QStringLiteral("startTime"), -400);
    reply = mutate(QStringLiteral("functions.show.item.move"), extra);
    QVERIFY(reply.value(QStringLiteral("ok")).toBool());
    QCOMPARE(sf->startTime(), quint32(0));
    QCOMPARE(m_track->showFunctions().count(), 0);
    QCOMPARE(second->showFunctions().count(), 1);
    QCOMPARE(m_show->getTrackFromShowFunctionID(m_itemId), second);

    QString trackId;
    QJsonObject item = itemById(getDetail(), QString::number(m_itemId), &trackId);
    QCOMPARE(trackId, secondId);
    QCOMPARE(item.value(QStringLiteral("startTime")).toInt(), 0);
}

void ApiShowDomain_Test::itemMoveRejectsOverlapAndLocked()
{
    // a second item at 8000..13000 on the same track
    QJsonObject extra;
    extra.insert(QStringLiteral("trackId"), QString::number(m_track->id()));
    extra.insert(QStringLiteral("functionId"), QString::number(m_scene2->id()));
    extra.insert(QStringLiteral("startTime"), 8000);
    QJsonObject reply = mutate(QStringLiteral("functions.show.item.add"), extra);
    QString otherId = reply.value(QStringLiteral("result")).toObject().value(QStringLiteral("itemId")).toString();

    // moving the first (0..5000) to 6000 would cover 6000..11000: blocked,
    // the nearest free spot is 3000 (left of the blocker, shift 3000) rather
    // than 13000 (shift 7000)
    extra = QJsonObject();
    extra.insert(QStringLiteral("itemId"), QString::number(m_itemId));
    extra.insert(QStringLiteral("trackId"), QString::number(m_track->id()));
    extra.insert(QStringLiteral("startTime"), 6000);
    reply = sendAndWaitForReply(QStringLiteral("functions.show.item.move"), showParams(extra));
    QCOMPARE(reply.value(QStringLiteral("ok")).toBool(), false);
    QJsonObject error = reply.value(QStringLiteral("error")).toObject();
    QCOMPARE(error.value(QStringLiteral("code")).toString(), QStringLiteral("INVALID_PARAMS"));
    QCOMPARE(error.value(QStringLiteral("details")).toObject().value(QStringLiteral("blockingItemId")).toString(), otherId);
    QCOMPARE(error.value(QStringLiteral("details")).toObject().value(QStringLiteral("suggestedStartTime")).toInt(), 3000);
    QCOMPARE(m_show->showFunction(m_itemId)->startTime(), quint32(0)); // unchanged

    // a locked item does not move
    m_show->showFunction(m_itemId)->setLocked(true);
    extra.insert(QStringLiteral("startTime"), 1000);
    reply = sendAndWaitForReply(QStringLiteral("functions.show.item.move"), showParams(extra));
    QCOMPARE(reply.value(QStringLiteral("error")).toObject().value(QStringLiteral("code")).toString(), QStringLiteral("INVALID_PARAMS"));
    QCOMPARE(m_show->showFunction(m_itemId)->startTime(), quint32(0));

    // unknown item / track
    m_show->showFunction(m_itemId)->setLocked(false);
    extra.insert(QStringLiteral("itemId"), QStringLiteral("777"));
    reply = sendAndWaitForReply(QStringLiteral("functions.show.item.move"), showParams(extra));
    QCOMPARE(reply.value(QStringLiteral("error")).toObject().value(QStringLiteral("code")).toString(), QStringLiteral("NOT_FOUND"));
    extra.insert(QStringLiteral("itemId"), QString::number(m_itemId));
    extra.insert(QStringLiteral("trackId"), QStringLiteral("777"));
    reply = sendAndWaitForReply(QStringLiteral("functions.show.item.move"), showParams(extra));
    QCOMPARE(reply.value(QStringLiteral("error")).toObject().value(QStringLiteral("code")).toString(), QStringLiteral("NOT_FOUND"));
}

void ApiShowDomain_Test::itemResizeRejectsOverlapAndMinimum()
{
    QSignalSpy spy(m_client, &QWebSocket::textMessageReceived);
    QJsonObject extra;
    extra.insert(QStringLiteral("trackId"), QString::number(m_track->id()));
    extra.insert(QStringLiteral("functionId"), QString::number(m_scene2->id()));
    extra.insert(QStringLiteral("startTime"), 8000);
    QJsonObject reply = mutate(QStringLiteral("functions.show.item.add"), extra);
    QString otherId = reply.value(QStringLiteral("result")).toObject().value(QStringLiteral("itemId")).toString();

    // grow to exactly the next item's start: legal
    extra = QJsonObject();
    extra.insert(QStringLiteral("itemId"), QString::number(m_itemId));
    extra.insert(QStringLiteral("duration"), 8000);
    reply = mutate(QStringLiteral("functions.show.item.resize"), extra);
    QVERIFY(reply.value(QStringLiteral("ok")).toBool());
    QCOMPARE(m_show->showFunction(m_itemId)->duration(), quint32(8000));
    QJsonObject ev = waitForEvent(spy, QStringLiteral("functions.show.item.resized"), [](const QJsonObject &d) { return d.value(QStringLiteral("duration")).toInt() == 8000; });
    QVERIFY(ev.isEmpty() == false);

    // one more ms overlaps: the details say how far it may grow
    extra.insert(QStringLiteral("duration"), 8001);
    reply = sendAndWaitForReply(QStringLiteral("functions.show.item.resize"), showParams(extra));
    QCOMPARE(reply.value(QStringLiteral("ok")).toBool(), false);
    QJsonObject error = reply.value(QStringLiteral("error")).toObject();
    QCOMPARE(error.value(QStringLiteral("code")).toString(), QStringLiteral("INVALID_PARAMS"));
    QCOMPARE(error.value(QStringLiteral("details")).toObject().value(QStringLiteral("blockingItemId")).toString(), otherId);
    QCOMPARE(error.value(QStringLiteral("details")).toObject().value(QStringLiteral("maxDuration")).toInt(), 8000);
    QCOMPARE(m_show->showFunction(m_itemId)->duration(), quint32(8000));

    // below the minimum (Time: 1 ms)
    extra.insert(QStringLiteral("duration"), 0);
    reply = sendAndWaitForReply(QStringLiteral("functions.show.item.resize"), showParams(extra));
    QCOMPARE(reply.value(QStringLiteral("error")).toObject().value(QStringLiteral("code")).toString(), QStringLiteral("INVALID_PARAMS"));

    // Beats: 125 ms
    m_show->setTimeDivisionType(Show::BPM_4_4);
    extra.insert(QStringLiteral("duration"), 100);
    reply = sendAndWaitForReply(QStringLiteral("functions.show.item.resize"), showParams(extra));
    QCOMPARE(reply.value(QStringLiteral("error")).toObject().value(QStringLiteral("code")).toString(), QStringLiteral("INVALID_PARAMS"));
    extra.insert(QStringLiteral("duration"), 125);
    reply = mutate(QStringLiteral("functions.show.item.resize"), extra);
    QVERIFY(reply.value(QStringLiteral("ok")).toBool());
    QCOMPARE(m_show->showFunction(m_itemId)->duration(), quint32(125));
}

void ApiShowDomain_Test::itemSetColorSetLockedRemove()
{
    QSignalSpy spy(m_client, &QWebSocket::textMessageReceived);
    ShowFunction *sf = m_show->showFunction(m_itemId);

    QJsonObject extra;
    extra.insert(QStringLiteral("itemId"), QString::number(m_itemId));
    extra.insert(QStringLiteral("color"), QStringLiteral("#123456"));
    QJsonObject reply = mutate(QStringLiteral("functions.show.item.setColor"), extra);
    QVERIFY(reply.value(QStringLiteral("ok")).toBool());
    QCOMPARE(sf->color().name(), QStringLiteral("#123456"));
    QJsonObject ev = waitForEvent(spy, QStringLiteral("functions.show.item.colorChanged"), [](const QJsonObject &d) { return d.value(QStringLiteral("color")).toString() == QStringLiteral("#123456"); });
    QVERIFY(ev.isEmpty() == false);

    extra.insert(QStringLiteral("color"), QStringLiteral("not-a-colour"));
    reply = sendAndWaitForReply(QStringLiteral("functions.show.item.setColor"), showParams(extra));
    QCOMPARE(reply.value(QStringLiteral("error")).toObject().value(QStringLiteral("code")).toString(), QStringLiteral("INVALID_PARAMS"));

    extra = QJsonObject();
    extra.insert(QStringLiteral("itemId"), QString::number(m_itemId));
    extra.insert(QStringLiteral("locked"), true);
    reply = mutate(QStringLiteral("functions.show.item.setLocked"), extra);
    QVERIFY(reply.value(QStringLiteral("ok")).toBool());
    QCOMPARE(sf->isLocked(), true);
    ev = waitForEvent(spy, QStringLiteral("functions.show.item.lockedChanged"), [](const QJsonObject &d) { return d.value(QStringLiteral("locked")).toBool(); });
    QVERIFY(ev.isEmpty() == false);
    QCOMPARE(itemById(getDetail(), QString::number(m_itemId)).value(QStringLiteral("locked")).toBool(), true);

    // a locked item cannot be resized either
    extra = QJsonObject();
    extra.insert(QStringLiteral("itemId"), QString::number(m_itemId));
    extra.insert(QStringLiteral("duration"), 4000);
    reply = sendAndWaitForReply(QStringLiteral("functions.show.item.resize"), showParams(extra));
    QCOMPARE(reply.value(QStringLiteral("error")).toObject().value(QStringLiteral("code")).toString(), QStringLiteral("INVALID_PARAMS"));

    // remove is all-or-nothing
    QJsonArray ids;
    ids.append(QString::number(m_itemId));
    ids.append(QStringLiteral("999"));
    extra = QJsonObject();
    extra.insert(QStringLiteral("itemIds"), ids);
    reply = sendAndWaitForReply(QStringLiteral("functions.show.item.remove"), showParams(extra));
    QCOMPARE(reply.value(QStringLiteral("error")).toObject().value(QStringLiteral("code")).toString(), QStringLiteral("NOT_FOUND"));
    QCOMPARE(m_track->showFunctions().count(), 1);

    ids = QJsonArray();
    ids.append(QString::number(m_itemId));
    extra.insert(QStringLiteral("itemIds"), ids);
    reply = mutate(QStringLiteral("functions.show.item.remove"), extra);
    QVERIFY(reply.value(QStringLiteral("ok")).toBool());
    QCOMPARE(m_track->showFunctions().count(), 0);
    QVERIFY(m_show->showFunction(m_itemId) == nullptr);
    ev = waitForEvent(spy, QStringLiteral("functions.show.item.removed"), [this](const QJsonObject &d) { return d.value(QStringLiteral("itemIds")).toArray().contains(QString::number(m_itemId)); });
    QVERIFY(ev.isEmpty() == false);
    QCOMPARE(getDetail().value(QStringLiteral("totalDuration")).toInt(), 0);
}

/*****************************************************************************
 * ripple edits
 *****************************************************************************/

void ApiShowDomain_Test::rippleInsertShiftsAndExtends()
{
    QSignalSpy spy(m_client, &QWebSocket::textMessageReceived);
    // track 1: Scene A 0..5000, Chaser 6000..9000 ; track 2: Scene B 7000..12000
    QJsonObject extra;
    extra.insert(QStringLiteral("trackId"), QString::number(m_track->id()));
    extra.insert(QStringLiteral("functionId"), QString::number(m_chaser->id()));
    extra.insert(QStringLiteral("startTime"), 6000);
    QString chaserItemId = mutate(QStringLiteral("functions.show.item.add"), extra).value(QStringLiteral("result")).toObject().value(QStringLiteral("itemId")).toString();
    QString secondId = mutate(QStringLiteral("functions.show.track.add"), QJsonObject()).value(QStringLiteral("result")).toObject().value(QStringLiteral("trackId")).toString();
    extra.insert(QStringLiteral("trackId"), secondId);
    extra.insert(QStringLiteral("functionId"), QString::number(m_scene2->id()));
    extra.insert(QStringLiteral("startTime"), 7000);
    QString sceneBItemId = mutate(QStringLiteral("functions.show.item.add"), extra).value(QStringLiteral("result")).toObject().value(QStringLiteral("itemId")).toString();

    // insert 2000 ms at 2000: Scene A (covering) grows to 7000, everything
    // starting after the cursor shifts right by 2000
    int before = revision();
    extra = QJsonObject();
    extra.insert(QStringLiteral("cursorTime"), 2000);
    extra.insert(QStringLiteral("length"), 2000);
    QJsonObject reply = mutate(QStringLiteral("functions.show.rippleInsertTime"), extra);
    QVERIFY(reply.value(QStringLiteral("ok")).toBool());
    QCOMPARE(reply.value(QStringLiteral("result")).toObject().value(QStringLiteral("changed")).toBool(), true);
    QVERIFY(reply.value(QStringLiteral("result")).toObject().value(QStringLiteral("docRevision")).toInt() > before);

    QCOMPARE(m_show->showFunction(m_itemId)->startTime(), quint32(0));
    QCOMPARE(m_show->showFunction(m_itemId)->duration(), quint32(7000));
    QCOMPARE(m_show->showFunction(chaserItemId.toUInt())->startTime(), quint32(8000));
    QCOMPARE(m_show->showFunction(chaserItemId.toUInt())->duration(), quint32(3000));
    QCOMPARE(m_show->showFunction(sceneBItemId.toUInt())->startTime(), quint32(9000));
    QCOMPARE(m_show->showFunction(sceneBItemId.toUInt())->duration(), quint32(5000));

    QJsonObject ev = waitForEvent(spy, QStringLiteral("functions.show.itemsChanged"), [](const QJsonObject &) { return true; });
    QVERIFY(ev.isEmpty() == false);
    QJsonArray patch = ev.value(QStringLiteral("data")).toObject().value(QStringLiteral("patch")).toArray();
    // Scene A duration, Chaser startTime, Scene B startTime
    QCOMPARE(patch.count(), 3);
    QStringList paths;
    for (const QJsonValue &op : patch)
    {
        QCOMPARE(op.toObject().value(QStringLiteral("op")).toString(), QStringLiteral("replace"));
        paths << op.toObject().value(QStringLiteral("path")).toString();
    }
    QVERIFY(paths.contains(QStringLiteral("/tracks/0/items/0/duration")));
    QVERIFY(paths.contains(QStringLiteral("/tracks/0/items/1/startTime")));
    QVERIFY(paths.contains(QStringLiteral("/tracks/1/items/0/startTime")));

    // insert 1000 at 9500, inside the Chaser (8000..11000): the Chaser item
    // grows to 4000 and, like the Qt editor, the step under the cursor gets
    // the time (Common -> PerStep first); Scene B (9000..14000) grows too
    extra.insert(QStringLiteral("cursorTime"), 9500);
    extra.insert(QStringLiteral("length"), 1000);
    reply = mutate(QStringLiteral("functions.show.rippleInsertTime"), extra);
    QVERIFY(reply.value(QStringLiteral("ok")).toBool());
    QCOMPARE(m_show->showFunction(chaserItemId.toUInt())->duration(), quint32(4000));
    QCOMPARE(m_chaser->durationMode(), Chaser::PerStep);
    QCOMPARE(m_chaser->totalDuration(), quint32(4000));
    QCOMPARE(m_chaser->stepAt(1)->duration, quint32(2000)); // 1500 ms into the chaser = step 1
    QCOMPARE(m_show->showFunction(sceneBItemId.toUInt())->duration(), quint32(6000));
}

void ApiShowDomain_Test::rippleCutShrinksAndPulls()
{
    // track 1: Scene A 0..5000, Scene B 8000..13000
    QJsonObject extra;
    extra.insert(QStringLiteral("trackId"), QString::number(m_track->id()));
    extra.insert(QStringLiteral("functionId"), QString::number(m_scene2->id()));
    extra.insert(QStringLiteral("startTime"), 8000);
    QString sceneBItemId = mutate(QStringLiteral("functions.show.item.add"), extra).value(QStringLiteral("result")).toObject().value(QStringLiteral("itemId")).toString();

    // cut 1500 at 1000: Scene A shrinks to 3500, Scene B pulls left to 6500
    extra = QJsonObject();
    extra.insert(QStringLiteral("cursorTime"), 1000);
    extra.insert(QStringLiteral("length"), 1500);
    QJsonObject reply = mutate(QStringLiteral("functions.show.rippleCutTime"), extra);
    QVERIFY(reply.value(QStringLiteral("ok")).toBool());
    QCOMPARE(reply.value(QStringLiteral("result")).toObject().value(QStringLiteral("changed")).toBool(), true);
    QCOMPARE(m_show->showFunction(m_itemId)->duration(), quint32(3500));
    QCOMPARE(m_show->showFunction(sceneBItemId.toUInt())->startTime(), quint32(6500));
    QCOMPARE(getDetail().value(QStringLiteral("totalDuration")).toInt(), 11500);

    // a cut longer than the item leaves the minimum duration (1 ms in Time mode)
    extra.insert(QStringLiteral("length"), 10000);
    reply = mutate(QStringLiteral("functions.show.rippleCutTime"), extra);
    QVERIFY(reply.value(QStringLiteral("ok")).toBool());
    QCOMPARE(m_show->showFunction(m_itemId)->duration(), quint32(1));
}

void ApiShowDomain_Test::rippleWithNothingAtCursorIsNoOp()
{
    QSignalSpy spy(m_client, &QWebSocket::textMessageReceived);
    int before = revision();
    QJsonObject extra;
    extra.insert(QStringLiteral("cursorTime"), 60000);
    extra.insert(QStringLiteral("length"), 1000);
    QJsonObject reply = mutate(QStringLiteral("functions.show.rippleInsertTime"), extra);
    QVERIFY(reply.value(QStringLiteral("ok")).toBool());
    QCOMPARE(reply.value(QStringLiteral("result")).toObject().value(QStringLiteral("changed")).toBool(), false);
    QCOMPARE(reply.value(QStringLiteral("result")).toObject().value(QStringLiteral("docRevision")).toInt(), before);
    reply = mutate(QStringLiteral("functions.show.rippleCutTime"), extra);
    QVERIFY(reply.value(QStringLiteral("ok")).toBool());
    QCOMPARE(reply.value(QStringLiteral("result")).toObject().value(QStringLiteral("changed")).toBool(), false);
    QCOMPARE(revision(), before);
    QCOMPARE(m_show->showFunction(m_itemId)->duration(), quint32(5000));

    // no itemsChanged went out
    QTest::qWait(100);
    QJsonObject ev = waitForEvent(spy, QStringLiteral("functions.show.itemsChanged"), [](const QJsonObject &) { return true; }, 200);
    QVERIFY(ev.isEmpty());

    // a length below the minimum is refused
    extra.insert(QStringLiteral("length"), 0);
    reply = sendAndWaitForReply(QStringLiteral("functions.show.rippleInsertTime"), showParams(extra));
    QCOMPARE(reply.value(QStringLiteral("error")).toObject().value(QStringLiteral("code")).toString(), QStringLiteral("INVALID_PARAMS"));
}

void ApiShowDomain_Test::rippleSkipsLockedItems()
{
    m_show->showFunction(m_itemId)->setLocked(true);
    QJsonObject extra;
    extra.insert(QStringLiteral("cursorTime"), 2000);
    extra.insert(QStringLiteral("length"), 1000);
    QJsonObject reply = mutate(QStringLiteral("functions.show.rippleInsertTime"), extra);
    QVERIFY(reply.value(QStringLiteral("ok")).toBool());
    QCOMPARE(reply.value(QStringLiteral("result")).toObject().value(QStringLiteral("changed")).toBool(), false);
    QCOMPARE(m_show->showFunction(m_itemId)->duration(), quint32(5000));
    reply = mutate(QStringLiteral("functions.show.rippleCutTime"), extra);
    QCOMPARE(reply.value(QStringLiteral("result")).toObject().value(QStringLiteral("changed")).toBool(), false);
    QCOMPARE(m_show->showFunction(m_itemId)->duration(), quint32(5000));
}

/*****************************************************************************
 * revision
 *****************************************************************************/

void ApiShowDomain_Test::staleRevisionIsConflict()
{
    QJsonObject params;
    params.insert(QStringLiteral("showId"), QString::number(m_show->id()));
    params.insert(QStringLiteral("trackId"), QString::number(m_track->id()));
    params.insert(QStringLiteral("mute"), true);
    params.insert(QStringLiteral("baseRevision"), revision() + 5);
    QJsonObject reply = sendAndWaitForReply(QStringLiteral("functions.show.track.setMute"), params);
    QCOMPARE(reply.value(QStringLiteral("ok")).toBool(), false);
    QCOMPARE(reply.value(QStringLiteral("error")).toObject().value(QStringLiteral("code")).toString(), QStringLiteral("CONFLICT"));
    QCOMPARE(reply.value(QStringLiteral("error")).toObject().value(QStringLiteral("details")).toObject().value(QStringLiteral("docRevision")).toInt(), revision());
    QCOMPARE(m_track->isMute(), false);

    // a non-Show function is refused
    params.insert(QStringLiteral("showId"), QString::number(m_scene->id()));
    params.insert(QStringLiteral("baseRevision"), revision());
    reply = sendAndWaitForReply(QStringLiteral("functions.show.track.setMute"), params);
    QCOMPARE(reply.value(QStringLiteral("error")).toObject().value(QStringLiteral("code")).toString(), QStringLiteral("INVALID_PARAMS"));
}

/*****************************************************************************
 * playhead
 *****************************************************************************/

void ApiShowDomain_Test::playheadEventIsGatedAndFollowsStartOffset()
{
    const QString topic = QStringLiteral("functions.show.%1.playhead").arg(m_show->id());
    QSignalSpy spy(m_client, &QWebSocket::textMessageReceived);

    // functions.start with the new startTime offset: the Show plays from 1000 ms
    QJsonObject params;
    params.insert(QStringLiteral("functionId"), QString::number(m_show->id()));
    params.insert(QStringLiteral("startTime"), 1000);
    QJsonObject reply = sendAndWaitForReply(QStringLiteral("functions.start"), params);
    QVERIFY(reply.value(QStringLiteral("ok")).toBool());
    QVERIFY(QTest::qWaitFor([this]() { return m_show->isRunning(); }, 2000));

    // not subscribed: nothing arrives although the runner ticks
    QTest::qWait(300);
    QJsonObject ev = waitForEvent(spy, topic, [](const QJsonObject &) { return true; }, 50);
    QVERIFY2(ev.isEmpty(), "playhead events must be subscribe-gated");

    QJsonArray topics;
    topics.append(topic);
    QJsonObject sub;
    sub.insert(QStringLiteral("topics"), topics);
    reply = sendAndWaitForReply(QStringLiteral("subscribe"), sub);
    QVERIFY(reply.value(QStringLiteral("ok")).toBool());

    // collect for half a second: ticks arrive, throttled, increasing, from
    // beyond the start offset
    QTest::qWait(550);
    QList<int> times;
    for (const QList<QVariant> &frame : spy)
    {
        QJsonObject obj = QJsonDocument::fromJson(frame.at(0).toString().toUtf8()).object();
        if (obj.value(QStringLiteral("type")).toString() != QStringLiteral("event") ||
            obj.value(QStringLiteral("topic")).toString() != topic)
            continue;
        QJsonObject data = obj.value(QStringLiteral("data")).toObject();
        QCOMPARE(data.value(QStringLiteral("functionId")).toString(), QString::number(m_show->id()));
        QVERIFY(obj.value(QStringLiteral("originClientId")).isNull());
        times << data.value(QStringLiteral("time")).toInt();
    }
    QVERIFY2(times.count() >= 3, qPrintable(QStringLiteral("expected several playhead events, got %1").arg(times.count())));
    QVERIFY2(times.count() <= 8, qPrintable(QStringLiteral("playhead events must be throttled to ~10/s, got %1 in 550 ms").arg(times.count())));
    for (int i = 1; i < times.count(); i++)
        QVERIFY(times.at(i) > times.at(i - 1));
    // the Show started at 1000 ms and had run ~300 ms before the subscription
    QVERIFY2(times.first() >= 1000, qPrintable(QStringLiteral("first playhead %1 should be past the 1000 ms start offset").arg(times.first())));
    QVERIFY(times.last() < 5000);

    params.remove(QStringLiteral("startTime"));
    reply = sendAndWaitForReply(QStringLiteral("functions.stop"), params);
    QVERIFY(reply.value(QStringLiteral("ok")).toBool());
    QVERIFY(QTest::qWaitFor([this]() { return m_show->isRunning() == false; }, 2000));
}

QTEST_GUILESS_MAIN(ApiShowDomain_Test)
