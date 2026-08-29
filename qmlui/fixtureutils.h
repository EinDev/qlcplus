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

    /** Calculate the gobo wheel speed depending on $cap preset */
    static bool goboTiming(const QLCCapability *cap, uchar value, int &speed);

    /** Calculate the rise/fall periods for a shutter channel $ch, considering presets */
    static int shutterTimings(const QLCChannel *ch, uchar value, int &highTime, int &lowTime);
};

#endif // FIXTUREUTILS_H
