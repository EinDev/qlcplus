/*
  Q Light Controller Plus
  fixturegroupeditor.cpp

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

#include <qmath.h>
#include <algorithm>
#include <QImage>
#include <QDebug>
#include <QTimer>
#include <QQmlContext>

#include "fixturegroupeditor.h"
#include "fixturemanager.h"
#include "fixtureutils.h"
#include "treemodel.h"
#include "tardis.h"
#include "doc.h"

FixtureGroupEditor::FixtureGroupEditor(QQuickView *view, Doc *doc,
                                       FixtureManager *fxMgr, QObject *parent)
    : QObject(parent)
    , m_view(view)
    , m_doc(doc)
    , m_fixtureManager(fxMgr)
{
    Q_ASSERT(m_doc != nullptr);

    m_view->rootContext()->setContextProperty("fixtureGroupEditor", this);
    qmlRegisterUncreatableType<FixtureGroupEditor>("org.qlcplus.classes", 1, 0,  "FixtureGroupEditor", "Can't create a FixtureGroupEditor!");

    connect(m_doc, SIGNAL(loaded()), this, SLOT(slotDocLoaded()));

    // keep the groups list model in sync with the project, so views
    // displaying it (e.g. the 2D/3D groups bar) are always up to date
    connect(m_doc, SIGNAL(fixtureGroupAdded(quint32)), this, SIGNAL(groupsListModelChanged()));
    connect(m_doc, SIGNAL(fixtureGroupRemoved(quint32)), this, SIGNAL(groupsListModelChanged()));
    connect(m_doc, SIGNAL(fixtureGroupChanged(quint32)), this, SLOT(slotFixtureGroupChanged(quint32)));
}

FixtureGroupEditor::~FixtureGroupEditor()
{
    m_view->rootContext()->setContextProperty("fixtureGroupEditor", nullptr);
}

QVariant FixtureGroupEditor::groupsListModel()
{
    QVariantList groupsList;

    foreach (FixtureGroup *grp, m_doc->fixtureGroups())
    {
        QVariantMap grpMap;
        grpMap.insert("mIcon", "qrc:/group.svg");
        grpMap.insert("mLabel", grp->name());
        grpMap.insert("mValue", grp->id());
        grpMap.insert("mCount", grp->fixtureList().count());
        groupsList.append(grpMap);
    }

    return QVariant::fromValue(groupsList);
}

void FixtureGroupEditor::resetGroup()
{
    if (m_editGroup == nullptr)
        return;

    QByteArray before = groupContentsSnapshot();
    m_editGroup->reset();
    enqueueGroupContentsChange(before);
    updateGroupMap();
}

void FixtureGroupEditor::regenerateFromDmxOrder(int rows)
{
    if (m_editGroup == nullptr)
        return;

    QList<quint32> fixtureIDs = m_editGroup->fixtureList();
    if (fixtureIDs.isEmpty())
        return;

    QByteArray before = groupContentsSnapshot();

    std::sort(fixtureIDs.begin(), fixtureIDs.end(), [this] (quint32 left, quint32 right)
    {
        Fixture *leftFixture = m_doc->fixture(left);
        Fixture *rightFixture = m_doc->fixture(right);

        if (leftFixture == nullptr || rightFixture == nullptr)
            return false;

        return *leftFixture < *rightFixture;
    });

    int totalHeads = 0;
    for (quint32 fxID : std::as_const(fixtureIDs))
    {
        Fixture *fixture = m_doc->fixture(fxID);
        if (fixture != nullptr)
            totalHeads += qMax(1, fixture->heads());
    }

    int columns = rows > 0 ? qCeil(qreal(totalHeads) / rows) : qCeil(qSqrt(qreal(totalHeads)));
    if (columns <= 0)
        columns = 1;

    m_editGroup->setSize(QSize(columns, qCeil(qreal(totalHeads) / columns)));
    m_editGroup->reset();

    int cellIndex = 0;
    for (quint32 fxID : std::as_const(fixtureIDs))
    {
        Fixture *fixture = m_doc->fixture(fxID);
        if (fixture == nullptr)
            continue;

        m_editGroup->assignFixture(fxID, QLCPoint(cellIndex % columns, cellIndex / columns));
        cellIndex += qMax(1, fixture->heads());
    }

    enqueueGroupContentsChange(before);

    m_groupSelection.clear();
    updateGroupMap();
    emit groupSizeChanged();
}

void FixtureGroupEditor::slotDocLoaded()
{
    emit groupsListModelChanged();
}

void FixtureGroupEditor::slotFixtureGroupChanged(quint32 id)
{
    // Keep the currently open grid editor in sync whenever its group's
    // contents change from elsewhere (e.g. a Tardis undo/redo restoring
    // a previous name/size/heads state). FixtureGroup::changed() can fire
    // once per head for a single user gesture (moveSelection/transformSelection
    // touch every selected head individually), so coalesce into a single
    // deferred refresh instead of rebuilding the grid map synchronously on
    // every emission - this ran during normal grid editing, not just undo.
    if (m_editGroup != nullptr && id == m_editGroup->id() && m_pendingGroupRefresh == false)
    {
        m_pendingGroupRefresh = true;
        QTimer::singleShot(0, this, [this, id]() {
            m_pendingGroupRefresh = false;

            // The group may have been deleted (and its C++ object freed) by
            // the time this runs, e.g. undo/redo of removing the last fixture
            // from a group also removes the group itself - guard against a
            // dangling m_editGroup by re-resolving the id through Doc.
            if (m_editGroup != nullptr && m_editGroup == m_doc->fixtureGroup(id))
            {
                emit groupNameChanged();
                emit groupSizeChanged();
                updateGroupMap();
            }
        });
    }

    emit groupsListModelChanged();
}

/*********************************************************************
 * Fixture Group Grid Editing
 *********************************************************************/

void FixtureGroupEditor::setEditGroup(QVariant reference)
{
    if (reference.canConvert<FixtureGroup *>() == false)
        return;

    m_editGroup = reference.value<FixtureGroup *>();

    emit groupNameChanged();
    emit groupSizeChanged();
    updateGroupMap();
}

quint32 FixtureGroupEditor::groupID() const
{
    return m_editGroup == nullptr ? FixtureGroup::invalidId() : m_editGroup->id();
}

QString FixtureGroupEditor::groupName() const
{
    return m_editGroup == nullptr ? "" : m_editGroup->name();
}

void FixtureGroupEditor::setGroupName(QString name)
{
    if (m_editGroup == nullptr || m_editGroup->name() == name)
        return;

    Tardis::instance()->enqueueAction(Tardis::FixtureGroupSetName, m_editGroup->id(), m_editGroup->name(), name);

    m_editGroup->setName(name);

    emit groupNameChanged();
}

QSize FixtureGroupEditor::groupSize() const
{
    if (m_editGroup == nullptr)
        return QSize();

    return m_editGroup->size();
}

void FixtureGroupEditor::setGroupSize(QSize size)
{
    if (m_editGroup == nullptr || size == m_editGroup->size())
        return;

    QByteArray before = groupContentsSnapshot();
    m_editGroup->setSize(size);
    enqueueGroupContentsChange(before);
    emit groupSizeChanged();
    updateGroupMap();
}

QVariantList FixtureGroupEditor::groupMap()
{
    return m_groupMap;
}

QVariantList FixtureGroupEditor::groupLabels()
{
    return m_groupLabels;
}

QVariantList FixtureGroupEditor::selectionData()
{
    return m_groupSelection;
}

void FixtureGroupEditor::resetSelection()
{
    m_groupSelection.clear();
}

QVariantList FixtureGroupEditor::groupSelection(int x, int y, int mouseMods)
{
    qDebug() << "Requested selection at" << x << y << "mods:" << mouseMods;
    if (m_editGroup == nullptr)
        return m_groupSelection;

    int absIndex = (y * m_editGroup->size().width()) + x;

    if (m_groupSelection.contains(absIndex))
        return m_groupSelection;

    //if (mouseMods == 0)
    //    m_groupSelection.clear();

    GroupHead head = m_editGroup->head(QLCPoint(x, y));
    if (head.isValid() == false)
    {
        m_groupSelection.clear();
        return m_groupSelection;
    }

    Fixture *fixture = m_doc->fixture(head.fxi);
    if (fixture == nullptr)
        return m_groupSelection;

    m_groupSelection.append(absIndex);

    std::sort(m_groupSelection.begin(), m_groupSelection.end(),
              [](QVariant a, QVariant b) {
                  return a.toUInt() < b.toUInt();
              });

    qDebug() << "Selection size" << m_groupSelection.count() << m_groupSelection;

    return m_groupSelection;
}

QVariantList FixtureGroupEditor::fixtureSelection(QVariant reference, int x, int y, int mouseMods)
{
    if (m_editGroup == nullptr)
        return m_groupSelection;

    if (mouseMods == 0)
        m_groupSelection.clear();

    int absIndex = (y * m_editGroup->size().width()) + x;

    if (reference.canConvert<Fixture *>())
    {
        Fixture *fixture = reference.value<Fixture *>();

        for (int headIdx = 0; headIdx < fixture->heads(); headIdx++)
            m_groupSelection.append(absIndex + headIdx);
    }

    return m_groupSelection;
}

QVariantList FixtureGroupEditor::headSelection(int x, int y, int mouseMods)
{
    if (m_editGroup == nullptr)
        return m_groupSelection;

    if (mouseMods == 0)
        m_groupSelection.clear();

    int absIndex = (y * m_editGroup->size().width()) + x;
    m_groupSelection.append(absIndex);

    return m_groupSelection;
}

bool FixtureGroupEditor::addFixture(QVariant reference, int x, int y)
{
    if (m_editGroup == nullptr)
        return false;

    qDebug() << Q_FUNC_INFO << reference << x << y;

    if (reference.canConvert<Fixture *>())
    {
        Fixture *fixture = reference.value<Fixture *>();
        QByteArray before = groupContentsSnapshot();
        if (m_editGroup->assignFixture(fixture->id(), QLCPoint(x, y)) == true)
        {
            enqueueGroupContentsChange(before);
            updateGroupMap();
            return true;
        }
    }

    return false;
}

bool FixtureGroupEditor::addHead(quint32 itemID, int headIndex, int x, int y)
{
    if (m_editGroup == nullptr)
        return false;

    quint32 fixtureID = FixtureUtils::itemFixtureID(itemID);
    GroupHead head(fixtureID, headIndex);
    QByteArray before = groupContentsSnapshot();
    if (m_editGroup->assignHead(QLCPoint(x, y), head) == true)
    {
        enqueueGroupContentsChange(before);
        updateGroupMap();
        return true;
    }

    return false;
}

QLCPoint FixtureGroupEditor::pointFromAbsolute(int absoluteIndex)
{
    if (m_editGroup == nullptr)
        return QLCPoint(0, 0);

    int yPos = qFloor(absoluteIndex / m_editGroup->size().width());
    int xPos = absoluteIndex - (yPos * m_editGroup->size().width());
    return QLCPoint(xPos, yPos);
}

bool FixtureGroupEditor::checkSelection(int x, int y, int offset)
{
    Q_UNUSED(x)
    Q_UNUSED(y)

    if (m_editGroup == nullptr)
        return false;

    // search for heads already occupying the target positions
    for (int i = 0; i < m_groupSelection.count(); i++)
    {
        int targetPos = m_groupSelection.at(i).toInt() + offset;
        if (m_groupSelection.contains(targetPos))
            continue;

        GroupHead head = m_editGroup->head(pointFromAbsolute(targetPos));
        if (head.isValid())
            return false;
    }

    return true;
}

void FixtureGroupEditor::moveSelection(int x, int y, int offset)
{
    if (m_editGroup == nullptr)
        return;

    if (checkSelection(x, y, offset) == false)
        return;

    QByteArray before = groupContentsSnapshot();

    QList<GroupHead> headsList;

    for (int i = 0; i < m_groupSelection.count(); i++)
    {
        QLCPoint pt = pointFromAbsolute(m_groupSelection.at(i).toInt());
        headsList.append(m_editGroup->head(pt));
        m_editGroup->resignHead(pt);
    }

    for (int i = 0; i < headsList.count(); i++)
    {
        QLCPoint pt = pointFromAbsolute(m_groupSelection.at(i).toInt() + offset);
        if (pt.x() >= m_editGroup->size().width())
        {
            pt.setY(pt.y() + 1);
            pt.setX(pt.x() - m_editGroup->size().width());
        }
        m_editGroup->assignHead(pt, headsList.at(i));
    }

    enqueueGroupContentsChange(before);

    updateGroupMap();

    for (int i = 0; i < m_groupSelection.count(); i++)
        m_groupSelection.replace(i, m_groupSelection.at(i).toInt() + offset);
}

void FixtureGroupEditor::deleteSelection()
{
    if (m_editGroup == nullptr || m_groupSelection.isEmpty())
        return;

    for (QVariant head : m_groupSelection)
    {
        QLCPoint point = pointFromAbsolute(head.toInt());
        GroupHead gHead = m_editGroup->head(point);
        Fixture *fixture = m_doc->fixture(gHead.fxi);
        if (fixture != nullptr && fixture->heads() == 1)
        {
            QString fxPath = QString("%1%2%3").arg(m_editGroup->name()).arg(TreeModel::separator()).arg(fixture->name());
            quint32 itemID = FixtureUtils::fixtureItemID(fixture->id(), gHead.head, 0);
            // deleteFixtureInGroup() removes this fixture's head from the group
            // and enqueues its own Tardis undo action for it, so the
            // resignHead() below is a no-op in this branch.
            m_fixtureManager->deleteFixtureInGroup(m_editGroup->id(), itemID, fxPath);
            m_editGroup->resignHead(point);
        }
        else
        {
            QByteArray before = groupContentsSnapshot();
            if (m_editGroup->resignHead(point))
                enqueueGroupContentsChange(before);
        }
    }

    m_groupSelection.clear();

    updateGroupMap();
}

void FixtureGroupEditor::transformSelection(int transformation)
{
    if (m_editGroup == nullptr)
        return;

    QByteArray before = groupContentsSnapshot();

    int minX = m_editGroup->size().width();
    int minY = m_editGroup->size().height();
    int maxX = 0;
    int maxY = 0;
    QList<QPoint> pointsList;
    QList<GroupHead> headsList;

    /** If the selection list is empty, it means the operation
     *  has to be performed on the whole group, so create
     *  a selection with everything in it */
    if (m_groupSelection.isEmpty())
    {
        for (int y = 0; y < m_editGroup->size().height(); y++)
        {
            for (int x = 0; x < m_editGroup->size().width(); x++)
            {
                int absIndex = (y * m_editGroup->size().width()) + x;
                GroupHead head = m_editGroup->head(QLCPoint(x, y));
                if (head.isValid())
                    m_groupSelection.append(absIndex);
            }
        }
    }

    /** From the current selection:
     *  - create a list of the original head points
     *  - create a list of the original GroupHeads
     *  - determine the rectangular size of the selection
     *  - remove the original heads
     */
    for (QVariant headOffset : m_groupSelection)
    {
        int yPos = qFloor(headOffset.toInt() / m_editGroup->size().width());
        int xPos = headOffset.toInt() - (yPos * m_editGroup->size().width());

        if (yPos < minY) minY = yPos;
        if (yPos > maxY) maxY = yPos;
        if (xPos < minX) minX = xPos;
        if (xPos > maxX) maxX = xPos;
        pointsList.append(QPoint(xPos, yPos));
        headsList.append(m_editGroup->head(QLCPoint(xPos, yPos)));

        // WARNING: point of no return !
        m_editGroup->resignHead(QLCPoint(xPos, yPos));
    }

    /** Here's the trick. Instead of dragging in a lot of code to perform
     *  transformations, let's leverage the QImage/QTransform ready-made code.
     *  Here a QImage is filled with "pixels" (at the scaled position)
     *  whose color is actually the head position in the points list
     *  created above */
    QImage matrix(maxX - minX + 1, maxY - minY + 1, QImage::Format_RGB32);
    matrix.fill(Qt::black);
    qDebug() << "Original matrix size is" << matrix.size();

    for (int i = 0; i < pointsList.count(); i++)
    {
        QPoint point = pointsList.at(i);
        matrix.setPixel(point.x() - minX, point.y() - minY, QRgb(i + 1));
        //qDebug() << "set pixel" << (point.x() - minX) << (point.y() - minY) << m_groupSelection.at(i).toUInt();
    }

    /** Perform the requested transformation ! */
    QTransform transform;
    QImage trImage;

    switch(TransformType(transformation))
    {
        case Rotate90:
            transform = transform.rotate(90);
            trImage = matrix.transformed(transform);
        break;
        case Rotate180:
            transform = transform.rotate(180);
            trImage = matrix.transformed(transform);
        break;
        case Rotate270:
            transform = transform.rotate(270);
            trImage = matrix.transformed(transform);
        break;
        case HorizontalFlip:
#if (QT_VERSION < QT_VERSION_CHECK(6, 9, 0))
            trImage = matrix.mirrored(true, false);
#else
            trImage = matrix.flipped(Qt::Horizontal);
#endif
        break;
        case VerticalFlip:
#if (QT_VERSION < QT_VERSION_CHECK(6, 9, 0))
            trImage = matrix.mirrored(false, true);
#else
            trImage = matrix.flipped(Qt::Vertical);
#endif
        break;
    }

    /** Now assign to the group the original heads but on the new
     *  positions. Also, restore the original selection with
     *  the transformed head positions */
    m_groupSelection.clear();

    for (int y = 0; y < trImage.height(); y++)
    {
        for (int x = 0; x < trImage.width(); x++)
        {
            unsigned int pixel = 0x00FFFFFF & (unsigned int)trImage.pixel(x, y);
            //qDebug() << x << y << "pixel:" << QString::number(pixel);

            if (pixel == 0)
                continue;

            m_editGroup->assignHead(QLCPoint(x + minX, y + minY), headsList.at(pixel - 1));
            int absIndex = ((y + minY) * m_editGroup->size().width()) + (x + minX);
            m_groupSelection.append(absIndex);
        }
    }

    enqueueGroupContentsChange(before);

    /** Finally, inform the UI that the map has changed */
    updateGroupMap();
}

QString FixtureGroupEditor::getTooltip(int x, int y)
{
    if (m_editGroup == nullptr)
        return "";

    GroupHead head = m_editGroup->head(QLCPoint(x, y));
    if (head.isValid() == false)
        return "";

    Fixture *fixture = m_doc->fixture(head.fxi);
    if (fixture == nullptr)
        return "";

    QString tooltip = QString("%1\nHead: %2\nAddress: %3\nUniverse: %4")
            .arg(fixture->name())
            .arg(head.head + 1)
            .arg(fixture->address() + 1)
            .arg(fixture->universe() + 1);
    return tooltip;
}

QByteArray FixtureGroupEditor::groupContentsSnapshot() const
{
    if (m_editGroup == nullptr)
        return QByteArray();

    return Tardis::instance()->actionToByteArray(Tardis::FixtureGroupSetContents, m_editGroup->id());
}

void FixtureGroupEditor::enqueueGroupContentsChange(const QByteArray &before)
{
    if (m_editGroup == nullptr)
        return;

    QByteArray after = Tardis::instance()->actionToByteArray(Tardis::FixtureGroupSetContents, m_editGroup->id());

    // Skip recording a no-op undo step, e.g. transformSelection()/moveSelection()
    // called with nothing actually selected/moved
    if (after == before)
        return;

    Tardis::instance()->enqueueAction(Tardis::FixtureGroupSetContents, m_editGroup->id(), before, after);
}

void FixtureGroupEditor::updateGroupMap()
{
    /** Data format:
    * Fixture ID | headIndex | isOdd | fixture type (a lookup for icons)
    */

   m_groupMap.clear();
   m_groupLabels.clear();

   if (m_editGroup == nullptr)
       return;

   int gridWidth = m_editGroup->size().width();

   for (int y = 0; y < m_editGroup->size().height(); y++)
   {
       for (int x = 0; x < gridWidth; x++)
       {
            GroupHead head = m_editGroup->head(QLCPoint(x, y));
            if (head.isValid())
            {
                Fixture *fx = m_doc->fixture(head.fxi);
                m_groupMap.append(head.fxi); // item ID
                m_groupMap.append((gridWidth * y) + x); // absolute index
                m_groupMap.append(0); // isOdd
                m_groupMap.append(fx->type()); // item type

                QString str = QString("%1\nH:%2 A:%3 U:%4").arg(fx->name())
                                                       .arg(head.head + 1)
                                                       .arg(fx->address() + 1)
                                                       .arg(fx->universe() + 1);
                m_groupLabels.append(head.fxi); // item ID
                m_groupLabels.append((gridWidth * y) + x); // absolute index
                m_groupLabels.append(1); // width
                m_groupLabels.append(str); // label
            }
       }
   }
   emit groupMapChanged();
   emit groupLabelsChanged();
}
