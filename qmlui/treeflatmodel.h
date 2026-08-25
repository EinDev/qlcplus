/*
  Q Light Controller Plus
  treeflatmodel.h

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

#ifndef TREEFLATMODEL_H
#define TREEFLATMODEL_H

#include <QAbstractListModel>
#include <QHash>
#include <QVector>

#include "treemodel.h"
#include "treemodelitem.h"

/**
 * Flattens a recursive TreeModel (any of the app's tree/folder views: Fixture Groups,
 * Functions, the import dialog's trees, the manual input channel picker, etc.) into a
 * single flat list of the rows currently visible (i.e. not hidden behind a collapsed
 * ancestor), each carrying a "depth" for indentation.
 *
 * This exists so a ListView bound to it gets a uniform, real per-row model: every row is
 * an actual model entry, so ListView's own contentHeight/virtualization/scroll-position
 * math is simply correct, with nothing to estimate or override. The previous approach in
 * each of these views (nesting an unvirtualized Repeater per tree node, i.e. rendering an
 * entire subtree inside one oversized ListView delegate) made a single row's real height
 * range from one row (collapsed) to thousands of pixels (deeply expanded), which is what
 * made ListView's own contentHeight estimate - and briefly, an attempt to override it with
 * an exact value - both unreliable. Giving ListView real, uniform-height rows instead
 * avoids that whole class of problem.
 *
 * Expand/collapse (TreeModel::IsExpandedRole, changed anywhere in the tree) is handled
 * with precise incremental beginInsertRows/beginRemoveRows so scroll position for
 * unrelated rows is never disturbed. Structural changes (items added/removed anywhere,
 * or the source tree being swapped out entirely) are handled by a full rebuild() -
 * acceptable since every known source TreeModel already fully rebuilds (losing expand
 * state) on every such change today.
 */
class TreeFlatModel : public QAbstractListModel
{
    Q_OBJECT
    Q_DISABLE_COPY(TreeFlatModel)

    Q_PROPERTY(QObject *sourceModel READ sourceModel WRITE setSourceModel NOTIFY sourceModelChanged)

public:
    enum FlatRoles
    {
        // fixed roles: identical values to TreeModel::FixedRoles so they can be
        // forwarded straight through to the owning TreeModel without translation
        LabelRole = TreeModel::LabelRole,
        PathRole = TreeModel::PathRole,
        IsExpandedRole = TreeModel::IsExpandedRole,
        IsSelectedRole = TreeModel::IsSelectedRole,
        IsCheckableRole = TreeModel::IsCheckableRole,
        IsCheckedRole = TreeModel::IsCheckedRole,
        ItemsCountRole = TreeModel::ItemsCountRole,
        HasChildrenRole = TreeModel::HasChildrenRole,

        // custom columns: fixed role ids of our own, resolved against whatever the
        // current source tree's column layout actually is at rebuild() time
        ClassRefRole = TreeModel::FixedRolesEnd,
        TypeRole,
        IdRole,
        SubIdRole,
        ChIdxRole,
        InGroupRole,
        FlagsRole,
        CanFadeRole,
        PrecedenceRole,
        ModifierRole,

        DepthRole
    };

    TreeFlatModel(QObject *parent = 0);
    ~TreeFlatModel();

    QObject *sourceModel() const;
    void setSourceModel(QObject *model);

    /** Fully re-flatten the source tree from scratch. Called automatically when
     *  sourceModel is set (including when the same TreeModel instance is reused across
     *  multiple rebuilds by its owner, e.g. cleared and repopulated in place), and should
     *  also be called explicitly from QML whenever the source's own "tree changed" signal
     *  fires (e.g. groupsTreeModelChanged, functionsTreeModelChanged) since that generally
     *  means a structural change happened without the TreeModel pointer itself changing. */
    Q_INVOKABLE void rebuild();

    /** @reimp */
    int rowCount(const QModelIndex &parent = QModelIndex()) const override;

    /** @reimp */
    QVariant data(const QModelIndex &index, int role) const override;

    /** @reimp */
    bool setData(const QModelIndex &index, const QVariant &value, int role) override;

protected:
    QHash<int, QByteArray> roleNames() const override;

private slots:
    void slotSourceRoleChanged(TreeModelItem *item, int role, const QVariant &value);

    /** The source model is about to delete some/all of its TreeModelItems (a structural
     *  change: TreeModel::clear()/removeItem(), or a future modelReset-style change).
     *  Drop every row referencing that model immediately and synchronously, before any
     *  deletion can happen, so data()/setData() can never dereference a just-freed
     *  TreeModelItem in the window between the deletion and whatever higher-level "tree
     *  changed" signal (groupsTreeModelChanged, functionsListChanged, ...) eventually
     *  calls rebuild() to repopulate. */
    void slotSourceInvalidated();

private:
    struct FlatRow
    {
        TreeModelItem *item;
        TreeModel *owner;
        int depth;
    };

    /** Recursively append $tree's items (and, for each already-expanded one, its
     *  visible descendants) to $out at the given $depth. */
    static void appendSubtree(TreeModel *tree, int depth, QVector<FlatRow> &out);

    /** Rebuild m_indexOfItem for every row starting at $from. */
    void reindexFrom(int from);

private:
    TreeModel *m_sourceModel;
    QVector<FlatRow> m_rows;
    QHash<TreeModelItem *, int> m_indexOfItem;
    QHash<int, int> m_customRoleToOwnerRole;

signals:
    void sourceModelChanged();
};

#endif // TREEFLATMODEL_H
