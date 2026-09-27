/*
  Q Light Controller Plus - Unit test
  keypadparser_test.cpp

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

#include "keypadparser_test.h"
#include "keypadparser.h"
#include "qlcfile.h"
#include "fixture.h"
#include "doc.h"
#undef private
#undef protected

#include "../common/resource_paths.h"

void KeyPadParser_Test::initTestCase()
{
    m_doc = new Doc(this);

    QDir dir(INTERNAL_FIXTUREDIR);
    dir.setFilter(QDir::Files);
    dir.setNameFilters(QStringList() << QString("*%1").arg(KExtFixture));
    QVERIFY(m_doc->fixtureDefCache()->loadMap(dir));
}

void KeyPadParser_Test::parsing()
{
    KeyPadParser parser;
    QList<SceneValue> scvList;
    QByteArray universeValues;
    quint32 chnum;

    universeValues.fill(0, 512);

    /* set channel 13 to value 148 */
    scvList = parser.parseCommand(m_doc, "13 AT 148", universeValues);

    QCOMPARE(scvList.count(), 1);
    QCOMPARE(scvList.first().fxi, Fixture::invalidId());
    QCOMPARE(scvList.first().channel, quint32(12));
    QCOMPARE(scvList.first().value, uchar(148));

    /* set channels 3 to 15 to value 133 */
    scvList = parser.parseCommand(m_doc, "3 THRU 15 AT 133", universeValues);

    QCOMPARE(scvList.count(), 13);
    chnum = 2;

    foreach (SceneValue scv, scvList)
    {
        QCOMPARE(scv.fxi, Fixture::invalidId());
        QCOMPARE(scv.channel, chnum++);
        QCOMPARE(scv.value, uchar(133));
    }

    /* Set channel 18 to value 255 */
    scvList = parser.parseCommand(m_doc, "18 FULL", universeValues);

    QCOMPARE(scvList.count(), 1);
    QCOMPARE(scvList.first().fxi, Fixture::invalidId());
    QCOMPARE(scvList.first().channel, quint32(17));
    QCOMPARE(scvList.first().value, uchar(255));

    /* Set channels 1 to 10 to value 255 */
    scvList = parser.parseCommand(m_doc, "1 THRU 10 FULL", universeValues);
    chnum = 0;

    QCOMPARE(scvList.count(), 10);
    foreach (SceneValue scv, scvList)
    {
        QCOMPARE(scv.fxi, Fixture::invalidId());
        QCOMPARE(scv.channel, chnum++);
        QCOMPARE(scv.value, uchar(255));
    }

    /* Set channel 4 to value 0 */
    scvList = parser.parseCommand(m_doc, "4 ZERO", universeValues);

    QCOMPARE(scvList.count(), 1);
    QCOMPARE(scvList.first().fxi, Fixture::invalidId());
    QCOMPARE(scvList.first().channel, quint32(3));
    QCOMPARE(scvList.first().value, uchar(0));

    scvList = parser.parseCommand(m_doc, "18 THRU 25 AT 0 THRU 255", universeValues);
    chnum = 17;
    float val = 0;
    float delta = 255.0 / 7;

    QCOMPARE(scvList.count(), 8);
    foreach (SceneValue scv, scvList)
    {
        QCOMPARE(scv.fxi, Fixture::invalidId());
        QCOMPARE(scv.channel, chnum++);
        QCOMPARE(scv.value, uchar(val));
        universeValues[chnum] = val;
        val += delta;
    }

    scvList = parser.parseCommand(m_doc, "AT ZERO", universeValues);
    chnum = 17;

    QCOMPARE(scvList.count(), 8);
    foreach (SceneValue scv, scvList)
    {
        QCOMPARE(scv.fxi, Fixture::invalidId());
        QCOMPARE(scv.channel, chnum++);
        QCOMPARE(scv.value, uchar(0));
        universeValues[chnum] = val;
    }

    scvList = parser.parseCommand(m_doc, "AT 123", universeValues);
    chnum = 17;

    QCOMPARE(scvList.count(), 8);
    foreach (SceneValue scv, scvList)
    {
        QCOMPARE(scv.fxi, Fixture::invalidId());
        QCOMPARE(scv.channel, chnum++);
        QCOMPARE(scv.value, uchar(123));
        universeValues[chnum] = val;
    }

    scvList = parser.parseCommand(m_doc, "AT FULL", universeValues);
    chnum = 17;

    QCOMPARE(scvList.count(), 8);
    foreach (SceneValue scv, scvList)
    {
        QCOMPARE(scv.fxi, Fixture::invalidId());
        QCOMPARE(scv.channel, chnum++);
        QCOMPARE(scv.value, uchar(255));
        universeValues[chnum] = val;
    }

    /* Set channels 6, 8, 10, 12, 14 and 16 to value 100 */
    scvList = parser.parseCommand(m_doc, "6 THRU 16 BY 2 AT 100", universeValues);
    chnum = 5;

    QCOMPARE(scvList.count(), 6);
    foreach (SceneValue scv, scvList)
    {
        QCOMPARE(scv.fxi, Fixture::invalidId());
        QCOMPARE(scv.channel, chnum);
        QCOMPARE(scv.value, uchar(100));
        universeValues[chnum] = 100;
        chnum += 2;
    }

    /* Increase the value of the channels set by last command by 20% */
    scvList = parser.parseCommand(m_doc, "6 THRU 16 BY 2 +% 20", universeValues);
    chnum = 5;

    QCOMPARE(scvList.count(), 6);
    foreach (SceneValue scv, scvList)
    {
        QCOMPARE(scv.fxi, Fixture::invalidId());
        QCOMPARE(scv.channel, chnum);
        QCOMPARE(scv.value, uchar(120));
        universeValues[chnum] = 120;
        chnum += 2;
    }

    /* Decrease the value of the channels set by last command by 40% */
    scvList = parser.parseCommand(m_doc, "6 THRU 16 BY 2 -% 40", universeValues);
    chnum = 5;

    QCOMPARE(scvList.count(), 6);
    foreach (SceneValue scv, scvList)
    {
        QCOMPARE(scv.fxi, Fixture::invalidId());
        QCOMPARE(scv.channel, chnum);
        QCOMPARE(scv.value, uchar(72));
        universeValues[chnum] = 72;
        chnum += 2;
    }
}

void KeyPadParser_Test::invalidInput()
{
    KeyPadParser parser;
    QByteArray universeValues;
    universeValues.fill(0, 512);

    /* No document / empty command */
    QVERIFY(parser.parseCommand(NULL, "1 AT 2", universeValues).isEmpty());
    QVERIFY(parser.parseCommand(m_doc, "", universeValues).isEmpty());

    /* No channel given and no previous channel list: nothing to do */
    QVERIFY(parser.parseCommand(m_doc, "AT 5", universeValues).isEmpty());

    /* Channel 0 is not a valid channel number */
    QVERIFY(parser.parseCommand(m_doc, "0 AT 5", universeValues).isEmpty());

    /* Extra spaces and unknown tokens are skipped */
    QList<SceneValue> scvList = parser.parseCommand(m_doc, "1  AT foo -5 2", universeValues);
    QCOMPARE(scvList.count(), 1);
    QCOMPARE(scvList.first().channel, quint32(0));
    QCOMPARE(scvList.first().value, uchar(2));

    /* Unbalanced command: THRU without a target channel affects one channel */
    scvList = parser.parseCommand(m_doc, "3 THRU AT 9", universeValues);
    QCOMPARE(scvList.count(), 1);
    QCOMPARE(scvList.first().channel, quint32(2));
    QCOMPARE(scvList.first().value, uchar(9));
}

void KeyPadParser_Test::plusMinus()
{
    KeyPadParser parser;
    QByteArray universeValues;
    universeValues.fill(0, 512);
    universeValues[4] = 100;

    QList<SceneValue> scvList = parser.parseCommand(m_doc, "5 + 20", universeValues);
    QCOMPARE(scvList.count(), 1);
    QCOMPARE(scvList.first().channel, quint32(4));
    QCOMPARE(scvList.first().value, uchar(120));

    scvList = parser.parseCommand(m_doc, "5 - 30", universeValues);
    QCOMPARE(scvList.count(), 1);
    QCOMPARE(scvList.first().value, uchar(70));

    /* Results are clamped to the DMX range */
    scvList = parser.parseCommand(m_doc, "5 + 200", universeValues);
    QCOMPARE(scvList.first().value, uchar(255));

    scvList = parser.parseCommand(m_doc, "5 - 200", universeValues);
    QCOMPARE(scvList.first().value, uchar(0));
}

void KeyPadParser_Test::percentTokens()
{
    KeyPadParser parser;
    QByteArray universeValues;
    universeValues.fill(0, 512);
    universeValues[4] = 100;

    /* "+" followed by a separate "%" token */
    QList<SceneValue> scvList = parser.parseCommand(m_doc, "5 + % 50", universeValues);
    QCOMPARE(scvList.count(), 1);
    QCOMPARE(scvList.first().channel, quint32(4));
    QCOMPARE(scvList.first().value, uchar(150));

    /* "-" followed by a separate "%" token */
    scvList = parser.parseCommand(m_doc, "5 - % 50", universeValues);
    QCOMPARE(scvList.count(), 1);
    QCOMPARE(scvList.first().value, uchar(50));

    /* Combined tokens */
    scvList = parser.parseCommand(m_doc, "5 +% 10", universeValues);
    QCOMPARE(scvList.first().value, uchar(110));
    scvList = parser.parseCommand(m_doc, "5 -% 10", universeValues);
    QCOMPARE(scvList.first().value, uchar(90));

    /* A lone "%" without a preceding +/- is ignored */
    scvList = parser.parseCommand(m_doc, "5 %", universeValues);
    QCOMPARE(scvList.count(), 1);
    QCOMPARE(scvList.first().value, uchar(100));
}

void KeyPadParser_Test::fullZeroByNumbers()
{
    KeyPadParser parser;
    QByteArray universeValues;
    universeValues.fill(0, 512);
    universeValues[4] = 100;

    /* Numbers following FULL / ZERO don't change the outcome */
    QList<SceneValue> scvList = parser.parseCommand(m_doc, "5 FULL 12", universeValues);
    QCOMPARE(scvList.count(), 1);
    QCOMPARE(scvList.first().channel, quint32(4));
    QCOMPARE(scvList.first().value, uchar(255));

    scvList = parser.parseCommand(m_doc, "5 ZERO 12", universeValues);
    QCOMPARE(scvList.count(), 1);
    QCOMPARE(scvList.first().value, uchar(0));

    /* FULL / ZERO spanning a range */
    scvList = parser.parseCommand(m_doc, "1 THRU 4 ZERO", universeValues);
    QCOMPARE(scvList.count(), 4);
    foreach (SceneValue scv, scvList)
        QCOMPARE(scv.value, uchar(0));

    /* BY without a number keeps the default step of 1 */
    scvList = parser.parseCommand(m_doc, "1 THRU 4 BY AT 7", universeValues);
    QCOMPARE(scvList.count(), 4);
}

void KeyPadParser_Test::outOfUniverse()
{
    KeyPadParser parser;
    QByteArray universeValues;
    universeValues.fill(0, 512);

    /* Channels beyond the universe size are skipped */
    QList<SceneValue> scvList = parser.parseCommand(m_doc, "510 THRU 515 AT 7", universeValues);
    QCOMPARE(scvList.count(), 3);
    QCOMPARE(scvList.at(0).channel, quint32(509));
    QCOMPARE(scvList.at(2).channel, quint32(511));
    foreach (SceneValue scv, scvList)
        QCOMPARE(scv.value, uchar(7));

    /* Universe data shorter than the requested channels: missing values read as 0 */
    QByteArray shortValues;
    shortValues.fill(10, 4);
    scvList = parser.parseCommand(m_doc, "1 THRU 6 + 5", shortValues);
    QCOMPARE(scvList.count(), 6);
    QCOMPARE(scvList.at(0).value, uchar(15));
    QCOMPARE(scvList.at(3).value, uchar(15));
    QCOMPARE(scvList.at(4).value, uchar(5));
    QCOMPARE(scvList.at(5).value, uchar(5));
}

void KeyPadParser_Test::malformedRanges()
{
    // Crash audit (io.simpleDesk.sendKeypadCommand passes operator input
    // straight through): these used to abort on QByteArray::at()'s bounds
    // assert, or loop forever appending values.
    KeyPadParser parser;
    QByteArray universeValues;
    universeValues.fill(0, 512);

    /* A bare channel beyond the universe data: nothing to set, no crash */
    QCOMPARE(parser.parseCommand(m_doc, "513", universeValues).count(), 0);
    QCOMPARE(parser.parseCommand(m_doc, "600 AT 50", universeValues).count(), 0);

    /* BY 0 would never advance: treated as the default step of 1 */
    QList<SceneValue> scvList = parser.parseCommand(m_doc, "1 BY 0", universeValues);
    QCOMPARE(scvList.count(), 1);
    scvList = parser.parseCommand(m_doc, "1 THRU 4 BY 0 AT 9", universeValues);
    QCOMPARE(scvList.count(), 4);

    /* A range ending before it starts selects nothing (THRU 0 used to
     * wrap to UINT_MAX and loop forever) */
    QCOMPARE(parser.parseCommand(m_doc, "1 THRU 0 AT 5", universeValues).count(), 0);
    QCOMPARE(parser.parseCommand(m_doc, "10 THRU 3 AT 5", universeValues).count(), 0);

    /* A huge THRU stops at the universe end instead of iterating ~4e9 times */
    scvList = parser.parseCommand(m_doc, "510 THRU 4294967295 AT 5", universeValues);
    QCOMPARE(scvList.count(), 3);
    QCOMPARE(scvList.last().channel, quint32(511));
}

void KeyPadParser_Test::cleanupTestCase()
{
    delete m_doc;
}

QTEST_APPLESS_MAIN(KeyPadParser_Test)
