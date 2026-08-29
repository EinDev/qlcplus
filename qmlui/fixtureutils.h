/*
  Q Light Controller Plus
  fixtureutils.h

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

#ifndef FIXTUREUTILS_H
#define FIXTUREUTILS_H

#include <QColor>
#include <QList>
#include <QMatrix4x4>
#include <QPointF>
#include <QVector3D>

class Doc;
class Fixture;
class FixtureGroup;
class QLCChannel;
class QLCCapability;
class QLCFixtureMode;
class MonitorProperties;

class FixtureUtils final
{
public:
    FixtureUtils();

    /** Returns a unique item ID composed by the provided parameters */
    static quint32 fixtureItemID(quint32 fid, quint16 headIndex, quint16 linkedIndex);
    /** Returns a Fixture ID from the given itemID composite ID */
    static quint32 itemFixtureID(quint32 itemID);
    /** Returns the head index from the given itemID composite ID */
    static quint16 itemHeadIndex(quint32 itemID);
    /** Returns the linked index from the given itemID composite ID */
    static quint16 itemLinkedIndex(quint32 itemID);

    /** Projects a fixture's 3D doc-space position (mm; X=width, Y=height/up,
     *  Z=depth) onto the 2D coordinates of one of the 2D view's points of view.
     *  Each case is a different, easy-to-invert combination: TopView keeps X and
     *  maps doc Z onto screen Y, dropping height (Y) entirely - a fixture's
     *  height never affects its Top View position. Front/RightSide/LeftSide
     *  views instead use doc Y, but flip it (gridSize.y * units - pos.y) since
     *  screen Y grows downward while doc height grows upward; RightSideView
     *  additionally flips X the same way, using doc Z. Mixing up which axis a
     *  view drops vs. flips vs. keeps as-is silently mispositions fixtures in
     *  just that one view, e.g. see MainView3D::updateFixturePosition()/
     *  updateFixtureRotation() (mainview3d.cpp) for the 3D side of the same
     *  fixture, which follows its own related-but-different convention: X/Z are
     *  centered on the grid (gridMeters/2 subtracted) but Y is not, and all
     *  three rotation angles are applied negated relative to the degrees doc
     *  value passed in. */
    static QPointF item2DPosition(const MonitorProperties *monProps, int pointOfView, QVector3D pos);
    static float item2DRotation(int pointOfView, QVector3D rot);
    static QSizeF item2DDimension(const QLCFixtureMode *fxMode, int pointOfView);
    static void alignItem(QVector3D refPos, QVector3D &origPos, int pointOfView, int alignment);

    static QVector3D item3DPosition(const MonitorProperties *monProps, QPointF point, float thirdVal);

    /** Returns true if every fixture belonging to $group - expanded to every
     *  one of its heads/linked sub-items via $monProps - has its itemID
     *  present in $selectedFixtures. A null or empty group is never
     *  considered fully selected. */
    static bool isGroupFullySelected(const FixtureGroup *group, const MonitorProperties *monProps,
                                      const QList<quint32> &selectedFixtures);

    /** Computes the "Invert Selection in Group(s)" result for the current
     *  fixture selection: every Fixture Group in $groups that has at least
     *  one currently-selected member ("candidate group") contributes its own
     *  complement - the itemIDs (expanded to heads/linked sub-items via
     *  $monProps) that belong to that group but are NOT in $selectedFixtures.
     *  The return value is the union of these per-group complements across
     *  every candidate group (deduplicated). A fixture selected but belonging
     *  to no group contributes nothing and never appears in the result.
     *  Returns an empty list if $selectedFixtures is empty or no group has a
     *  selected member. */
    static QList<quint32> invertGroupSelection(const QList<FixtureGroup *> &groups,
                                                const MonitorProperties *monProps,
                                                const QList<quint32> &selectedFixtures);

    /** Returns the first available space (in mm) for a rectangle
     * of the given width and height.
     * This method works with the monitor properties and the fixtures list */
    static QPointF available2DPosition(Doc *doc, int pointOfView, QRectF fxRect);

    /** Perform a linear blending of $b over $a with the given $mix amount */
    static QColor blendColors(QColor a, QColor b, float mix);

    /** Return the color of the head with $headIndex of $fixture.
     *  This considers: RGB / CMY / WAUVLI channels, dimmers and gel color */
    static QColor headColor(Fixture *fixture, int headIndex = 0);

    static QColor applyColorFilter(QColor source, QColor filter);

    /** Returns the file name of the generic 3D mesh used to draw the given
     *  fixture, which doubles as the resource key for its LightEmitter data.
     *  Empty for fixture types drawn without a mesh (LED bars, which are built
     *  procedurally from the fixture's own layout).
     *
     *  Single source of truth: MainView3D picks the mesh to load with it, and
     *  anything needing a fixture's real drawn geometry resolves the same file
     *  through it. */
    static QString fixtureLightResource(const Fixture *fixture);

    /** Reconstruct persisted light properties without requiring the 3D scene graph.
     *  Returns true when persisted metadata is available for the fixture model/head. */
    static bool lightProperties(const MonitorProperties *monProps, const Fixture *fixture,
                                int headIndex, QVector3D &lightPos, QMatrix4x4 &lightMatrix);

    /** Calculate the pan/tilt speed depending on the $ch preset */
    static void positionTimings(const QLCChannel *ch, uchar value, int &panDuration, int &tiltDuration);

    /** Accumulate one scanned channel's raw byte value onto a running 16-bit
     *  accumulator for whatever channel group it belongs to, using the same
     *  MSB/LSB convention as the existing Pan/Tilt scan in
     *  MainView3D::updateFixtureItem() (mainview3d.cpp, ~line 1438-1459): a
     *  channel with controlByte() == MSB contributes (value << 8), one with
     *  controlByte() == LSB contributes value as-is - both added onto
     *  whatever total the group has accumulated so far this scan.
     *
     *  Deliberately a plain stateless function, not a stateful accumulator
     *  class: a caller scanning a fixture's channels for the 9 Position/
     *  Rotation/Scale axes at once keeps one int per axis and calls this once
     *  per matching channel inside its own switch(ch->group()), exactly like
     *  the existing Pan/Tilt code does today - e.g.
     *  "case QLCChannel::PositionX: posX = accumulateChannelGroupValue(ch, value, posX); break;"
     *  If a group has no LSB channel at all, the result naturally lands as a
     *  multiple of 256 within the same 0-65535 domain (identical to how a
     *  coarse-only Pan/Tilt channel behaves today) - no separate 8-bit code
     *  path is needed, and none of the conversion helpers below need one either. */
    static int accumulateChannelGroupValue(const QLCChannel *ch, uchar value, int currentAccumulator);

    /** Convert an accumulated 16-bit (0-65535) PositionX/PositionY/PositionZ
     *  raw value to a world-space delta, in the same units MainView3D uses
     *  for fixture position (meters - see phy.width() / 1000.0 etc in
     *  mainview3d.cpp). DMX 32768 (mid-range) = delta 0, i.e. the fixture's
     *  own placed rest position; DMX 0 = the most-negative delta; DMX 65535 =
     *  the most-positive delta.
     *
     *  Default range is +/-2.5m, i.e. HALF of MonitorProperties' default 3D
     *  floor grid width/depth (GRID_DEFAULT_WIDTH = GRID_DEFAULT_DEPTH = 5m,
     *  monitorproperties.cpp) - so the full DMX range moves a fixture across
     *  one default-sized floor tile's width in either direction from its
     *  rest position, which is a sensible span for a generic "moving
     *  fixture" with no per-profile physical property to size this from
     *  (unlike Pan/Tilt's focusPanMax()/focusTiltMax()). */
    static float positionDeltaFromRaw(int raw);
    /** Inverse of positionDeltaFromRaw(): world-space delta -> raw 0-65535,
     *  clamped to that range. Round-trips through positionDeltaFromRaw()
     *  within +/-1 raw unit of quantization error. */
    static int positionRawFromDelta(float delta);

    /** Convert an accumulated 16-bit RotationX/RotationY/RotationZ raw value
     *  to a rotation delta in degrees. Modelled on how Pan/Tilt fall back to
     *  a fixed range when a fixture provides no matching physical property
     *  ("phy.focusPanMax() ? phy.focusPanMax() : 360", mainview3d.cpp:1146-1147)
     *  - the Position/Rotation/Scale groups have no equivalent physical-
     *  property field at all, so this always uses a fixed 540 degree range
     *  (matching the "540 Degrees" rotation channels documented for the
     *  user's own real-world "Mobile Truss" fixture profile), centered at 0:
     *  DMX 32768 = 0 degrees, DMX 0 = -270, DMX 65535 = +270. */
    static float rotationDeltaFromRaw(int raw);
    /** Inverse of rotationDeltaFromRaw(): degrees -> raw 0-65535, clamped to
     *  that range. Round-trips through rotationDeltaFromRaw() within +/-1 raw
     *  unit of quantization error. */
    static int rotationRawFromDelta(float degrees);

    /** Convert an accumulated 16-bit ScaleX/ScaleY/ScaleZ raw value to a
     *  linear scale factor: DMX 0 = 0.1x, DMX 65535 = 3.0x. */
    static float scaleFactorFromRaw(int raw);
    /** Inverse of scaleFactorFromRaw(): scale factor -> raw 0-65535, clamped
     *  to that range. Round-trips through scaleFactorFromRaw() within +/-1
     *  raw unit of quantization error. */
    static int scaleRawFromFactor(float factor);

    /** Scans $fixture's current channel values for PositionX/Y/Z and returns
     *  the resulting world-space delta (meters - see positionDeltaFromRaw()),
     *  one axis at a time via accumulateChannelGroupValue(). An axis with no
     *  matching channel on the fixture contributes 0 (no offset) rather than
     *  positionDeltaFromRaw(0)'s -2.5m - "no channel" and "channel currently
     *  at DMX 0" are different things and must not be conflated.
     *
     *  If $monProps is non-null, $fixture's per-fixture DMX-to-view invert
     *  flags (MonitorProperties::InvertedPosition{X,Y,Z}Flag) and scale
     *  multiplier (MonitorProperties::fixtureDmxScale()) are applied on top
     *  of the raw-to-delta conversion, per axis: delta = rawDelta * scale *
     *  (-1 if inverted). $monProps defaults to null for callers (and the
     *  existing unit tests) that only care about the un-adjusted global
     *  conversion; every real caller in qmlui passes its own MonitorProperties
     *  so the setting actually takes effect - see ContextManager::
     *  pushPositionDelta() for the exact-inverse write-back direction, which
     *  must apply the same scale/invert or a drag will jump/drift. */
    static QVector3D fixturePositionDelta(Fixture *fixture, const MonitorProperties *monProps = nullptr);

    /** Same as fixturePositionDelta(), for RotationX/Y/Z (degrees). An axis
     *  with no matching channel contributes 0 degrees. $monProps applies the
     *  fixture's InvertedRotation{X,Y,Z}Flag and DMX scale the same way. */
    static QVector3D fixtureRotationDelta(Fixture *fixture, const MonitorProperties *monProps = nullptr);

    /** Scans $fixture's current channel values for ScaleX/Y/Z and returns the
     *  resulting linear scale factor per axis (see scaleFactorFromRaw()). An
     *  axis with no matching channel contributes 1.0 (no scaling) rather than
     *  scaleFactorFromRaw(0)'s 0.1x. */
    static QVector3D fixtureScaleFactor(Fixture *fixture);

    /** The stage/grid's own center, in the same millimetre/corner-origin
     *  convention MonitorProperties::fixturePosition() uses (see
     *  MainView3D::updateFixturePosition()'s pos/1000 - gridMeters/2 scene
     *  conversion). Used as the absolute reference point for fixtures whose
     *  PositionX/Y/Z channels are DMX-driven - such a fixture's DMX value 32768
     *  (delta 0, see positionDeltaFromRaw()) means "at the center of the
     *  stage", matching the "50% = center" convention documented for the
     *  user's own real-world fixture profiles, not "wherever it happened to be
     *  manually placed". */
    static QVector3D gridCenterPosition(const MonitorProperties *monProps);

    /** Calculate the gobo wheel speed depending on $cap preset */
    static bool goboTiming(const QLCCapability *cap, uchar value, int &speed);

    /** Calculate the rise/fall periods for a shutter channel $ch, considering presets */
    static int shutterTimings(const QLCChannel *ch, uchar value, int &highTime, int &lowTime);
};

#endif // FIXTUREUTILS_H
