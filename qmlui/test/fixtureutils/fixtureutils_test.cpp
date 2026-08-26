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

#include <algorithm>

#include "fixtureutils_test.h"
#include "fixtureutils.h"
#include "monitorproperties.h"
#include "fixture.h"
#include "doc.h"

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

// Reimplements the comparator ContextManager::sortedSelectedFixtures() uses
// (qmlui/contextmanager.cpp, commit a71fdd756) - ContextManager itself can't
// be constructed headlessly (needs a live QQuickView*), so this exercises the
// same rule directly against real Fixture/Doc objects and hand-packed itemIDs:
// primary key is DMX order (Fixture::operator<), with head index then linked
// index as tiebreakers for itemIDs sharing one base fixture.
void FixtureUtils_Test::dmxOrderSortWithHeadTiebreak()
{
    Doc doc(this);

    Fixture *fxLow = new Fixture(&doc);
    fxLow->setChannels(1);
    fxLow->setAddress(5);
    doc.addFixture(fxLow);

    Fixture *fxHigh = new Fixture(&doc);
    fxHigh->setChannels(1);
    fxHigh->setAddress(50);
    doc.addFixture(fxHigh);

    // Three itemIDs all pointing at fxHigh, with scrambled head/linked
    // indices, plus one itemID pointing at fxLow (lower DMX address, so it
    // must sort before all of fxHigh's items regardless of head/linked).
    QList<quint32> itemIDs = {
        FixtureUtils::fixtureItemID(fxHigh->id(), 2, 0),
        FixtureUtils::fixtureItemID(fxHigh->id(), 0, 1),
        FixtureUtils::fixtureItemID(fxLow->id(),  0, 0),
        FixtureUtils::fixtureItemID(fxHigh->id(), 0, 0),
        FixtureUtils::fixtureItemID(fxHigh->id(), 1, 0),
    };

    std::sort(itemIDs.begin(), itemIDs.end(), [&doc] (quint32 left, quint32 right)
    {
        Fixture *leftFixture = doc.fixture(FixtureUtils::itemFixtureID(left));
        Fixture *rightFixture = doc.fixture(FixtureUtils::itemFixtureID(right));

        if (leftFixture == nullptr || rightFixture == nullptr)
            return false;

        if (leftFixture != rightFixture)
            return *leftFixture < *rightFixture;

        quint16 leftHead = FixtureUtils::itemHeadIndex(left);
        quint16 rightHead = FixtureUtils::itemHeadIndex(right);
        if (leftHead != rightHead)
            return leftHead < rightHead;

        return FixtureUtils::itemLinkedIndex(left) < FixtureUtils::itemLinkedIndex(right);
    });

    QCOMPARE(itemIDs.count(), 5);
    // fxLow first (lower DMX address)...
    QCOMPARE(FixtureUtils::itemFixtureID(itemIDs.at(0)), fxLow->id());
    // ...then all of fxHigh's items, head-ascending, linked-ascending within
    // the tied head 0.
    QCOMPARE(FixtureUtils::itemFixtureID(itemIDs.at(1)), fxHigh->id());
    QCOMPARE(FixtureUtils::itemHeadIndex(itemIDs.at(1)), (quint16)0);
    QCOMPARE(FixtureUtils::itemLinkedIndex(itemIDs.at(1)), (quint16)0);

    QCOMPARE(FixtureUtils::itemFixtureID(itemIDs.at(2)), fxHigh->id());
    QCOMPARE(FixtureUtils::itemHeadIndex(itemIDs.at(2)), (quint16)0);
    QCOMPARE(FixtureUtils::itemLinkedIndex(itemIDs.at(2)), (quint16)1);

    QCOMPARE(FixtureUtils::itemFixtureID(itemIDs.at(3)), fxHigh->id());
    QCOMPARE(FixtureUtils::itemHeadIndex(itemIDs.at(3)), (quint16)1);

    QCOMPARE(FixtureUtils::itemFixtureID(itemIDs.at(4)), fxHigh->id());
    QCOMPARE(FixtureUtils::itemHeadIndex(itemIDs.at(4)), (quint16)2);
}

QTEST_APPLESS_MAIN(FixtureUtils_Test)
