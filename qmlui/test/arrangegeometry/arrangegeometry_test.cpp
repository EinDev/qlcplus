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
#include <QPointF>
#include <QVector>
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

// Mirrors ContextManager::detectedCircleDiameter's math.
static qreal detectedDiameter(const QVector<QPointF> &localPoints)
{
    qreal sumRadius = 0;
    for (const QPointF &p : localPoints)
        sumRadius += qSqrt(p.x() * p.x() + p.y() * p.y());

    return (sumRadius / localPoints.count()) * 2.0;
}

// Mirrors ContextManager::detectedLineFit's PCA math.
static void detectedLine(const QVector<QPointF> &localPoints, qreal &angleDegrees, qreal &length)
{
    qreal sxx = 0, syy = 0, sxy = 0;
    for (const QPointF &p : localPoints)
    {
        sxx += p.x() * p.x();
        syy += p.y() * p.y();
        sxy += p.x() * p.y();
    }

    qreal angleRad = 0.5 * qAtan2(2.0 * sxy, sxx - syy);
    qreal dirH = qCos(angleRad);
    qreal dirV = qSin(angleRad);

    qreal minProj = 0, maxProj = 0;
    for (int i = 0; i < localPoints.count(); i++)
    {
        qreal proj = localPoints.at(i).x() * dirH + localPoints.at(i).y() * dirV;
        if (i == 0 || proj < minProj)
            minProj = proj;
        if (i == 0 || proj > maxProj)
            maxProj = proj;
    }

    angleDegrees = qRadiansToDegrees(angleRad);
    length = maxProj - minProj;
}

// Mirrors ContextManager::faceFixtureTowards's bearing math: the angle (in
// the same "0 = +h axis, 90 = +v axis" convention circlePoint()/linePoint()
// place points with) from a point back towards the origin (the centroid,
// since all the geometry above is expressed relative to it already).
static qreal bearingToCenterDegrees(const QPointF &localPoint)
{
    return qRadiansToDegrees(qAtan2(-localPoint.y(), -localPoint.x()));
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

void ArrangeGeometry_Test::detectedDiameterMatchesKnownRadius()
{
    // Same 4 points as circleFourPointsAreNinetyDegreesApart(), radius 50 ->
    // diameter 100. Every point is exactly on the circle, so the average
    // radius is exact, not just approximate.
    QVector<QPointF> points;
    for (int i = 0; i < 4; i++)
    {
        QVector3D p = circlePoint(100.0, i, 4);
        points.append(QPointF(p.x(), p.y()));
    }

    QVERIFY(qAbs(detectedDiameter(points) - 100.0) < EPS);
}

void ArrangeGeometry_Test::detectedLineMatchesKnownAngleAndLength()
{
    // 5 fixtures on a 200mm line angled 40 degrees - within the PCA angle's
    // (-90, 90] principal range, so no ambiguity with the equivalent
    // 40 - 180 = -140 degree orientation of the same line.
    QVector<QPointF> points;
    for (int i = 0; i < 5; i++)
    {
        QVector3D p = linePoint(200.0, i, 5, 40.0);
        points.append(QPointF(p.x(), p.y()));
    }

    qreal angleDegrees, length;
    detectedLine(points, angleDegrees, length);

    // Unlike the 90-degree-multiple test data elsewhere in this file (which
    // lands on exact cos/sin values), 40 degrees doesn't - and QVector3D
    // (linePoint()'s return type) stores components as single-precision
    // float, so the round-trip through it picks up ~1e-5 rounding noise
    // that a tighter tolerance would flag as a false failure.
    QVERIFY(qAbs(angleDegrees - 40.0) < 1e-3);
    QVERIFY(qAbs(length - 200.0) < 1e-3);
}

void ArrangeGeometry_Test::bearingToCenterIsOppositeOfCirclePlacementAngle()
{
    // A fixture placed at 30 degrees around the circle sits at direction
    // (cos 30, sin 30) from the centroid, so it must face back along
    // (-cos 30, -sin 30) - i.e. 30 + 180 = 210 degrees - to look at the centre.
    qreal angleRad = qDegreesToRadians(30.0);
    QPointF onCircle(50.0 * qCos(angleRad), 50.0 * qSin(angleRad));

    qreal bearing = bearingToCenterDegrees(onCircle);
    qreal expected = 30.0 + 180.0;
    // normalize both to [0, 360) before comparing
    if (bearing < 0) bearing += 360.0;
    if (expected >= 360.0) expected -= 360.0;

    QVERIFY(qAbs(bearing - expected) < 1e-6);
}

QTEST_MAIN(ArrangeGeometry_Test)
