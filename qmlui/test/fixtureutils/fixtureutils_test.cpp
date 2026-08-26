/*
  Q Light Controller - Unit test
  fixtureutils_test.cpp

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

#include "fixtureutils_test.h"
#include "fixtureutils.h"
#include "monitorproperties.h"

// Every mass-selection, arrange/align, and drag-position code path this
// session touched identifies a fixture (or one head of a multi-head/linked
// fixture) via this single packed 32-bit itemID - get the packing wrong and
// selection, arrange placement, and drag position updates would all silently
// point at the wrong fixture/head. Round-trip it.
void FixtureUtils_Test::itemIDRoundTrip()
{
    quint32 itemID = FixtureUtils::fixtureItemID(42, 3, 7);

    QCOMPARE(FixtureUtils::itemFixtureID(itemID), (quint32)42);
    QCOMPARE(FixtureUtils::itemHeadIndex(itemID), (quint16)3);
    QCOMPARE(FixtureUtils::itemLinkedIndex(itemID), (quint16)7);
}

// headIndex/linkedIndex are packed into 8 bits each - their max representable
// value (255) is exactly the kind of off-by-one edge a shift/mask scheme like
// this can get wrong.
void FixtureUtils_Test::itemIDRoundTripAtBitBoundaries()
{
    quint32 itemID = FixtureUtils::fixtureItemID(1, 255, 255);

    QCOMPARE(FixtureUtils::itemFixtureID(itemID), (quint32)1);
    QCOMPARE(FixtureUtils::itemHeadIndex(itemID), (quint16)255);
    QCOMPARE(FixtureUtils::itemLinkedIndex(itemID), (quint16)255);

    // headIndex/linkedIndex must not bleed into the fixture ID's bits.
    quint32 zeroHeadLinked = FixtureUtils::fixtureItemID(1, 0, 0);
    QVERIFY(itemID != zeroHeadLinked);
    QCOMPARE(FixtureUtils::itemFixtureID(zeroHeadLinked), (quint32)1);
}

// ContextManager::setFixturesAlignment() (and, by the same logic, the new
// Arrange popup) relies on alignItem() to move only the axis being aligned,
// leaving the other in-plane axis (and depth) untouched - this is what makes
// "align left" not also snap fixtures' front/back position together.
void FixtureUtils_Test::alignTopViewMovesOnlyTheAlignedAxis()
{
    QVector3D ref(100, 50, 200);
    QVector3D pos(10, 999, 20);

    FixtureUtils::alignItem(ref, pos, MonitorProperties::TopView, Qt::AlignLeft);
    QCOMPARE(pos, QVector3D(100, 999, 20));

    pos = QVector3D(10, 999, 20);
    FixtureUtils::alignItem(ref, pos, MonitorProperties::TopView, Qt::AlignTop);
    QCOMPARE(pos, QVector3D(10, 999, 200));
}

// Side views align a different physical axis (Z, not X) for "left", since
// the in-plane horizontal axis changes with the point of view - this is the
// exact per-view axis mapping getting it wrong would silently misalign
// fixtures only when viewed from the side.
void FixtureUtils_Test::alignLeftViewMovesOnlyTheAlignedAxis()
{
    QVector3D ref(100, 50, 200);
    QVector3D pos(10, 999, 20);

    FixtureUtils::alignItem(ref, pos, MonitorProperties::LeftSideView, Qt::AlignLeft);
    QCOMPARE(pos, QVector3D(10, 999, 200));

    pos = QVector3D(10, 999, 20);
    FixtureUtils::alignItem(ref, pos, MonitorProperties::LeftSideView, Qt::AlignTop);
    QCOMPARE(pos, QVector3D(10, 50, 20));
}

QTEST_APPLESS_MAIN(FixtureUtils_Test)
