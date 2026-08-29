/*
  Q Light Controller Plus
  fixturegroupeditor.h

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

#ifndef FIXTUREGROUPEDITOR_H
#define FIXTUREGROUPEDITOR_H

#include <QQuickView>
#include <QByteArray>
#include <QObject>

#include <qlcpoint.h>

class Doc;
class Fixture;
class FixtureGroup;
class FixtureManager;

/**
 * Editor for a single FixtureGroup's head layout (the grid used by 2D/3D group
 * buttons and Simple Desk's group view).
 *
 * m_editGroup is a raw, non-owning pointer into Doc - it can be deleted out
 * from under this class at any time, not just via an explicit "delete this
 * group" action: deleteSelection() removes single-head fixtures via
 * FixtureManager::deleteFixtureInGroup(), which auto-deletes the whole
 * FixtureGroup once it becomes empty (Doc::deleteFixtureGroup(), see doc.h -
 * it deletes the object immediately and synchronously right after emitting
 * fixtureGroupRemoved()). slotFixtureGroupRemoved() is connected directly to
 * that signal specifically so it can null out m_editGroup (and m_editGroupId)
 * before deleteFixtureInGroup() even returns; deleteSelection() then re-checks
 * m_editGroup for nullptr after that call before touching it again, and
 * updateGroupMap() is only invoked if it's still valid. Any new code path that
 * can trigger a FixtureGroup deletion (directly, or as this kind of side
 * effect) must go through the same synchronous-signal pattern - a check made
 * on a later event-loop tick is already too late.
 */
class FixtureGroupEditor final : public QObject
{
    Q_OBJECT

    Q_PROPERTY(QVariant groupsListModel READ groupsListModel NOTIFY groupsListModelChanged)
    Q_PROPERTY(quint32 groupID READ groupID CONSTANT)
    Q_PROPERTY(QString groupName READ groupName WRITE setGroupName NOTIFY groupNameChanged)
    Q_PROPERTY(QSize groupSize READ groupSize WRITE setGroupSize NOTIFY groupSizeChanged)
    Q_PROPERTY(QVariantList groupMap READ groupMap NOTIFY groupMapChanged)
    Q_PROPERTY(QVariantList groupLabels READ groupLabels NOTIFY groupLabelsChanged)
    Q_PROPERTY(QVariantList selectionData READ selectionData NOTIFY selectionDataChanged)

public:
    FixtureGroupEditor(QQuickView *view, Doc *doc, FixtureManager *fxMgr, QObject *parent = 0);
    ~FixtureGroupEditor();

    /** Returns the data model to display a list of FixtureGroups with icons */
    QVariant groupsListModel();

    /** Empty the Fixture Group currently being edited */
    Q_INVOKABLE void resetGroup();

    /** Clear the Fixture Group currently being edited and reassign all of its
     *  fixtures into a fresh grid in DMX order (universe/address, then head
     *  index), row-major. $rows <= 0 picks a near-square grid automatically;
     *  otherwise the column count is derived from the given row count. */
    Q_INVOKABLE void regenerateFromDmxOrder(int rows);

public slots:
    /** Slot called whenever a new workspace has been loaded */
    void slotDocLoaded();

    /** Slot called whenever a FixtureGroup's properties change (e.g. a Tardis
     *  undo/redo restoring a previous state) so the currently open grid editor,
     *  if any, stays in sync */
    void slotFixtureGroupChanged(quint32 id);

    /** Slot called whenever a FixtureGroup is removed from the Doc - by any
     *  path (deleteSelection() emptying it out, an explicit group deletion,
     *  or an undo/redo). Clears m_editGroup if it's the group being removed,
     *  since Doc::deleteFixtureGroup() deletes the object immediately (not
     *  deferred) right after emitting this signal - without this, m_editGroup
     *  would dangle for any caller still using it. */
    void slotFixtureGroupRemoved(quint32 id);

signals:
    /** Notify the listeners that the FixtureGroup list model has changed */
    void groupsListModelChanged();

private:
    /** Reference to the QML view root */
    QQuickView *m_view;
    /** Reference to the project workspace */
    Doc *m_doc;
    /** Reference to the Fixture Manager */
    FixtureManager *m_fixtureManager;
    /** Reference to the Fixture Group currently being edited */
    FixtureGroup *m_editGroup;
    /** True while a coalesced slotFixtureGroupChanged() refresh is already
     *  scheduled, to avoid rebuilding the grid map once per head mutation
     *  when a multi-head operation (move/transform/undo of one) fires
     *  FixtureGroup::changed() many times in a single gesture */
    bool m_pendingGroupRefresh = false;
    /** ID of m_editGroup, cached separately so slotFixtureGroupRemoved() can
     *  check "is this my group?" by ID alone, without dereferencing
     *  m_editGroup. Every current FixtureGroup removal path (Doc::
     *  deleteFixtureGroup(), Doc::clearContents()) emits fixtureGroupRemoved
     *  before deleting the object, but comparing by cached ID rather than
     *  ever dereferencing m_editGroup is kept as a defensive habit - a future
     *  removal path that got the ordering wrong would otherwise silently
     *  reintroduce a use-after-free here. */
    quint32 m_editGroupId;

    /*********************************************************************
     * Fixture Group Grid Editing
     *********************************************************************/
public:
    enum TransformType
    {
        Rotate90,
        Rotate180,
        Rotate270,
        HorizontalFlip,
        VerticalFlip
    };
    Q_ENUM(TransformType)

    /** Set the reference of a FixtureGroup for editing */
    Q_INVOKABLE void setEditGroup(QVariant reference);

    quint32 groupID() const;

    /** Get/Set the name of the Fixture Group currently being edited */
    QString groupName() const;
    void setGroupName(QString name);

    /** Get/Set the size of the Fixture Group currently being edited */
    QSize groupSize() const;
    void setGroupSize(QSize size);

    /** Returns the heads data for representation in a QML GridEditor */
    QVariantList groupMap();

    /** Returns the head labels data for representation in a QML GridEditor */
    QVariantList groupLabels();

    /** Returns a list of indices with the selected heads */
    QVariantList selectionData();

    /** Resets the currently selected items */
    Q_INVOKABLE void resetSelection();

    /** Check the head at the provided $x,$y position and
     *  returns a list of indices with the selected heads */
    Q_INVOKABLE QVariantList groupSelection(int x, int y, int mouseMods);

    /** Returns a selection array from the provided $reference */
    Q_INVOKABLE QVariantList fixtureSelection(QVariant reference, int x, int y, int mouseMods);

    /** Returns a selection array from the provided $itemID and $headIndex */
    Q_INVOKABLE QVariantList headSelection(int x, int y, int mouseMods);

    /** Add a Fixture with the provided $reference to x,y position */
    Q_INVOKABLE bool addFixture(QVariant reference, int x, int y);

    /** Add a Fixture head of the provided $itemID and $headIndex to x,y position */
    Q_INVOKABLE bool addHead(quint32 itemID, int headIndex, int x, int y);

    /** Check if the current selection can be moved by $offset cells */
    Q_INVOKABLE bool checkSelection(int x, int y, int offset);

    /** Move the current selection by $offset cells */
    Q_INVOKABLE void moveSelection(int x, int y, int offset);

    /** Delete the currently selected items */
    Q_INVOKABLE void deleteSelection();

    /** Rotate the current selection by $degrees */
    Q_INVOKABLE void transformSelection(int transformation);

    /** Get a string to be displayed as tooltip for a head at position x,y */
    Q_INVOKABLE QString getTooltip(int x, int y);

private:
    void updateGroupMap();
    QLCPoint pointFromAbsolute(int absoluteIndex);

    /** Take an undo/redo snapshot of the currently edited group's full contents
     *  (name, size and heads), for use as the "before" or "after" value of a
     *  Tardis::FixtureGroupSetContents action */
    QByteArray groupContentsSnapshot() const;

    /** Enqueue a Tardis::FixtureGroupSetContents action recording that the
     *  currently edited group's contents changed from $before to their
     *  current (already mutated) state */
    void enqueueGroupContentsChange(const QByteArray &before);

signals:
    void groupSizeChanged();
    void groupNameChanged();
    void groupMapChanged();
    void groupLabelsChanged();
    void selectionDataChanged();

private:
    /** An array-like map of the heads data in  group */
    QVariantList m_groupMap;
    /** An array-like map of the heads labels in  group */
    QVariantList m_groupLabels;
    /** An array of data representing the currently selected items on a Grid editor */
    QVariantList m_groupSelection;
};

#endif // FIXTUREGROUPEDITOR_H
