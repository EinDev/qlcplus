/*
  Q Light Controller Plus
  monitorlayout.cpp

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

#include <QMapIterator>
#include <QSet>
#include <QVector4D>
#include <QtMath>
#include <algorithm>

#include "monitorlayout.h"
#include "monitorproperties.h"
#include "qlcfixturemode.h"
#include "qlcphysical.h"
#include "qlcchannel.h"
#include "fixturegroup.h"
#include "grouphead.h"
#include "qlcpoint.h"
#include "fixture.h"
#include "doc.h"

/*****************************************************************************
 * Axes
 *****************************************************************************/

void MonitorLayout::planeAxes(int pointOfView, int &hAxis, int &vAxis, int &dAxis)
{
    switch (pointOfView)
    {
        case MonitorProperties::TopView:
            hAxis = 0; vAxis = 2; dAxis = 1; // X / Z, depth is height (Y)
        break;
        case MonitorProperties::RightSideView:
        case MonitorProperties::LeftSideView:
            hAxis = 2; vAxis = 1; dAxis = 0; // Z / Y, depth is left-right (X)
        break;
        case MonitorProperties::Undefined:
        case MonitorProperties::FrontView:
        default:
            hAxis = 0; vAxis = 1; dAxis = 2; // X / Y, depth is front-back (Z)
        break;
    }
}

qreal MonitorLayout::axisValue(const QVector3D &v, int axis)
{
    return axis == 0 ? v.x() : axis == 1 ? v.y() : v.z();
}

void MonitorLayout::setAxisValue(QVector3D &v, int axis, qreal value)
{
    if (axis == 0)
        v.setX(value);
    else if (axis == 1)
        v.setY(value);
    else
        v.setZ(value);
}

QVector3D MonitorLayout::centroid(const QList<Item> &items)
{
    QVector3D sum(0, 0, 0);
    if (items.isEmpty())
        return sum;

    for (const Item &item : items)
        sum += item.position;

    return sum / items.count();
}

/*****************************************************************************
 * Point-of-view projections
 *****************************************************************************/

QPointF MonitorLayout::item2DPosition(const MonitorProperties *monProps, int pointOfView, QVector3D pos)
{
    QPointF point(0, 0);
    float gridUnits = monProps->gridUnits() == MonitorProperties::Meters ? 1000.0 : 304.8;

    switch (pointOfView)
    {
        case MonitorProperties::TopView:
            point.setX(pos.x());
            point.setY(pos.z());
        break;
        case MonitorProperties::Undefined:
        case MonitorProperties::FrontView:
            point.setX(pos.x());
            point.setY((monProps->gridSize().y() * gridUnits) - pos.y());
        break;
        case MonitorProperties::RightSideView:
            point.setX((monProps->gridSize().x() * gridUnits) - pos.z());
            point.setY((monProps->gridSize().y() * gridUnits) - pos.y());
        break;
        case MonitorProperties::LeftSideView:
            point.setX(pos.z());
            point.setY((monProps->gridSize().y() * gridUnits) - pos.y());
        break;
    }

    return point;
}

float MonitorLayout::item2DRotation(int pointOfView, QVector3D rot)
{
    switch (pointOfView)
    {
        case MonitorProperties::TopView:
            return rot.y();
        case MonitorProperties::RightSideView:
        case MonitorProperties::LeftSideView:
            return rot.x();
        default:
            return rot.z();
    }
}

QSizeF MonitorLayout::item2DDimension(const QLCFixtureMode *fxMode, int pointOfView)
{
    QSizeF size(300, 300);

    if (fxMode == nullptr)
        return size;

    QLCPhysical phy = fxMode->physical();
    if (phy.width() == 0)
        phy.setWidth(300);
    if (phy.height() == 0)
        phy.setHeight(300);
    if (phy.depth() == 0)
        phy.setDepth(300);

    switch (pointOfView)
    {
        case MonitorProperties::TopView:
            size.setWidth(phy.width());
            size.setHeight(phy.depth());
        break;
        case MonitorProperties::Undefined:
        case MonitorProperties::FrontView:
            size.setWidth(phy.width());
            size.setHeight(phy.height());
        break;
        case MonitorProperties::RightSideView:
        case MonitorProperties::LeftSideView:
            size.setWidth(phy.depth());
            size.setHeight(phy.height());
        break;
    }

    return size;
}

void MonitorLayout::alignItem(QVector3D refPos, QVector3D &origPos, int pointOfView, int alignment)
{
    switch (pointOfView)
    {
        case MonitorProperties::TopView:
            switch (alignment)
            {
                case Qt::AlignTop: origPos.setZ(refPos.z()); break;
                case Qt::AlignLeft: origPos.setX(refPos.x()); break;
            }
        break;
        case MonitorProperties::Undefined:
        case MonitorProperties::FrontView:
            switch (alignment)
            {
                case Qt::AlignTop: origPos.setY(refPos.y()); break;
                case Qt::AlignLeft: origPos.setX(refPos.x()); break;
            }
        break;
        case MonitorProperties::RightSideView:
        case MonitorProperties::LeftSideView:
            switch (alignment)
            {
                case Qt::AlignTop: origPos.setY(refPos.y()); break;
                case Qt::AlignLeft: origPos.setZ(refPos.z()); break;
            }
        break;
    }
}

QVector3D MonitorLayout::item3DPosition(const MonitorProperties *monProps, QPointF point, float thirdVal)
{
    QVector3D pos(point.x(), point.y(), thirdVal);
    float gridUnits = monProps->gridUnits() == MonitorProperties::Meters ? 1000.0 : 304.8;

    // Must stay the exact inverse of item2DPosition() above for each point of
    // view - every non-TopView branch there flips Y via
    // (gridSize().y() * gridUnits) - pos.y(), so recovering pos.y() here needs
    // the same subtraction applied to point.y() (not a plain pass-through).
    switch (monProps->pointOfView())
    {
        case MonitorProperties::TopView:
            pos = QVector3D(point.x(), thirdVal, point.y());
        break;
        case MonitorProperties::Undefined:
        case MonitorProperties::FrontView:
            pos = QVector3D(point.x(), (monProps->gridSize().y() * gridUnits) - point.y(), thirdVal);
        break;
        case MonitorProperties::RightSideView:
            pos = QVector3D(thirdVal, (monProps->gridSize().y() * gridUnits) - point.y(),
                             (monProps->gridSize().x() * gridUnits) - point.x());
        break;
        case MonitorProperties::LeftSideView:
            pos = QVector3D(thirdVal, (monProps->gridSize().y() * gridUnits) - point.y(), point.x());
        break;
    }

    return pos;
}

QVector3D MonitorLayout::gridCenterPosition(const MonitorProperties *monProps)
{
    if (monProps == nullptr)
        return QVector3D(0, 0, 0);

    float unitScale = monProps->gridUnits() == MonitorProperties::Meters ? 1.0f : 0.3048f;
    QVector3D gridMeters = monProps->gridSize() * unitScale;

    return gridMeters * 500.0f;
}

/*****************************************************************************
 * Ordering
 *****************************************************************************/

QList<MonitorLayout::Item> MonitorLayout::sortedByAddress(Doc *doc, const QList<Item> &items)
{
    QList<Item> sorted = items;

    std::stable_sort(sorted.begin(), sorted.end(), [doc] (const Item &left, const Item &right)
    {
        Fixture *leftFixture = doc->fixture(left.fixtureId);
        Fixture *rightFixture = doc->fixture(right.fixtureId);

        if (leftFixture == nullptr || rightFixture == nullptr)
            return false;

        if (leftFixture != rightFixture)
            return *leftFixture < *rightFixture;

        if (left.headIndex != right.headIndex)
            return left.headIndex < right.headIndex;

        return left.linkedIndex < right.linkedIndex;
    });

    return sorted;
}

QList<MonitorLayout::Item> MonitorLayout::groupOrdered(Doc *doc, const QList<Item> &items)
{
    if (items.isEmpty())
        return sortedByAddress(doc, items);

    // Find a single FixtureGroup that contains every item's fixture
    FixtureGroup *commonGroup = nullptr;
    for (const Item &item : items)
    {
        FixtureGroup *fxGroup = nullptr;

        for (FixtureGroup *group : doc->fixtureGroups())
        {
            if (group->fixtureList().contains(item.fixtureId))
            {
                fxGroup = group;
                break;
            }
        }

        if (fxGroup == nullptr)
            return sortedByAddress(doc, items);

        if (commonGroup == nullptr)
            commonGroup = fxGroup;
        else if (commonGroup != fxGroup)
            return sortedByAddress(doc, items);
    }

    // Order the selection by the group's own grid. QLCPoint::operator< sorts
    // row-major (y then x), and headsMap() is a QMap keyed by QLCPoint, so
    // iterating it already yields top-left to bottom-right order.
    QList<Item> ordered;
    QList<int> used;

    QMapIterator<QLCPoint, GroupHead> it(commonGroup->headsMap());
    while (it.hasNext())
    {
        it.next();
        GroupHead gh = it.value();
        if (gh.isValid() == false)
            continue;

        quint16 head = gh.head >= 0 ? quint16(gh.head) : 0;
        for (int i = 0; i < items.count(); i++)
        {
            const Item &item = items.at(i);
            if (used.contains(i) == false && item.fixtureId == gh.fxi &&
                item.headIndex == head && item.linkedIndex == 0)
            {
                ordered.append(item);
                used.append(i);
                break;
            }
        }
    }

    // Safety net: if the group's heads don't reconstruct the full selection
    // (e.g. a head/linked-index mismatch), fall back rather than silently
    // dropping fixtures from the arrangement.
    if (ordered.count() != items.count())
        return sortedByAddress(doc, items);

    return ordered;
}

/*****************************************************************************
 * Layout operations
 *****************************************************************************/

void MonitorLayout::faceTowards(Item &item, const QVector3D &centroid, int hAxis, int vAxis, int dAxis)
{
    qreal dh = axisValue(centroid, hAxis) - axisValue(item.position, hAxis);
    qreal dv = axisValue(centroid, vAxis) - axisValue(item.position, vAxis);
    if (qFuzzyIsNull(dh) && qFuzzyIsNull(dv))
        return; // fixture is sitting right on the centroid - no facing to compute

    qreal bearingDeg = qRadiansToDegrees(qAtan2(dv, dh));
    setAxisValue(item.rotation, dAxis, bearingDeg);
    item.rotationChanged = true;
}

QList<MonitorLayout::Item> MonitorLayout::arrangeCircle(const QList<Item> &items, int pointOfView,
                                                        qreal diameter, bool lookAtCenter)
{
    QList<Item> result = items;
    int count = result.count();
    if (count == 0)
        return result;

    int hAxis, vAxis, dAxis;
    planeAxes(pointOfView, hAxis, vAxis, dAxis);
    QVector3D center = centroid(items);
    qreal radius = diameter / 2.0;

    for (int i = 0; i < count; i++)
    {
        Item &item = result[i];
        if (item.locked)
            continue;

        qreal angleRad = qDegreesToRadians(360.0 * i / count);
        QVector3D newPos = center;
        setAxisValue(newPos, hAxis, axisValue(center, hAxis) + radius * qCos(angleRad));
        setAxisValue(newPos, vAxis, axisValue(center, vAxis) + radius * qSin(angleRad));

        item.position = newPos;
        item.positionChanged = true;

        if (lookAtCenter)
            faceTowards(item, center, hAxis, vAxis, dAxis);
    }

    return result;
}

QList<MonitorLayout::Item> MonitorLayout::arrangeGrid(const QList<Item> &items, int pointOfView, qreal width,
                                                      qreal height, int columns, qreal angleDegrees)
{
    QList<Item> result = items;
    int count = result.count();
    if (count == 0)
        return result;

    if (columns <= 0)
        columns = qCeil(qSqrt(qreal(count)));
    int rows = qCeil(qreal(count) / columns);

    int hAxis, vAxis, dAxis;
    planeAxes(pointOfView, hAxis, vAxis, dAxis);
    QVector3D center = centroid(items);

    qreal colStep = columns > 1 ? width / (columns - 1) : 0;
    qreal rowStep = rows > 1 ? height / (rows - 1) : 0;
    qreal angleRad = qDegreesToRadians(angleDegrees);

    for (int i = 0; i < count; i++)
    {
        Item &item = result[i];
        if (item.locked)
            continue;

        int col = i % columns;
        int row = i / columns;
        qreal h = -width / 2.0 + col * colStep;
        qreal v = -height / 2.0 + row * rowStep;

        qreal rh = h * qCos(angleRad) - v * qSin(angleRad);
        qreal rv = h * qSin(angleRad) + v * qCos(angleRad);

        QVector3D newPos = center;
        setAxisValue(newPos, hAxis, axisValue(center, hAxis) + rh);
        setAxisValue(newPos, vAxis, axisValue(center, vAxis) + rv);

        item.position = newPos;
        item.positionChanged = true;
    }

    return result;
}

QList<MonitorLayout::Item> MonitorLayout::arrangeLine(const QList<Item> &items, int pointOfView, qreal length,
                                                      qreal angleDegrees, bool lookAtCenter)
{
    QList<Item> result = items;
    int count = result.count();
    if (count == 0)
        return result;

    int hAxis, vAxis, dAxis;
    planeAxes(pointOfView, hAxis, vAxis, dAxis);
    QVector3D center = centroid(items);

    qreal angleRad = qDegreesToRadians(angleDegrees);
    qreal step = count > 1 ? length / (count - 1) : 0;
    qreal startOffset = -length / 2.0;

    for (int i = 0; i < count; i++)
    {
        Item &item = result[i];
        if (item.locked)
            continue;

        qreal dist = startOffset + i * step;
        QVector3D newPos = center;
        setAxisValue(newPos, hAxis, axisValue(center, hAxis) + dist * qCos(angleRad));
        setAxisValue(newPos, vAxis, axisValue(center, vAxis) + dist * qSin(angleRad));

        item.position = newPos;
        item.positionChanged = true;

        if (lookAtCenter)
            faceTowards(item, center, hAxis, vAxis, dAxis);
    }

    return result;
}

QList<MonitorLayout::Item> MonitorLayout::rotateAroundCentroid(const QList<Item> &items, int pointOfView,
                                                               qreal angleDegrees)
{
    QList<Item> result = items;
    if (result.isEmpty())
        return result;

    int hAxis, vAxis, dAxis;
    planeAxes(pointOfView, hAxis, vAxis, dAxis);
    QVector3D center = centroid(items);

    qreal angleRad = qDegreesToRadians(angleDegrees);
    qreal cosA = qCos(angleRad);
    qreal sinA = qSin(angleRad);

    for (Item &item : result)
    {
        if (item.locked)
            continue;

        qreal h = axisValue(item.position, hAxis) - axisValue(center, hAxis);
        qreal v = axisValue(item.position, vAxis) - axisValue(center, vAxis);

        QVector3D newPos = item.position;
        setAxisValue(newPos, hAxis, axisValue(center, hAxis) + (h * cosA - v * sinA));
        setAxisValue(newPos, vAxis, axisValue(center, vAxis) + (h * sinA + v * cosA));

        item.position = newPos;
        item.positionChanged = true;
    }

    return result;
}

QList<MonitorLayout::Item> MonitorLayout::moveToCenter(const QList<Item> &items, const QVector3D &gridCenter)
{
    QList<Item> result = items;
    if (result.isEmpty())
        return result;

    QVector3D offset = gridCenter - centroid(items);
    if (offset.isNull())
        return result;

    for (Item &item : result)
    {
        if (item.locked)
            continue;

        item.position += offset;
        item.positionChanged = true;
    }

    return result;
}

QList<MonitorLayout::Item> MonitorLayout::align(const QList<Item> &items, int pointOfView, int alignment)
{
    QList<Item> result = items;
    if (result.isEmpty())
        return result;

    QVector3D refPos = result.first().position;

    for (Item &item : result)
    {
        QVector3D pos = item.position;
        alignItem(refPos, pos, pointOfView, alignment);
        if (pos != item.position)
        {
            item.position = pos;
            item.positionChanged = true;
        }
    }

    return result;
}

QList<MonitorLayout::Item> MonitorLayout::distribute(const QList<Item> &items, const MonitorProperties *monProps,
                                                     int direction)
{
    QList<Item> result = items;
    if (result.count() < 3)
        return result;

    int pointOfView = monProps->pointOfView();
    qreal min = 1000000;
    qreal max = 0;
    qreal fixturesSize = 0;
    QVector<int> sortedIdx;
    QVector<qreal> sortedPos;

    /* cycle through the items and do the following:
     * 1- calculate the total width/height
     * 2- sort the items from the leftmost/topmost one
     * 3- detect the minimum and maximum items position
     */
    for (int idx = 0; idx < result.count(); idx++)
    {
        const Item &item = result.at(idx);
        QPointF fxPos = item2DPosition(monProps, pointOfView, item.position);
        qreal pos = direction == Qt::Horizontal ? fxPos.x() : fxPos.y();
        qreal size = direction == Qt::Horizontal ? item.size2D.width() : item.size2D.height();
        int i = 0;

        // 1
        fixturesSize += size;

        // 2
        for (i = 0; i < sortedPos.count(); i++)
        {
            if (pos < sortedPos[i])
                break;
        }
        if (sortedPos.isEmpty() || i == sortedIdx.count())
        {
            sortedIdx.append(idx);
            sortedPos.append(pos);
        }
        else
        {
            sortedIdx.insert(i, idx);
            sortedPos.insert(i, pos);
        }

        // 3
        if (pos + size > max)
            max = pos + size;
        if (pos < min)
            min = pos;
    }

    qreal gap = ((max - min) - fixturesSize) / (sortedIdx.count() - 1);
    qreal newPos = min;

    for (int n = 0; n < sortedIdx.count(); n++)
    {
        Item &item = result[sortedIdx[n]];
        qreal size = direction == Qt::Horizontal ? item.size2D.width() : item.size2D.height();

        // the first and last item don't need any adjustment
        if (n > 0 && n < sortedIdx.count() - 1)
        {
            QVector3D fxPos = item.position;
            switch (pointOfView)
            {
                case MonitorProperties::TopView:
                    if (direction == Qt::Horizontal)
                        fxPos.setX(newPos);
                    else
                        fxPos.setZ(newPos);
                break;
                case MonitorProperties::RightSideView:
                    if (direction == Qt::Horizontal)
                        fxPos.setZ(monProps->gridSize().z() - newPos);
                    else
                        fxPos.setY(newPos);
                break;
                case MonitorProperties::LeftSideView:
                    if (direction == Qt::Horizontal)
                        fxPos.setZ(newPos);
                    else
                        fxPos.setY(newPos);
                break;
                default:
                    if (direction == Qt::Horizontal)
                        fxPos.setX(newPos);
                    else
                        fxPos.setY(newPos);
                break;
            }

            item.position = fxPos;
            item.positionChanged = true;
        }

        newPos += size + gap;
    }

    return result;
}

qreal MonitorLayout::detectedCircleDiameter(const QList<Item> &items, int pointOfView)
{
    if (items.count() < 2)
        return 0;

    int hAxis, vAxis, dAxis;
    planeAxes(pointOfView, hAxis, vAxis, dAxis);
    Q_UNUSED(dAxis)
    QVector3D center = centroid(items);

    qreal sumRadius = 0;
    for (const Item &item : items)
    {
        qreal h = axisValue(item.position, hAxis) - axisValue(center, hAxis);
        qreal v = axisValue(item.position, vAxis) - axisValue(center, vAxis);
        sumRadius += qSqrt(h * h + v * v);
    }

    return (sumRadius / items.count()) * 2.0;
}

void MonitorLayout::detectedLineFit(const QList<Item> &items, int pointOfView, qreal &angleRadians, qreal &length)
{
    angleRadians = 0;
    length = 0;

    if (items.count() < 2)
        return;

    int hAxis, vAxis, dAxis;
    planeAxes(pointOfView, hAxis, vAxis, dAxis);
    Q_UNUSED(dAxis)
    QVector3D center = centroid(items);

    // Principal axis of the position scatter, via the dominant eigenvector of
    // its 2D covariance matrix - the same "structure tensor" formula used to
    // recover a blob's orientation in image processing.
    QVector<QPointF> local;
    local.reserve(items.count());
    qreal sxx = 0, syy = 0, sxy = 0;

    for (const Item &item : items)
    {
        qreal h = axisValue(item.position, hAxis) - axisValue(center, hAxis);
        qreal v = axisValue(item.position, vAxis) - axisValue(center, vAxis);
        local.append(QPointF(h, v));

        sxx += h * h;
        syy += v * v;
        sxy += h * v;
    }

    if (qFuzzyIsNull(sxx) && qFuzzyIsNull(syy) && qFuzzyIsNull(sxy))
        return; // every fixture sits on top of its neighbours - no direction to detect

    angleRadians = 0.5 * qAtan2(2.0 * sxy, sxx - syy);
    qreal dirH = qCos(angleRadians);
    qreal dirV = qSin(angleRadians);

    qreal minProj = 0, maxProj = 0;
    for (int i = 0; i < local.count(); i++)
    {
        qreal proj = local.at(i).x() * dirH + local.at(i).y() * dirV;
        if (i == 0 || proj < minProj)
            minProj = proj;
        if (i == 0 || proj > maxProj)
            maxProj = proj;
    }

    length = maxProj - minProj;
}

/*****************************************************************************
 * Pick a 3D point
 *****************************************************************************/

QMatrix4x4 MonitorLayout::lightMatrixFromRotation(const QMatrix4x4 &rotMatrix)
{
    QVector4D xb = rotMatrix * QVector4D(1, 0, 0, 0);
    QVector4D yb = rotMatrix * QVector4D(0, 1, 0, 0);
    QVector4D zb = rotMatrix * QVector4D(0, 0, 1, 0);

    QVector3D xa = QVector3D(xb.x(), xb.y(), xb.z()).normalized();
    QVector3D ya = QVector3D(yb.x(), yb.y(), yb.z()).normalized();
    QVector3D za = QVector3D(zb.x(), zb.y(), zb.z()).normalized();

    return QMatrix4x4(
        xa.x(), xa.y(), xa.z(), 0,
        ya.x(), ya.y(), ya.z(), 0,
        za.x(), za.y(), za.z(), 0,
        0, 0, 0, 1
    ).transposed();
}

bool MonitorLayout::aimPanTilt(const Fixture *fixture, quint32 itemFlags, const QVector3D &point,
                               const QVector3D &lightPos, const QMatrix4x4 &lightMatrix,
                               bool &hasPan, qreal &panDegrees, bool &hasTilt, qreal &tiltDegrees)
{
    hasPan = false;
    hasTilt = false;
    panDegrees = 0;
    tiltDegrees = 0;

    if (fixture == nullptr || fixture->fixtureMode() == nullptr)
        return false;

    quint32 panMSB = fixture->channelNumber(QLCChannel::Pan, QLCChannel::MSB);
    quint32 tiltMSB = fixture->channelNumber(QLCChannel::Tilt, QLCChannel::MSB);

    // don't even bother if the fixture doesn't have PAN/TILT channels
    if (panMSB == QLCChannel::invalid() && tiltMSB == QLCChannel::invalid())
        return false;

    QLCPhysical phy = fixture->fixtureMode()->physical();
    QVector3D dir = (point - lightPos).normalized();

    if (panMSB != QLCChannel::invalid())
    {
        hasPan = true;

        // rotate x-axis according to light matrix.
        QVector4D res = lightMatrix * QVector4D(1.0, 0.0, 0.0, 0.0);
        QVector3D xa = QVector3D(res.x(), res.y(), res.z());

        // rotate z-axis according to light matrix.
        res = lightMatrix * QVector4D(0.0, 0.0, 1.0, 0.0);
        QVector3D za = QVector3D(res.x(), res.y(), res.z());

        QVector3D projDirX = QVector3D::dotProduct(dir, xa) * xa;
        QVector3D projDirZ = QVector3D::dotProduct(dir, za) * za;

        qreal b = projDirX.length();
        qreal c = projDirZ.length();
        qreal panDeg = 0;
        if (!qFuzzyIsNull(b) || !qFuzzyIsNull(c))
            panDeg = qRadiansToDegrees(M_PI_2 - qAtan(c / b)); // PI/2 - angle

        bool xLeft = QVector3D::dotProduct(projDirX, xa) < 0.0 ? true : false;
        bool zBack = QVector3D::dotProduct(projDirZ, za) < 0.0 ? true : false;

        if (xLeft && !zBack)
            panDeg = 90.0 + (90.0 - panDeg);
        else if (!xLeft && !zBack)
            panDeg = 180.0 + panDeg;
        else if (!xLeft && zBack)
            panDeg = 270.0 + (90.0 - panDeg);

        if (itemFlags & MonitorProperties::InvertedPanFlag)
        {
            double maxPanDeg = phy.focusPanMax() ? phy.focusPanMax() : 360;
            panDeg = maxPanDeg - panDeg;
        }

        panDegrees = panDeg;
    }

    if (tiltMSB != QLCChannel::invalid())
    {
        hasTilt = true;

        // rotate y-axis according to light matrix.
        QVector4D res = lightMatrix * QVector4D(0.0, -1.0, 0.0, 0.0);
        QVector3D ya = QVector3D(res.x(), res.y(), res.z());

        qreal tiltDeg = qRadiansToDegrees(qAcos(qBound(-1.0f, QVector3D::dotProduct(dir, ya), 1.0f)));

        // clamp the tilt.
        if (tiltDeg < 0.0)
            tiltDeg = 0.0;

        if (tiltDeg > phy.focusTiltMax() / 2)
            tiltDeg = phy.focusTiltMax() / 2;

        if (itemFlags & MonitorProperties::InvertedTiltFlag)
            tiltDeg = phy.focusTiltMax() / 2 + tiltDeg;
        else
            tiltDeg = phy.focusTiltMax() / 2 - tiltDeg;

        tiltDegrees = tiltDeg;
    }

    return true;
}
