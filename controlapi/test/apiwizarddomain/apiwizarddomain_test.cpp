/*
  Q Light Controller Plus - Control API unit test
  apiwizarddomain_test.cpp

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

#include "apiwizarddomain_test.h"
#include "apiserver.h"

#include "fixturegroup.h"
#include "fixture.h"
#include "scene.h"
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

/*********************************************************************
 * FakeWizardHost
 *********************************************************************/

QJsonObject FakeWizardHost::wizardProjectOptions()
{
    QJsonObject g;
    g.insert(QStringLiteral("groupId"), 7.0);
    g.insert(QStringLiteral("name"), QStringLiteral("Front"));
    g.insert(QStringLiteral("fixtureIds"), QJsonArray({ 0.0, 1.0 }));
    g.insert(QStringLiteral("suggestedRole"), 2);   // Back
    g.insert(QStringLiteral("hasMovement"), true);
    g.insert(QStringLiteral("hasGobo"), true);

    QJsonObject ctrl;
    ctrl.insert(QStringLiteral("universe"), 3);
    ctrl.insert(QStringLiteral("profile"), QStringLiteral("APC mini"));

    QJsonObject env;
    env.insert(QStringLiteral("width"), 12);
    env.insert(QStringLiteral("height"), 6);
    env.insert(QStringLiteral("depth"), 8);

    QJsonObject result;
    result.insert(QStringLiteral("groups"), QJsonArray({ g }));
    result.insert(QStringLiteral("controllers"), QJsonArray({ ctrl }));
    result.insert(QStringLiteral("envSize"), env);
    return result;
}

QJsonObject FakeWizardHost::wizardPreview(const ApiWizardChoices &choices, QString *error)
{
    Q_UNUSED(error)
    lastChoices = choices;

    QJsonObject g;
    g.insert(QStringLiteral("groupId"), QJsonValue());
    g.insert(QStringLiteral("name"), choices.groups.first().name);
    QJsonArray ids;
    for (quint32 id : choices.groups.first().fixtureIds)
        ids.append(double(id));
    g.insert(QStringLiteral("fixtureIds"), ids);
    g.insert(QStringLiteral("role"), 6);   // Blinder
    g.insert(QStringLiteral("hasShutter"), true);

    QJsonObject eff;
    eff.insert(QStringLiteral("flag"), 1 << 12);   // StrobeChase
    eff.insert(QStringLiteral("name"), QStringLiteral("Strobe Chase"));
    eff.insert(QStringLiteral("family"), QStringLiteral("Intensity"));
    eff.insert(QStringLiteral("enabled"), true);
    eff.insert(QStringLiteral("available"), true);
    eff.insert(QStringLiteral("preview"), QStringLiteral("1 chaser(s)"));

    QJsonObject summary;
    summary.insert(QStringLiteral("section"), QStringLiteral("Functions"));
    summary.insert(QStringLiteral("detail"), QStringLiteral("1 effect(s) selected"));

    QJsonObject result;
    result.insert(QStringLiteral("showType"), choices.showType);
    result.insert(QStringLiteral("groups"), QJsonArray({ g }));
    result.insert(QStringLiteral("placesFixtures"), true);
    result.insert(QStringLiteral("stageType"), 2);   // Rock
    result.insert(QStringLiteral("envSize"), QJsonObject({ { QStringLiteral("width"), 9 } }));
    result.insert(QStringLiteral("effects"), QJsonArray({ eff }));
    result.insert(QStringLiteral("controller"), QJsonObject({ { QStringLiteral("universe"), -1 } }));
    result.insert(QStringLiteral("summary"), QJsonArray({ summary }));
    return result;
}

bool FakeWizardHost::wizardGenerate(const ApiWizardChoices &choices, QString *error)
{
    lastChoices = choices;
    generateCalls++;
    if (failGenerate)
    {
        *error = QStringLiteral("fake failure");
        return false;
    }

    FixtureGroup *grp = new FixtureGroup(m_doc);
    grp->setName(choices.groups.first().name);
    for (quint32 id : choices.groups.first().fixtureIds)
        grp->assignFixture(id);
    m_doc->addFixtureGroup(grp);

    Scene *scene = new Scene(m_doc);
    scene->setName(QStringLiteral("Generated"));
    m_doc->addFunction(scene);
    return true;
}

/*********************************************************************
 * Test
 *********************************************************************/

void ApiWizardDomain_Test::init()
{
    m_doc = new Doc(nullptr);
    m_fixtures.clear();
    for (int i = 0; i < 3; i++)
    {
        Fixture *fixture = new Fixture(m_doc);
        fixture->setName(QStringLiteral("Dimmer %1").arg(i));
        fixture->setUniverse(0);
        fixture->setAddress(i * 4);
        fixture->setChannels(1);
        QVERIFY(m_doc->addFixture(fixture));
        m_fixtures.append(fixture->id());
    }

    FixtureGroup *grp = new FixtureGroup(m_doc);
    grp->setName(QStringLiteral("Existing"));
    grp->assignFixture(m_fixtures.at(0));
    QVERIFY(m_doc->addFixtureGroup(grp));
    m_group = grp->id();

    FixtureGroup *empty = new FixtureGroup(m_doc);
    empty->setName(QStringLiteral("Empty"));
    QVERIFY(m_doc->addFixtureGroup(empty));

    m_host = new FakeWizardHost(m_doc);
    m_apiServer = new ApiServer(m_host, m_doc);
    connectClient();
}

void ApiWizardDomain_Test::connectClient()
{
    QVERIFY(m_apiServer->listen(0, QHostAddress::LocalHost));
    m_client = new QWebSocket();
    m_client->open(QUrl(QStringLiteral("ws://127.0.0.1:%1/qlcplusapi").arg(m_apiServer->serverPort())));
    QVERIFY(QTest::qWaitFor([this]() { return m_client->state() == QAbstractSocket::ConnectedState; }, 2000));
    QVERIFY(sendAndWaitForReply(QStringLiteral("hello"), QJsonObject()).value(QStringLiteral("ok")).toBool());
}

void ApiWizardDomain_Test::cleanup()
{
    delete m_client;
    m_client = nullptr;
    delete m_host;   // owns m_apiServer
    m_host = nullptr;
    m_apiServer = nullptr;
    delete m_doc;
    m_doc = nullptr;
}

QJsonObject ApiWizardDomain_Test::sendAndWaitForReply(const QString &method, const QJsonObject &params)
{
    static int seq = 0;
    const QString requestId = QStringLiteral("w-%1").arg(++seq);
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

QJsonObject ApiWizardDomain_Test::waitForEvent(QSignalSpy &spy, const QString &topic, int timeoutMs)
{
    QJsonObject found;
    (void)QTest::qWaitFor([&]()
    {
        for (const QList<QVariant> &frame : spy)
        {
            QJsonObject obj = QJsonDocument::fromJson(frame.at(0).toString().toUtf8()).object();
            if (obj.value(QStringLiteral("type")).toString() == QStringLiteral("event") &&
                obj.value(QStringLiteral("topic")).toString() == topic)
            {
                found = obj.value(QStringLiteral("data")).toObject();
                return true;
            }
        }
        return false;
    }, timeoutMs);
    return found;
}

QString ApiWizardDomain_Test::errorCode(const QJsonObject &reply) const
{
    return reply.value(QStringLiteral("error")).toObject().value(QStringLiteral("code")).toString();
}

void ApiWizardDomain_Test::unsupportedWithoutHost()
{
    delete m_client;
    m_client = nullptr;
    delete m_host;
    m_host = nullptr;

    m_apiServer = new ApiServer(nullptr, m_doc);
    connectClient();

    QCOMPARE(errorCode(sendAndWaitForReply(QStringLiteral("core.wizard.getOptions"), QJsonObject())),
             QStringLiteral("UNSUPPORTED"));
    QCOMPARE(errorCode(sendAndWaitForReply(QStringLiteral("core.wizard.generate"), QJsonObject())),
             QStringLiteral("UNSUPPORTED"));

    delete m_client;
    m_client = nullptr;
    delete m_apiServer;
    m_apiServer = nullptr;
}

void ApiWizardDomain_Test::getOptionsReturnsCataloguesAndProject()
{
    const QJsonObject reply = sendAndWaitForReply(QStringLiteral("core.wizard.getOptions"), QJsonObject());
    QVERIFY2(reply.value(QStringLiteral("ok")).toBool(), qPrintable(QJsonDocument(reply).toJson()));
    const QJsonObject r = reply.value(QStringLiteral("result")).toObject();

    const QJsonArray showTypes = r.value(QStringLiteral("showTypes")).toArray();
    QCOMPARE(showTypes.count(), 5);
    QCOMPARE(showTypes.at(1).toObject().value(QStringLiteral("id")).toString(), QStringLiteral("Concert"));
    QCOMPARE(showTypes.at(1).toObject().value(QStringLiteral("stageType")).toString(), QStringLiteral("Rock"));
    QCOMPARE(showTypes.at(0).toObject().value(QStringLiteral("tags")).toArray().count(), 4);

    const QJsonArray roles = r.value(QStringLiteral("roles")).toArray();
    QCOMPARE(roles.count(), 9);
    QCOMPARE(roles.at(6).toObject().value(QStringLiteral("id")).toString(), QStringLiteral("Blinder"));
    QVERIFY(roles.at(0).toObject().value(QStringLiteral("placement")).toString().isEmpty() == false);

    QCOMPARE(r.value(QStringLiteral("stageTypes")).toArray().count(), 4);
    const QJsonArray effects = r.value(QStringLiteral("effects")).toArray();
    QCOMPARE(effects.count(), 20);
    QCOMPARE(effects.last().toObject().value(QStringLiteral("id")).toString(), QStringLiteral("AmbientLoop"));
    QCOMPARE(effects.last().toObject().value(QStringLiteral("family")).toString(), QStringLiteral("Show Cues"));

    // Host data, with ids as strings and roles as names
    const QJsonObject group = r.value(QStringLiteral("groups")).toArray().at(0).toObject();
    QCOMPARE(group.value(QStringLiteral("groupId")).toString(), QStringLiteral("7"));
    QCOMPARE(group.value(QStringLiteral("suggestedRole")).toString(), QStringLiteral("Back"));
    QCOMPARE(group.value(QStringLiteral("fixtureIds")).toArray().at(1).toString(), QStringLiteral("1"));
    QCOMPARE(group.value(QStringLiteral("hasGobo")).toBool(), true);
    QCOMPARE(group.value(QStringLiteral("hasRGB")).toBool(), false);
    QCOMPARE(r.value(QStringLiteral("controllers")).toArray().count(), 1);
    QCOMPARE(r.value(QStringLiteral("envSize")).toObject().value(QStringLiteral("width")).toInt(), 12);
}

void ApiWizardDomain_Test::previewConvertsChoicesBothWays()
{
    QJsonObject newGroup;
    newGroup.insert(QStringLiteral("name"), QStringLiteral("Strobes"));
    newGroup.insert(QStringLiteral("fixtureIds"), QJsonArray({ QString::number(m_fixtures.at(1)), QString::number(m_fixtures.at(2)) }));
    newGroup.insert(QStringLiteral("role"), QStringLiteral("Blinder"));
    QJsonObject existing;
    existing.insert(QStringLiteral("groupId"), QString::number(m_group));

    QJsonObject choices;
    choices.insert(QStringLiteral("showType"), QStringLiteral("Theatrical"));
    choices.insert(QStringLiteral("groups"), QJsonArray({ newGroup, existing }));
    choices.insert(QStringLiteral("stageType"), QStringLiteral("Theatre"));
    choices.insert(QStringLiteral("envSize"), QJsonObject({ { QStringLiteral("width"), 10 }, { QStringLiteral("height"), 5 }, { QStringLiteral("depth"), 7 } }));
    choices.insert(QStringLiteral("effects"), QJsonArray({ QStringLiteral("StrobeChase"), QStringLiteral("AmbientLoop") }));
    choices.insert(QStringLiteral("controller"), QJsonObject({ { QStringLiteral("universe"), 3 }, { QStringLiteral("feedback"), false } }));

    const QJsonObject reply = sendAndWaitForReply(QStringLiteral("core.wizard.preview"), QJsonObject({ { QStringLiteral("choices"), choices } }));
    QVERIFY2(reply.value(QStringLiteral("ok")).toBool(), qPrintable(QJsonDocument(reply).toJson()));

    // What the host received, in engine values
    const ApiWizardChoices &c = m_host->lastChoices;
    QCOMPARE(c.showType, 2);
    QCOMPARE(c.groups.count(), 2);
    QCOMPARE(c.groups.at(0).groupId, quint32(UINT_MAX));
    QCOMPARE(c.groups.at(0).name, QStringLiteral("Strobes"));
    QCOMPARE(c.groups.at(0).fixtureIds, QList<quint32>({ m_fixtures.at(1), m_fixtures.at(2) }));
    QCOMPARE(c.groups.at(0).role, 6);
    QCOMPARE(c.groups.at(1).groupId, m_group);
    QCOMPARE(c.groups.at(1).name, QStringLiteral("Existing"));
    QCOMPARE(c.groups.at(1).hasFixtureIds, false);
    QCOMPARE(c.groups.at(1).role, -1);
    QCOMPARE(c.stageType, 3);
    QVERIFY(c.hasEnvSize);
    QCOMPARE(c.envDepth, 7.0);
    QVERIFY(c.hasEffects);
    QCOMPARE(c.effectFlags, QList<int>({ 1 << 12, 1 << 22 }));
    QVERIFY(c.hasController);
    QCOMPARE(c.ctrlUniverse, 3);
    QCOMPARE(c.ctrlFeedback, false);
    QCOMPARE(c.ctrlMap, true);

    // What the client got back, in wire names
    const QJsonObject r = reply.value(QStringLiteral("result")).toObject();
    QCOMPARE(r.value(QStringLiteral("showType")).toString(), QStringLiteral("Theatrical"));
    QCOMPARE(r.value(QStringLiteral("stageType")).toString(), QStringLiteral("Rock"));
    const QJsonObject g = r.value(QStringLiteral("groups")).toArray().at(0).toObject();
    QVERIFY(g.value(QStringLiteral("groupId")).isNull());
    QCOMPARE(g.value(QStringLiteral("role")).toString(), QStringLiteral("Blinder"));
    QCOMPARE(g.value(QStringLiteral("fixtureIds")).toArray().at(0).toString(), QString::number(m_fixtures.at(1)));
    QCOMPARE(r.value(QStringLiteral("effects")).toArray().at(0).toObject().value(QStringLiteral("id")).toString(), QStringLiteral("StrobeChase"));
    QCOMPARE(r.value(QStringLiteral("summary")).toArray().count(), 1);
    QVERIFY(r.value(QStringLiteral("placesFixtures")).toBool());

    // Read-only
    QCOMPARE(m_doc->fixtureGroups().count(), 2);
}

void ApiWizardDomain_Test::previewRejectsInvalidChoices()
{
    auto preview = [this](const QJsonObject &choices) {
        return errorCode(sendAndWaitForReply(QStringLiteral("core.wizard.preview"), QJsonObject({ { QStringLiteral("choices"), choices } })));
    };
    const QJsonObject okGroup({ { QStringLiteral("name"), QStringLiteral("G") },
                                { QStringLiteral("fixtureIds"), QJsonArray({ QString::number(m_fixtures.at(0)) }) } });

    QCOMPARE(preview(QJsonObject({ { QStringLiteral("showType"), QStringLiteral("Rave") }, { QStringLiteral("groups"), QJsonArray({ okGroup }) } })),
             QStringLiteral("INVALID_PARAMS"));
    QCOMPARE(preview(QJsonObject({ { QStringLiteral("showType"), QStringLiteral("Custom") }, { QStringLiteral("groups"), QJsonArray() } })),
             QStringLiteral("INVALID_PARAMS"));
    // unknown fixture
    QCOMPARE(preview(QJsonObject({ { QStringLiteral("showType"), QStringLiteral("Custom") },
                                   { QStringLiteral("groups"), QJsonArray({ QJsonObject({ { QStringLiteral("name"), QStringLiteral("G") },
                                                                                          { QStringLiteral("fixtureIds"), QJsonArray({ QStringLiteral("999") }) } }) }) } })),
             QStringLiteral("INVALID_PARAMS"));
    // unknown group
    QCOMPARE(preview(QJsonObject({ { QStringLiteral("showType"), QStringLiteral("Custom") },
                                   { QStringLiteral("groups"), QJsonArray({ QJsonObject({ { QStringLiteral("groupId"), QStringLiteral("999") } }) }) } })),
             QStringLiteral("INVALID_PARAMS"));
    // only an empty existing group: nothing to generate for
    QCOMPARE(preview(QJsonObject({ { QStringLiteral("showType"), QStringLiteral("Custom") },
                                   { QStringLiteral("groups"), QJsonArray({ QJsonObject({ { QStringLiteral("groupId"), QString::number(m_group + 1) } }) }) } })),
             QStringLiteral("INVALID_PARAMS"));
    // a new group needs a name
    QCOMPARE(preview(QJsonObject({ { QStringLiteral("showType"), QStringLiteral("Custom") },
                                   { QStringLiteral("groups"), QJsonArray({ QJsonObject({ { QStringLiteral("fixtureIds"), QJsonArray({ QString::number(m_fixtures.at(0)) }) } }) }) } })),
             QStringLiteral("INVALID_PARAMS"));
    // unknown role / effect / stage
    QJsonObject badRole = okGroup;
    badRole.insert(QStringLiteral("role"), QStringLiteral("Laser"));
    QCOMPARE(preview(QJsonObject({ { QStringLiteral("showType"), QStringLiteral("Custom") }, { QStringLiteral("groups"), QJsonArray({ badRole }) } })),
             QStringLiteral("INVALID_PARAMS"));
    QCOMPARE(preview(QJsonObject({ { QStringLiteral("showType"), QStringLiteral("Custom") }, { QStringLiteral("groups"), QJsonArray({ okGroup }) },
                                   { QStringLiteral("effects"), QJsonArray({ QStringLiteral("Lasers") }) } })),
             QStringLiteral("INVALID_PARAMS"));
    QCOMPARE(preview(QJsonObject({ { QStringLiteral("showType"), QStringLiteral("Custom") }, { QStringLiteral("groups"), QJsonArray({ okGroup }) },
                                   { QStringLiteral("stageType"), QStringLiteral("Arena") } })),
             QStringLiteral("INVALID_PARAMS"));
}

void ApiWizardDomain_Test::generateChecksRevisionAndBroadcasts()
{
    QJsonObject group({ { QStringLiteral("name"), QStringLiteral("New Wash") },
                        { QStringLiteral("fixtureIds"), QJsonArray({ QString::number(m_fixtures.at(1)) }) } });
    QJsonObject choices({ { QStringLiteral("showType"), QStringLiteral("ClubNight") }, { QStringLiteral("groups"), QJsonArray({ group }) } });

    QJsonObject params({ { QStringLiteral("choices"), choices }, { QStringLiteral("baseRevision"), int(m_doc->docRevision()) + 3 } });
    QCOMPARE(errorCode(sendAndWaitForReply(QStringLiteral("core.wizard.generate"), params)), QStringLiteral("CONFLICT"));
    QCOMPARE(m_host->generateCalls, 0);

    QSignalSpy spy(m_client, &QWebSocket::textMessageReceived);
    params.insert(QStringLiteral("baseRevision"), int(m_doc->docRevision()));
    const QJsonObject reply = sendAndWaitForReply(QStringLiteral("core.wizard.generate"), params);
    QVERIFY2(reply.value(QStringLiteral("ok")).toBool(), qPrintable(QJsonDocument(reply).toJson()));
    QCOMPARE(m_host->generateCalls, 1);
    QCOMPARE(m_host->lastChoices.showType, 0);
    QVERIFY(m_host->lastChoices.hasEffects == false);
    QVERIFY(m_host->lastChoices.hasController == false);

    const QJsonObject r = reply.value(QStringLiteral("result")).toObject();
    QCOMPARE(r.value(QStringLiteral("docRevision")).toInt(), int(m_doc->docRevision()));
    QCOMPARE(r.value(QStringLiteral("fixtureGroupIds")).toArray().count(), 1);
    QCOMPARE(r.value(QStringLiteral("functionIds")).toArray().count(), 1);

    const QJsonObject groupEvent = waitForEvent(spy, QStringLiteral("fixtures.group.created"));
    QCOMPARE(groupEvent.value(QStringLiteral("group")).toObject().value(QStringLiteral("name")).toString(), QStringLiteral("New Wash"));
    const QJsonObject fnEvent = waitForEvent(spy, QStringLiteral("functions.created"));
    QCOMPARE(fnEvent.value(QStringLiteral("function")).toObject().value(QStringLiteral("name")).toString(), QStringLiteral("Generated"));
    // a new group is placed on stage: the monitor is announced too
    QVERIFY(waitForEvent(spy, QStringLiteral("fixtures.monitor.changed")).isEmpty() == false);
    const QJsonObject done = waitForEvent(spy, QStringLiteral("core.wizard.generated"));
    QCOMPARE(done.value(QStringLiteral("functionIds")).toArray(), r.value(QStringLiteral("functionIds")).toArray());
}

void ApiWizardDomain_Test::generateReportsHostFailure()
{
    m_host->failGenerate = true;
    QJsonObject group({ { QStringLiteral("groupId"), QString::number(m_group) } });
    QJsonObject params({ { QStringLiteral("choices"), QJsonObject({ { QStringLiteral("showType"), QStringLiteral("Custom") },
                                                                    { QStringLiteral("groups"), QJsonArray({ group }) } }) },
                         { QStringLiteral("baseRevision"), int(m_doc->docRevision()) } });
    const QJsonObject reply = sendAndWaitForReply(QStringLiteral("core.wizard.generate"), params);
    QCOMPARE(errorCode(reply), QStringLiteral("INVALID_PARAMS"));
    QCOMPARE(reply.value(QStringLiteral("error")).toObject().value(QStringLiteral("message")).toString(), QStringLiteral("fake failure"));
}

QTEST_GUILESS_MAIN(ApiWizardDomain_Test)
