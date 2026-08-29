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
#include "fixturegroup.h"
#include "fixture.h"
#include "doc.h"

#include "scene.h"
#include "chaser.h"
#include "chaserstep.h"
#include "efx.h"
#include "efxfixture.h"
#include "grouphead.h"
#include "collection.h"

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

// ContextManager::isGroupFullySelected() (qmlui/contextmanager.cpp, added in
// 73ea55c7b/cb3abe039/34cbbac79) drives the group-row-highlight feature in
// both FixtureGroupsBar.qml and the left-hand Fixture Groups list.
// ContextManager itself needs a live QQuickView* to construct, but the check
// only ever touches Doc/FixtureGroup/MonitorProperties, so its logic was
// extracted into FixtureUtils::isGroupFullySelected() (same commit as this
// test) so it can be exercised directly against real engine objects.
void FixtureUtils_Test::groupFullySelectedWhenAllMembersSelected()
{
    Doc doc(this);

    Fixture *fxA = new Fixture(&doc);
    fxA->setChannels(1);
    fxA->setAddress(1);
    doc.addFixture(fxA);

    Fixture *fxB = new Fixture(&doc);
    fxB->setChannels(1);
    fxB->setAddress(2);
    doc.addFixture(fxB);

    FixtureGroup grp(&doc);
    grp.assignFixture(fxA->id());
    grp.assignFixture(fxB->id());

    MonitorProperties monProps;
    QList<quint32> selected = {
        FixtureUtils::fixtureItemID(fxA->id(), 0, 0),
        FixtureUtils::fixtureItemID(fxB->id(), 0, 0)
    };

    QVERIFY(FixtureUtils::isGroupFullySelected(&grp, &monProps, selected));
}

void FixtureUtils_Test::groupNotFullySelectedWhenOneMemberMissing()
{
    Doc doc(this);

    Fixture *fxA = new Fixture(&doc);
    fxA->setChannels(1);
    fxA->setAddress(1);
    doc.addFixture(fxA);

    Fixture *fxB = new Fixture(&doc);
    fxB->setChannels(1);
    fxB->setAddress(2);
    doc.addFixture(fxB);

    FixtureGroup grp(&doc);
    grp.assignFixture(fxA->id());
    grp.assignFixture(fxB->id());

    MonitorProperties monProps;
    // fxB never makes it into the selection.
    QList<quint32> selected = { FixtureUtils::fixtureItemID(fxA->id(), 0, 0) };

    QVERIFY(FixtureUtils::isGroupFullySelected(&grp, &monProps, selected) == false);
}

// The empty-group early return is a deliberate behavioural choice (an empty
// group can never read as "fully selected", however the selection looks),
// not an accident of the loop just never running - pin it down explicitly.
void FixtureUtils_Test::emptyGroupIsNeverFullySelected()
{
    Doc doc(this);
    FixtureGroup grp(&doc);
    MonitorProperties monProps;

    QVERIFY(FixtureUtils::isGroupFullySelected(&grp, &monProps, QList<quint32>()) == false);

    Fixture *fx = new Fixture(&doc);
    fx->setChannels(1);
    fx->setAddress(1);
    doc.addFixture(fx);
    QList<quint32> selected = { FixtureUtils::fixtureItemID(fx->id(), 0, 0) };
    QVERIFY(FixtureUtils::isGroupFullySelected(&grp, &monProps, selected) == false);
}

// The subtlest part of the real logic: every one of a fixture's
// MonitorProperties sub-items (extra heads AND linked copies, tracked
// independently of the base fixture) must be present in the selection, not
// just the base fixture's own itemID.
void FixtureUtils_Test::groupNotFullySelectedWhenOnlySomeHeadsSelected()
{
    Doc doc(this);

    Fixture *fx = new Fixture(&doc);
    fx->setChannels(1);
    fx->setAddress(1);
    doc.addFixture(fx);

    FixtureGroup grp(&doc);
    grp.assignFixture(fx->id());

    MonitorProperties monProps;
    // Register an extra head (1,0) and an extra linked copy (0,1) as
    // sub-items alongside the implicit base item (0,0).
    monProps.setFixturePosition(fx->id(), 1, 0, QVector3D());
    monProps.setFixturePosition(fx->id(), 0, 1, QVector3D());

    quint32 base   = FixtureUtils::fixtureItemID(fx->id(), 0, 0);
    quint32 head1  = FixtureUtils::fixtureItemID(fx->id(), 1, 0);
    quint32 linked1 = FixtureUtils::fixtureItemID(fx->id(), 0, 1);

    // Only the base item selected - the extra head and linked copy are not.
    QVERIFY(FixtureUtils::isGroupFullySelected(&grp, &monProps, QList<quint32>{base}) == false);
    // Base + head, still missing the linked copy.
    QVERIFY(FixtureUtils::isGroupFullySelected(&grp, &monProps, QList<quint32>{base, head1}) == false);
    // All three sub-items present - now fully selected.
    QVERIFY(FixtureUtils::isGroupFullySelected(&grp, &monProps, QList<quint32>{base, head1, linked1}));
}

// FixtureUtils::functionFixtures()/functionsFixtures() back
// ContextManager::selectFixturesInFunctions() (the "Select Fixtures in
// Function(s)" Function Manager toolbar action). A Scene's own components()
// already returns exactly the distinct fixture IDs referenced by its
// SceneValues - this pins that down through the wrapper.
void FixtureUtils_Test::functionFixturesForScene()
{
    Doc doc(this);

    Fixture *fxA = new Fixture(&doc);
    fxA->setChannels(2);
    fxA->setAddress(1);
    doc.addFixture(fxA);

    Fixture *fxB = new Fixture(&doc);
    fxB->setChannels(2);
    fxB->setAddress(3);
    doc.addFixture(fxB);

    Scene *scene = new Scene(&doc);
    scene->setValue(fxA->id(), 0, 255);
    scene->setValue(fxA->id(), 1, 128); // second channel of the same fixture - must not duplicate fxA
    scene->setValue(fxB->id(), 0, 64);
    doc.addFunction(scene);

    QList<quint32> result = FixtureUtils::functionFixtures(&doc, scene->id());
    QSet<quint32> resultSet(result.begin(), result.end());
    QSet<quint32> expected = { fxA->id(), fxB->id() };

    QCOMPARE(resultSet, expected);
}

// EFX::components() already returns exactly the distinct fixture IDs named
// by its EFXFixtures.
void FixtureUtils_Test::functionFixturesForEFX()
{
    Doc doc(this);

    Fixture *fxA = new Fixture(&doc);
    fxA->setChannels(1);
    fxA->setAddress(1);
    doc.addFixture(fxA);

    Fixture *fxB = new Fixture(&doc);
    fxB->setChannels(1);
    fxB->setAddress(2);
    doc.addFixture(fxB);

    EFX *efx = new EFX(&doc);
    efx->addFixture(fxA->id(), 0);
    efx->addFixture(fxB->id(), 0);
    doc.addFunction(efx);

    QList<quint32> result = FixtureUtils::functionFixtures(&doc, efx->id());
    QSet<quint32> resultSet(result.begin(), result.end());
    QSet<quint32> expected = { fxA->id(), fxB->id() };

    QCOMPARE(resultSet, expected);
}

// NOTE: RGBMatrix::components() (which the RGBMatrixType case in
// FixtureUtils::collectFunctionFixtures() dispatches to verbatim, same as
// Scene/EFX - it already returns the associated Fixture Group's own
// fixtureList()) is NOT covered by a test here. Unlike Scene/EFX, merely
// constructing a real RGBMatrix - even just to reach that dispatch, with no
// RGBMatrix-specific logic of this feature's own left to exercise - pulls in
// RGBAlgorithm::algorithms(doc), which unconditionally constructs an
// RGBImage, whose constructor requires a live QGuiApplication (confirmed via
// gdb backtrace: RGBImage::RGBImage -> ... -> QPixmap ctor -> fatal
// "Must construct a QGuiApplication before a QPixmap"). This test suite runs
// under QTEST_APPLESS_MAIN (a bare QCoreApplication, matching every other
// test here and the project's documented "no QQuickView/GUI" constraint for
// qmlui/test) and switching the whole suite to QTEST_MAIN just for this one
// case was judged out of scope for this feature - a real, pre-existing
// engine-level constructibility constraint on RGBMatrix, not something this
// feature's own logic introduced.

// The one genuinely open question this feature needed investigating: a
// Chaser's own components() returns its steps' Function IDs, NOT fixture
// IDs (unlike Scene/EFX/RGBMatrix) - so a Chaser with two Scene steps must
// have both step Scenes resolved and unioned, not just the Chaser's own
// (empty, for fixture purposes) direct fixture list.
void FixtureUtils_Test::functionFixturesRecursesIntoChaserSteps()
{
    Doc doc(this);

    Fixture *fxA = new Fixture(&doc);
    fxA->setChannels(1);
    fxA->setAddress(1);
    doc.addFixture(fxA);

    Fixture *fxB = new Fixture(&doc);
    fxB->setChannels(1);
    fxB->setAddress(2);
    doc.addFixture(fxB);

    Scene *sceneA = new Scene(&doc);
    sceneA->setValue(fxA->id(), 0, 255);
    doc.addFunction(sceneA);

    Scene *sceneB = new Scene(&doc);
    sceneB->setValue(fxB->id(), 0, 255);
    doc.addFunction(sceneB);

    Chaser *chaser = new Chaser(&doc);
    chaser->addStep(ChaserStep(sceneA->id()));
    chaser->addStep(ChaserStep(sceneB->id()));
    doc.addFunction(chaser);

    QList<quint32> result = FixtureUtils::functionFixtures(&doc, chaser->id());
    QSet<quint32> resultSet(result.begin(), result.end());
    QSet<quint32> expected = { fxA->id(), fxB->id() };

    QCOMPARE(resultSet, expected);
}

// A Chaser step referencing a Function type this feature doesn't support
// (here, a Collection) contributes no fixtures from that step, but must not
// prevent the Chaser's other, supported steps from still being resolved.
void FixtureUtils_Test::functionFixturesSkipsUnsupportedChaserStepType()
{
    Doc doc(this);

    Fixture *fxA = new Fixture(&doc);
    fxA->setChannels(1);
    fxA->setAddress(1);
    doc.addFixture(fxA);

    Fixture *fxUnreachable = new Fixture(&doc);
    fxUnreachable->setChannels(1);
    fxUnreachable->setAddress(2);
    doc.addFixture(fxUnreachable);

    Scene *sceneA = new Scene(&doc);
    sceneA->setValue(fxA->id(), 0, 255);
    doc.addFunction(sceneA);

    // A Collection referencing sceneA too - not itself a supported type, so
    // its own fixtures (here, transitively sceneA's) must not surface when
    // reached only via this Collection step.
    Collection *collection = new Collection(&doc);
    doc.addFunction(collection);

    Chaser *chaser = new Chaser(&doc);
    chaser->addStep(ChaserStep(sceneA->id()));
    chaser->addStep(ChaserStep(collection->id()));
    doc.addFunction(chaser);

    QList<quint32> result = FixtureUtils::functionFixtures(&doc, chaser->id());
    QSet<quint32> resultSet(result.begin(), result.end());
    QSet<quint32> expected = { fxA->id() };

    QCOMPARE(resultSet, expected);
    QVERIFY(resultSet.contains(fxUnreachable->id()) == false);
}

// A pathological indirect Chaser reference cycle (A's steps include B, B's
// steps include A) must not infinite-loop or crash - each Function is only
// ever resolved once, and every fixture reachable before the cycle closes
// is still returned.
void FixtureUtils_Test::functionFixturesIsCycleSafeAcrossChasers()
{
    Doc doc(this);

    Fixture *fxA = new Fixture(&doc);
    fxA->setChannels(1);
    fxA->setAddress(1);
    doc.addFixture(fxA);

    Fixture *fxB = new Fixture(&doc);
    fxB->setChannels(1);
    fxB->setAddress(2);
    doc.addFixture(fxB);

    Scene *sceneA = new Scene(&doc);
    sceneA->setValue(fxA->id(), 0, 255);
    doc.addFunction(sceneA);

    Scene *sceneB = new Scene(&doc);
    sceneB->setValue(fxB->id(), 0, 255);
    doc.addFunction(sceneB);

    Chaser *chaserA = new Chaser(&doc);
    doc.addFunction(chaserA);

    Chaser *chaserB = new Chaser(&doc);
    chaserB->addStep(ChaserStep(sceneB->id()));
    chaserB->addStep(ChaserStep(chaserA->id()));
    doc.addFunction(chaserB);

    chaserA->addStep(ChaserStep(sceneA->id()));
    chaserA->addStep(ChaserStep(chaserB->id()));

    QList<quint32> result = FixtureUtils::functionFixtures(&doc, chaserA->id());
    QSet<quint32> resultSet(result.begin(), result.end());
    QSet<quint32> expected = { fxA->id(), fxB->id() };

    QCOMPARE(resultSet, expected);
}

// A missing Function ID, and a Function of an unsupported type queried
// directly (not just as an unresolved Chaser step), both yield an empty
// result rather than an error.
void FixtureUtils_Test::functionFixturesReturnsEmptyForInvalidOrUnsupportedFunction()
{
    Doc doc(this);

    QVERIFY(FixtureUtils::functionFixtures(&doc, Function::invalidId()).isEmpty());

    Collection *collection = new Collection(&doc);
    doc.addFunction(collection);
    QVERIFY(FixtureUtils::functionFixtures(&doc, collection->id()).isEmpty());
}

// functionsFixtures() is the union across every Function currently selected
// in the Function Manager - here a directly selected Scene and a directly
// selected EFX, each referencing a different fixture.
void FixtureUtils_Test::functionsFixturesUnionsAcrossSelectedFunctions()
{
    Doc doc(this);

    Fixture *fxA = new Fixture(&doc);
    fxA->setChannels(1);
    fxA->setAddress(1);
    doc.addFixture(fxA);

    Fixture *fxB = new Fixture(&doc);
    fxB->setChannels(1);
    fxB->setAddress(2);
    doc.addFixture(fxB);

    Scene *scene = new Scene(&doc);
    scene->setValue(fxA->id(), 0, 255);
    doc.addFunction(scene);

    EFX *efx = new EFX(&doc);
    efx->addFixture(fxB->id(), 0);
    doc.addFunction(efx);

    QList<quint32> result = FixtureUtils::functionsFixtures(&doc, QList<quint32>{ scene->id(), efx->id() });
    QSet<quint32> resultSet(result.begin(), result.end());
    QSet<quint32> expected = { fxA->id(), fxB->id() };

    QCOMPARE(resultSet, expected);
}

QTEST_APPLESS_MAIN(FixtureUtils_Test)
