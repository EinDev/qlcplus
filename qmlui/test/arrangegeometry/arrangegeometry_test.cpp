/*
  Q Light Controller - Unit test
  arrangegeometry_test.cpp

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

#include <QtTest/QtTest>
#include <QVector3D>
#include <qmath.h>

#include "arrangegeometry_test.h"

/*
  ContextManager can't be constructed in a test (needs a live QQuickView*
  and a real Doc*, see CLAUDE.md's "Unit testing" section), so the placement
  formulas from ContextManager::arrangeFixturesInCircle/InGrid/InLine
  (qmlui/contextmanager.cpp) are faithfully reimplemented here as free
  functions, byte-for-byte matching the source math. If either drifts from
  the source, update both sides together.

  All three helpers below place a point in "local" (h, v) plane coordinates
  relative to the selection's centroid, exactly like the real functions do
  via vecAxis()/setVecAxis() against whichever hAxis/vAxis fixturePlaneAxes()
  picked for the current 2D/3D viewpoint. These tests only exercise the
  math itself, so they fix (hAxis, vAxis) = (x, y) - the axis mapping
  fixturePlaneAxes() uses for MonitorProperties::Undefined/FrontView - and
  work entirely in the local plane; the real code's extra step of adding
  this offset onto a (possibly non-zero) 3D centroid on whichever axes the
  viewpoint picked is not itself under test here.
*/

// Mirrors ContextManager::arrangeFixturesInCircle's per-point math.
static QVector3D circlePoint(qreal diameter, int i, int count)
{
    qreal radius = diameter / 2.0;
    qreal angleRad = qDegreesToRadians(360.0 * i / count);
    return QVector3D(radius * qCos(angleRad), radius * qSin(angleRad), 0);
}

// Mirrors ContextManager::arrangeFixturesInGrid's per-point math. Takes
// $rows explicitly (the source derives it as qCeil(count / columns) from
// the caller's fixture count - tests pass it directly for clarity).
static QVector3D gridPointRows(qreal width, qreal height, int columns, int rows, int i, qreal angleDegrees)
{
    qreal colStep = columns > 1 ? width / (columns - 1) : 0;
    qreal rowStep = rows > 1 ? height / (rows - 1) : 0;
    qreal angleRad = qDegreesToRadians(angleDegrees);

    int col = i % columns;
    int row = i / columns;
    qreal h = -width / 2.0 + col * colStep;
    qreal v = -height / 2.0 + row * rowStep;

    qreal rh = h * qCos(angleRad) - v * qSin(angleRad);
    qreal rv = h * qSin(angleRad) + v * qCos(angleRad);

    return QVector3D(rh, rv, 0);
}

// Mirrors ContextManager::arrangeFixturesInLine's per-point math.
static QVector3D linePoint(qreal length, int i, int count, qreal angleDegrees)
{
    qreal angleRad = qDegreesToRadians(angleDegrees);
    qreal step = count > 1 ? length / (count - 1) : 0;
    qreal startOffset = -length / 2.0;
    qreal dist = startOffset + i * step;

    return QVector3D(dist * qCos(angleRad), dist * qSin(angleRad), 0);
}

static const qreal EPS = 1e-9;

void ArrangeGeometry_Test::circleFourPointsAreNinetyDegreesApart()
{
    // 4 fixtures on a 100mm-diameter circle -> radius 50, spaced 90 degrees
    // apart starting at angle 0 (along +X), going counter-clockwise into +Y.
    QVector3D p0 = circlePoint(100.0, 0, 4);
    QVector3D p1 = circlePoint(100.0, 1, 4);
    QVector3D p2 = circlePoint(100.0, 2, 4);
    QVector3D p3 = circlePoint(100.0, 3, 4);

    QVERIFY(qAbs(p0.x() - 50.0) < EPS && qAbs(p0.y() - 0.0) < EPS);
    QVERIFY(qAbs(p1.x() - 0.0) < EPS  && qAbs(p1.y() - 50.0) < EPS);
    QVERIFY(qAbs(p2.x() - (-50.0)) < EPS && qAbs(p2.y() - 0.0) < EPS);
    QVERIFY(qAbs(p3.x() - 0.0) < EPS  && qAbs(p3.y() - (-50.0)) < EPS);

    // All four points are equidistant from the centre (the circle's radius).
    for (const QVector3D &p : {p0, p1, p2, p3})
        QVERIFY(qAbs(p.length() - 50.0) < EPS);
}

void ArrangeGeometry_Test::gridUnrotatedOffsets()
{
    // 6 fixtures, 3 columns x 2 rows, 200x100mm, no rotation.
    // colStep = 200/2 = 100, rowStep = 100/1 = 100.
    // Column offsets from -100..+100, row offsets from -50..+50.
    QVector3D p0 = gridPointRows(200.0, 100.0, 3, 2, 0, 0.0); // col 0, row 0
    QVector3D p2 = gridPointRows(200.0, 100.0, 3, 2, 2, 0.0); // col 2, row 0
    QVector3D p3 = gridPointRows(200.0, 100.0, 3, 2, 3, 0.0); // col 0, row 1
    QVector3D p5 = gridPointRows(200.0, 100.0, 3, 2, 5, 0.0); // col 2, row 1

    QVERIFY(qAbs(p0.x() - (-100.0)) < EPS && qAbs(p0.y() - (-50.0)) < EPS);
    QVERIFY(qAbs(p2.x() - 100.0) < EPS    && qAbs(p2.y() - (-50.0)) < EPS);
    QVERIFY(qAbs(p3.x() - (-100.0)) < EPS && qAbs(p3.y() - 50.0) < EPS);
    QVERIFY(qAbs(p5.x() - 100.0) < EPS    && qAbs(p5.y() - 50.0) < EPS);
}

void ArrangeGeometry_Test::gridRotated90SwapsAxes()
{
    // Same 6-fixture grid, rotated 90 degrees: a CCW rotation maps
    // (h, v) -> (-v, h), so column spread becomes vertical and row spread
    // becomes horizontal.
    QVector3D p0 = gridPointRows(200.0, 100.0, 3, 2, 0, 90.0); // unrotated (-100,-50)
    QVector3D p5 = gridPointRows(200.0, 100.0, 3, 2, 5, 90.0); // unrotated (100, 50)

    QVERIFY(qAbs(p0.x() - 50.0) < EPS  && qAbs(p0.y() - (-100.0)) < EPS);
    QVERIFY(qAbs(p5.x() - (-50.0)) < EPS && qAbs(p5.y() - 100.0) < EPS);
}

void ArrangeGeometry_Test::gridRotated180Mirrors()
{
    // 180 degrees maps (h, v) -> (-h, -v): every point ends up diametrically
    // opposite its unrotated position.
    QVector3D p0 = gridPointRows(200.0, 100.0, 3, 2, 0, 180.0); // unrotated (-100,-50)
    QVector3D p5 = gridPointRows(200.0, 100.0, 3, 2, 5, 180.0); // unrotated (100, 50)

    QVERIFY(qAbs(p0.x() - 100.0) < EPS && qAbs(p0.y() - 50.0) < EPS);
    QVERIFY(qAbs(p5.x() - (-100.0)) < EPS && qAbs(p5.y() - (-50.0)) < EPS);
}

void ArrangeGeometry_Test::lineEndpointsAndCenter()
{
    // 3 fixtures on a 100mm line, angled 90 degrees (i.e. running along the
    // v axis instead of h): step = 100/2 = 50, so positions land at
    // v = -50, 0, +50 with h essentially 0 throughout.
    QVector3D p0 = linePoint(100.0, 0, 3, 90.0);
    QVector3D p1 = linePoint(100.0, 1, 3, 90.0);
    QVector3D p2 = linePoint(100.0, 2, 3, 90.0);

    QVERIFY(qAbs(p0.x() - 0.0) < EPS && qAbs(p0.y() - (-50.0)) < EPS);
    QVERIFY(qAbs(p1.x() - 0.0) < EPS && qAbs(p1.y() - 0.0) < EPS); // exact centre
    QVERIFY(qAbs(p2.x() - 0.0) < EPS && qAbs(p2.y() - 50.0) < EPS);
}

QTEST_MAIN(ArrangeGeometry_Test)
