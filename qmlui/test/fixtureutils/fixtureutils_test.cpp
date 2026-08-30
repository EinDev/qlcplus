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
#include <QSet>

#include "fixtureutils_test.h"
#include "fixtureutils.h"
#include "monitorproperties.h"
#include "fixturegroup.h"
#include "qlcfixturedef.h"
#include "qlcfixturemode.h"
#include "qlcchannel.h"
#include "fixture.h"
#include "doc.h"
#include "scene.h"
#include "chaser.h"
#include "chaserstep.h"
#include "efx.h"
#include "efxfixture.h"
#include "grouphead.h"
#include "collection.h"

// Builds a Fixture with a single, MSB-only (coarse) channel of $group set to
// $value - just enough to exercise FixtureUtils::fixturePositionDelta()/
// fixtureRotationDelta()/fixtureScaleFactor(), which each scan a fixture's
// real channels/values rather than taking raw ints directly.
static Fixture *createSingleChannelFixture(Doc *doc, QLCChannel::Group group, uchar value)
{
    QLCFixtureDef *def = new QLCFixtureDef();
    QLCFixtureMode *mode = new QLCFixtureMode(def);

    QLCChannel *channel = new QLCChannel();
    channel->setName("Test Channel");
    channel->setGroup(group);
    channel->setControlByte(QLCChannel::MSB);
    // insertChannel() below requires the channel to already be registered
    // with the mode's parent QLCFixtureDef, or it silently refuses to add it.
    def->addChannel(channel);
    mode->insertChannel(channel, 0);

    Fixture *fixture = new Fixture(doc);
    fixture->setAddress(0);
    fixture->setFixtureDefinition(def, mode);
    doc->addFixture(fixture);

    QByteArray values(1, char(value));
    fixture->setChannelValues(values);

    return fixture;
}

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

// accumulateChannelGroupValue() is meant to be a drop-in generalization of
// the MSB/LSB accumulation MainView3D::updateFixtureItem() already hand-rolls
// for Pan/Tilt (mainview3d.cpp ~1438-1459): "if (ch->controlByte() == MSB)
// acc += (value << 8); else acc += value;". Reimplement that exact
// hand-rolled version here as a reference and check both agree, for a
// fine (MSB+LSB) pair of channels.
void FixtureUtils_Test::accumulateChannelGroupValueMatchesPanTiltMSBLSB()
{
    QLCChannel msbChannel;
    msbChannel.setControlByte(QLCChannel::MSB);
    QLCChannel lsbChannel;
    lsbChannel.setControlByte(QLCChannel::LSB);

    uchar msbValue = 0xAB;
    uchar lsbValue = 0xCD;

    int reference = 0;
    reference += (msbValue << 8);
    reference += (lsbValue);

    int accumulated = 0;
    accumulated = FixtureUtils::accumulateChannelGroupValue(&msbChannel, msbValue, accumulated);
    accumulated = FixtureUtils::accumulateChannelGroupValue(&lsbChannel, lsbValue, accumulated);

    QCOMPARE(accumulated, reference);
    QCOMPARE(accumulated, (int)0xABCD);
}

// A group with only a coarse (MSB) channel and no LSB pair must land as a
// multiple of 256 within the same 0-65535 domain - exactly how a coarse-only
// Pan/Tilt channel behaves today - rather than needing (or silently falling
// into) a separate 0-255 code path.
void FixtureUtils_Test::accumulateChannelGroupValueCoarseOnlyMatchesPanTilt()
{
    QLCChannel msbOnlyChannel;
    msbOnlyChannel.setControlByte(QLCChannel::MSB);

    int accumulated = FixtureUtils::accumulateChannelGroupValue(&msbOnlyChannel, 0x7F, 0);

    QCOMPARE(accumulated, (int)(0x7F << 8));
    QVERIFY(accumulated % 256 == 0);
    QVERIFY(accumulated <= 65535);
}

// DMX 32768 (mid-range) must decode to the documented rest position, delta 0,
// for every axis - this is the "fixture hasn't been touched" case and the
// single most load-bearing value other workstreams will rely on.
void FixtureUtils_Test::positionDeltaMidpointIsZero()
{
    QCOMPARE(FixtureUtils::positionDeltaFromRaw(32768), 0.0f);
}

// Pin down the documented +/-800m default range (MonitorProperties'
// PreviewItem::m_positionRange default, sized for drone-scale rigs) at both
// raw extremes, plus an explicit-range case proving the new $range parameter
// itself is honored, not just the default.
void FixtureUtils_Test::positionDeltaAtExtremesMatchesDocumentedRange()
{
    QCOMPARE(FixtureUtils::positionDeltaFromRaw(0), -800.0f);
    QVERIFY(qAbs(FixtureUtils::positionDeltaFromRaw(65535) - 800.0f) < 0.1f);

    QCOMPARE(FixtureUtils::positionDeltaFromRaw(0, 2.5f), -2.5f);
    QVERIFY(qAbs(FixtureUtils::positionDeltaFromRaw(65535, 2.5f) - 2.5f) < 0.001f);
}

// The write-back direction (engineering value -> raw DMX) other workstreams
// need for drag-to-set-DMX must round-trip through the decode direction
// within quantization error.
void FixtureUtils_Test::positionDeltaRoundTrips()
{
    for (int raw : { 0, 1, 100, 32768, 40000, 65534, 65535 })
    {
        float delta = FixtureUtils::positionDeltaFromRaw(raw);
        int roundTripped = FixtureUtils::positionRawFromDelta(delta);
        QVERIFY2(qAbs(roundTripped - raw) <= 1,
                  qPrintable(QString("raw %1 -> delta %2 -> raw %3")
                             .arg(raw).arg(delta).arg(roundTripped)));
    }

    // out-of-range engineering values must clamp, not wrap or overflow.
    // With the new 800.0m default range, +/-100 no longer exceeds it (only
    // +/-12.5%) - use deltas that actually exceed +/-800 to still saturate.
    QCOMPARE(FixtureUtils::positionRawFromDelta(-2000.0f), 0);
    QCOMPARE(FixtureUtils::positionRawFromDelta(2000.0f), 65535);
}

void FixtureUtils_Test::rotationDeltaMidpointIsZero()
{
    QCOMPARE(FixtureUtils::rotationDeltaFromRaw(32768), 0.0f);
}

// Pin down the documented 540 degree (+/-270) default range, matching the
// user's own "Mobile Truss" fixture profile's rotation channels.
void FixtureUtils_Test::rotationDeltaAtExtremesMatchesDocumentedRange()
{
    QCOMPARE(FixtureUtils::rotationDeltaFromRaw(0), -270.0f);
    QVERIFY(qAbs(FixtureUtils::rotationDeltaFromRaw(65535) - 270.0f) < 0.01f);
}

void FixtureUtils_Test::rotationDeltaRoundTrips()
{
    for (int raw : { 0, 1, 100, 32768, 40000, 65534, 65535 })
    {
        float degrees = FixtureUtils::rotationDeltaFromRaw(raw);
        int roundTripped = FixtureUtils::rotationRawFromDelta(degrees);
        QVERIFY2(qAbs(roundTripped - raw) <= 1,
                  qPrintable(QString("raw %1 -> degrees %2 -> raw %3")
                             .arg(raw).arg(degrees).arg(roundTripped)));
    }

    QCOMPARE(FixtureUtils::rotationRawFromDelta(-1000.0f), 0);
    QCOMPARE(FixtureUtils::rotationRawFromDelta(1000.0f), 65535);
}

// Pin down the documented 0.1x-3.0x scale factor range at both raw extremes
// (decided with the user, not derived from any physical-property field).
void FixtureUtils_Test::scaleFactorAtExtremesMatchesDocumentedRange()
{
    QCOMPARE(FixtureUtils::scaleFactorFromRaw(0), 0.1f);
    QVERIFY(qAbs(FixtureUtils::scaleFactorFromRaw(65535) - 3.0f) < 0.001f);
}

void FixtureUtils_Test::scaleFactorRoundTrips()
{
    for (int raw : { 0, 1, 100, 32768, 40000, 65534, 65535 })
    {
        float factor = FixtureUtils::scaleFactorFromRaw(raw);
        int roundTripped = FixtureUtils::scaleRawFromFactor(factor);
        QVERIFY2(qAbs(roundTripped - raw) <= 1,
                  qPrintable(QString("raw %1 -> factor %2 -> raw %3")
                             .arg(raw).arg(factor).arg(roundTripped)));
    }

    // out-of-range factors must clamp to the 0-65535 raw domain.
    QCOMPARE(FixtureUtils::scaleRawFromFactor(0.0f), 0);
    QCOMPARE(FixtureUtils::scaleRawFromFactor(10.0f), 65535);
}

// fixturePositionDelta() is the single source of truth MainView3D::
// updateFixtureItem() (DMX->transform) and ContextManager::setFixturesPosition()
// (drag->DMX write-back) both rely on to know "what is this fixture's current
// live position offset" - an axis with no channel at all on the fixture must
// read back as exactly 0 (unaffected), never positionDeltaFromRaw(0)'s
// negative range extreme, or every axis a profile doesn't define would
// silently drag the fixture off to one side.
void FixtureUtils_Test::fixturePositionDeltaIgnoresAbsentAxes()
{
    Doc doc(this);
    Fixture *fixture = createSingleChannelFixture(&doc, QLCChannel::PositionX, 0xFF);

    QVector3D delta = FixtureUtils::fixturePositionDelta(fixture);

    QCOMPARE(delta.x(), FixtureUtils::positionDeltaFromRaw(0xFF << 8));
    QCOMPARE(delta.y(), 0.0f);
    QCOMPARE(delta.z(), 0.0f);
}

// Same guarantee as above, for RotationX/Y/Z.
void FixtureUtils_Test::fixtureRotationDeltaIgnoresAbsentAxes()
{
    Doc doc(this);
    Fixture *fixture = createSingleChannelFixture(&doc, QLCChannel::RotationZ, 0xFF);

    QVector3D delta = FixtureUtils::fixtureRotationDelta(fixture);

    QCOMPARE(delta.z(), FixtureUtils::rotationDeltaFromRaw(0xFF << 8));
    QCOMPARE(delta.x(), 0.0f);
    QCOMPARE(delta.y(), 0.0f);
}

// fixtureScaleFactor() is not delta-based like position/rotation - an absent
// axis must default to 1.0 (no scaling), not scaleFactorFromRaw(0)'s 0.1x,
// or a fixture defining only ScaleX would get silently squashed to 10% on
// the Y/Z axes it never asked to control.
void FixtureUtils_Test::fixtureScaleFactorDefaultsToOneForAbsentAxes()
{
    Doc doc(this);
    Fixture *fixture = createSingleChannelFixture(&doc, QLCChannel::ScaleX, 0xFF);

    QVector3D factor = FixtureUtils::fixtureScaleFactor(fixture);

    QCOMPARE(factor.x(), FixtureUtils::scaleFactorFromRaw(0xFF << 8));
    QCOMPARE(factor.y(), 1.0f);
    QCOMPARE(factor.z(), 1.0f);
}

// A fixture with no per-fixture MonitorProperties entry at all (never
// registered) must behave identically to passing no MonitorProperties* at
// all - the whole point of the feature's default being backward compatible.
void FixtureUtils_Test::fixturePositionDeltaUnaffectedByDefaultMonProps()
{
    Doc doc(this);
    Fixture *fixture = createSingleChannelFixture(&doc, QLCChannel::PositionX, 0xFF);
    MonitorProperties monProps;

    QVector3D withoutMonProps = FixtureUtils::fixturePositionDelta(fixture);
    QVector3D withDefaultMonProps = FixtureUtils::fixturePositionDelta(fixture, &monProps);

    QCOMPARE(withDefaultMonProps, withoutMonProps);
}

// The core of the invert/range feature: a fixture's own MonitorProperties
// entry must flip the sign and apply the position range of exactly the axis
// its flag/range addresses, leaving the raw-to-delta conversion of every
// other axis (and every other fixture) untouched. Position no longer reads
// DmxScale/RotationScale at all - it uses its own independent range.
void FixtureUtils_Test::fixturePositionDeltaAppliesPerFixtureInvertAndRange()
{
    Doc doc(this);
    Fixture *fixture = createSingleChannelFixture(&doc, QLCChannel::PositionX, 0xFF);
    MonitorProperties monProps;
    monProps.setFixtureFlags(fixture->id(), 0, 0, MonitorProperties::InvertedPositionXFlag);
    monProps.setFixturePositionRange(fixture->id(), 0, 0, 100.0f);

    QVector3D delta = FixtureUtils::fixturePositionDelta(fixture, &monProps);

    float rawDelta = FixtureUtils::positionDeltaFromRaw(0xFF << 8, 100.0f);
    QCOMPARE(delta.x(), rawDelta * -1.0f);
    // Axes with no channel at all must stay exactly 0 regardless of invert/range.
    QCOMPARE(delta.y(), 0.0f);
    QCOMPARE(delta.z(), 0.0f);
}

// Same guarantee as above, for RotationX/Y/Z - proving rotation still reads
// the independent rotation scale (unaffected by the position range split).
void FixtureUtils_Test::fixtureRotationDeltaAppliesPerFixtureInvertAndScale()
{
    Doc doc(this);
    Fixture *fixture = createSingleChannelFixture(&doc, QLCChannel::RotationZ, 0xFF);
    MonitorProperties monProps;
    monProps.setFixtureFlags(fixture->id(), 0, 0, MonitorProperties::InvertedRotationZFlag);
    monProps.setFixtureRotationScale(fixture->id(), 0, 0, 0.5f);

    QVector3D delta = FixtureUtils::fixtureRotationDelta(fixture, &monProps);

    float rawDelta = FixtureUtils::rotationDeltaFromRaw(0xFF << 8);
    QCOMPARE(delta.z(), rawDelta * 0.5f * -1.0f);
    QCOMPARE(delta.x(), 0.0f);
    QCOMPARE(delta.y(), 0.0f);
}

// FixtureUtils::invertGroupSelection() backs ContextManager::invertGroupSelection()
// (the "Invert Selection in Group(s)" action). This reproduces the worked
// example from the feature spec exactly: Group A = {1,2,3,4}, Group B =
// {5,6,7}, fixture 8 belongs to no group. Selection = {2,5,8}. Both A and B
// are candidate groups (each has a selected member); the result must be the
// union of their complements relative to the selection - {1,3,4} u {6,7} -
// with fixture 8 (ungrouped) and the originally-selected 2/5 all absent.
void FixtureUtils_Test::invertGroupSelectionWorkedExample()
{
    Doc doc(this);
    QList<Fixture *> fixtures;

    for (int i = 1; i <= 8; i++)
    {
        Fixture *fx = new Fixture(&doc);
        fx->setChannels(1);
        fx->setAddress(i);
        doc.addFixture(fx);
        fixtures.append(fx);
    }

    // fixtures[0..7] correspond to fixture numbers 1..8 in the spec
    FixtureGroup grpA(&doc);
    grpA.assignFixture(fixtures.at(0)->id()); // 1
    grpA.assignFixture(fixtures.at(1)->id()); // 2
    grpA.assignFixture(fixtures.at(2)->id()); // 3
    grpA.assignFixture(fixtures.at(3)->id()); // 4

    FixtureGroup grpB(&doc);
    grpB.assignFixture(fixtures.at(4)->id()); // 5
    grpB.assignFixture(fixtures.at(5)->id()); // 6
    grpB.assignFixture(fixtures.at(6)->id()); // 7

    // fixture 8 (fixtures.at(7)) is deliberately in no group

    MonitorProperties monProps;
    QList<quint32> selected = {
        FixtureUtils::fixtureItemID(fixtures.at(1)->id(), 0, 0), // 2
        FixtureUtils::fixtureItemID(fixtures.at(4)->id(), 0, 0), // 5
        FixtureUtils::fixtureItemID(fixtures.at(7)->id(), 0, 0), // 8
    };

    QList<FixtureGroup *> groups = { &grpA, &grpB };
    QList<quint32> result = FixtureUtils::invertGroupSelection(groups, &monProps, selected);

    QSet<quint32> resultSet(result.begin(), result.end());
    QSet<quint32> expected = {
        FixtureUtils::fixtureItemID(fixtures.at(0)->id(), 0, 0), // 1
        FixtureUtils::fixtureItemID(fixtures.at(2)->id(), 0, 0), // 3
        FixtureUtils::fixtureItemID(fixtures.at(3)->id(), 0, 0), // 4
        FixtureUtils::fixtureItemID(fixtures.at(5)->id(), 0, 0), // 6
        FixtureUtils::fixtureItemID(fixtures.at(6)->id(), 0, 0), // 7
    };

    QCOMPARE(resultSet, expected);
}

// No selection at all means no candidate groups can even be found - this
// must be a strict no-op (empty result), never e.g. "select everything".
void FixtureUtils_Test::invertGroupSelectionNoOpWhenNothingSelected()
{
    Doc doc(this);

    Fixture *fx = new Fixture(&doc);
    fx->setChannels(1);
    fx->setAddress(1);
    doc.addFixture(fx);

    FixtureGroup grp(&doc);
    grp.assignFixture(fx->id());

    MonitorProperties monProps;
    QList<FixtureGroup *> groups = { &grp };

    QVERIFY(FixtureUtils::invertGroupSelection(groups, &monProps, QList<quint32>()).isEmpty());
}

// A selected fixture belonging to no group must not make any group a
// candidate on its own, and must never appear in the result either.
void FixtureUtils_Test::invertGroupSelectionDropsUngroupedFixture()
{
    Doc doc(this);

    Fixture *fxGrouped = new Fixture(&doc);
    fxGrouped->setChannels(1);
    fxGrouped->setAddress(1);
    doc.addFixture(fxGrouped);

    Fixture *fxUngrouped = new Fixture(&doc);
    fxUngrouped->setChannels(1);
    fxUngrouped->setAddress(2);
    doc.addFixture(fxUngrouped);

    FixtureGroup grp(&doc);
    grp.assignFixture(fxGrouped->id());

    MonitorProperties monProps;
    // Only the ungrouped fixture is selected - no group has a selected
    // member, so there are no candidate groups at all.
    QList<quint32> selected = { FixtureUtils::fixtureItemID(fxUngrouped->id(), 0, 0) };
    QList<FixtureGroup *> groups = { &grp };

    QVERIFY(FixtureUtils::invertGroupSelection(groups, &monProps, selected).isEmpty());
}

// A candidate group where every member is already selected has an empty
// complement - it must contribute nothing to the result (not be treated as
// an error or fall back to something else).
void FixtureUtils_Test::invertGroupSelectionEmptyWhenGroupFullySelected()
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
    QList<FixtureGroup *> groups = { &grp };

    QVERIFY(FixtureUtils::invertGroupSelection(groups, &monProps, selected).isEmpty());
}

// FixtureUtils::candidateGroupsForSelection() backs the "Invert Selection in
// Group(s)" dialog's disambiguation step (ContextManager::invertGroupSelection()):
// it must isolate "does this group have >=1 selected member" from the actual
// complement computation, so the caller can decide whether to show a dialog
// (more than one candidate) before ever touching the selection.
void FixtureUtils_Test::candidateGroupsForSelectionFindsSingleCandidate()
{
    Doc doc(this);

    Fixture *fxGrouped = new Fixture(&doc);
    fxGrouped->setChannels(1);
    fxGrouped->setAddress(1);
    doc.addFixture(fxGrouped);

    FixtureGroup grp(&doc);
    grp.assignFixture(fxGrouped->id());

    MonitorProperties monProps;
    QList<quint32> selected = { FixtureUtils::fixtureItemID(fxGrouped->id(), 0, 0) };
    QList<FixtureGroup *> groups = { &grp };

    QList<FixtureGroup *> candidates = FixtureUtils::candidateGroupsForSelection(groups, &monProps, selected);

    QCOMPARE(candidates.count(), 1);
    QCOMPARE(candidates.first(), &grp);
}

// Same worked example as invertGroupSelectionWorkedExample(): both Group A
// and Group B have a selected member, so both must come back as candidates
// (this is exactly the "more than one candidate" case the new dialog exists
// for).
void FixtureUtils_Test::candidateGroupsForSelectionFindsMultipleCandidates()
{
    Doc doc(this);
    QList<Fixture *> fixtures;

    for (int i = 1; i <= 8; i++)
    {
        Fixture *fx = new Fixture(&doc);
        fx->setChannels(1);
        fx->setAddress(i);
        doc.addFixture(fx);
        fixtures.append(fx);
    }

    FixtureGroup grpA(&doc);
    grpA.assignFixture(fixtures.at(0)->id()); // 1
    grpA.assignFixture(fixtures.at(1)->id()); // 2
    grpA.assignFixture(fixtures.at(2)->id()); // 3
    grpA.assignFixture(fixtures.at(3)->id()); // 4

    FixtureGroup grpB(&doc);
    grpB.assignFixture(fixtures.at(4)->id()); // 5
    grpB.assignFixture(fixtures.at(5)->id()); // 6
    grpB.assignFixture(fixtures.at(6)->id()); // 7

    MonitorProperties monProps;
    QList<quint32> selected = {
        FixtureUtils::fixtureItemID(fixtures.at(1)->id(), 0, 0), // 2
        FixtureUtils::fixtureItemID(fixtures.at(4)->id(), 0, 0), // 5
        FixtureUtils::fixtureItemID(fixtures.at(7)->id(), 0, 0), // 8 (ungrouped)
    };

    QList<FixtureGroup *> groups = { &grpA, &grpB };
    QList<FixtureGroup *> candidates = FixtureUtils::candidateGroupsForSelection(groups, &monProps, selected);

    QCOMPARE(candidates.count(), 2);
    QVERIFY(candidates.contains(&grpA));
    QVERIFY(candidates.contains(&grpB));
}

// No selection at all means no group can possibly be a candidate.
void FixtureUtils_Test::candidateGroupsForSelectionEmptyWhenNothingSelected()
{
    Doc doc(this);

    Fixture *fx = new Fixture(&doc);
    fx->setChannels(1);
    fx->setAddress(1);
    doc.addFixture(fx);

    FixtureGroup grp(&doc);
    grp.assignFixture(fx->id());

    MonitorProperties monProps;
    QList<FixtureGroup *> groups = { &grp };

    QVERIFY(FixtureUtils::candidateGroupsForSelection(groups, &monProps, QList<quint32>()).isEmpty());
}

// A selected fixture belonging to no group must not make any group a
// candidate.
void FixtureUtils_Test::candidateGroupsForSelectionEmptyWhenSelectionUngrouped()
{
    Doc doc(this);

    Fixture *fxGrouped = new Fixture(&doc);
    fxGrouped->setChannels(1);
    fxGrouped->setAddress(1);
    doc.addFixture(fxGrouped);

    Fixture *fxUngrouped = new Fixture(&doc);
    fxUngrouped->setChannels(1);
    fxUngrouped->setAddress(2);
    doc.addFixture(fxUngrouped);

    FixtureGroup grp(&doc);
    grp.assignFixture(fxGrouped->id());

    MonitorProperties monProps;
    QList<quint32> selected = { FixtureUtils::fixtureItemID(fxUngrouped->id(), 0, 0) };
    QList<FixtureGroup *> groups = { &grp };

    QVERIFY(FixtureUtils::candidateGroupsForSelection(groups, &monProps, selected).isEmpty());
}

// This is the exact mechanism the new "Invert Selection in Group(s)" dialog
// relies on: when the user unchecks Group B in the disambiguation dialog,
// ContextManager calls FixtureUtils::invertGroupSelection() again with just
// { grpA } instead of the full candidate list. Confirms that pre-filtering
// $groups down to a subset scopes the result to that subset's own
// complement, with zero change needed in invertGroupSelection() itself.
void FixtureUtils_Test::invertGroupSelectionOnPreFilteredSubsetMatchesScopedCandidate()
{
    Doc doc(this);
    QList<Fixture *> fixtures;

    for (int i = 1; i <= 7; i++)
    {
        Fixture *fx = new Fixture(&doc);
        fx->setChannels(1);
        fx->setAddress(i);
        doc.addFixture(fx);
        fixtures.append(fx);
    }

    FixtureGroup grpA(&doc);
    grpA.assignFixture(fixtures.at(0)->id()); // 1
    grpA.assignFixture(fixtures.at(1)->id()); // 2
    grpA.assignFixture(fixtures.at(2)->id()); // 3
    grpA.assignFixture(fixtures.at(3)->id()); // 4

    FixtureGroup grpB(&doc);
    grpB.assignFixture(fixtures.at(4)->id()); // 5
    grpB.assignFixture(fixtures.at(5)->id()); // 6
    grpB.assignFixture(fixtures.at(6)->id()); // 7

    MonitorProperties monProps;
    QList<quint32> selected = {
        FixtureUtils::fixtureItemID(fixtures.at(1)->id(), 0, 0), // 2
        FixtureUtils::fixtureItemID(fixtures.at(4)->id(), 0, 0), // 5
    };

    // Both groups are candidates against the full selection...
    QList<FixtureGroup *> allGroups = { &grpA, &grpB };
    QList<FixtureGroup *> candidates =
        FixtureUtils::candidateGroupsForSelection(allGroups, &monProps, selected);
    QCOMPARE(candidates.count(), 2);

    // ...but calling invertGroupSelection() with just { grpA } (as if the
    // user unchecked Group B in the dialog) must produce only grpA's own
    // complement, exactly as if Group B didn't exist at all.
    QList<FixtureGroup *> onlyGrpA = { &grpA };
    QList<quint32> result = FixtureUtils::invertGroupSelection(onlyGrpA, &monProps, selected);

    QSet<quint32> resultSet(result.begin(), result.end());
    QSet<quint32> expected = {
        FixtureUtils::fixtureItemID(fixtures.at(0)->id(), 0, 0), // 1
        FixtureUtils::fixtureItemID(fixtures.at(2)->id(), 0, 0), // 3
        FixtureUtils::fixtureItemID(fixtures.at(3)->id(), 0, 0), // 4
    };

    QCOMPARE(resultSet, expected);
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
