/*
  Q Light Controller Plus - Unit test
  functionpathutils_test.cpp

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

#include "functionpathutils_test.h"
#include "functionpathutils.h"

static const QChar S = QLatin1Char('/');

void FunctionPathUtils_Test::membershipIsByFullPath()
{
    // the user's case: folders "A/X" and "X" must never be confused
    QVERIFY(FunctionPathUtils::isInFolder("A/X", "A/X", S));
    QVERIFY(FunctionPathUtils::isInFolder("A/X/deep", "A/X", S));
    QVERIFY(!FunctionPathUtils::isInFolder("X", "A/X", S));
    QVERIFY(!FunctionPathUtils::isInFolder("X/deep", "A/X", S));
    QVERIFY(!FunctionPathUtils::isInFolder("A/X", "X", S));
    QVERIFY(!FunctionPathUtils::isInFolder("A/X/deep", "X", S));
    QVERIFY(FunctionPathUtils::isInFolder("X/deep", "X", S));

    // and a name must match on a folder boundary only
    QVERIFY(!FunctionPathUtils::isInFolder("Foobar", "Foo", S));
    QVERIFY(!FunctionPathUtils::isInFolder("Foobar/x", "Foo", S));
    QVERIFY(FunctionPathUtils::isInFolder("Foo/bar", "Foo", S));
}

void FunctionPathUtils_Test::membershipAtRoot()
{
    QVERIFY(FunctionPathUtils::isInFolder("", "", S));
    QVERIFY(FunctionPathUtils::isInFolder("A", "", S));
    QVERIFY(FunctionPathUtils::isInFolder("A/X", "", S));
    QVERIFY(!FunctionPathUtils::isInFolder("", "A", S));
}

void FunctionPathUtils_Test::descendantExcludesTheFolderItself()
{
    QVERIFY(!FunctionPathUtils::isDescendantOf("A/X", "A/X", S));
    QVERIFY(FunctionPathUtils::isDescendantOf("A/X/Y", "A/X", S));
    QVERIFY(!FunctionPathUtils::isDescendantOf("A/XY", "A/X", S));
    QVERIFY(FunctionPathUtils::isDescendantOf("A", "", S));
    QVERIFY(!FunctionPathUtils::isDescendantOf("", "", S));
}

void FunctionPathUtils_Test::rebaseRenamesNestedFolder()
{
    // rename "A/X" to "A/Y": only paths inside A/X follow
    QCOMPARE(FunctionPathUtils::rebase("A/X", "A/X", "A/Y", S), QString("A/Y"));
    QCOMPARE(FunctionPathUtils::rebase("A/X/sub", "A/X", "A/Y", S), QString("A/Y/sub"));
    QCOMPARE(FunctionPathUtils::rebase("X", "A/X", "A/Y", S), QString("X"));
    QCOMPARE(FunctionPathUtils::rebase("X/sub", "A/X", "A/Y", S), QString("X/sub"));
    QCOMPARE(FunctionPathUtils::rebase("A/XY", "A/X", "A/Y", S), QString("A/XY"));
}

void FunctionPathUtils_Test::rebaseLeavesUnrelatedPathsAlone()
{
    QCOMPARE(FunctionPathUtils::rebase("B/X", "A", "C", S), QString("B/X"));
    QCOMPARE(FunctionPathUtils::rebase("", "A", "C", S), QString(""));
}

void FunctionPathUtils_Test::rebaseToRootDropsTheSeparator()
{
    // dissolving "A/X" into the root: "A/X/f" -> "f", "A/X" -> ""
    QCOMPARE(FunctionPathUtils::rebase("A/X/f", "A/X", "", S), QString("f"));
    QCOMPARE(FunctionPathUtils::rebase("A/X", "A/X", "", S), QString(""));
}

void FunctionPathUtils_Test::rebaseFromRootPrefixesEverything()
{
    QCOMPARE(FunctionPathUtils::rebase("f", "", "N", S), QString("N/f"));
    QCOMPARE(FunctionPathUtils::rebase("", "", "N", S), QString("N"));
}

void FunctionPathUtils_Test::segments()
{
    QCOMPARE(FunctionPathUtils::lastSegment("A/B/C", S), QString("C"));
    QCOMPARE(FunctionPathUtils::lastSegment("C", S), QString("C"));
    QCOMPARE(FunctionPathUtils::parentPath("A/B/C", S), QString("A/B"));
    QCOMPARE(FunctionPathUtils::parentPath("C", S), QString(""));
    QCOMPARE(FunctionPathUtils::join("", "C", S), QString("C"));
    QCOMPARE(FunctionPathUtils::join("A", "C", S), QString("A/C"));
    QCOMPARE(FunctionPathUtils::join("A", "", S), QString("A"));
}

void FunctionPathUtils_Test::moveIntoOwnSubtreeIsRefused()
{
    QVERIFY(FunctionPathUtils::isMoveIntoOwnSubtree("A", "A", S));
    QVERIFY(FunctionPathUtils::isMoveIntoOwnSubtree("A", "A/X", S));
    QVERIFY(FunctionPathUtils::isMoveIntoOwnSubtree("A/X", "A/X/Y", S));
    QVERIFY(!FunctionPathUtils::isMoveIntoOwnSubtree("A/X", "A", S));
    QVERIFY(!FunctionPathUtils::isMoveIntoOwnSubtree("A", "AB", S));
    QVERIFY(!FunctionPathUtils::isMoveIntoOwnSubtree("A", "", S));
    QVERIFY(!FunctionPathUtils::isMoveIntoOwnSubtree("A/X", "X", S));
}

void FunctionPathUtils_Test::moveToRoot()
{
    QCOMPARE(FunctionPathUtils::movedFolderPath("A/X", "", S), QString("X"));
    QCOMPARE(FunctionPathUtils::movedFolderPath("X", "", S), QString("X"));
    // and the functions inside follow
    QCOMPARE(FunctionPathUtils::rebase("A/X/f", "A/X", FunctionPathUtils::movedFolderPath("A/X", "", S), S),
             QString("X/f"));
}

void FunctionPathUtils_Test::moveIntoFolder()
{
    QCOMPARE(FunctionPathUtils::movedFolderPath("A/X", "B", S), QString("B/X"));
    QCOMPARE(FunctionPathUtils::movedFolderPath("X", "B/C", S), QString("B/C/X"));
    QCOMPARE(FunctionPathUtils::rebase("X/f", "X", FunctionPathUtils::movedFolderPath("X", "B/C", S), S),
             QString("B/C/X/f"));
}

void FunctionPathUtils_Test::renamedFolderPath()
{
    QCOMPARE(FunctionPathUtils::renamedFolderPath("A/X", "Y", S), QString("A/Y"));
    QCOMPARE(FunctionPathUtils::renamedFolderPath("X", "Y", S), QString("Y"));
}

void FunctionPathUtils_Test::notationConversion()
{
    const QChar tree = QLatin1Char('`');
    QCOMPARE(FunctionPathUtils::toTreePath("A/X/f", tree), QString("A`X`f"));
    QCOMPARE(FunctionPathUtils::toFunctionPath("A`X`f", tree), QString("A/X/f"));
    QCOMPARE(FunctionPathUtils::toTreePath("", tree), QString(""));
}

QTEST_APPLESS_MAIN(FunctionPathUtils_Test)
