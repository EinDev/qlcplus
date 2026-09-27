/*
  Q Light Controller Plus
  monitorlayout.h

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

#ifndef MONITORLAYOUT_H
#define MONITORLAYOUT_H

#include <QList>
#include <QMatrix4x4>
#include <QPointF>
#include <QSizeF>
#include <QVector3D>

class Doc;
class Fixture;
class QLCFixtureMode;
class MonitorProperties;

/** @addtogroup engine Engine
 * @{
 */

/**
 * Host-free geometry behind the 2D/3D preview's fixture placement tools:
 * the point-of-view projections (FixtureUtils in qmlui delegates here), the
 * "Arrange fixtures" circle / grid / line layouts, align / distribute,
 * rotate around the centroid, move to the stage centre, the "detect from
 * placement" estimates and the "Pick a 3D point" pan/tilt maths.
 *
 * Everything is a pure function over a plain list of Item - no Doc signals,
 * no Tardis, no view objects - so qmlui's ContextManager (which adds undo,
 * DMX-driven positions and view refreshes around it) and the Control API's
 * ApiMonitorDomain (fixtures.monitor.arrange / aimAt) run the exact same
 * code and produce identical results. Positions are millimetres in the
 * document space MonitorProperties stores (X = width, Y = height/up,
 * Z = depth), rotations are degrees, angles given to the ops are degrees.
 */
class MonitorLayout final
{
public:
    /** One fixture preview item (a fixture, or one head / linked copy of it). */
    struct Item
    {
        quint32 fixtureId = 0;
        quint16 headIndex = 0;
        quint16 linkedIndex = 0;
        /** Opaque host-defined key carried through unchanged (qmlui stores its
         *  packed itemID here so it can map results back without re-encoding). */
        quint32 hostKey = 0;
        QVector3D position;   ///< mm, document space
        QVector3D rotation;   ///< degrees
        /** Footprint in the current point of view (mm) - only distribute()
         *  reads it; fill it with item2DDimension(). */
        QSizeF size2D = QSizeF(300, 300);
        /** MonitorProperties::LockedFlag - the arrange / rotate / centre ops
         *  leave locked items where they are (their slot in the layout stays
         *  empty), exactly like the Qt UI. */
        bool locked = false;
        /** Set by the ops on the returned copies: which fields they changed. */
        bool positionChanged = false;
        bool rotationChanged = false;
    };

    /** The two on-screen axes (h = screen X, v = screen Y) and the depth axis
     *  of a point of view, as indices into QVector3D (0 = X, 1 = Y, 2 = Z). */
    static void planeAxes(int pointOfView, int &hAxis, int &vAxis, int &dAxis);
    static qreal axisValue(const QVector3D &v, int axis);
    static void setAxisValue(QVector3D &v, int axis, qreal value);

    /** Mean position of the items (zero vector for an empty list). */
    static QVector3D centroid(const QList<Item> &items);

    /********************************************************************
     * Point-of-view projections (the former FixtureUtils statics)
     ********************************************************************/

    /** Projects a doc-space position onto the 2D view's screen plane for
     *  the given point of view. See FixtureUtils::item2DPosition's doc for
     *  the per-view conventions. */
    static QPointF item2DPosition(const MonitorProperties *monProps, int pointOfView, QVector3D pos);
    static float item2DRotation(int pointOfView, QVector3D rot);
    /** Physical footprint of a mode in the 2D view's plane (300 mm defaults
     *  for missing dimensions). */
    static QSizeF item2DDimension(const QLCFixtureMode *fxMode, int pointOfView);
    /** Qt::AlignLeft / Qt::AlignTop origPos onto refPos in the view plane. */
    static void alignItem(QVector3D refPos, QVector3D &origPos, int pointOfView, int alignment);
    /** Exact inverse of item2DPosition() for the monitor's current POV. */
    static QVector3D item3DPosition(const MonitorProperties *monProps, QPointF point, float thirdVal);
    /** Centre of the stage grid in mm. */
    static QVector3D gridCenterPosition(const MonitorProperties *monProps);

    /********************************************************************
     * Ordering
     ********************************************************************/

    /** The Qt UI's default arrangement order: Fixture::operator< (DMX
     *  address), then head index, then linked index. Items whose fixture no
     *  longer exists keep their relative order at the end. */
    static QList<Item> sortedByAddress(Doc *doc, const QList<Item> &items);
    /** The order arrangeGrid() uses: if a single Fixture Group contains
     *  every item's fixture, the group's own grid order (row-major), else
     *  sortedByAddress(). */
    static QList<Item> groupOrdered(Doc *doc, const QList<Item> &items);

    /********************************************************************
     * Layout operations - each returns a copy of the input list with the
     * new position / rotation and the *Changed flags set on the items it
     * moved. Callers pass the items already in the order they want.
     ********************************************************************/

    static QList<Item> arrangeCircle(const QList<Item> &items, int pointOfView, qreal diameter, bool lookAtCenter);
    /** columns <= 0 picks a near-square grid. */
    static QList<Item> arrangeGrid(const QList<Item> &items, int pointOfView, qreal width, qreal height,
                                   int columns, qreal angleDegrees);
    static QList<Item> arrangeLine(const QList<Item> &items, int pointOfView, qreal length, qreal angleDegrees,
                                   bool lookAtCenter);
    static QList<Item> rotateAroundCentroid(const QList<Item> &items, int pointOfView, qreal angleDegrees);
    /** Translates every unlocked item so the centroid lands on gridCenter. */
    static QList<Item> moveToCenter(const QList<Item> &items, const QVector3D &gridCenter);
    /** Aligns every item to the FIRST item (Qt::AlignLeft / Qt::AlignTop).
     *  Like the Qt UI, ignores the locked flag. */
    static QList<Item> align(const QList<Item> &items, int pointOfView, int alignment);
    /** Equal gaps between the items along direction (Qt::Horizontal /
     *  Qt::Vertical) using their size2D; needs >= 3 items, the outermost
     *  two stay put. Like the Qt UI, ignores the locked flag. */
    static QList<Item> distribute(const QList<Item> &items, const MonitorProperties *monProps, int direction);

    /** "Detect from placement" - mean distance from the centroid, doubled. */
    static qreal detectedCircleDiameter(const QList<Item> &items, int pointOfView);
    /** "Detect from placement" - principal axis of the position scatter and
     *  the items' extent along it. */
    static void detectedLineFit(const QList<Item> &items, int pointOfView, qreal &angleRadians, qreal &length);

    /********************************************************************
     * Pick a 3D point
     ********************************************************************/

    /** Turns a rotation matrix (MonitorProperties::fixtureRotationMatrix)
     *  into the orthonormal "light matrix" the pan/tilt maths expects. */
    static QMatrix4x4 lightMatrixFromRotation(const QMatrix4x4 &rotMatrix);

    /** Pan / tilt degrees that point fixture's beam (emitted from
     *  lightPos with the head's light matrix) at point. Both vectors must be
     *  in the same space and unit (metres in both hosts; only their
     *  difference matters). itemFlags are the item's MonitorProperties flags
     *  (InvertedPan / InvertedTilt). hasPan / hasTilt tell which of the two
     *  the fixture actually has channels for. Returns false when the fixture
     *  has neither. */
    static bool aimPanTilt(const Fixture *fixture, quint32 itemFlags, const QVector3D &point,
                           const QVector3D &lightPos, const QMatrix4x4 &lightMatrix,
                           bool &hasPan, qreal &panDegrees, bool &hasTilt, qreal &tiltDegrees);

private:
    static void faceTowards(Item &item, const QVector3D &centroid, int hAxis, int vAxis, int dAxis);
};

/** @} */

#endif // MONITORLAYOUT_H
