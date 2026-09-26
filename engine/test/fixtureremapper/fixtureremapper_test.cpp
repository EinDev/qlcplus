/*
  Q Light Controller Plus - Unit test
  fixtureremapper_test.cpp

  Copyright (c) Massimo Callegari

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

#include <QtTest>

#define protected public
#define private public
#include "mastertimer_stub.h"
#include "fixtureremapper.h"
#include "qlcfixturedefcache.h"
#include "monitorproperties.h"
#include "channelmodifier.h"
#include "qlcfixturemode.h"
#include "qlcfixturedef.h"
#include "channelsgroup.h"
#include "fixturegroup.h"
#include "collection.h"
#include "efxfixture.h"
#include "qlcchannel.h"
#include "scenevalue.h"
#include "chaserstep.h"
#include "grouphead.h"
#include "sequence.h"
#include "fixture.h"
#include "scene.h"
#include "efx.h"
#include "doc.h"
#undef private
#undef protected

#include "fixtureremapper_test.h"

QTEST_MAIN(FixtureRemapper_Test)

/* -------------------------------------------------------------------------
 * Helpers
 * ------------------------------------------------------------------------- */

static QLCFixtureDef *makeDef(const QString &name)
{
    QLCFixtureDef *def = new QLCFixtureDef();
    def->setManufacturer("Test");
    def->setModel(name);
    return def;
}

static QLCChannel *makeChannel(const QString &name,
                               QLCChannel::Group group,
                               QLCChannel::ControlByte cb = QLCChannel::MSB,
                               QLCChannel::PrimaryColour colour = QLCChannel::NoColour)
{
    QLCChannel *ch = new QLCChannel();
    ch->setName(name);
    ch->setGroup(group);
    ch->setControlByte(cb);
    ch->setColour(colour);
    return ch;
}

Fixture *FixtureRemapper_Test::buildMovingHead(Doc *doc, quint32 address,
                                               const QString &defName,
                                               const QString &modeName)
{
    QLCFixtureDef *def = makeDef(defName);
    def->addChannel(makeChannel("Dimmer", QLCChannel::Intensity));
    def->addChannel(makeChannel("Pan",    QLCChannel::Pan));
    def->addChannel(makeChannel("Tilt",   QLCChannel::Tilt));

    QLCFixtureMode *mode = new QLCFixtureMode(def);
    mode->setName(modeName);
    mode->insertChannel(def->channel("Dimmer"), 0);
    mode->insertChannel(def->channel("Pan"),    1);
    mode->insertChannel(def->channel("Tilt"),   2);
    def->addMode(mode);

    Fixture *fxi = new Fixture(doc);
    fxi->setAddress(address);
    fxi->setUniverse(0);
    fxi->setFixtureDefinition(def, mode);
    doc->addFixture(fxi);
    return fxi;
}

Fixture *FixtureRemapper_Test::buildDimmer(Doc *doc, quint32 address, quint32 channels)
{
    Fixture *fxi = new Fixture(doc);
    fxi->setAddress(address);
    fxi->setUniverse(0);
    QLCFixtureDef *def = fxi->genericDimmerDef(channels);
    QLCFixtureMode *mode = fxi->genericDimmerMode(def, channels);
    fxi->setFixtureDefinition(def, mode);
    doc->addFixture(fxi);
    return fxi;
}

Fixture *FixtureRemapper_Test::buildRGB(Doc *doc, quint32 address, const QString &defName)
{
    QLCFixtureDef *def = makeDef(defName);
    def->addChannel(makeChannel("Red",   QLCChannel::Intensity, QLCChannel::MSB, QLCChannel::Red));
    def->addChannel(makeChannel("Green", QLCChannel::Intensity, QLCChannel::MSB, QLCChannel::Green));
    def->addChannel(makeChannel("Blue",  QLCChannel::Intensity, QLCChannel::MSB, QLCChannel::Blue));

    QLCFixtureMode *mode = new QLCFixtureMode(def);
    mode->setName("RGB");
    mode->insertChannel(def->channel("Red"),   0);
    mode->insertChannel(def->channel("Green"), 1);
    mode->insertChannel(def->channel("Blue"),  2);
    def->addMode(mode);

    Fixture *fxi = new Fixture(doc);
    fxi->setAddress(address);
    fxi->setUniverse(0);
    fxi->setFixtureDefinition(def, mode);
    doc->addFixture(fxi);
    return fxi;
}

/* -------------------------------------------------------------------------
 * Test lifecycle
 * ------------------------------------------------------------------------- */

void FixtureRemapper_Test::initTestCase()
{
    m_doc = new Doc(this);
}

void FixtureRemapper_Test::cleanupTestCase()
{
    delete m_doc;
}

void FixtureRemapper_Test::init()
{
}

void FixtureRemapper_Test::cleanup()
{
    m_doc->clearContents();
}

/* -------------------------------------------------------------------------
 * Tests
 * ------------------------------------------------------------------------- */

void FixtureRemapper_Test::testAddChannelRemap()
{
    FixtureRemapper remapper;
    QVERIFY(remapper.sourceList().isEmpty());
    QVERIFY(remapper.targetList().isEmpty());

    remapper.addChannelRemap(1, 0, 2, 3);
    remapper.addChannelRemap(1, 1, 2, 4);

    QCOMPARE(remapper.sourceList().count(), 2);
    QCOMPARE(remapper.targetList().count(), 2);

    QCOMPARE(remapper.sourceList().at(0).fxi,     quint32(1));
    QCOMPARE(remapper.sourceList().at(0).channel, quint32(0));
    QCOMPARE(remapper.targetList().at(0).fxi,     quint32(2));
    QCOMPARE(remapper.targetList().at(0).channel, quint32(3));

    QCOMPARE(remapper.sourceList().at(1).fxi,     quint32(1));
    QCOMPARE(remapper.sourceList().at(1).channel, quint32(1));
    QCOMPARE(remapper.targetList().at(1).fxi,     quint32(2));
    QCOMPARE(remapper.targetList().at(1).channel, quint32(4));
}

void FixtureRemapper_Test::testReset()
{
    FixtureRemapper remapper;
    remapper.addChannelRemap(1, 0, 2, 0);
    remapper.addChannelRemap(1, 1, 2, 1);
    QVERIFY(!remapper.sourceList().isEmpty());

    remapper.reset();
    QVERIFY(remapper.sourceList().isEmpty());
    QVERIFY(remapper.targetList().isEmpty());
}

void FixtureRemapper_Test::testRemapSceneValues()
{
    QList<SceneValue> srcList;
    srcList << SceneValue(1, 0) << SceneValue(1, 1) << SceneValue(1, 2);

    QList<SceneValue> tgtList;
    tgtList << SceneValue(2, 5) << SceneValue(2, 6) << SceneValue(2, 7);

    QList<SceneValue> funcList;
    funcList << SceneValue(1, 0, 200)   // maps to (2,5,200)
             << SceneValue(1, 2, 100)   // maps to (2,7,100)
             << SceneValue(1, 99, 50);  // no match → dropped

    QList<SceneValue> result = FixtureRemapper::remapSceneValues(funcList, srcList, tgtList);

    QCOMPARE(result.count(), 2);
    // result is sorted by (fxi, channel)
    QCOMPARE(result.at(0).fxi,     quint32(2));
    QCOMPARE(result.at(0).channel, quint32(5));
    QCOMPARE(result.at(0).value,   uchar(200));
    QCOMPARE(result.at(1).fxi,     quint32(2));
    QCOMPARE(result.at(1).channel, quint32(7));
    QCOMPARE(result.at(1).value,   uchar(100));
}

void FixtureRemapper_Test::testAutoConnectOneToOne()
{
    // Two fixtures with the same def name and mode name → 1:1 mapping
    Doc srcDoc(this);
    Doc tgtDoc(this);

    Fixture *src = buildMovingHead(&srcDoc, 0,  "MH1", "Mode1");
    Fixture *tgt = buildMovingHead(&tgtDoc, 10, "MH1", "Mode1");

    // src has id 0, tgt has id 0 in its own doc; for our remapper test
    // the IDs are what matter in the result lists
    FixtureRemapper remapper;
    QList<QPair<quint32, quint32>> pairs = remapper.autoConnectFixtures(src, tgt);

    // 3 channels: Dimmer→Dimmer, Pan→Pan, Tilt→Tilt
    QCOMPARE(pairs.count(), 3);
    QCOMPARE(pairs.at(0).first,  quint32(0));
    QCOMPARE(pairs.at(0).second, quint32(0));
    QCOMPARE(pairs.at(1).first,  quint32(1));
    QCOMPARE(pairs.at(1).second, quint32(1));
    QCOMPARE(pairs.at(2).first,  quint32(2));
    QCOMPARE(pairs.at(2).second, quint32(2));

    QCOMPARE(remapper.sourceList().count(), 3);
    QCOMPARE(remapper.targetList().count(), 3);

    // All source entries belong to src fixture
    for (const SceneValue &sv : remapper.sourceList())
        QCOMPARE(sv.fxi, src->id());
    for (const SceneValue &sv : remapper.targetList())
        QCOMPARE(sv.fxi, tgt->id());
}

void FixtureRemapper_Test::testAutoConnectSemantic()
{
    // Two RGB fixtures with different def names → semantic matching by group+colour
    Doc srcDoc(this);
    Doc tgtDoc(this);

    Fixture *src = buildRGB(&srcDoc, 0,  "LED-A");
    Fixture *tgt = buildRGB(&tgtDoc, 10, "LED-B");

    FixtureRemapper remapper;
    QList<QPair<quint32, quint32>> pairs = remapper.autoConnectFixtures(src, tgt);

    // Red→Red, Green→Green, Blue→Blue
    QCOMPARE(pairs.count(), 3);
    QCOMPARE(pairs.at(0).first,  quint32(0)); // src ch0 Red
    QCOMPARE(pairs.at(0).second, quint32(0)); // tgt ch0 Red
    QCOMPARE(pairs.at(1).first,  quint32(1)); // src ch1 Green
    QCOMPARE(pairs.at(1).second, quint32(1)); // tgt ch1 Green
    QCOMPARE(pairs.at(2).first,  quint32(2)); // src ch2 Blue
    QCOMPARE(pairs.at(2).second, quint32(2)); // tgt ch2 Blue
}

void FixtureRemapper_Test::testAutoConnectGenericDimmer()
{
    Doc srcDoc(this);
    Doc tgtDoc(this);

    Fixture *src = buildDimmer(&srcDoc, 0,  4);
    Fixture *tgt = buildDimmer(&tgtDoc, 10, 4);

    FixtureRemapper remapper;
    QList<QPair<quint32, quint32>> pairs = remapper.autoConnectFixtures(src, tgt);

    QCOMPARE(pairs.count(), 4);
    for (int i = 0; i < 4; i++)
    {
        QCOMPARE(pairs.at(i).first,  quint32(i));
        QCOMPARE(pairs.at(i).second, quint32(i));
    }
}

void FixtureRemapper_Test::testApplyRemapScene()
{
    // src fixture id=0, tgt fixture id=1 (assigned by addFixture)
    Fixture *src = buildMovingHead(m_doc, 0,  "MH", "M1");
    quint32 srcId = src->id();

    // Build the target fixture in a temporary doc, then applyRemap will
    // call replaceFixtures to swap them into m_doc
    Doc targetDoc(this);
    Fixture *tgt = buildMovingHead(&targetDoc, 20, "MH", "M1");
    quint32 tgtId = tgt->id();

    // Create a Scene that references src fixture channels
    Scene *s = new Scene(m_doc);
    s->addFixture(srcId);
    s->setValue(SceneValue(srcId, 0, 255)); // Dimmer full
    s->setValue(SceneValue(srcId, 1, 128)); // Pan mid
    m_doc->addFunction(s);

    // Build remapper: 3 channels 1:1 (same def+mode)
    FixtureRemapper remapper;
    remapper.autoConnectFixtures(src, tgt);

    remapper.applyRemap(m_doc, targetDoc.fixtures());

    // After applyRemap the scene values must point to tgtId
    QList<SceneValue> vals = s->values();
    QCOMPARE(vals.count(), 2);
    for (const SceneValue &sv : vals)
        QCOMPARE(sv.fxi, tgtId);

    // Values preserved
    bool foundDimmer = false, foundPan = false;
    for (const SceneValue &sv : vals)
    {
        if (sv.channel == 0) { QCOMPARE(sv.value, uchar(255)); foundDimmer = true; }
        if (sv.channel == 1) { QCOMPARE(sv.value, uchar(128)); foundPan = true;   }
    }
    QVERIFY(foundDimmer);
    QVERIFY(foundPan);
}

void FixtureRemapper_Test::testApplyRemapSequence()
{
    Fixture *src = buildMovingHead(m_doc, 0,  "MH", "M1");
    quint32 srcId = src->id();

    Doc targetDoc(this);
    Fixture *tgt = buildMovingHead(&targetDoc, 20, "MH", "M1");
    quint32 tgtId = tgt->id();

    // Sequence bound to a scene — add both to doc before adding steps
    Scene *boundScene = new Scene(m_doc);
    m_doc->addFunction(boundScene);

    Sequence *seq = new Sequence(m_doc);
    seq->setBoundSceneID(boundScene->id());
    m_doc->addFunction(seq);  // must be added before addStep so seq->id() is valid

    // Steps reference the bound scene function id, not the sequence id
    ChaserStep step(boundScene->id());
    step.values << SceneValue(srcId, 0, 200) << SceneValue(srcId, 2, 50);
    seq->addStep(step);

    FixtureRemapper remapper;
    remapper.autoConnectFixtures(src, tgt);
    remapper.applyRemap(m_doc, targetDoc.fixtures());

    ChaserStep *cs = seq->stepAt(0);
    QVERIFY(cs != nullptr);
    QCOMPARE(cs->values.count(), 2);
    for (const SceneValue &sv : cs->values)
        QCOMPARE(sv.fxi, tgtId);
}

void FixtureRemapper_Test::testApplyRemapFixtureGroup()
{
    Fixture *src = buildMovingHead(m_doc, 0, "MH", "M1");
    quint32 srcId = src->id();

    Doc targetDoc(this);
    Fixture *tgt = buildMovingHead(&targetDoc, 20, "MH", "M1");
    quint32 tgtId = tgt->id();

    FixtureGroup *group = new FixtureGroup(m_doc);
    group->setName("TestGroup");
    GroupHead head(srcId, 0);
    group->assignHead(QLCPoint(0, 0), head);
    m_doc->addFixtureGroup(group);

    FixtureRemapper remapper;
    remapper.autoConnectFixtures(src, tgt);
    remapper.applyRemap(m_doc, targetDoc.fixtures());

    // The group head at (0,0) should now reference tgtId
    GroupHead remappedHead = group->head(QLCPoint(0, 0));
    QCOMPARE(remappedHead.fxi, tgtId);
}

void FixtureRemapper_Test::testApplyRemapChannelsGroup()
{
    Fixture *src = buildMovingHead(m_doc, 0, "MH", "M1");
    quint32 srcId = src->id();

    Doc targetDoc(this);
    Fixture *tgt = buildMovingHead(&targetDoc, 20, "MH", "M1");
    quint32 tgtId = tgt->id();

    ChannelsGroup *grp = new ChannelsGroup(m_doc);
    grp->addChannel(srcId, 0);
    grp->addChannel(srcId, 1);
    m_doc->addChannelsGroup(grp);

    FixtureRemapper remapper;
    remapper.autoConnectFixtures(src, tgt);
    remapper.applyRemap(m_doc, targetDoc.fixtures());

    QList<SceneValue> channels = grp->getChannels();
    QCOMPARE(channels.count(), 2);
    for (const SceneValue &sv : channels)
        QCOMPARE(sv.fxi, tgtId);
}

void FixtureRemapper_Test::testApplyRemapMonitor()
{
    Fixture *src = buildMovingHead(m_doc, 0, "MH", "M1");
    quint32 srcId = src->id();

    Doc targetDoc(this);
    // Add a placeholder so tgt gets a different ID than src (src has ID 0)
    buildDimmer(&targetDoc, 0, 1);
    Fixture *tgt = buildMovingHead(&targetDoc, 20, "MH", "M1");
    quint32 tgtId = tgt->id();
    QVERIFY(tgtId != srcId);

    MonitorProperties *props = m_doc->monitorProperties();
    FixturePreviewItem item;
    item.m_baseItem.m_position = QVector3D(1.0f, 2.0f, 3.0f);
    props->setFixtureProperties(srcId, item);
    QVERIFY(props->fixtureItemsID().contains(srcId));

    FixtureRemapper remapper;
    remapper.autoConnectFixtures(src, tgt);
    remapper.applyRemap(m_doc, targetDoc.fixtures());

    // srcId entry removed, tgtId entry present with same position
    QVERIFY(!props->fixtureItemsID().contains(srcId));
    QVERIFY(props->fixtureItemsID().contains(tgtId));
    QCOMPARE(props->fixtureProperties(tgtId).m_baseItem.m_position,
             QVector3D(1.0f, 2.0f, 3.0f));
}

void FixtureRemapper_Test::testAutoConnectNullAndUntyped()
{
    FixtureRemapper remapper;
    Fixture *dimmer = buildDimmer(m_doc, 0, 2);

    QVERIFY(remapper.autoConnectFixtures(nullptr, dimmer).isEmpty());
    QVERIFY(remapper.autoConnectFixtures(dimmer, nullptr).isEmpty());
    QVERIFY(remapper.sourceList().isEmpty());
    QVERIFY(remapper.targetList().isEmpty());

    // Two fixtures without any definition: a bare channel count is all they
    // have, which still qualifies for a direct index mapping. setChannels()
    // would build a generic dimmer definition, so poke the count in directly.
    Fixture src(this);
    src.m_channels = 3;
    Fixture tgt(this);
    tgt.m_channels = 2;
    QVERIFY(src.fixtureDef() == nullptr && src.fixtureMode() == nullptr);

    src.setChannelCanFade(1, false);
    ChannelModifier mod;
    mod.setName("Modifier");
    src.setChannelModifier(0, &mod);

    QList<QPair<quint32, quint32>> pairs = remapper.autoConnectFixtures(&src, &tgt);

    // the third source channel has no counterpart on the target
    QCOMPARE(pairs.count(), 2);
    QCOMPARE(pairs.at(0), qMakePair(quint32(0), quint32(0)));
    QCOMPARE(pairs.at(1), qMakePair(quint32(1), quint32(1)));
    QVERIFY(tgt.channelCanFade(0) == true);
    QVERIFY(tgt.channelCanFade(1) == false);
    QVERIFY(tgt.channelModifier(0) == &mod);
    QVERIFY(tgt.channelModifier(1) == nullptr);
}

void FixtureRemapper_Test::testAutoConnectMixedTyped()
{
    Doc srcDoc(this);
    Fixture *typed = buildMovingHead(&srcDoc, 0, "MH", "M1");

    Fixture bare(this);
    bare.m_channels = 3;
    QVERIFY(bare.fixtureDef() == nullptr);

    // Without a definition on one side there is nothing to match against:
    // neither direction produces a single pair
    FixtureRemapper remapper;
    QVERIFY(remapper.autoConnectFixtures(typed, &bare).isEmpty());
    QVERIFY(remapper.autoConnectFixtures(&bare, typed).isEmpty());
    QVERIFY(remapper.sourceList().isEmpty());
}

void FixtureRemapper_Test::testAutoConnectOneToOneFlags()
{
    Doc srcDoc(this);
    Doc tgtDoc(this);

    Fixture *src = buildMovingHead(&srcDoc, 0,  "MH1", "Mode1");
    Fixture *tgt = buildMovingHead(&tgtDoc, 10, "MH1", "Mode1");

    src->setChannelCanFade(2, false);
    src->setForcedHTPChannels(QList<int>() << 1);
    src->setForcedLTPChannels(QList<int>() << 0);
    ChannelModifier mod;
    mod.setName("Modifier");
    src->setChannelModifier(1, &mod);

    FixtureRemapper remapper;
    QCOMPARE(remapper.autoConnectFixtures(src, tgt).count(), 3);

    QVERIFY(tgt->channelCanFade(0) == true);
    QVERIFY(tgt->channelCanFade(2) == false);
    QVERIFY(tgt->channelModifier(1) == &mod);
    QVERIFY(tgt->channelModifier(0) == nullptr);
    QCOMPARE(tgt->forcedHTPChannels(), QList<int>() << 1);
    QCOMPARE(tgt->forcedLTPChannels(), QList<int>() << 0);
}

void FixtureRemapper_Test::testAutoConnectSemanticMisses()
{
    Doc srcDoc(this);
    Doc tgtDoc(this);

    // Source: Dimmer, coarse Pan, fine Pan
    QLCFixtureDef *srcDef = makeDef("SRC");
    srcDef->addChannel(makeChannel("Dimmer",   QLCChannel::Intensity));
    srcDef->addChannel(makeChannel("Pan",      QLCChannel::Pan, QLCChannel::MSB));
    srcDef->addChannel(makeChannel("Pan fine", QLCChannel::Pan, QLCChannel::LSB));
    QLCFixtureMode *srcMode = new QLCFixtureMode(srcDef);
    srcMode->setName("3ch");
    srcMode->insertChannel(srcDef->channel("Dimmer"),   0);
    srcMode->insertChannel(srcDef->channel("Pan"),      1);
    srcMode->insertChannel(srcDef->channel("Pan fine"), 2);
    srcDef->addMode(srcMode);

    // Target: fine Pan, Tilt, coarse Pan, Dimmer - every source channel has
    // to skip at least one candidate with the wrong group or control byte
    QLCFixtureDef *tgtDef = makeDef("TGT");
    tgtDef->addChannel(makeChannel("Pan fine", QLCChannel::Pan, QLCChannel::LSB));
    tgtDef->addChannel(makeChannel("Tilt",     QLCChannel::Tilt, QLCChannel::MSB));
    tgtDef->addChannel(makeChannel("Pan",      QLCChannel::Pan, QLCChannel::MSB));
    tgtDef->addChannel(makeChannel("Dimmer",   QLCChannel::Intensity));
    QLCFixtureMode *tgtMode = new QLCFixtureMode(tgtDef);
    tgtMode->setName("4ch");
    tgtMode->insertChannel(tgtDef->channel("Pan fine"), 0);
    tgtMode->insertChannel(tgtDef->channel("Tilt"),     1);
    tgtMode->insertChannel(tgtDef->channel("Pan"),      2);
    tgtMode->insertChannel(tgtDef->channel("Dimmer"),   3);
    tgtDef->addMode(tgtMode);

    Fixture *src = new Fixture(&srcDoc);
    src->setAddress(0);
    src->setUniverse(0);
    src->setFixtureDefinition(srcDef, srcMode);
    srcDoc.addFixture(src);
    src->setChannelCanFade(0, false);

    Fixture *tgt = new Fixture(&tgtDoc);
    tgt->setAddress(10);
    tgt->setUniverse(0);
    tgt->setFixtureDefinition(tgtDef, tgtMode);
    tgtDoc.addFixture(tgt);

    FixtureRemapper remapper;
    QList<QPair<quint32, quint32>> pairs = remapper.autoConnectFixtures(src, tgt);

    QCOMPARE(pairs.count(), 3);
    QCOMPARE(pairs.at(0), qMakePair(quint32(0), quint32(3))); // Dimmer
    QCOMPARE(pairs.at(1), qMakePair(quint32(1), quint32(2))); // Pan
    QCOMPARE(pairs.at(2), qMakePair(quint32(2), quint32(0))); // Pan fine

    // the fade flag follows the semantic match, not the channel index
    QVERIFY(tgt->channelCanFade(3) == false);
    QVERIFY(tgt->channelCanFade(0) == true);
}

void FixtureRemapper_Test::testApplyRemapEFX()
{
    Fixture *src = buildMovingHead(m_doc, 0, "MH", "M1");
    quint32 srcId = src->id();

    Doc targetDoc(this);
    Fixture *tgtDimmer = buildDimmer(&targetDoc, 0, 1);
    Fixture *tgt = buildMovingHead(&targetDoc, 20, "MH", "M1");
    quint32 tgtId = tgt->id();
    QVERIFY(tgtId != tgtDimmer->id());

    // Doc::replaceFixtures() re-resolves every non-generic definition through
    // the doc's fixture cache: without this the remapped moving head would
    // end up with no definition, and therefore no pan/tilt channels
    QVERIFY(m_doc->fixtureDefCache()->addFixtureDef(
                const_cast<QLCFixtureDef*>(tgt->fixtureDef())) == true);

    EFX *efx = new EFX(m_doc);
    EFXFixture *ef = new EFXFixture(efx);
    ef->setHead(GroupHead(srcId, 0));
    QVERIFY(efx->addFixture(ef));
    m_doc->addFunction(efx);

    // a function type the remapper leaves untouched
    Collection *collection = new Collection(m_doc);
    m_doc->addFunction(collection);

    FixtureRemapper remapper;
    remapper.addChannelRemap(srcId + 55, 0, tgtId, 1);      // some other source fixture
    remapper.addChannelRemap(srcId, 0, tgtId + 99, 0);      // target missing from the doc
    remapper.addChannelRemap(srcId, 0, tgtId, 50);          // target channel out of range
    remapper.addChannelRemap(srcId, 0, tgtDimmer->id(), 0); // dimmer: no pan/tilt there
    remapper.addChannelRemap(srcId, 1, tgtId, 1);           // Pan: the head moves here
    remapper.addChannelRemap(srcId, 2, tgtId, 2);           // Tilt: same target, once only

    remapper.applyRemap(m_doc, targetDoc.fixtures());

    QCOMPARE(efx->fixtures().count(), 1);
    QCOMPARE(efx->fixtures().first()->head().fxi, tgtId);
    QCOMPARE(efx->fixtures().first()->head().head, 0);
    QVERIFY(m_doc->function(collection->id()) == collection);
}

void FixtureRemapper_Test::testApplyRemapGroupInvalidHead()
{
    Fixture *src = buildMovingHead(m_doc, 0, "MH", "M1");
    quint32 srcId = src->id();

    Doc targetDoc(this);
    Fixture *tgt = buildMovingHead(&targetDoc, 20, "MH", "M1");
    quint32 tgtId = tgt->id();

    FixtureGroup *group = new FixtureGroup(m_doc);
    group->assignHead(QLCPoint(0, 0), GroupHead(srcId, 0));
    // an invalid placeholder entry, as left behind by a removed fixture
    group->m_heads[QLCPoint(1, 1)] = GroupHead();
    m_doc->addFixtureGroup(group);

    FixtureRemapper remapper;
    remapper.autoConnectFixtures(src, tgt);
    remapper.applyRemap(m_doc, targetDoc.fixtures());

    QCOMPARE(group->head(QLCPoint(0, 0)).fxi, tgtId);
    QVERIFY(group->head(QLCPoint(1, 1)).isValid() == false);
}
