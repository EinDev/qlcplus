/*
  Q Light Controller Plus
  fixturegroupflatmodel.h

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

#ifndef FIXTUREGROUPFLATMODEL_H
#define FIXTUREGROUPFLATMODEL_H

#include <QAbstractListModel>
#include <QHash>
#include <QVector>

#include "treemodel.h"

class TreeModelItem;

/**
 * Flattens a recursive TreeModel (as used by the Fixture Groups tree: Universe/Group
 * -> Fixture -> Head/Channel) into a single flat list of the rows currently visible
 * (i.e. not hidden behind a collapsed ancestor), each carrying a "depth" for indentation.
 *
 * This exists so a ListView bound to it gets a uniform, real per-row model: every row is
 * an actual model entry, so ListView's own contentHeight/virtualization/scroll-position
 * math is simply correct, with nothing to estimate or override. The previous approach
 * (nesting an unvirtualized Repeater per tree node, then trying to override
 * ListView.contentHeight with an exact value) fought ListView's own internal layout
 * bookkeeping and broke scroll position - this proxy avoids that class of problem entirely.
 *
 * Expand/collapse (TreeModel::IsExpandedRole, changed anywhere in the tree) is handled
 * with precise incremental beginInsertRows/beginRemoveRows so scroll position for
 * unrelated rows is never disturbed. Structural changes (fixtures/groups added or
 * removed) are handled by a full rebuild() - acceptable since the underlying TreeModel
 * itself already fully rebuilds (losing expand state) on every such change.
 */
class FixtureGroupFlatModel : public QAbstractListModel
{
    Q_OBJECT
    Q_DISABLE_COPY(FixtureGroupFlatModel)

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

    FixtureGroupFlatModel(QObject *parent = 0);
    ~FixtureGroupFlatModel();

    QObject *sourceModel() const;
    void setSourceModel(QObject *model);

    /** Fully re-flatten the source tree from scratch. Called automatically when
     *  sourceModel is set, and should be called from QML whenever the source tree's
     *  own groupsTreeModelChanged fires (i.e. a structural change happened). */
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

#endif // FIXTUREGROUPFLATMODEL_H
