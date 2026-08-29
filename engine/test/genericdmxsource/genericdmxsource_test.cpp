/*
  Q Light Controller Plus - Unit test
  genericdmxsource_test.cpp

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
#include "genericdmxsource_test.h"
#include "genericdmxsource.h"
#include "genericfader.h"
#include "mastertimer.h"
#include "universe.h"
#include "doc.h"
#include "fixture.h"
#undef private
#undef protected

void GenericDMXSource_Test::initTestCase()
{
    m_doc = new Doc(this);
}

void GenericDMXSource_Test::cleanupTestCase()
{
    delete m_doc;
}

void GenericDMXSource_Test::init()
{
    m_doc->clearContents();
    Fixture *fxi = new Fixture(m_doc);
    fxi->setChannels(1);
    fxi->setAddress(0);
    m_doc->addFixture(fxi);
    m_fxiId = fxi->id();
}

void GenericDMXSource_Test::cleanup()
{
    m_doc->clearContents();
}

void GenericDMXSource_Test::setUnset()
{
    GenericDMXSource src(m_doc);
    src.setOutputEnabled(true);
    src.set(m_fxiId, 0, 100);
    QCOMPARE(src.channelsCount(), quint32(1));
    QList<SceneValue> list = src.channels();
    QCOMPARE(list.size(), 1);
    QCOMPARE(list.first().fxi, m_fxiId);
    QCOMPARE(list.first().channel, quint32(0));
    QCOMPARE(list.first().value, uchar(100));

    src.unset(m_fxiId, 0);
    QCOMPARE(src.channelsCount(), quint32(0));
}

void GenericDMXSource_Test::unsetAll()
{
    GenericDMXSource src(m_doc);
    src.setOutputEnabled(true);
    src.set(m_fxiId, 0, 50);
    src.writeDMX(nullptr, m_doc->inputOutputMap()->universes());
    QCOMPARE(src.channelsCount(), quint32(1));
    src.unsetAll();
    src.writeDMX(nullptr, m_doc->inputOutputMap()->universes());
    QCOMPARE(src.channelsCount(), quint32(0));
}

void GenericDMXSource_Test::writeDMXAppliesValueToUniverse()
{
    QList<Universe*> ua = m_doc->inputOutputMap()->universes();

    GenericDMXSource src(m_doc);
    src.setOutputEnabled(true);
    src.set(m_fxiId, 0, 200);

    src.writeDMX(nullptr, ua);

    QSharedPointer<GenericFader> fader = src.m_fadersMap.value(ua[0]->id());
    QVERIFY(!fader.isNull());
    fader->write(ua[0], MasterTimer::tick());

    QCOMPARE(uchar(ua[0]->preGMValues()[0]), uchar(200));
}

void GenericDMXSource_Test::writeDMXSkipsWhenOutputDisabled()
{
    QList<Universe*> ua = m_doc->inputOutputMap()->universes();

    GenericDMXSource src(m_doc);
    src.setOutputEnabled(false);
    src.set(m_fxiId, 0, 200);

    src.writeDMX(nullptr, ua);

    // Nothing should be forwarded to the universe/fader while output is
    // disabled - only the queued value itself is retained.
    QVERIFY(src.m_fadersMap.isEmpty());
    QCOMPARE(uchar(ua[0]->preGMValues()[0]), uchar(0));
    QCOMPARE(src.channelsCount(), quint32(1));
}

void GenericDMXSource_Test::writeDMXIgnoresUnknownFixture()
{
    QList<Universe*> ua = m_doc->inputOutputMap()->universes();

    GenericDMXSource src(m_doc);
    src.setOutputEnabled(true);
    src.set(m_fxiId + 999, 0, 200); // no such fixture exists in m_doc

    src.writeDMX(nullptr, ua); // must not crash on a dangling fixture ID

    QVERIFY(src.m_fadersMap.isEmpty());
    QCOMPARE(src.channelsCount(), quint32(1)); // value stays queued, just never applied
}

void GenericDMXSource_Test::writeDMXSkipsChannelsBeyondAvailableUniverses()
{
    GenericDMXSource src(m_doc);
    src.setOutputEnabled(true);
    src.set(m_fxiId, 0, 200);

    QList<Universe*> noUniverses; // simulate none of the target universes being available
    src.writeDMX(nullptr, noUniverses); // must not crash or index out of bounds

    QCOMPARE(src.channelsCount(), quint32(1)); // value stays queued, just never applied
}

void GenericDMXSource_Test::unsetRemovesFaderChannel()
{
    // Regression test: once a channel has been written at least once,
    // GenericDMXSource::unset() must also detach it from whichever Universe
    // GenericFader is holding it - otherwise GenericFader::write() (invoked
    // on every Universe tick, independently of GenericDMXSource) would keep
    // outputting the last value forever, even though this source's own
    // bookkeeping (m_values / channels() / channelsCount()) correctly shows
    // the channel as gone.
    GenericDMXSource src(m_doc);
    src.setOutputEnabled(true);
    src.set(m_fxiId, 0, 255);

    QList<Universe *> ua = m_doc->inputOutputMap()->universes();
    src.writeDMX(nullptr, ua);

    Universe *uni = ua.at(0);
    QSharedPointer<GenericFader> fader = src.m_fadersMap.value(uni->id(), QSharedPointer<GenericFader>());
    QVERIFY(!fader.isNull());
    QCOMPARE(fader->channelsCount(), 1);

    src.unset(m_fxiId, 0);
    QCOMPARE(src.channelsCount(), quint32(0));

    // this is the actual regression check: before the fix, the fader kept
    // the channel latched at value 255 even though GenericDMXSource itself
    // reports nothing set any more
    QCOMPARE(fader->channelsCount(), 0);
}

void GenericDMXSource_Test::setDefaultsToUnspecifiedFeature()
{
    // Backward compatibility: every pre-existing caller that doesn't pass a
    // Feature (like all of the tests above) must keep behaving exactly as
    // before, and be tagged with the Unspecified fallback.
    GenericDMXSource src(m_doc);
    src.setOutputEnabled(true);
    src.set(m_fxiId, 0, 100);

    QPair<quint32,quint32> key(m_fxiId, quint32(0));
    QVERIFY(src.m_features.contains(key));
    QCOMPARE(src.m_features.value(key), GenericDMXSource::Unspecified);
}

void GenericDMXSource_Test::setStoresSpecificFeature()
{
    GenericDMXSource src(m_doc);
    src.setOutputEnabled(true);
    src.set(m_fxiId, 0, 100, GenericDMXSource::DragPositionPush);

    QPair<quint32,quint32> key(m_fxiId, quint32(0));
    QCOMPARE(src.m_features.value(key), GenericDMXSource::DragPositionPush);

    // Overwriting the same channel with a different Feature must replace,
    // not merge, the tag - same convention as m_values.
    src.set(m_fxiId, 0, 50, GenericDMXSource::PalettePreview);
    QCOMPARE(src.m_features.value(key), GenericDMXSource::PalettePreview);
    QCOMPARE(src.channels().first().value, uchar(50));
}

void GenericDMXSource_Test::unsetClearsFeature()
{
    GenericDMXSource src(m_doc);
    src.setOutputEnabled(true);
    src.set(m_fxiId, 0, 100, GenericDMXSource::ColorTool);

    QPair<quint32,quint32> key(m_fxiId, quint32(0));
    QVERIFY(src.m_features.contains(key));

    src.unset(m_fxiId, 0);
    QVERIFY(src.m_features.contains(key) == false);
}

void GenericDMXSource_Test::unsetAllViaWriteDMXClearsFeature()
{
    QList<Universe*> ua = m_doc->inputOutputMap()->universes();

    GenericDMXSource src(m_doc);
    src.setOutputEnabled(true);
    src.set(m_fxiId, 0, 100, GenericDMXSource::BeamTool);
    src.writeDMX(nullptr, ua);

    QPair<quint32,quint32> key(m_fxiId, quint32(0));
    QVERIFY(src.m_features.contains(key));

    // unsetAll() only queues the clear request - it is processed on the
    // next writeDMX(), same as m_values (see GenericDMXSource_Test::unsetAll()).
    src.unsetAll();
    src.writeDMX(nullptr, ua);
    QVERIFY(src.m_features.isEmpty());
}

void GenericDMXSource_Test::findFeatureForFaderMatchesOwningInstance()
{
    QList<Universe*> ua = m_doc->inputOutputMap()->universes();

    // Two live instances, mirroring e.g. ContextManager's m_source and a
    // SceneEditor's m_source both being alive at once.
    GenericDMXSource dragSrc(m_doc);
    dragSrc.setOutputEnabled(true);
    dragSrc.set(m_fxiId, 0, 10, GenericDMXSource::DragPositionPush);
    dragSrc.writeDMX(nullptr, ua);

    QSharedPointer<GenericFader> dragFader = dragSrc.m_fadersMap.value(ua[0]->id());
    QVERIFY(!dragFader.isNull());

    GenericDMXSource::Feature feature;
    QVERIFY(GenericDMXSource::findFeatureForFader(dragFader.data(), m_fxiId, 0, feature));
    QCOMPARE(feature, GenericDMXSource::DragPositionPush);
}

void GenericDMXSource_Test::findFeatureForFaderFailsForForeignFader()
{
    QList<Universe*> ua = m_doc->inputOutputMap()->universes();

    GenericDMXSource src(m_doc);
    src.setOutputEnabled(true);
    src.set(m_fxiId, 0, 10, GenericDMXSource::ColorTool);
    src.writeDMX(nullptr, ua);

    // A fader nobody registered (e.g. a Function's own, or one already
    // dismissed) must not be misattributed to this - or any other live -
    // GenericDMXSource instance.
    GenericFader foreignFader;
    GenericDMXSource::Feature feature;
    QVERIFY(GenericDMXSource::findFeatureForFader(&foreignFader, m_fxiId, 0, feature) == false);

    // Same channel/value, but on a fader this instance never created at all.
    QVERIFY(GenericDMXSource::findFeatureForFader(nullptr, m_fxiId, 0, feature) == false);
}

QTEST_MAIN(GenericDMXSource_Test)
