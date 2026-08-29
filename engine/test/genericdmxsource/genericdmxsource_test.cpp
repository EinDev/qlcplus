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

QTEST_MAIN(GenericDMXSource_Test)
