/*
  Q Light Controller Plus - Unit test
  vcselectionprune_test.cpp

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

#include "vcselectionprune_test.h"
#include "vcselectionprune.h"

/* VirtualConsole's selection map is widget id -> QQuickItem*; any value type works here. */

void VCSelectionPrune_Test::survivorsStaySelected()
{
    // the operator selected 3, 5 and 9; an API client deletes 5 (and frame 7 with its child 8)
    QMap<quint32, int> selection { { 3, 30 }, { 5, 50 }, { 9, 90 } };
    QList<quint32> removed = vcPruneDeletedFromSelection(selection, QSet<quint32> { 5, 7, 8 });
    QCOMPARE(removed, QList<quint32>({ 5 }));
    QCOMPARE(selection.keys(), QList<quint32>({ 3, 9 }));
    QCOMPARE(selection.value(3), 30);
    QCOMPARE(selection.value(9), 90);
}

void VCSelectionPrune_Test::deletingTheWholeSelectionEmptiesIt()
{
    // the desktop's own Delete: the selection itself (a frame and one of its children) goes
    QMap<quint32, int> selection { { 4, 1 }, { 6, 2 } };
    QList<quint32> removed = vcPruneDeletedFromSelection(selection, QSet<quint32> { 4, 6, 10, 11 });
    QCOMPARE(removed, QList<quint32>({ 4, 6 }));
    QVERIFY(selection.isEmpty());
}

void VCSelectionPrune_Test::deletingUnselectedWidgetsChangesNothing()
{
    QMap<quint32, int> selection { { 1, 1 } };
    QVERIFY(vcPruneDeletedFromSelection(selection, QSet<quint32> { 2 }).isEmpty());
    QCOMPARE(selection.keys(), QList<quint32>({ 1 }));

    QMap<quint32, int> empty;
    QVERIFY(vcPruneDeletedFromSelection(empty, QSet<quint32> { 1 }).isEmpty());
}

QTEST_APPLESS_MAIN(VCSelectionPrune_Test)
