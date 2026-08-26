/*
  Q Light Controller - Unit test
  qlcpoint_test.cpp

  Copyright (c) Heikki Junnila

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
#include <QMap>

#include "qlcpoint_test.h"
#include "qlcpoint.h"

void QLCPoint_Test::initial()
{
    QLCPoint pt;
    QCOMPARE(pt.isNull(), true);

    pt = QLCPoint(1, 2);
    QCOMPARE(pt.isNull(), false);
    QCOMPARE(pt.x(), 1);
    QCOMPARE(pt.y(), 2);
}

void QLCPoint_Test::equals()
{
    QLCPoint pt1(1, 1);
    QLCPoint pt2(2, 2);
    QCOMPARE(pt1, pt1);
    QCOMPARE(pt2, pt2);
    QVERIFY(pt1 != pt2);
}

void QLCPoint_Test::hash()
{
    QLCPoint pt(10, 10);
    qDebug() << qHash(pt);
}

void QLCPoint_Test::lessThan()
{
    // ContextManager::groupOrSortedSelectedFixtures() (qmlui/contextmanager.cpp,
    // commit cb3abe039) relies on QLCPoint::operator< sorting row-major (y then
    // x) so that iterating a QMap<QLCPoint, GroupHead> (FixtureGroup::headsMap())
    // already yields a Fixture Group's grid in top-left-to-bottom-right order.
    // This was never directly verified - do so here.

    // Same row: ordered by x
    QVERIFY(QLCPoint(0, 0) < QLCPoint(1, 0));
    QVERIFY(!(QLCPoint(1, 0) < QLCPoint(0, 0)));

    // Different row: ordered by y, regardless of x
    QVERIFY(QLCPoint(5, 0) < QLCPoint(0, 1));
    QVERIFY(!(QLCPoint(0, 1) < QLCPoint(5, 0)));

    // Equal points: neither is less than the other
    QVERIFY(!(QLCPoint(2, 3) < QLCPoint(2, 3)));

    // A QMap<QLCPoint, ...>, exactly as FixtureGroup::headsMap() returns,
    // must iterate row-major top-left to bottom-right for a scrambled
    // insertion order - this is the actual assumption the qmlui code depends on.
    QMap<QLCPoint, int> grid;
    grid[QLCPoint(1, 1)] = 5; // row 1, col 1
    grid[QLCPoint(0, 0)] = 0; // row 0, col 0
    grid[QLCPoint(2, 0)] = 2; // row 0, col 2
    grid[QLCPoint(1, 0)] = 1; // row 0, col 1
    grid[QLCPoint(0, 1)] = 4; // row 1, col 0

    QList<int> iterationOrder;
    QMapIterator<QLCPoint, int> it(grid);
    while (it.hasNext())
    {
        it.next();
        iterationOrder << it.value();
    }

    QCOMPARE(iterationOrder, QList<int>({ 0, 1, 2, 4, 5 }));
}

QTEST_APPLESS_MAIN(QLCPoint_Test)
