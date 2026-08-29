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
#include "qlcfixturedef.h"
#include "qlcfixturemode.h"
#include "qlcchannel.h"
#include "fixture.h"
#include "doc.h"

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

// Pin down the documented +/-2.5m default range (anchored to half of
// MonitorProperties' default 3D floor grid width/depth, GRID_DEFAULT_WIDTH =
// GRID_DEFAULT_DEPTH = 5m) at both raw extremes.
void FixtureUtils_Test::positionDeltaAtExtremesMatchesDocumentedRange()
{
    QCOMPARE(FixtureUtils::positionDeltaFromRaw(0), -2.5f);
    QVERIFY(qAbs(FixtureUtils::positionDeltaFromRaw(65535) - 2.5f) < 0.001f);
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
    QCOMPARE(FixtureUtils::positionRawFromDelta(-100.0f), 0);
    QCOMPARE(FixtureUtils::positionRawFromDelta(100.0f), 65535);
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
// read back as exactly 0 (unaffected), never positionDeltaFromRaw(0)'s -2.5m,
// or every axis a profile doesn't define would silently drag the fixture off
// to one side.
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

// The core of the invert/scale feature: a fixture's own MonitorProperties
// entry must flip the sign and apply the multiplier of exactly the axis its
// flag/scale addresses, leaving the raw-to-delta conversion of every other
// axis (and every other fixture) untouched.
void FixtureUtils_Test::fixturePositionDeltaAppliesPerFixtureInvertAndScale()
{
    Doc doc(this);
    Fixture *fixture = createSingleChannelFixture(&doc, QLCChannel::PositionX, 0xFF);
    MonitorProperties monProps;
    monProps.setFixtureFlags(fixture->id(), 0, 0, MonitorProperties::InvertedPositionXFlag);
    monProps.setFixtureDmxScale(fixture->id(), 0, 0, 2.0f);

    QVector3D delta = FixtureUtils::fixturePositionDelta(fixture, &monProps);

    float rawDelta = FixtureUtils::positionDeltaFromRaw(0xFF << 8);
    QCOMPARE(delta.x(), rawDelta * 2.0f * -1.0f);
    // Axes with no channel at all must stay exactly 0 regardless of invert/scale.
    QCOMPARE(delta.y(), 0.0f);
    QCOMPARE(delta.z(), 0.0f);
}

// Same guarantee as above, for RotationX/Y/Z.
void FixtureUtils_Test::fixtureRotationDeltaAppliesPerFixtureInvertAndScale()
{
    Doc doc(this);
    Fixture *fixture = createSingleChannelFixture(&doc, QLCChannel::RotationZ, 0xFF);
    MonitorProperties monProps;
    monProps.setFixtureFlags(fixture->id(), 0, 0, MonitorProperties::InvertedRotationZFlag);
    monProps.setFixtureDmxScale(fixture->id(), 0, 0, 0.5f);

    QVector3D delta = FixtureUtils::fixtureRotationDelta(fixture, &monProps);

    float rawDelta = FixtureUtils::rotationDeltaFromRaw(0xFF << 8);
    QCOMPARE(delta.z(), rawDelta * 0.5f * -1.0f);
    QCOMPARE(delta.x(), 0.0f);
    QCOMPARE(delta.y(), 0.0f);
}

QTEST_APPLESS_MAIN(FixtureUtils_Test)
